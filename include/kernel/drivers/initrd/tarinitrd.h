#ifndef TARINITRD_H
#define TARINITRD_H

#include "fs/vfs.h"
#include "types.h"
#include "libc/string.h"
#include "common/hashmap.h"
#include "common/doublyLinkedList.h"

namespace Filesystem {

class TarReader : VFSDriver
{
    class TarHeader
    {
    private:
        char m_filename[100];
        char m_mode[8];
        char m_uid[8];
        char m_gid[8];
        char m_size[12];
        char m_mtime[12];
        char m_chksum[8];
        char m_typeflag[1];
        char m_reserved[355];
    public:
        char* get_filename() { return m_filename; }
        void set_filename(char* filename) { strcpy(m_filename, filename); }
        uint64_t get_size()
        {
            uint64_t size = 0;
            unsigned int i;
            uint64_t count = 1;
        
            for (i = 11; i > 0; i--, count *= 8)
                size += ((m_size[i - 1] - '0') * count);
        
            return size; 
        }
        TarHeader* get_next_header()
        {
            auto header = this;
            header++;
            auto pointer = reinterpret_cast<uint8_t*>(header);
            pointer += BYTE_ALIGN_UP(get_size(), TAR_ALIGN);
            return reinterpret_cast<TarHeader*>(pointer);
        }
    } __attribute__((packed));

    struct TarDirectoryEntry
    {
        char filename[256];
        uint64_t inodeNum;
    };

    struct TarDirectoryDescriptor
    {
        Common::DoublyLinkedList<TarDirectoryEntry> entries;
    };

 public:
    static TarReader& instance()
    {
        static TarReader instance;
        return instance;
    }
    TarReader(TarReader const&) = delete;
    TarReader& operator=(TarReader const&) = delete;
    void load_initrd();
    uint64_t vfsdriver_read(VirtualFilesystemNode*, uint64_t, uint64_t, char*);
    uint64_t vfsdriver_write(VirtualFilesystemNode*, uint64_t, uint64_t, char*);
    void vfsdriver_open(VirtualFilesystemNode*, uint64_t);
    void vfsdriver_close(VirtualFilesystemNode*);
    VirtualFilesystemDirectoryEntry* vfsdriver_readdir(VirtualFilesystemNode*, uint64_t index);
    VirtualFilesystemNode* vfsdriver_finddir(VirtualFilesystemNode*, char* name);
    // The initrd is read-only, so files cannot be created in it.
    VirtualFilesystemNode* vfsdriver_create(VirtualFilesystemNode*, char* name) { return nullptr; }
    uint64_t vfsdriver_filelen(VirtualFilesystemNode*);
private:
    static constexpr uint64_t INITRD_ADDRESS = 0xFFFFFFFFA0000000;
    static constexpr uint64_t TAR_ALIGN = 0x200;
    TarReader() {}
    uint64_t m_mountNum;
    uint64_t m_headerCount;
    uint64_t read_header(TarHeader* header, uint64_t offset, uint64_t size, uint8_t* buf);
    Common::Hashmap<uint64_t, TarDirectoryDescriptor> m_directoryMap;
    Common::Hashmap<uint64_t, TarHeader*> m_fileMap;
    uint64_t m_dirVirtualNodeNum;
    void resolve_path(char* substr, uint64_t fileInodeNum, TarHeader* currentHeader);
    VirtualFilesystemNode* make_vnode_from_direntry(TarDirectoryEntry entry);

};

}

#endif

