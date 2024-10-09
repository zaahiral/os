#include "loader/elf.h"
#include "libc/string.h"
#include "process/taskManager.h"
#include "fs/file.h"

namespace Loader
{
    /*
        This is a loader for ELF-64 executables - refer to the ELF-64 Specification: https://uclibc.org/docs/elf-64-gen.pdf
    */
   extern "C" void start_userspace_task_iretq(void* entryPoint, void* kstack);
   extern "C" void start_userspace_task_sysret(uint64_t entryPoint, uint64_t userStackPointer, uint64_t argc, uint64_t argv, uint64_t envp);

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
        // Older kernel callers load a program directly into their own task rather than using spawn.
        auto& files = *task->get_file_descriptor_table();
        if (!files.get_id_limit() && task->get_tlTable().get_physical_address().get()
            == Memory::MemoryManager::instance().get_main_address_space().get_physical_address().get())
        {
            auto console = new Filesystem::OpenFile();
            console->kind = Filesystem::OpenFile::CONSOLE;
            console->references = 3;
            for (uint64_t fd = 0; fd < 3; fd++) { files.insert_at(fd, console); }
        }
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
            // Uninitialised thread-local variables take up no memory in the program, as each thread gets its own copy.
            if ((sectionHeader->sh_flags & SHF_TLS) && sectionHeader->sh_type == SHT_NOBITS) { continue; }
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
        // We copy the ELF file to 0x400000 for the libc. Programs linked with the default linker script start at 0x401000,
        // so the page may not have been mapped for a section.
        if (!Memory::MemoryManager::instance().get_physical_address(Memory::VirtualAddress(0x400000), task->get_tlTable()).get())
        {
            auto request = Memory::VirtualMemoryAllocationRequest(Memory::VirtualAddress(0x400000));
            request.allowUserAccess = true;
            request.allowWrite = true;
            Memory::MemoryManager::instance().alloc_page(request, task->get_tlTable());
        }
        memcpy((void*)0x400000, elfFile, sizeof(Elf64_Ehdr));

        // A program has one thread, so its thread-local variables go after the program, copied from the TLS segment.
        // On x86-64 they end at the thread pointer, which points to itself, and the linker places each variable at a
        // negative offset from it, rounded to the segment's alignment.
        for (uint64_t i = 0; i < elfFile->e_phnum; i++)
        {
            auto programHeader = reinterpret_cast<Elf64_Phdr*>(reinterpret_cast<uint64_t>(elfFile) + elfFile->e_phoff + i * elfFile->e_phentsize);
            if (programHeader->p_type != PT_TLS) { continue; }
            uint64_t alignment = programHeader->p_align ? programHeader->p_align : 1;
            uint64_t blockSize = BYTE_ALIGN_UP(programHeader->p_memsz, alignment);
            uint64_t threadPointer = BYTE_ALIGN_UP(programEnd + blockSize, alignment > 16 ? alignment : 16);
            uint64_t blockStart = threadPointer - blockSize;
            for (uint64_t page = BYTE_ALIGN_DOWN(blockStart, Memory::PAGE_4KiB); page < threadPointer + sizeof(uint64_t); page += Memory::PAGE_4KiB)
            {
                if (!Memory::MemoryManager::instance().get_physical_address(Memory::VirtualAddress(page), task->get_tlTable()).get())
                {
                    auto request = Memory::VirtualMemoryAllocationRequest(Memory::VirtualAddress(page));
                    request.allowUserAccess = true;
                    request.allowWrite = true;
                    Memory::MemoryManager::instance().alloc_page(request, task->get_tlTable());
                }
            }
            memcpy((void*)blockStart, (void*)(reinterpret_cast<uint64_t>(elfFile) + programHeader->p_offset), programHeader->p_filesz);
            memset((void*)(blockStart + programHeader->p_filesz), 0, blockSize - programHeader->p_filesz);
            *reinterpret_cast<uint64_t*>(threadPointer) = threadPointer;
            task->set_thread_pointer(threadPointer);
            programEnd = threadPointer + sizeof(uint64_t);
        }

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

    // Copy the environment and a command line onto the top of the program's stack and split the command line into words,
    // followed by the envp and argv arrays that point to them. Returns the stack pointer that the program starts with.
    uint64_t push_arguments(const char* commandLine, const char* environment, uint64_t& argc, uint64_t& argv, uint64_t& envp)
    {
        uint64_t environmentLength = 0;
        uint64_t variableCount = 0;
        for (; environment[environmentLength]; variableCount++) { environmentLength += strlen(environment + environmentLength) + 1; }
        // The empty string after the last variable is copied too.
        auto stackPointer = USER_STACK_BASE + USER_STACK_SIZE - (environmentLength + 1);
        auto variables = reinterpret_cast<char*>(stackPointer);
        memcpy(variables, (char*)environment, environmentLength + 1);

        stackPointer -= strlen(commandLine) + 1;
        auto words = reinterpret_cast<char*>(stackPointer);
        strcpy(words, (char*)commandLine);

        char* arguments[MAX_PROGRAM_ARGUMENTS];
        argc = 0;
        for (char* c = words; *c && argc < MAX_PROGRAM_ARGUMENTS;)
        {
            // Spaces between words become null terminators.
            while (*c == ' ') { *c++ = '\0'; }
            if (!*c) { break; }
            arguments[argc++] = c;
            while (*c && *c != ' ') { c++; }
        }

        // The envp and argv arrays end with null pointers, and are aligned to 16 bytes.
        stackPointer = BYTE_ALIGN_DOWN(stackPointer - (variableCount + 1) * sizeof(char*), 16);
        auto envpArray = reinterpret_cast<char**>(stackPointer);
        char* variable = variables;
        for (uint64_t i = 0; i < variableCount; i++, variable += strlen(variable) + 1) { envpArray[i] = variable; }
        envpArray[variableCount] = nullptr;
        envp = stackPointer;

        stackPointer = BYTE_ALIGN_DOWN(stackPointer - (argc + 1) * sizeof(char*), 16);
        auto argvArray = reinterpret_cast<char**>(stackPointer);
        for (uint64_t i = 0; i < argc; i++) { argvArray[i] = arguments[i]; }
        argvArray[argc] = nullptr;
        argv = stackPointer;
        // The program starts as if its entry point had been called, which leaves the stack 8 bytes below a 16-byte boundary.
        return stackPointer - 8;
    }

    void user_program_task()
    {
        Task::TaskManager::instance().lock();
        auto task = Task::TaskManager::instance().get_current_task_pointer();
        auto start = static_cast<ProgramStart*>(task->get_argument());
        auto commandLine = start->commandLine;
        // The program's path is the first word of its command line.
        char path[MAX_COMMAND_LINE_LENGTH];
        uint64_t length = 0;
        while (commandLine[length] && commandLine[length] != ' ') { path[length] = commandLine[length]; length++; }
        path[length] = '\0';

        bool isLoaded = prepare_elf(path, task);
        auto entryPoint = task->get_entry_point();
        uint64_t argc = 0;
        uint64_t argv = 0;
        uint64_t envp = 0;
        uint64_t stackPointer = 0;
        if (isLoaded) { stackPointer = push_arguments(commandLine, start->environment, argc, argv, envp); }
        delete start;
        Task::TaskManager::instance().unlock();

        // A program that cannot be loaded exits straight away with an error code.
        if (!isLoaded) { Task::TaskManager::instance().exit_task(-1); }
        // The task is already running, so its thread pointer is set now rather than on its next task switch.
        CPU::set_fs_base(Task::TaskManager::instance().get_current_task_pointer()->get_thread_pointer());
        start_userspace_task_sysret((uint64_t)entryPoint, stackPointer, argc, argv, envp);
    }

    Task::TaskID spawn_user_program(const char* path, const char* arguments, const char* environment, const SpawnFileAction* actions, uint64_t actionCount)
    {
        // The task keeps its own copy of the command line and environment, which it frees once the program is loaded.
        // Arguments that would make the command line too long are cut short.
        auto start = new ProgramStart();
        auto argument = start->commandLine;
        uint64_t length = 0;
        for (const char* c = path; *c && length < MAX_COMMAND_LINE_LENGTH - 1; c++) { argument[length++] = *c; }
        if (arguments && *arguments && length < MAX_COMMAND_LINE_LENGTH - 1) { argument[length++] = ' '; }
        for (const char* c = arguments; c && *c && length < MAX_COMMAND_LINE_LENGTH - 1; c++) { argument[length++] = *c; }
        argument[length] = '\0';

        // Variables that do not fit in the environment are left out.
        if (!environment) { environment = DEFAULT_ENVIRONMENT; }
        length = 0;
        while (environment && environment[length])
        {
            uint64_t variableLength = strlen(environment + length) + 1;
            if (length + variableLength >= MAX_ENVIRONMENT_LENGTH) { break; }
            memcpy(start->environment + length, (char*)environment + length, variableLength);
            length += variableLength;
        }
        start->environment[length] = '\0';

        auto& manager = Task::TaskManager::instance();
        manager.lock();
        Task::FileDescriptorTable files;
        auto parent = manager.get_current_task_pointer();
        if (parent && parent->get_heap_start())
        {
            auto& parentFiles = *parent->get_file_descriptor_table();
            for (uint64_t fd = 0; fd < parentFiles.get_id_limit(); fd++)
            {
                auto entry = parentFiles.get(fd);
                if (entry.is_end()) { continue; }
                Filesystem::retain_file(entry->last);
                files.insert_at(fd, entry->last);
            }
        }
        else
        {
            auto console = new Filesystem::OpenFile();
            console->kind = Filesystem::OpenFile::CONSOLE;
            console->references = 3;
            for (uint64_t fd = 0; fd < 3; fd++) { files.insert_at(fd, console); }
        }
        bool isValid = true;
        for (uint64_t i = 0; i < actionCount && isValid; i++)
        {
            auto& action = actions[i];
            if (action.operation == SPAWN_DUP2)
            {
                isValid = Filesystem::duplicate_file(files, action.fd, action.newFd) >= 0;
            }
            else if (action.operation == SPAWN_CLOSE) { isValid = Filesystem::close_file(files, action.fd); }
            else { isValid = false; }
        }
        if (!isValid)
        {
            Filesystem::close_files(files);
            manager.unlock();
            delete start;
            return -2;
        }
        auto task = manager.create_new_user_task();
        // The temporary table transfers its references to the new task's table.
        auto& childFiles = *task.get_file_descriptor_table();
        for (uint64_t fd = 0; fd < files.get_id_limit(); fd++)
        {
            auto entry = files.get(fd);
            if (!entry.is_end()) { childFiles.insert_at(fd, entry->last); }
        }
        manager.unlock();
        task.set_entry_point((void*)& user_program_task);
        task.set_argument(start);
        Task::TaskManager::instance().add_task(task);
        return task.get_task_id();
    }
}
