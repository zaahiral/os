#include "drivers/initrd/tarinitrd.h"

namespace Filesystem {

    // By resolving a path, we add directories to the directory map and add files to the file map.
    void TarReader::resolve_path(char* substring, uint64_t fileInodeNum, TarHeader* currentHeader)
    {
        // We start with the root directory node.
        uint64_t workingDirectory = 0;

        while (substring[0])
        {
            bool isDirectory = false;
            uint64_t substringIndex = 0;
            // Check if the substring contains a directory.
            for (char* pointer = substring; *pointer!='\0'; pointer++)
            {
                if (*pointer == '/')
                {
                    isDirectory = true;
                    substringIndex = pointer - substring;
                    break;
                }
            }
            if (isDirectory)
            {
                char directoryName[100];
                // Get directory name
                strncpy(directoryName, substring, substringIndex);
                directoryName[substringIndex] = '\0';
                strcpy(substring, substring + substringIndex + 1);

                Common::DoublyLinkedListIterator<TarDirectoryEntry> it = (&m_directoryMap.find(0)->last)->entries.first();
                // Try and find directory in map.
                for (it=m_directoryMap.find(workingDirectory)->last.entries.first(); !it.is_end(); ++it)
                {
                    if (!strcmp(it->filename, directoryName)) { break; }
                }
                // Add directory to directory map and to parent directory's entries.
                if (it.is_end())
                {
                    m_directoryMap.insert(++m_dirVirtualNodeNum, TarDirectoryDescriptor());
                    auto entry = TarDirectoryEntry();
                    strcpy(entry.filename, directoryName);
                    entry.inodeNum = m_dirVirtualNodeNum;
                    m_directoryMap.find(workingDirectory)->last.entries.append(entry);
                }
                // FIXME: set this to it in the case that the dir already exists
                workingDirectory = m_dirVirtualNodeNum;
            }
            else
            {
                // Add file to directory and to file map.
                if (substring[0] == '\0') { return; }
                auto entry = TarDirectoryEntry();
                entry.inodeNum = fileInodeNum;
                strcpy(entry.filename, substring);
                m_directoryMap.find(workingDirectory)->last.entries.append(entry);
                m_fileMap.insert(fileInodeNum, currentHeader);
                substring[0] = '\0';
            }
        }
    }

    void TarReader::load_initrd()
    {
        auto rootNode = VirtualFilesystemNode();
        rootNode.name[0] = '\0';
        rootNode.fs = nullptr;
        rootNode.flags = VirtualFilesystemNode::VFS_DIRECTORY;
        rootNode.inodeNum = 0;
        rootNode.driver = this;

        m_mountNum = VirtualFilesystemManager::instance().register_filesystem(RealFilesystem(rootNode, this));
        m_headerCount = 0;

        for (auto header = reinterpret_cast<TarHeader*>(INITRD_ADDRESS); *header->get_filename(); header = header->get_next_header())
        {
            m_headerCount++;
        }
        
        m_dirVirtualNodeNum = m_headerCount;
        // The directory map gives us a directory descriptor from the vnode number.
        m_directoryMap = Common::Hashmap<uint64_t, TarDirectoryDescriptor>();
        // The file map gives us the file TAR header from the vnode number.
        m_fileMap = Common::Hashmap<uint64_t, TarHeader*>();

        // Insert the root directory descriptor.
        auto rootDescriptor = TarDirectoryDescriptor();
        m_directoryMap.insert(0, rootDescriptor);
        char substr[100];

        int headerNo = 1;

        for (auto header = reinterpret_cast<TarHeader*>(INITRD_ADDRESS); *header->get_filename(); header = header->get_next_header())
        {
            memcpy(substr, header->get_filename(), 100);
            resolve_path(substr, headerNo, header);
            headerNo++;
        }
    }

    uint64_t TarReader::vfsdriver_read(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t size, char* buf)
    {
        auto header = static_cast<TarHeader*>(vnode->fs);
        return read_header(header, offset, size, (uint8_t*)buf);
    }

    uint64_t TarReader::vfsdriver_write(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t size, char* buf) { return 0; }

    void TarReader::vfsdriver_open(VirtualFilesystemNode* vnode, uint64_t flags) {}

    void TarReader::vfsdriver_close(VirtualFilesystemNode* vnode) {}

    VirtualFilesystemDirectoryEntry* TarReader::vfsdriver_readdir(VirtualFilesystemNode* vnode, uint64_t index)
    {
        auto* descriptor = &m_directoryMap.find(vnode->inodeNum)->last;
        auto it = descriptor->entries.first();
        for (uint64_t i = 0; i < index && !it.is_end(); i++) { ++it; }
        if (it.is_end()) { return nullptr; }

        // The caller is responsible for freeing the directory entry.
        auto entry = new VirtualFilesystemDirectoryEntry();
        entry->inodeNum = it->inodeNum;
        strcpy(entry->name, it->filename);
        return entry;
    }

    // Get file/directory from directory VFSnode.
    VirtualFilesystemNode* TarReader::vfsdriver_finddir(VirtualFilesystemNode* vnode, char* name)
    {
        auto* descriptor = &m_directoryMap.find(vnode->inodeNum)->last;
        for (auto it = descriptor->entries.first(); !it.is_end(); ++it)
        {
            if (!strcmp(it->filename, name)) {
                return make_vnode_from_direntry(*it);
            }
        }
        return nullptr;
    }

    uint64_t TarReader::vfsdriver_filelen(VirtualFilesystemNode* vnode)
    {
        TarHeader* header = (TarHeader*)vnode->fs;
        return header->get_size();
    }

    VirtualFilesystemNode* TarReader::make_vnode_from_direntry(TarDirectoryEntry entry)
    {
        // Make a temporary vnode to return to the VFS.
        auto node = new VirtualFilesystemNode();
        node->driver = this;
        node->flags = VirtualFilesystemNode::VFS_DIRECTORY;
        // If the inode number is less than the header count, then the inode is the file header number.
        if ((entry.inodeNum <= m_headerCount) && entry.inodeNum)
        {
            node->flags = VirtualFilesystemNode::VFS_FILE;
            m_fileMap.find(entry.inodeNum);
            node->fs = m_fileMap.find(entry.inodeNum)->last;
        }
        node->inodeNum = entry.inodeNum;
        strcpy(node->name, entry.filename);
        
        return node;
    }

    uint64_t TarReader::read_header(TarHeader* header, uint64_t offset, uint64_t size, uint8_t* buf)
    {
        auto content = reinterpret_cast<uint8_t*>(header) + 512 + offset;
        memcpy(buf, content, size);
        return size;
    }
}
