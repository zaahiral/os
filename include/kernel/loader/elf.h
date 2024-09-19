#ifndef ELF_H
#define ELF_H

#include "fs/vfs.h"
#include "memory/MemoryManager.h"
#include "process/task.h"
#include "types.h"

typedef uint64_t    Elf64_Addr;
typedef uint16_t    Elf64_Half;
typedef uint64_t    Elf64_Off;
typedef int32_t     Elf64_Sword;
typedef int64_t     Elf64_Sxword;
typedef uint32_t    Elf64_Word;
typedef uint64_t    Elf64_Lword;
typedef uint64_t    Elf64_Xword;

#define EI_NIDENT 16

typedef struct {
    unsigned char   e_ident[EI_NIDENT]; /* File identification. */
    Elf64_Half  e_type;     /* File type. */
    Elf64_Half  e_machine;  /* Machine architecture. */
    Elf64_Word  e_version;  /* ELF format version. */
    Elf64_Addr  e_entry;    /* Entry point. */
    Elf64_Off   e_phoff;    /* Program header file offset. */
    Elf64_Off   e_shoff;    /* Section header file offset. */
    Elf64_Word  e_flags;    /* Architecture-specific flags. */
    Elf64_Half  e_ehsize;   /* Size of ELF header in bytes. */
    Elf64_Half  e_phentsize;    /* Size of program header entry. */
    Elf64_Half  e_phnum;    /* Number of program header entries. */
    Elf64_Half  e_shentsize;    /* Size of section header entry. */
    Elf64_Half  e_shnum;    /* Number of section header entries. */
    Elf64_Half  e_shstrndx; /* Section name strings section. */
} Elf64_Ehdr;

#define ELFMAG0 0x7F 
#define ELFMAG1 'E'  
#define ELFMAG2 'L'  
#define ELFMAG3 'F'

#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2

#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB

enum Elf_Ident {
    EI_MAG0     = 0,
    EI_MAG1     = 1,
    EI_MAG2     = 2,
    EI_MAG3     = 3,
    EI_CLASS    = 4,
    EI_DATA     = 5,
    EI_VERSION  = 6,
    EI_OSABI    = 7,
    EI_ABIVER   = 8, 
    EI_PAD      = 9  
};

enum Elf_Type {
    ET_NONE     = 0,
    ET_REL      = 1,
    ET_EXEC     = 2,
    ET_DYN      = 3,
    ET_CORE     = 4,
    ET_LOPROC   = 0xff00,
    ET_HIPROC   = 0xffff
};

enum Elf_Machine {
    EM_NONE         = 0,
    EM_M32          = 1,
    EM_SPARC        = 2,
    EM_386          = 3,
    EM_68K          = 4,
    EM_88K          = 5,
    EM_860          = 7,
    EM_MIPS         = 8,
    EM_MIPS_RS4_BE  = 10,
    EM_AMD64        = 62
};

enum Elf_Version {
    EV_NONE     = 0,
    EV_CURRENT  = 1
};

typedef struct
{
    Elf64_Word sh_name;
    Elf64_Word sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr sh_addr;
    Elf64_Off sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word sh_link;
    Elf64_Word sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
} Elf64_Shdr;

enum sh_type
{
    SHT_NULL = 0,
    SHT_PROGBITS = 1,
    SHT_SYMTAB = 2,
    SHT_STRTAB = 3,
    SHT_RELA = 4,
    SHT_HASH = 5,
    SHT_DYNAMIC = 6,
    SHT_NOTE = 7,
    SHT_NOBITS = 8,
    SHT_REL = 9,
    SHT_SHLIB = 10,
    SHT_DYNSYM = 11
};

namespace Loader
{
    // Each program has a 16KiB stack at 0x1000000.
    static constexpr uint64_t USER_STACK_BASE = 0x1000000;
    static constexpr uint64_t USER_STACK_SIZE = 0x4000;
    // Memory from mmap starts at 1GiB, well away from the program, its heap and its stack.
    static constexpr uint64_t USER_MAP_BASE = 0x40000000;
    // A program's command line is its path followed by its arguments, separated by spaces.
    static constexpr uint64_t MAX_COMMAND_LINE_LENGTH = 256;
    static constexpr uint64_t MAX_PROGRAM_ARGUMENTS = 16;

    bool check_elf_header_ident(Elf64_Ehdr *header);
    bool check_elf_header_support(Elf64_Ehdr *header);
    // Load an ELF file into a task's address space, and return false if it is not a valid executable.
    bool prepare_elf(char* path, Task::Task* task);
    // The entry point of a task that runs a user-space program, whose command line is the task's argument.
    void user_program_task();
    // Start a user-space program in a new task with optional arguments, and return the task's ID.
    Task::TaskID spawn_user_program(const char* path, const char* arguments = nullptr);
} 

#endif
