#include "fs/vfs.h"

namespace Filesystem
{
    namespace
    {
        // Remove the . and .. parts of a path after checking that the preceding path is a directory.
        bool normalise_path(char* path)
        {
            char* output = path;
            char* input = path;
            while (*input)
            {
                char* end = input;
                while (*end && *end != '/') { end++; }
                uint64_t length = end - input;
                // The next part is found before anything is written, as the output may overwrite the separator.
                char* next = *end ? end + 1 : end;
                bool isCurrentDirectory = length == 1 && input[0] == '.';
                bool isParentDirectory = length == 2 && input[0] == '.' && input[1] == '.';
                if (isCurrentDirectory || isParentDirectory)
                {
                    *output = '\0';
                    auto directory = VirtualFilesystemManager::instance().open_file(path, 0);
                    bool isDirectory = directory && (directory->flags & VirtualFilesystemNode::VFS_DIRECTORY);
                    if (directory) { delete directory; }
                    if (!isDirectory) { return false; }
                }
                if (isParentDirectory)
                {
                    // Remove the last part that was kept, along with the separator before it.
                    while (output > path && *(output - 1) != '/') { output--; }
                    if (output > path) { output--; }
                }
                else if (length && !isCurrentDirectory)
                {
                    // The output is never ahead of the input, so the part can be copied forwards.
                    if (output > path) { *output++ = '/'; }
                    for (uint64_t i = 0; i < length; i++) { output[i] = input[i]; }
                    output += length;
                }
                input = next;
            }
            *output = '\0';
            return true;
        }
    }

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
        if (!vnode || !(vnode->flags & VirtualFilesystemNode::VFS_DIRECTORY)) return nullptr;
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
        char fixedLengthFileName[MAX_PATH_LENGTH];
        // cOS has no working directory, so a path that starts with / is the same as one that does not.
        while (*filename == '/') { filename++; }
        uint64_t filenameLength = strlen(filename);
        if (filenameLength >= MAX_PATH_LENGTH) { return nullptr; }
        bool needsDirectory = filenameLength && filename[filenameLength - 1] == '/';
        strcpy(fixedLengthFileName, filename);
        if (!normalise_path(fixedLengthFileName)) { return nullptr; }

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
            char substring[MAX_PATH_LENGTH];
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
        if (needsDirectory && !(node->flags & VirtualFilesystemNode::VFS_DIRECTORY))
        {
            delete node;
            return nullptr;
        }
        return node;
    }

    VirtualFilesystemNode* VirtualFilesystemManager::open_parent(char* filename, char* directoryName, char*& name)
    {
        // Split the path into the directory and the name of the file in it.
        if (strlen(filename) >= MAX_PATH_LENGTH) { return nullptr; }
        strcpy(directoryName, filename);
        name = directoryName;
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
        return directory;
    }

    VirtualFilesystemNode* VirtualFilesystemManager::create_file(char* filename, bool isExclusive)
    {
        char directoryName[MAX_PATH_LENGTH];
        char* name;
        auto directory = open_parent(filename, directoryName, name);
        if (!directory) { return nullptr; }
        auto file = directory->driver->vfsdriver_create(directory, name, isExclusive);
        delete directory;
        return file;
    }

    VirtualFilesystemNode* VirtualFilesystemManager::create_directory(char* filename)
    {
        char directoryName[MAX_PATH_LENGTH];
        char* name;
        auto directory = open_parent(filename, directoryName, name);
        if (!directory) { return nullptr; }
        auto newDirectory = directory->driver->vfsdriver_mkdir(directory, name);
        delete directory;
        return newDirectory;
    }

    bool VirtualFilesystemManager::remove(char* filename)
    {
        char directoryName[MAX_PATH_LENGTH];
        char* name;
        auto directory = open_parent(filename, directoryName, name);
        if (!directory) { return false; }
        bool isRemoved = directory->driver->vfsdriver_remove(directory, name);
        delete directory;
        return isRemoved;
    }

    bool VirtualFilesystemManager::truncate(VirtualFilesystemNode* vnode)
    {
        if (!vnode) { return false; }
        return vnode->driver->vfsdriver_truncate(vnode);
    }
}
