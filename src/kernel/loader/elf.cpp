#include "loader/elf.h"
#include "libc/string.h"
#include "process/taskManager.h"

namespace Loader
{
    /*
        This is a loader for ELF-64 executables - refer to the ELF-64 Specification: https://uclibc.org/docs/elf-64-gen.pdf
    */
   extern "C" void start_userspace_task_iretq(void* entryPoint, void* kstack);
   extern "C" void start_userspace_task_sysret(uint64_t entryPoint, uint64_t userStackPointer);

    bool check_elf_header_ident(Elf64_Ehdr* header)
    {
        if (!header) { return false; }
        if (header->e_ident[EI_MAG0] != ELFMAG0 || header->e_ident[EI_MAG1] != ELFMAG1
            || header->e_ident[EI_MAG2] != ELFMAG2 || header->e_ident[EI_MAG3] != ELFMAG3) { return false; }
        return true;
    }

    bool check_elf_header_support(Elf64_Ehdr* header)
    {
        if (header->e_ident[EI_CLASS] != ELFCLASS64) { return false; }
        if (header->e_ident[EI_DATA] != ELFDATA2LSB) { return false; }
        if (header->e_machine != EM_AMD64) { return false; }
        if (header->e_ident[EI_VERSION] != EV_CURRENT) { return false; }
        if (header->e_type != ET_REL && header->e_type != ET_EXEC) { return false; }
        return true;
    }

    Elf64_Shdr* get_section_header(Elf64_Ehdr* header)
    {
        return reinterpret_cast<Elf64_Shdr*>(reinterpret_cast<uint64_t>(header) + header->e_shoff);
    }

    Elf64_Shdr* get_section(Elf64_Ehdr* header, int index)
    {
        return &get_section_header(header)[index];
    }

    void* get_string_table(Elf64_Ehdr* header)
    {
        return reinterpret_cast<uint8_t*>(header) + get_section(header, header->e_shstrndx)->sh_offset;
    }

    char* lookup_string_in_table(Elf64_Ehdr* header, int offset)
    {
        char* table = static_cast<char*>(get_string_table(header));
        return table + offset;
    }

    bool prepare_elf(char* path, Task::Task* task)
    {
        // Load the ELF file into memory.
        auto fd = Filesystem::VirtualFilesystemManager::instance().open_file(path, 0);
        if (!fd || !(fd->flags & Filesystem::VirtualFilesystemNode::VFS_FILE))
        {
            if (fd) { delete fd; }
            return false;
        }
        auto elfFile = reinterpret_cast<Elf64_Ehdr*>(kmalloc(Filesystem::VirtualFilesystemManager::instance().filelen(fd), 0));
        Filesystem::VirtualFilesystemManager::instance().read(fd, 0, Filesystem::VirtualFilesystemManager::instance().filelen(fd), elfFile);
        delete fd;
        if (!check_elf_header_ident(elfFile) || !check_elf_header_support(elfFile))
        {
            kfree(elfFile);
            return false;
        }

        // The heap starts on the page after the end of the last section.
        uint64_t programEnd = 0;

        // Iterate through ELF headers.
        for (uint8_t* idx = (uint8_t*)(((uint64_t)elfFile)+elfFile->e_shoff); (uint64_t)idx < ((uint64_t)elfFile)+elfFile->e_shoff+elfFile->e_shentsize * elfFile->e_shnum; idx += elfFile->e_shentsize)
        {
            auto sectionHeader = (Elf64_Shdr*)idx;
            // If the section has a virtual address, we need to load it in.
            if (sectionHeader->sh_addr)
            {
                // Allocate pages for the section.
                if (sectionHeader->sh_addr + sectionHeader->sh_size > programEnd) { programEnd = sectionHeader->sh_addr + sectionHeader->sh_size; }
                task->get_tlTable().regions.append(Memory::MappedRegion((void*)BYTE_ALIGN_DOWN(sectionHeader->sh_addr, Memory::PAGE_4KiB), (void*)BYTE_ALIGN_DOWN(sectionHeader->sh_addr, Memory::PAGE_4KiB) + sectionHeader->sh_size + Memory::PAGE_4KiB*2));
                for (uint8_t* page = (uint8_t*)(BYTE_ALIGN_DOWN(sectionHeader->sh_addr, Memory::PAGE_4KiB)); (uint64_t)page < BYTE_ALIGN_DOWN(sectionHeader->sh_addr, Memory::PAGE_4KiB) + sectionHeader->sh_size + Memory::PAGE_4KiB*2; page+= Memory::PAGE_4KiB)
                {
                    if (!Memory::MemoryManager::instance().get_physical_address(page, task->get_tlTable()).get())
                    {
                        auto request = Memory::VirtualMemoryAllocationRequest(page);
                        request.allowUserAccess = true;
                        request.allowWrite = true;
                        Memory::MemoryManager::instance().alloc_page(request, task->get_tlTable());
                    }
                }
                if (sectionHeader->sh_type == SHT_NOBITS)
                {
                    // These sections should be filled with zeros.
                    memset((void*)(sectionHeader->sh_addr), 0, sectionHeader->sh_size);
                } else
                {
                    // Copy the section data from the ELF file into memory.
                    memcpy((void*)(sectionHeader->sh_addr), (void *)(((uint64_t)elfFile) + sectionHeader->sh_offset), sectionHeader->sh_size);
                }
            }
        }
        // We copy the ELF file to 0x400000 for the libc.
        memcpy((void*)0x400000, elfFile, sizeof(Elf64_Ehdr));

        // Allocate the program's stack.
        for (uint8_t* userStackPointer = (uint8_t*)USER_STACK_BASE; userStackPointer < (uint8_t*)(USER_STACK_BASE + USER_STACK_SIZE); userStackPointer += Memory::PAGE_4KiB)
        {
            if (!Memory::MemoryManager::instance().get_physical_address(userStackPointer, task->get_tlTable()).get())
            {
                auto request = Memory::VirtualMemoryAllocationRequest(userStackPointer);
                request.allowUserAccess = true;
                request.allowWrite = true;
                Memory::MemoryManager::instance().alloc_page(request, task->get_tlTable());
            }
        }
        task->set_heap_start(BYTE_ALIGN_UP(programEnd, Memory::PAGE_4KiB));
        task->set_program_break(task->get_heap_start());
        task->set_next_map_address(USER_MAP_BASE);

        // The entry point must be read before the ELF file is freed.
        task->set_entry_point((void*)(elfFile->e_entry));
        kfree(elfFile);
        return true;
    }

    void user_program_task()
    {
        Task::TaskManager::instance().lock();
        auto task = Task::TaskManager::instance().get_current_task_pointer();
        auto path = static_cast<char*>(task->get_argument());
        bool isLoaded = prepare_elf(path, task);
        auto entryPoint = task->get_entry_point();
        delete[] path;
        Task::TaskManager::instance().unlock();

        // A program that cannot be loaded exits straight away with an error code.
        if (!isLoaded) { Task::TaskManager::instance().exit_task(-1); }
        start_userspace_task_sysret((uint64_t)entryPoint, USER_STACK_BASE + USER_STACK_SIZE);
    }

    Task::TaskID spawn_user_program(const char* path)
    {
        // The task keeps its own copy of the path, which it frees once the program is loaded.
        auto argument = new char[strlen(path) + 1];
        strcpy(argument, (char*)path);
        auto task = Task::TaskManager::instance().create_new_user_task();
        task.set_entry_point((void*)& user_program_task);
        task.set_argument(argument);
        Task::TaskManager::instance().add_task(task);
        return task.get_task_id();
    }
}
