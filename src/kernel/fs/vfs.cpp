#include "fs/vfs.h"

namespace Filesystem
{

    int VirtualFilesystemManager::register_filesystem(RealFilesystem filesystem)
    {
        filesystem.m_mountNumber = m_fsAllocNum++;
        m_mountedFilesystems.append(filesystem);
        return filesystem.m_mountNumber;
    }
    
    void VirtualFilesystemManager::unregister_filesystem(int mountNum)
    {
        for (auto it = m_mountedFilesystems.first(); !it.is_end(); ++it)
        {
            if (it->m_mountNumber == mountNum)
            {
                m_mountedFilesystems.remove(it);
                break;
            }
        }
    }

    uint64_t VirtualFilesystemManager::read(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t count, void* buf)
    {
        if (!vnode) return 0;
        return vnode->driver->vfsdriver_read(vnode, offset, count, (char*)buf);
    }

    uint64_t VirtualFilesystemManager::write(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t count, void *buf)
    {
        if (!vnode) return 0;
        return vnode->driver->vfsdriver_write(vnode, offset, count, (char*)buf);
    }

    VirtualFilesystemNode* VirtualFilesystemManager::finddir(VirtualFilesystemNode* vnode, char* filename)
    {
        if (!vnode) return nullptr;
        return vnode->driver->vfsdriver_finddir(vnode, filename);
    }

    VirtualFilesystemDirectoryEntry* VirtualFilesystemManager::readdir(VirtualFilesystemNode* vnode, uint64_t index)
    {
        if (!vnode) return nullptr;
        return vnode->driver->vfsdriver_readdir(vnode, index);
    }

    uint64_t VirtualFilesystemManager::filelen(VirtualFilesystemNode* vnode)
    {
        if (!vnode) return 0;
        return vnode->driver->vfsdriver_filelen(vnode);
    }

    VirtualFilesystemNode* VirtualFilesystemManager::open_file(char* filename, uint64_t flags)
    {
        // Currently we only support one mounted filesystem.
        Common::DoublyLinkedListIterator<RealFilesystem> filesystem = m_mountedFilesystems.first();
        for (; !filesystem.is_end(); ++filesystem)
        {
            if (filesystem->m_mountNumber == 0) { break; }
        }
        if (filesystem.is_end()) { return nullptr; }

        // Find the root node of the filesystem from its hashmap
        auto rootnode = filesystem->m_virtualNodeHashmap.find(filesystem->m_rootVirtualNodeNumber)->last;

        VirtualFilesystemNode* node = &rootnode;

        char fixedLengthFileName[180];
        strcpy(fixedLengthFileName, filename);

        // We traverse through the directory tree and update the file name with the relative path from the current directory.
        while(fixedLengthFileName[0])
        {
            uint64_t substringIndex = 0;
            // Find first occurence of directory separator if it exists.
            for (char* p = fixedLengthFileName; *p!='\0'; p++)
            {
                if (*p=='/') { substringIndex = p - fixedLengthFileName; break; }
            }
            char substring[180];
            if (substringIndex)
            {
                // The node is a directory, find next node by calling driver's finddir.
                strncpy(substring, fixedLengthFileName, substringIndex);
                substring[substringIndex] = '\0';
                strcpy(fixedLengthFileName, fixedLengthFileName + substringIndex + 1);
                auto oldNode = node;
                node = finddir(node, substring);
                if (oldNode->inodeNum != rootnode.inodeNum) { delete oldNode; }
            }
            else
            {
                // We have reached the file node.
                auto oldNode = node;
                node = finddir(node, fixedLengthFileName);
                if (oldNode->inodeNum != rootnode.inodeNum) { delete oldNode; }
                fixedLengthFileName[0] = '\0';
            }
        }
        return node;
    }
}
