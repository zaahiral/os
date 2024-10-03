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

        constexpr uint64_t FAT32_MAX_NAME = 255;
        constexpr uint64_t FAT32_LONG_NAME_PART = 13;

        uint8_t short_name_checksum(const char* name)
        {
            uint8_t checksum = 0;
            for (int i = 0; i < 11; i++) { checksum = ((checksum & 1) << 7) + (checksum >> 1) + (uint8_t)name[i]; }
            return checksum;
        }

        bool is_short_character(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '$' || c == '%' || c == '\'' || c == '-' || c == '_' || c == '@' || c == '~'
                || c == '`' || c == '!' || c == '(' || c == ')' || c == '{' || c == '}' || c == '^' || c == '#' || c == '&';
        }

        // VFAT stores UTF-16, while VFS paths use UTF-8 and have room for 255 bytes.
        bool encode_long_name(const char* name, uint16_t* characters, uint64_t& length)
        {
            length = 0;
            uint64_t position = 0;
            while (name[position])
            {
                uint32_t c = (uint8_t)name[position++];
                uint32_t minimum = 0;
                int remaining = 0;
                if (c >= 0xF0 && c <= 0xF4) { c &= 7; remaining = 3; minimum = 0x10000; }
                else if (c >= 0xE0 && c <= 0xEF) { c &= 15; remaining = 2; minimum = 0x800; }
                else if (c >= 0xC2 && c <= 0xDF) { c &= 31; remaining = 1; minimum = 0x80; }
                else if (c >= 0x80) { return false; }
                for (int i = 0; i < remaining; i++)
                {
                    uint8_t next = name[position++];
                    if ((next & 0xC0) != 0x80) { return false; }
                    c = (c << 6) | (next & 63);
                }
                if (position > FAT32_MAX_NAME || c < minimum || c > 0x10FFFF || c == 0xFFFF || (c >= 0xD800 && c <= 0xDFFF)) { return false; }
                if (c < 32 || c == '"' || c == '*' || c == '/' || c == ':' || c == '<' || c == '>' || c == '?' || c == '\\' || c == '|') { return false; }
                if (c >= 0x10000)
                {
                    c -= 0x10000;
                    characters[length++] = 0xD800 | (c >> 10);
                    characters[length++] = 0xDC00 | (c & 1023);
                }
                else { characters[length++] = c; }
            }
            return position && name[position - 1] != '.' && name[position - 1] != ' ';
        }

        bool decode_long_name(const uint16_t* characters, uint64_t count, char* name)
        {
            uint64_t length = 0;
            bool hasEnd = false;
            for (uint64_t i = 0; i < count; i++)
            {
                uint32_t c = characters[i];
                if (hasEnd)
                {
                    if (c != 0xFFFF) { return false; }
                    continue;
                }
                if (!c) { hasEnd = true; continue; }
                if (i >= FAT32_MAX_NAME || c == 0xFFFF) { return false; }
                if (c >= 0xD800 && c <= 0xDBFF)
                {
                    if (++i >= count || characters[i] < 0xDC00 || characters[i] > 0xDFFF) { return false; }
                    c = 0x10000 + ((c - 0xD800) << 10) + characters[i] - 0xDC00;
                }
                else if (c >= 0xDC00 && c <= 0xDFFF) { return false; }
                int bytes = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
                if (length + bytes > FAT32_MAX_NAME) { return false; }
                if (bytes == 1) { name[length++] = c; }
                else
                {
                    name[length++] = (bytes == 2 ? 0xC0 : bytes == 3 ? 0xE0 : 0xF0) | (c >> (6 * (bytes - 1)));
                    for (int j = bytes - 2; j >= 0; j--) { name[length++] = 0x80 | ((c >> (6 * j)) & 63); }
                }
            }
            name[length] = '\0';
            return length != 0;
        }

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
                if (!is_short_character(c)) { return false; }
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
            for (; *a && *b; a++, b++)
            {
                char left = *a;
                char right = *b;
                if (left >= 'A' && left <= 'Z') { left += 'a' - 'A'; }
                if (right >= 'A' && right <= 'Z') { right += 'a' - 'A'; }
                if (left != right) { return false; }
            }
            return *a == *b;
        }

        struct LongName
        {
            uint16_t characters[260];
            uint8_t expected = 0;
            uint8_t count = 0;
            uint8_t checksum = 0;
            uint64_t firstSlot = 0;

            void consume(FAT32DirectoryEntry& entry, uint64_t slot)
            {
                auto part = reinterpret_cast<FAT32LongNameEntry*>(&entry);
                uint8_t ordinal = part->ordinal & 0x1F;
                if (part->ordinal & 0x40)
                {
                    expected = ordinal;
                    count = ordinal;
                    checksum = part->checksum;
                    firstSlot = slot;
                }
                if (!expected || ordinal != expected || ordinal > 20 || (part->ordinal & 0xA0)
                    || part->type || part->firstCluster || checksum != part->checksum)
                {
                    expected = count = 0;
                    return;
                }
                uint64_t offset = (ordinal - 1) * FAT32_LONG_NAME_PART;
                for (int i = 0; i < 5; i++) { characters[offset++] = part->name1[i]; }
                for (int i = 0; i < 6; i++) { characters[offset++] = part->name2[i]; }
                for (int i = 0; i < 2; i++) { characters[offset++] = part->name3[i]; }
                expected--;
            }

            bool matches(FAT32DirectoryEntry& entry)
            {
                return count && !expected && checksum == short_name_checksum(entry.name);
            }
        };

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

        // Fill in the directory entry of a new file or directory, whose contents start at a cluster, or 0 if it is empty.
        void make_entry(FAT32DirectoryEntry& entry, const char* shortName, uint8_t attributes, uint32_t cluster)
        {
            memset(&entry, 0, sizeof(entry));
            memcpy(entry.name, (char*)shortName, sizeof(entry.name));
            entry.attributes = attributes;
            entry.creationDate = FAT32_DEFAULT_DATE;
            entry.accessedDate = FAT32_DEFAULT_DATE;
            entry.modificationDate = FAT32_DEFAULT_DATE;
            set_entry_cluster(entry, cluster);
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

    // Free every cluster in a chain, so that they can be allocated again.
    void FAT32Driver::free_chain(uint32_t cluster)
    {
        // The freed clusters may start another chain later, so the last chain found is forgotten.
        m_lastChainStart = 0;
        while (cluster >= 2 && cluster < FAT32_END_OF_CHAIN)
        {
            uint32_t next = get_fat_entry(cluster);
            set_fat_entry(cluster, 0);
            if (cluster < m_nextFreeCluster) { m_nextFreeCluster = cluster; }
            cluster = next;
        }
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
        uint64_t i = 0;
        // Files are mostly read and written in order, so we continue from the last cluster found in the same chain.
        if (firstCluster == m_lastChainStart && index >= m_lastChainIndex)
        {
            cluster = m_lastChainCluster;
            i = m_lastChainIndex;
        }
        for (; i < index; i++)
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
        m_lastChainStart = firstCluster;
        m_lastChainIndex = index;
        m_lastChainCluster = cluster;
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

    // Long names need consecutive slots, which may cross a sector or cluster boundary.
    bool FAT32Driver::find_free_slots(uint32_t directoryCluster, uint64_t count, uint64_t& firstSlot)
    {
        uint64_t freeCount = 0;
        bool isAtEnd = false;
        FAT32DirectoryEntry entry;
        uint64_t inode;
        uint64_t slotsPerCluster = m_sectorsPerCluster * FAT32_ENTRIES_PER_SECTOR;
        for (uint64_t slot = 0; ; slot++)
        {
            if (!read_slot(directoryCluster, slot, inode, entry))
            {
                if (!get_cluster_at(directoryCluster, slot / slotsPerCluster, true)
                    || !read_slot(directoryCluster, slot, inode, entry)) { return false; }
            }
            if (entry.name[0] == FAT32_ENTRY_END) { isAtEnd = true; }
            if (isAtEnd || (uint8_t)entry.name[0] == FAT32_ENTRY_DELETED)
            {
                if (!freeCount) { firstSlot = slot; }
                if (++freeCount == count)
                {
                    // Keep the end marker after the new entry if the directory already has room for it.
                    if (isAtEnd && read_slot(directoryCluster, slot + 1, inode, entry))
                    {
                        memset(&entry, 0, sizeof(entry));
                        write_entry(inode, entry);
                    }
                    return true;
                }
            }
            else { freeCount = 0; }
        }
    }

    bool FAT32Driver::short_name_exists(uint32_t directoryCluster, const char* shortName)
    {
        FAT32DirectoryEntry entry;
        uint64_t inode;
        for (uint64_t slot = 0; read_slot(directoryCluster, slot, inode, entry); slot++)
        {
            if (entry.name[0] == FAT32_ENTRY_END) { break; }
            if ((uint8_t)entry.name[0] == FAT32_ENTRY_DELETED || entry.attributes == FAT32_ATTRIBUTE_LONG_NAME) { continue; }
            bool isEqual = true;
            for (int i = 0; i < 11; i++) { if (entry.name[i] != shortName[i]) { isEqual = false; break; } }
            if (isEqual) { return true; }
        }
        return false;
    }

    bool FAT32Driver::add_entry(uint32_t directoryCluster, const char* name, uint8_t attributes, uint32_t cluster,
        uint64_t& inode, FAT32DirectoryEntry& entry)
    {
        uint16_t characters[260];
        uint64_t length;
        if (!encode_long_name(name, characters, length)) { return false; }
        char shortName[11];
        bool needsLongName = !to_short_name(name, shortName);
        if (needsLongName)
        {
            const char* extension = nullptr;
            for (const char* c = name; *c; c++) { if (*c == '.') { extension = c + 1; } }
            char base[8];
            uint64_t baseLength = 0;
            for (const char* c = name; *c && (!extension || c < extension - 1) && baseLength < 8; c++)
            {
                char value = *c;
                if (value == ' ' || value == '.') { continue; }
                if (value >= 'a' && value <= 'z') { value -= 'a' - 'A'; }
                base[baseLength++] = is_short_character(value) ? value : '_';
            }
            if (!baseLength) { base[baseLength++] = '_'; }
            bool isUnique = false;
            for (uint32_t suffix = 1; suffix < 1000000; suffix++)
            {
                memset(shortName, ' ', sizeof(shortName));
                char digits[6];
                uint64_t digitCount = 0;
                for (uint32_t value = suffix; value; value /= 10) { digits[digitCount++] = '0' + value % 10; }
                uint64_t prefixLength = baseLength < 7 - digitCount ? baseLength : 7 - digitCount;
                memcpy(shortName, base, prefixLength);
                shortName[prefixLength] = '~';
                for (uint64_t i = 0; i < digitCount; i++) { shortName[prefixLength + 1 + i] = digits[digitCount - 1 - i]; }
                if (extension)
                {
                    uint64_t i = 8;
                    for (const char* c = extension; *c && i < 11; c++)
                    {
                        char value = *c;
                        if (value == ' ' || value == '.') { continue; }
                        if (value >= 'a' && value <= 'z') { value -= 'a' - 'A'; }
                        shortName[i++] = is_short_character(value) ? value : '_';
                    }
                }
                if (!short_name_exists(directoryCluster, shortName)) { isUnique = true; break; }
            }
            if (!isUnique) { return false; }
        }
        else if (short_name_exists(directoryCluster, shortName)) { return false; }

        uint64_t partCount = needsLongName ? (length + FAT32_LONG_NAME_PART - 1) / FAT32_LONG_NAME_PART : 0;
        uint64_t slot;
        if (!find_free_slots(directoryCluster, partCount + 1, slot)) { return false; }
        uint8_t checksum = short_name_checksum(shortName);
        for (uint64_t ordinal = partCount; ordinal; ordinal--)
        {
            FAT32LongNameEntry part;
            memset(&part, 0, sizeof(part));
            part.ordinal = ordinal | (ordinal == partCount ? 0x40 : 0);
            part.attributes = FAT32_ATTRIBUTE_LONG_NAME;
            part.checksum = checksum;
            uint16_t values[FAT32_LONG_NAME_PART];
            for (uint64_t i = 0; i < FAT32_LONG_NAME_PART; i++)
            {
                uint64_t position = (ordinal - 1) * FAT32_LONG_NAME_PART + i;
                values[i] = position < length ? characters[position] : position == length ? 0 : 0xFFFF;
            }
            for (int i = 0; i < 5; i++) { part.name1[i] = values[i]; }
            for (int i = 0; i < 6; i++) { part.name2[i] = values[5 + i]; }
            for (int i = 0; i < 2; i++) { part.name3[i] = values[11 + i]; }
            read_slot(directoryCluster, slot++, inode, entry);
            write_entry(inode, *reinterpret_cast<FAT32DirectoryEntry*>(&part));
        }
        read_slot(directoryCluster, slot, inode, entry);
        make_entry(entry, shortName, attributes, cluster);
        write_entry(inode, entry);
        return true;
    }

    // A directory is empty if it holds nothing but its . and .. entries.
    bool FAT32Driver::is_empty_directory(uint32_t directoryCluster)
    {
        uint64_t inode;
        FAT32DirectoryEntry entry;
        for (uint64_t slot = 0; read_slot(directoryCluster, slot, inode, entry); slot++)
        {
            if (entry.name[0] == FAT32_ENTRY_END) { break; }
            if (is_visible_entry(entry)) { return false; }
        }
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

    VirtualFilesystemNode* FAT32Driver::make_vnode(uint64_t inode, FAT32DirectoryEntry& entry, const char* name)
    {
        auto node = new VirtualFilesystemNode();
        node->driver = this;
        node->fs = nullptr;
        node->inodeNum = inode;
        node->flags = (entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) ? VirtualFilesystemNode::VFS_DIRECTORY : VirtualFilesystemNode::VFS_FILE;
        if (name) { memcpy(node->name, (char*)name, strlen(name) + 1); }
        else { from_short_name(entry.name, node->name); }
        return node;
    }

    bool FAT32Driver::next_entry(uint32_t directoryCluster, uint64_t& slot, uint64_t& inode,
        FAT32DirectoryEntry& entry, char* name, uint64_t& firstSlot)
    {
        LongName longName;
        while (read_slot(directoryCluster, slot, inode, entry))
        {
            uint64_t currentSlot = slot++;
            if (entry.name[0] == FAT32_ENTRY_END) { return false; }
            if ((uint8_t)entry.name[0] == FAT32_ENTRY_DELETED) { longName.count = longName.expected = 0; continue; }
            if (entry.attributes == FAT32_ATTRIBUTE_LONG_NAME) { longName.consume(entry, currentSlot); continue; }
            bool hasSequence = longName.matches(entry);
            bool hasLongName = hasSequence && decode_long_name(longName.characters, longName.count * FAT32_LONG_NAME_PART, name);
            firstSlot = hasSequence ? longName.firstSlot : currentSlot;
            longName.count = longName.expected = 0;
            if (!is_visible_entry(entry)) { continue; }
            if (!hasLongName) { from_short_name(entry.name, name); }
            return true;
        }
        return false;
    }

    // Find the entry with a name in a directory. The caller holds the spinlock, so the entry cannot change before it is used.
    bool FAT32Driver::find_entry(uint32_t directoryCluster, const char* name, uint64_t& inode, FAT32DirectoryEntry& entry,
        char* entryName, uint64_t* firstSlot, uint64_t* entrySlot)
    {
        uint64_t slot = 0;
        uint64_t start;
        char resolvedName[256];
        while (next_entry(directoryCluster, slot, inode, entry, resolvedName, start))
        {
            char shortName[13];
            from_short_name(entry.name, shortName);
            if (!names_equal(resolvedName, name) && !names_equal(shortName, name)) { continue; }
            if (entryName) { memcpy(entryName, resolvedName, strlen(resolvedName) + 1); }
            if (firstSlot) { *firstSlot = start; }
            if (entrySlot) { *entrySlot = slot - 1; }
            return true;
        }
        return false;
    }

    VirtualFilesystemNode* FAT32Driver::vfsdriver_finddir(VirtualFilesystemNode* vnode, char* name)
    {
        m_spinlock.acquire();
        uint64_t inode;
        FAT32DirectoryEntry entry;
        char entryName[256];
        bool isFound = find_entry(get_first_cluster(vnode), name, inode, entry, entryName);
        m_spinlock.release();
        return isFound ? make_vnode(inode, entry, entryName) : nullptr;
    }

    VirtualFilesystemDirectoryEntry* FAT32Driver::vfsdriver_readdir(VirtualFilesystemNode* vnode, uint64_t index)
    {
        m_spinlock.acquire();
        auto directoryCluster = get_first_cluster(vnode);
        uint64_t inode;
        FAT32DirectoryEntry entry;
        uint64_t slot = 0;
        uint64_t firstSlot;
        uint64_t visibleIndex = 0;
        char name[256];
        while (next_entry(directoryCluster, slot, inode, entry, name, firstSlot))
        {
            if (visibleIndex++ == index)
            {
                m_spinlock.release();
                // The caller is responsible for freeing the directory entry.
                auto directoryEntry = new VirtualFilesystemDirectoryEntry();
                directoryEntry->inodeNum = inode;
                memcpy(directoryEntry->name, name, strlen(name) + 1);
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
        // A FAT32 file's size is 32 bits, so a file stops just short of 4GiB.
        if (offset >= FAT32_MAX_FILE_SIZE) { return 0; }
        if (size > FAT32_MAX_FILE_SIZE - offset) { size = FAT32_MAX_FILE_SIZE - offset; }
        m_spinlock.acquire();
        FAT32DirectoryEntry entry;
        read_entry(vnode->inodeNum, entry);
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
        // A write past the end leaves a gap of zeros. New clusters are filled with zeros, but the rest of the last
        // cluster may hold old data, so that part of the gap is cleared.
        uint64_t gapEnd = BYTE_ALIGN_UP(entry.size, clusterSize) < offset ? BYTE_ALIGN_UP(entry.size, clusterSize) : offset;
        for (uint64_t position = entry.size; position < gapEnd;)
        {
            uint32_t cluster = get_cluster_at(get_entry_cluster(entry), position / clusterSize, false);
            if (!cluster) { break; }
            uint32_t sectorNumber = cluster_to_sector(cluster) + (position % clusterSize) / ATA_SECTOR_SIZE;
            Drivers::ATADriver::instance().read_sector(sectorNumber, sector);
            uint64_t sectorOffset = position % ATA_SECTOR_SIZE;
            uint64_t count = ATA_SECTOR_SIZE - sectorOffset;
            if (count > gapEnd - position) { count = gapEnd - position; }
            memset(&sector[sectorOffset], 0, count);
            Drivers::ATADriver::instance().write_sector(sectorNumber, sector);
            position += count;
        }

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
        if (bytesWritten && offset + bytesWritten > entry.size) { entry.size = offset + bytesWritten; }
        write_entry(vnode->inodeNum, entry);
        m_spinlock.release();
        return bytesWritten;
    }

    VirtualFilesystemNode* FAT32Driver::vfsdriver_create(VirtualFilesystemNode* vnode, char* name, bool isExclusive)
    {
        m_spinlock.acquire();
        auto directoryCluster = get_first_cluster(vnode);
        uint64_t inode;
        FAT32DirectoryEntry entry;
        char entryName[256];
        if (find_entry(directoryCluster, name, inode, entry, entryName))
        {
            m_spinlock.release();
            return isExclusive ? nullptr : make_vnode(inode, entry, entryName);
        }
        // A new file has no clusters until it is written to.
        bool isCreated = add_entry(directoryCluster, name, FAT32_ATTRIBUTE_ARCHIVE, 0, inode, entry);
        m_spinlock.release();
        return isCreated ? make_vnode(inode, entry, name) : nullptr;
    }

    VirtualFilesystemNode* FAT32Driver::vfsdriver_mkdir(VirtualFilesystemNode* vnode, char* name)
    {
        m_spinlock.acquire();
        auto parentCluster = get_first_cluster(vnode);
        uint64_t inode;
        FAT32DirectoryEntry entry;
        if (find_entry(parentCluster, name, inode, entry))
        {
            m_spinlock.release();
            return nullptr;
        }
        uint32_t cluster = allocate_cluster();
        if (!cluster)
        {
            m_spinlock.release();
            return nullptr;
        }
        // A directory starts with . for itself and .. for its parent, and .. is cluster 0 when the parent is the root.
        FAT32DirectoryEntry dotEntry;
        make_entry(dotEntry, ".          ", FAT32_ATTRIBUTE_DIRECTORY, cluster);
        write_entry(cluster_to_sector(cluster) * FAT32_ENTRIES_PER_SECTOR, dotEntry);
        make_entry(dotEntry, "..         ", FAT32_ATTRIBUTE_DIRECTORY, vnode->inodeNum ? parentCluster : 0);
        write_entry(cluster_to_sector(cluster) * FAT32_ENTRIES_PER_SECTOR + 1, dotEntry);

        bool isCreated = add_entry(parentCluster, name, FAT32_ATTRIBUTE_DIRECTORY, cluster, inode, entry);
        if (!isCreated) { free_chain(cluster); }
        m_spinlock.release();
        return isCreated ? make_vnode(inode, entry, name) : nullptr;
    }

    // Unlike on Unix, a file is removed straight away, even if a program still has it open.
    bool FAT32Driver::vfsdriver_remove(VirtualFilesystemNode* vnode, char* name)
    {
        m_spinlock.acquire();
        uint64_t inode;
        uint64_t firstSlot;
        uint64_t entrySlot;
        auto directoryCluster = get_first_cluster(vnode);
        FAT32DirectoryEntry entry;
        bool canRemove = find_entry(directoryCluster, name, inode, entry, nullptr, &firstSlot, &entrySlot)
            && (!(entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) || is_empty_directory(get_entry_cluster(entry)));
        if (canRemove)
        {
            free_chain(get_entry_cluster(entry));
            for (uint64_t slot = firstSlot; slot <= entrySlot; slot++)
            {
                read_slot(directoryCluster, slot, inode, entry);
                entry.name[0] = FAT32_ENTRY_DELETED;
                write_entry(inode, entry);
            }
        }
        m_spinlock.release();
        return canRemove;
    }

    bool FAT32Driver::vfsdriver_truncate(VirtualFilesystemNode* vnode)
    {
        if (!(vnode->flags & VirtualFilesystemNode::VFS_FILE)) { return false; }
        m_spinlock.acquire();
        FAT32DirectoryEntry entry;
        read_entry(vnode->inodeNum, entry);
        free_chain(get_entry_cluster(entry));
        set_entry_cluster(entry, 0);
        entry.size = 0;
        write_entry(vnode->inodeNum, entry);
        m_spinlock.release();
        return true;
    }
}
