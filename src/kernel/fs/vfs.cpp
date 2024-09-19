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
        char fixedLengthFileName[180];
        strcpy(fixedLengthFileName, filename);

        // If the first part of the path is a mount point, we start from that filesystem's root and remove the mount point from the path.
        // Otherwise, we start from the root of the first mounted filesystem.
        Common::DoublyLinkedListIterator<RealFilesystem> filesystem = m_mountedFilesystems.first();
        for (auto it = m_mountedFilesystems.first(); !it.is_end(); ++it)
        {
            uint64_t length = strlen(it->m_mountPoint);
            if (!length || strncmp(fixedLengthFileName, it->m_mountPoint, length)) { continue; }
            if (fixedLengthFileName[length] != '/' && fixedLengthFileName[length] != '\0') { continue; }
            filesystem = it;
            strcpy(fixedLengthFileName, fixedLengthFileName + length + (fixedLengthFileName[length] == '/'));
            break;
        }
        if (filesystem.is_end()) { return nullptr; }

        // Find the root node of the filesystem from its hashmap
        auto rootnode = filesystem->m_virtualNodeHashmap.find(filesystem->m_rootVirtualNodeNumber)->last;
        // The root node is a local copy, so if the path names the root itself we return a copy that the caller can free.
        if (!fixedLengthFileName[0]) { return new VirtualFilesystemNode(rootnode); }

        VirtualFilesystemNode* node = &rootnode;

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
            // A directory in the path does not exist.
            if (!node) { return nullptr; }
        }
        return node;
    }

    VirtualFilesystemNode* VirtualFilesystemManager::create_file(char* filename)
    {
        // Split the path into the directory and the name of the file to create in it.
        char directoryName[180];
        strcpy(directoryName, filename);
        char* name = directoryName;
        for (char* p = directoryName; *p; p++)
        {
            if (*p == '/') { name = p + 1; }
        }
        if (name == directoryName || !*name) { return nullptr; }
        *(name - 1) = '\0';

        auto directory = open_file(directoryName, 0);
        if (!directory || !(directory->flags & VirtualFilesystemNode::VFS_DIRECTORY))
        {
            if (directory) { delete directory; }
            return nullptr;
        }
        auto file = directory->driver->vfsdriver_create(directory, name);
        delete directory;
        return file;
    }
}
