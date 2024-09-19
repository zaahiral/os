#include "fs/fat32.h"
#include "memory/memory.h"

namespace Filesystem
{
    namespace
    {
        constexpr uint32_t FAT32_LAST_CLUSTER = 0x0FFFFFFF;
        // cOS has no real-time clock driver yet, so new files are dated 1 January 1980, the earliest FAT date.
        constexpr uint16_t FAT32_DEFAULT_DATE = (1 << 5) | 1;
        // The FSInfo sector caches the free cluster count and the next free cluster at these offsets.
        constexpr uint64_t FSINFO_FREE_COUNT_OFFSET = 488;
        constexpr uint64_t FSINFO_NEXT_FREE_OFFSET = 492;

        // Convert a name such as notes.txt to the padded 8.3 form "NOTES   TXT", and return false if it does not fit.
        bool to_short_name(const char* name, char* shortName)
        {
            for (int i = 0; i < 11; i++) { shortName[i] = ' '; }
            uint64_t position = 0;
            bool isExtension = false;
            for (; *name; name++)
            {
                char c = *name;
                if (c == '.')
                {
                    if (isExtension || !position) { return false; }
                    isExtension = true;
                    position = 8;
                    continue;
                }
                if ((!isExtension && position == 8) || position == 11) { return false; }
                if (c >= 'a' && c <= 'z') { c -= 'a' - 'A'; }
                shortName[position++] = c;
            }
            return position != 0;
        }

        // Convert a padded 8.3 name back to a name such as notes.txt, in lower case.
        void from_short_name(const char* shortName, char* name)
        {
            uint64_t length = 0;
            for (int i = 0; i < 8 && shortName[i] != ' '; i++) { name[length++] = shortName[i]; }
            if (shortName[8] != ' ') { name[length++] = '.'; }
            for (int i = 8; i < 11 && shortName[i] != ' '; i++) { name[length++] = shortName[i]; }
            name[length] = '\0';
            for (uint64_t i = 0; i < length; i++)
            {
                if (name[i] >= 'A' && name[i] <= 'Z') { name[i] += 'a' - 'A'; }
            }
        }

        bool names_equal(const char* a, const char* b)
        {
            for (int i = 0; i < 11; i++)
            {
                if (a[i] != b[i]) { return false; }
            }
            return true;
        }

        // Long name entries, deleted entries, volume labels and the . and .. entries are not shown as files.
        bool is_visible_entry(FAT32DirectoryEntry& entry)
        {
            if ((uint8_t)entry.name[0] == FAT32_ENTRY_DELETED || entry.name[0] == '.') { return false; }
            return entry.attributes != FAT32_ATTRIBUTE_LONG_NAME && !(entry.attributes & FAT32_ATTRIBUTE_VOLUME_ID);
        }

        uint32_t get_entry_cluster(FAT32DirectoryEntry& entry) { return ((uint32_t)entry.firstClusterHigh << 16) | entry.firstClusterLow; }

        void set_entry_cluster(FAT32DirectoryEntry& entry, uint32_t cluster)
        {
            entry.firstClusterHigh = cluster >> 16;
            entry.firstClusterLow = cluster & 0xFFFF;
        }
    }

    bool FAT32Driver::mount(const char* mountPoint)
    {
        uint8_t sector[ATA_SECTOR_SIZE];
        if (!Drivers::ATADriver::instance().read_sector(0, sector)) { return false; }
        if (sector[510] != 0x55 || sector[511] != 0xAA) { return false; }

        // FAT32 has no fixed root directory and no 16-bit FAT size, which tells it apart from FAT12 and FAT16.
        auto bootSector = reinterpret_cast<FAT32BootSector*>(sector);
        if (bootSector->bytesPerSector != ATA_SECTOR_SIZE || bootSector->rootEntryCount || bootSector->fatSize16 || !bootSector->fatSize32) { return false; }
        m_sectorsPerCluster = bootSector->sectorsPerCluster;
        m_fatStart = bootSector->reservedSectors;
        m_fatSize = bootSector->fatSize32;
        m_fatCount = bootSector->fatCount;
        m_dataStart = m_fatStart + m_fatCount * m_fatSize;
        m_rootCluster = bootSector->rootCluster;
        m_clusterCount = (bootSector->totalSectors32 - m_dataStart) / m_sectorsPerCluster;

        // We do not keep the FSInfo counts up to date, so we mark them as unknown and other systems count the free clusters themselves.
        uint16_t fsInfoSector = bootSector->fsInfoSector;
        if (fsInfoSector && Drivers::ATADriver::instance().read_sector(fsInfoSector, sector))
        {
            *reinterpret_cast<uint32_t*>(&sector[FSINFO_FREE_COUNT_OFFSET]) = 0xFFFFFFFF;
            *reinterpret_cast<uint32_t*>(&sector[FSINFO_NEXT_FREE_OFFSET]) = 0xFFFFFFFF;
            Drivers::ATADriver::instance().write_sector(fsInfoSector, sector);
        }

        auto rootNode = VirtualFilesystemNode();
        rootNode.name[0] = '\0';
        rootNode.fs = nullptr;
        rootNode.flags = VirtualFilesystemNode::VFS_DIRECTORY;
        rootNode.inodeNum = 0;
        rootNode.driver = this;
        VirtualFilesystemManager::instance().register_filesystem(RealFilesystem(rootNode, this, mountPoint));
        return true;
    }

    uint32_t FAT32Driver::get_fat_entry(uint32_t cluster)
    {
        uint8_t sector[ATA_SECTOR_SIZE];
        uint32_t offset = cluster * sizeof(uint32_t);
        Drivers::ATADriver::instance().read_sector(m_fatStart + offset / ATA_SECTOR_SIZE, sector);
        return *reinterpret_cast<uint32_t*>(&sector[offset % ATA_SECTOR_SIZE]) & FAT32_CLUSTER_MASK;
    }

    void FAT32Driver::set_fat_entry(uint32_t cluster, uint32_t value)
    {
        uint8_t sector[ATA_SECTOR_SIZE];
        uint32_t offset = cluster * sizeof(uint32_t);
        // Every copy of the FAT is updated, and the top four bits of each entry are reserved, so we keep them.
        for (uint32_t fat = 0; fat < m_fatCount; fat++)
        {
            uint32_t sectorNumber = m_fatStart + fat * m_fatSize + offset / ATA_SECTOR_SIZE;
            Drivers::ATADriver::instance().read_sector(sectorNumber, sector);
            auto entry = reinterpret_cast<uint32_t*>(&sector[offset % ATA_SECTOR_SIZE]);
            *entry = (*entry & ~FAT32_CLUSTER_MASK) | (value & FAT32_CLUSTER_MASK);
            Drivers::ATADriver::instance().write_sector(sectorNumber, sector);
        }
    }

    // Find a free cluster, mark it as the end of a chain, and fill it with zeros. Returns 0 if the disk is full.
    uint32_t FAT32Driver::allocate_cluster()
    {
        for (uint32_t cluster = m_nextFreeCluster; cluster < m_clusterCount + 2; cluster++)
        {
            if (get_fat_entry(cluster)) { continue; }
            set_fat_entry(cluster, FAT32_LAST_CLUSTER);
            m_nextFreeCluster = cluster + 1;

            uint8_t sector[ATA_SECTOR_SIZE];
            memset(sector, 0, ATA_SECTOR_SIZE);
            for (uint32_t i = 0; i < m_sectorsPerCluster; i++)
                Drivers::ATADriver::instance().write_sector(cluster_to_sector(cluster) + i, sector);
            return cluster;
        }
        return 0;
    }

    uint32_t FAT32Driver::get_first_cluster(VirtualFilesystemNode* vnode)
    {
        if (!vnode->inodeNum) { return m_rootCluster; }
        FAT32DirectoryEntry entry;
        read_entry(vnode->inodeNum, entry);
        return get_entry_cluster(entry);
    }

    // Follow a cluster chain to the cluster at an index, adding clusters to the end of the chain if asked to.
    // Returns 0 if the chain is too short, or the disk is full.
    uint32_t FAT32Driver::get_cluster_at(uint32_t firstCluster, uint64_t index, bool shouldAllocate)
    {
        uint32_t cluster = firstCluster;
        for (uint64_t i = 0; i < index; i++)
        {
            uint32_t next = get_fat_entry(cluster);
            if (next >= FAT32_END_OF_CHAIN)
            {
                if (!shouldAllocate) { return 0; }
                next = allocate_cluster();
                if (!next) { return 0; }
                set_fat_entry(cluster, next);
            }
            cluster = next;
        }
        return cluster;
    }

    // Read the directory entry in a slot of a directory, and return false past the end of the directory's clusters.
    bool FAT32Driver::read_slot(uint32_t directoryCluster, uint64_t slot, uint64_t& inode, FAT32DirectoryEntry& entry)
    {
        uint64_t slotsPerCluster = m_sectorsPerCluster * FAT32_ENTRIES_PER_SECTOR;
        uint32_t cluster = get_cluster_at(directoryCluster, slot / slotsPerCluster, false);
        if (!cluster) { return false; }
        uint64_t slotInCluster = slot % slotsPerCluster;
        inode = (cluster_to_sector(cluster) + slotInCluster / FAT32_ENTRIES_PER_SECTOR) * FAT32_ENTRIES_PER_SECTOR + slotInCluster % FAT32_ENTRIES_PER_SECTOR;
        read_entry(inode, entry);
        return true;
    }

    void FAT32Driver::read_entry(uint64_t inode, FAT32DirectoryEntry& entry)
    {
        uint8_t sector[ATA_SECTOR_SIZE];
        Drivers::ATADriver::instance().read_sector(inode / FAT32_ENTRIES_PER_SECTOR, sector);
        entry = reinterpret_cast<FAT32DirectoryEntry*>(sector)[inode % FAT32_ENTRIES_PER_SECTOR];
    }

    void FAT32Driver::write_entry(uint64_t inode, FAT32DirectoryEntry& entry)
    {
        uint8_t sector[ATA_SECTOR_SIZE];
        Drivers::ATADriver::instance().read_sector(inode / FAT32_ENTRIES_PER_SECTOR, sector);
        reinterpret_cast<FAT32DirectoryEntry*>(sector)[inode % FAT32_ENTRIES_PER_SECTOR] = entry;
        Drivers::ATADriver::instance().write_sector(inode / FAT32_ENTRIES_PER_SECTOR, sector);
    }

    VirtualFilesystemNode* FAT32Driver::make_vnode(uint64_t inode, FAT32DirectoryEntry& entry)
    {
        auto node = new VirtualFilesystemNode();
        node->driver = this;
        node->fs = nullptr;
        node->inodeNum = inode;
        node->flags = (entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) ? VirtualFilesystemNode::VFS_DIRECTORY : VirtualFilesystemNode::VFS_FILE;
        from_short_name(entry.name, node->name);
        return node;
    }

    VirtualFilesystemNode* FAT32Driver::vfsdriver_finddir(VirtualFilesystemNode* vnode, char* name)
    {
        char shortName[11];
        if (!to_short_name(name, shortName)) { return nullptr; }
        m_spinlock.acquire();
        auto directoryCluster = get_first_cluster(vnode);
        uint64_t inode;
        FAT32DirectoryEntry entry;
        for (uint64_t slot = 0; read_slot(directoryCluster, slot, inode, entry); slot++)
        {
            if (entry.name[0] == FAT32_ENTRY_END) { break; }
            if (is_visible_entry(entry) && names_equal(entry.name, shortName))
            {
                m_spinlock.release();
                return make_vnode(inode, entry);
            }
        }
        m_spinlock.release();
        return nullptr;
    }

    VirtualFilesystemDirectoryEntry* FAT32Driver::vfsdriver_readdir(VirtualFilesystemNode* vnode, uint64_t index)
    {
        m_spinlock.acquire();
        auto directoryCluster = get_first_cluster(vnode);
        uint64_t inode;
        FAT32DirectoryEntry entry;
        uint64_t visibleIndex = 0;
        for (uint64_t slot = 0; read_slot(directoryCluster, slot, inode, entry); slot++)
        {
            if (entry.name[0] == FAT32_ENTRY_END) { break; }
            if (!is_visible_entry(entry)) { continue; }
            if (visibleIndex++ == index)
            {
                m_spinlock.release();
                // The caller is responsible for freeing the directory entry.
                auto directoryEntry = new VirtualFilesystemDirectoryEntry();
                directoryEntry->inodeNum = inode;
                from_short_name(entry.name, directoryEntry->name);
                return directoryEntry;
            }
        }
        m_spinlock.release();
        return nullptr;
    }

    uint64_t FAT32Driver::vfsdriver_filelen(VirtualFilesystemNode* vnode)
    {
        if (!vnode->inodeNum) { return 0; }
        m_spinlock.acquire();
        FAT32DirectoryEntry entry;
        read_entry(vnode->inodeNum, entry);
        m_spinlock.release();
        return entry.size;
    }

    uint64_t FAT32Driver::vfsdriver_read(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t size, char* buf)
    {
        if (!(vnode->flags & VirtualFilesystemNode::VFS_FILE)) { return 0; }
        m_spinlock.acquire();
        FAT32DirectoryEntry entry;
        read_entry(vnode->inodeNum, entry);
        if (offset >= entry.size)
        {
            m_spinlock.release();
            return 0;
        }
        if (offset + size > entry.size) { size = entry.size - offset; }

        uint64_t clusterSize = m_sectorsPerCluster * ATA_SECTOR_SIZE;
        uint8_t sector[ATA_SECTOR_SIZE];
        uint64_t bytesRead = 0;
        while (bytesRead < size)
        {
            uint64_t position = offset + bytesRead;
            uint32_t cluster = get_cluster_at(get_entry_cluster(entry), position / clusterSize, false);
            if (!cluster) { break; }
            Drivers::ATADriver::instance().read_sector(cluster_to_sector(cluster) + (position % clusterSize) / ATA_SECTOR_SIZE, sector);
            uint64_t sectorOffset = position % ATA_SECTOR_SIZE;
            uint64_t count = ATA_SECTOR_SIZE - sectorOffset;
            if (count > size - bytesRead) { count = size - bytesRead; }
            memcpy(buf + bytesRead, &sector[sectorOffset], count);
            bytesRead += count;
        }
        m_spinlock.release();
        return bytesRead;
    }

    uint64_t FAT32Driver::vfsdriver_write(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t size, char* buf)
    {
        if (!(vnode->flags & VirtualFilesystemNode::VFS_FILE)) { return 0; }
        m_spinlock.acquire();
        FAT32DirectoryEntry entry;
        read_entry(vnode->inodeNum, entry);
        // Files cannot have holes, so a write past the end starts at the end.
        if (offset > entry.size) { offset = entry.size; }
        if (!get_entry_cluster(entry))
        {
            uint32_t cluster = allocate_cluster();
            if (!cluster)
            {
                m_spinlock.release();
                return 0;
            }
            set_entry_cluster(entry, cluster);
        }

        uint64_t clusterSize = m_sectorsPerCluster * ATA_SECTOR_SIZE;
        uint8_t sector[ATA_SECTOR_SIZE];
        uint64_t bytesWritten = 0;
        while (bytesWritten < size)
        {
            uint64_t position = offset + bytesWritten;
            uint32_t cluster = get_cluster_at(get_entry_cluster(entry), position / clusterSize, true);
            if (!cluster) { break; }
            // Only part of the sector may change, so we read it first and write all of it back.
            uint32_t sectorNumber = cluster_to_sector(cluster) + (position % clusterSize) / ATA_SECTOR_SIZE;
            Drivers::ATADriver::instance().read_sector(sectorNumber, sector);
            uint64_t sectorOffset = position % ATA_SECTOR_SIZE;
            uint64_t count = ATA_SECTOR_SIZE - sectorOffset;
            if (count > size - bytesWritten) { count = size - bytesWritten; }
            memcpy(&sector[sectorOffset], buf + bytesWritten, count);
            Drivers::ATADriver::instance().write_sector(sectorNumber, sector);
            bytesWritten += count;
        }
        if (offset + bytesWritten > entry.size) { entry.size = offset + bytesWritten; }
        write_entry(vnode->inodeNum, entry);
        m_spinlock.release();
        return bytesWritten;
    }

    VirtualFilesystemNode* FAT32Driver::vfsdriver_create(VirtualFilesystemNode* vnode, char* name)
    {
        char shortName[11];
        if (!to_short_name(name, shortName)) { return nullptr; }
        auto existingNode = vfsdriver_finddir(vnode, name);
        if (existingNode) { return existingNode; }

        m_spinlock.acquire();
        auto directoryCluster = get_first_cluster(vnode);
        uint64_t inode;
        FAT32DirectoryEntry entry;
        uint64_t slot = 0;
        bool hasFreeSlot = false;
        for (; read_slot(directoryCluster, slot, inode, entry); slot++)
        {
            if (entry.name[0] == FAT32_ENTRY_END || (uint8_t)entry.name[0] == FAT32_ENTRY_DELETED) { hasFreeSlot = true; break; }
        }
        if (!hasFreeSlot)
        {
            // The directory is full, so we add a cluster to it, which is filled with free entries.
            uint64_t slotsPerCluster = m_sectorsPerCluster * FAT32_ENTRIES_PER_SECTOR;
            if (!get_cluster_at(directoryCluster, slot / slotsPerCluster, true) || !read_slot(directoryCluster, slot, inode, entry))
            {
                m_spinlock.release();
                return nullptr;
            }
        }

        FAT32DirectoryEntry newEntry;
        memset(&newEntry, 0, sizeof(newEntry));
        memcpy(newEntry.name, shortName, sizeof(newEntry.name));
        newEntry.attributes = FAT32_ATTRIBUTE_ARCHIVE;
        newEntry.creationDate = FAT32_DEFAULT_DATE;
        newEntry.accessedDate = FAT32_DEFAULT_DATE;
        newEntry.modificationDate = FAT32_DEFAULT_DATE;
        write_entry(inode, newEntry);
        m_spinlock.release();
        return make_vnode(inode, newEntry);
    }
}
