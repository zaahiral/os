#ifndef VFS_H
#define VFS_H
#include "types.h"
#include "common/doublyLinkedList.h"
#include "common/hashmap.h"
#include "libc/string.h"

namespace Filesystem
{

    struct VirtualFilesystemNode;
    struct VirtualFilesystemDirectoryEntry;

    class VFSDriver
    {
    public: 
        virtual uint64_t vfsdriver_read(VirtualFilesystemNode*, uint64_t, uint64_t, char*) = 0;
        virtual uint64_t vfsdriver_write(VirtualFilesystemNode*, uint64_t, uint64_t, char*) = 0;
        virtual void vfsdriver_open(VirtualFilesystemNode*, uint64_t) = 0;
        virtual void vfsdriver_close(VirtualFilesystemNode*) = 0;
        virtual VirtualFilesystemDirectoryEntry* vfsdriver_readdir(VirtualFilesystemNode*, uint64_t index) = 0;
        virtual VirtualFilesystemNode* vfsdriver_finddir(VirtualFilesystemNode*, char* name) = 0;
        // Create a file in a directory node, or return nullptr if the filesystem is read-only.
        virtual VirtualFilesystemNode* vfsdriver_create(VirtualFilesystemNode*, char* name) = 0;
        // Create a directory in a directory node, or return nullptr if the filesystem is read-only.
        virtual VirtualFilesystemNode* vfsdriver_mkdir(VirtualFilesystemNode*, char* name) = 0;
        // Remove a file or an empty directory from a directory node, and return false if it cannot be removed.
        virtual bool vfsdriver_remove(VirtualFilesystemNode*, char* name) = 0;
        // Empty a file, and return false if the filesystem is read-only.
        virtual bool vfsdriver_truncate(VirtualFilesystemNode*) = 0;
        virtual uint64_t vfsdriver_filelen(VirtualFilesystemNode*) = 0;
    };

    struct VirtualFilesystemNode {
        uint64_t inodeNum;
        char name[256];

        void* fs;
        
        enum Flag
        {
            VFS_FILE = 0x01,
            VFS_DIRECTORY = 0x02
        };

        uint64_t flags = 0;
        uint64_t refcount = 0;
        
        uint64_t creationTime = 0;
        uint64_t modificationTime = 0;
        uint64_t accessedTime = 0;

        VFSDriver* driver;
    };

    struct VirtualFilesystemDirectoryEntry {
        uint64_t inodeNum;
        char name[256];
    };

    struct taskfd
    {
        uint64_t fd;
        uint64_t offset;
        Filesystem::VirtualFilesystemNode* node;
    };

    class VirtualFilesystemManager;

    // Concrete filesystems are mounted in the virtual filesystem.
    // The virtual filesystem calls the concrete filesystem's driver for file operations.
    class RealFilesystem
    {
        friend class VirtualFilesystemManager;
    protected:
        int m_mountNumber;
        VFSDriver* m_driver;
        int m_rootVirtualNodeNumber;
        Common::Hashmap<uint64_t, VirtualFilesystemNode> m_virtualNodeHashmap;
        // A filesystem with a mount point is reached through paths that start with it, such as disk/notes.txt.
        const char* m_mountPoint;
    public:
        RealFilesystem(VirtualFilesystemNode rootNode, VFSDriver* driver, const char* mountPoint = "") : m_rootVirtualNodeNumber(rootNode.inodeNum), m_driver(driver), m_mountPoint(mountPoint)
        {
            m_virtualNodeHashmap.insert(rootNode.inodeNum, rootNode);
        }
    };

    class VirtualFilesystemManager
    {
    public:
        static VirtualFilesystemManager& instance()
        {
            static VirtualFilesystemManager instance;
            return instance;
        }
        VirtualFilesystemManager(VirtualFilesystemManager const&) = delete;
        VirtualFilesystemManager& operator=(VirtualFilesystemManager const&) = delete;
        int register_filesystem(RealFilesystem filesystem); 
        void unregister_filesystem(int mountNum);
        
        VirtualFilesystemNode* open_file(char* filename, uint64_t flags);
        // Create a file, or open it if it already exists. Returns nullptr if the filesystem is read-only.
        VirtualFilesystemNode* create_file(char* filename);
        // Create a directory. Returns nullptr if it already exists, or the filesystem is read-only.
        VirtualFilesystemNode* create_directory(char* filename);
        // Remove a file or an empty directory, and return false if it cannot be removed.
        bool remove(char* filename);
        // Empty a file, and return false if the filesystem is read-only.
        bool truncate(VirtualFilesystemNode* vnode);
        // Read count bytes from offset into buf.
        uint64_t read(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t count, void *buf);
        // Write count bytes from offset into buf.
        uint64_t write(VirtualFilesystemNode* vnode, uint64_t offset, uint64_t count, void *buf);
        // Get item in directory node.
        VirtualFilesystemNode* finddir(VirtualFilesystemNode* vnode, char* filename);
        // Get the entry at an index in a directory node, or nullptr past the last entry.
        VirtualFilesystemDirectoryEntry* readdir(VirtualFilesystemNode* vnode, uint64_t index);
        uint64_t filelen(VirtualFilesystemNode* vnode);
    private:
        VirtualFilesystemManager() { }
        // Open the directory that a path is in, and point name at the last part of the path, which is copied into directoryName.
        VirtualFilesystemNode* open_parent(char* filename, char* directoryName, char*& name);
        int m_fsAllocNum = 0;
        Common::DoublyLinkedList<RealFilesystem> m_mountedFilesystems;
    };
}

#endif
