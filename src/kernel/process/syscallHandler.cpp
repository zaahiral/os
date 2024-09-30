#include "process/syscallHandler.h"
#include "process/taskManager.h"
#include "loader/elf.h"
#include "event/dispatcher.h"
#include "cpu/io/pit.h"
#include "print.h"

// TODO: Validate these syscalls so that everything is checked, as the arguments are from ring 3 and
// a broken/malicious program could cause a kernel crash

static constexpr uint64_t USER_ADDRESS_LIMIT = 0x0000800000000000;
// TODO: Allow vmmap to map files, not only anonymous memory

uint64_t sleep_for_syscall(uint64_t duration)
{
    Task::TaskManager::instance().sleep_for(duration);
    return 0;
}

// Arguments from ring 3 must not point into kernel memory, which starts in the higher half.
bool is_user_address(uint64_t address, uint64_t length)
{
    return address < USER_ADDRESS_LIMIT && length <= USER_ADDRESS_LIMIT - address;
}

// Map a zeroed, writable page into a user-space program's address space, unless it is already mapped.
// The program's address space is the current one, so the page can be cleared through its own address.
void map_user_page(uint64_t address, Memory::RegionableVirtualAddressSpace& addressSpace)
{
    if (!Memory::MemoryManager::instance().get_physical_address(address, addressSpace).get())
    {
        Memory::MemoryManager::instance().alloc_page(Memory::VirtualMemoryAllocationRequest(Memory::VirtualAddress(address), true, true), addressSpace);
    }
    memset((void*)address, 0, Memory::PAGE_4KiB);
}

void unmap_user_page(uint64_t address, Memory::RegionableVirtualAddressSpace& addressSpace)
{
    if (Memory::MemoryManager::instance().get_physical_address(address, addressSpace).get())
    {
        Memory::MemoryManager::instance().free_page(Memory::VirtualMemoryFreeRequest(Memory::VirtualAddress(address)), addressSpace);
    }
}

// Move the program break, which is the end of the program's heap, and return the new break.
// A break of 0 returns the current break, and a break outside the heap leaves it unchanged.
uint64_t brk_syscall(uint64_t newBreak)
{
    Task::TaskManager::instance().lock();
    auto task = Task::TaskManager::instance().get_current_task_pointer();
    auto oldBreak = task->get_program_break();
    if (!newBreak || newBreak < task->get_heap_start() || newBreak > Loader::USER_STACK_BASE)
    {
        Task::TaskManager::instance().unlock();
        return oldBreak;
    }
    // Map the pages up to the new break, or free the pages above it.
    for (uint64_t page = BYTE_ALIGN_UP(oldBreak, Memory::PAGE_4KiB); page < BYTE_ALIGN_UP(newBreak, Memory::PAGE_4KiB); page += Memory::PAGE_4KiB)
        map_user_page(page, task->get_tlTable());
    for (uint64_t page = BYTE_ALIGN_UP(newBreak, Memory::PAGE_4KiB); page < BYTE_ALIGN_UP(oldBreak, Memory::PAGE_4KiB); page += Memory::PAGE_4KiB)
        unmap_user_page(page, task->get_tlTable());
    task->set_program_break(newBreak);
    Task::TaskManager::instance().unlock();
    return newBreak;
}

// Map zeroed memory of at least the given length, and return its address. Addresses are not reused after munmap.
uint64_t vmmap_syscall(uint64_t length)
{
    if (!length) { return -1; }
    Task::TaskManager::instance().lock();
    auto task = Task::TaskManager::instance().get_current_task_pointer();
    auto address = task->get_next_map_address();
    length = BYTE_ALIGN_UP(length, Memory::PAGE_4KiB);
    if (!is_user_address(address, length))
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    for (uint64_t page = address; page < address + length; page += Memory::PAGE_4KiB)
        map_user_page(page, task->get_tlTable());
    task->set_next_map_address(address + length);
    Task::TaskManager::instance().unlock();
    return address;
}

uint64_t vmumap_syscall(uint64_t address, uint64_t length)
{
    // Only memory from mmap can be unmapped, and it must start on a page boundary.
    if (address < Loader::USER_MAP_BASE || !CHECK_ALIGN(address, Memory::PAGE_4KiB) || !is_user_address(address, length)) { return -1; }
    Task::TaskManager::instance().lock();
    auto task = Task::TaskManager::instance().get_current_task_pointer();
    for (uint64_t page = address; page < address + length; page += Memory::PAGE_4KiB)
        unmap_user_page(page, task->get_tlTable());
    Task::TaskManager::instance().unlock();
    return 0;
}

uint64_t fopen_syscall(uint64_t filename, uint64_t flags)
{
    if (!is_user_address(filename, 1)) { return -1; }
    if ((flags & FILE_CREATE) && (flags & FILE_EXCLUSIVE))
    {
        auto existingNode = Filesystem::VirtualFilesystemManager::instance().open_file((char*)filename, 0);
        if (existingNode)
        {
            delete existingNode;
            return -1;
        }
    }
    // With FILE_CREATE, a file that does not exist is created.
    auto vnode = (flags & FILE_CREATE) ? Filesystem::VirtualFilesystemManager::instance().create_file((char*)filename)
                                        : Filesystem::VirtualFilesystemManager::instance().open_file((char*)filename, flags);
    if (!vnode || !(vnode->flags & Filesystem::VirtualFilesystemNode::VFS_FILE)
        || ((flags & FILE_TRUNCATE) && !Filesystem::VirtualFilesystemManager::instance().truncate(vnode)))
    {
        if (vnode) { delete vnode; }
        return -1;
    }
    Filesystem::taskfd taskFileDescriptor;
    taskFileDescriptor.node = vnode;
    // With FILE_APPEND, writes start at the end of the file.
    taskFileDescriptor.offset = (flags & FILE_APPEND) ? Filesystem::VirtualFilesystemManager::instance().filelen(vnode) : 0;

    // The file descriptor is the ID of the entry in the task's file descriptor table.
    Task::TaskManager::instance().lock();
    auto& fileDescriptorTable = *Task::TaskManager::instance().get_current_task_pointer()->get_file_descriptor_table();
    auto entry = fileDescriptorTable.insert(taskFileDescriptor);
    entry->last.fd = entry->first;
    auto fd = entry->first;
    Task::TaskManager::instance().unlock();
    return fd;
}

uint64_t fread_syscall(uint64_t fd, uint64_t buf, uint64_t count)
{
    if (!is_user_address(buf, count)) { return -1; }
    Task::TaskManager::instance().lock();
    auto& fileDescriptorTable = *Task::TaskManager::instance().get_current_task_pointer()->get_file_descriptor_table();
    auto entry = fileDescriptorTable.get(fd);
    if (entry.is_end())
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    // Read from the current offset, but not past the end of the file.
    auto length = Filesystem::VirtualFilesystemManager::instance().filelen(entry->last.node);
    if (entry->last.offset + count > length) { count = length - entry->last.offset; }
    auto bytesRead = Filesystem::VirtualFilesystemManager::instance().read(entry->last.node, entry->last.offset, count, (void*)buf);
    entry->last.offset += bytesRead;
    Task::TaskManager::instance().unlock();
    return bytesRead;
}

uint64_t fwrite_syscall(uint64_t fd, uint64_t buf, uint64_t count)
{
    if (!is_user_address(buf, count)) { return -1; }
    Task::TaskManager::instance().lock();
    auto& fileDescriptorTable = *Task::TaskManager::instance().get_current_task_pointer()->get_file_descriptor_table();
    auto entry = fileDescriptorTable.get(fd);
    if (entry.is_end())
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    auto bytesWritten = Filesystem::VirtualFilesystemManager::instance().write(entry->last.node, entry->last.offset, count, (void*)buf);
    entry->last.offset += bytesWritten;
    Task::TaskManager::instance().unlock();
    return bytesWritten;
}

// Move a file's offset, and return the new offset.
uint64_t lseek_syscall(uint64_t fd, uint64_t offset, uint64_t whence)
{
    Task::TaskManager::instance().lock();
    auto& fileDescriptorTable = *Task::TaskManager::instance().get_current_task_pointer()->get_file_descriptor_table();
    auto entry = fileDescriptorTable.get(fd);
    if (entry.is_end() || whence > SEEK_FROM_END)
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    int64_t base = 0;
    if (whence == SEEK_FROM_CURRENT) { base = entry->last.offset; }
    if (whence == SEEK_FROM_END) { base = Filesystem::VirtualFilesystemManager::instance().filelen(entry->last.node); }
    int64_t newOffset = base + (int64_t)offset;
    if (newOffset < 0)
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    entry->last.offset = newOffset;
    Task::TaskManager::instance().unlock();
    return newOffset;
}

void fill_file_status(Filesystem::VirtualFilesystemNode* node, FileStatus* status)
{
    status->size = Filesystem::VirtualFilesystemManager::instance().filelen(node);
    status->isDirectory = (node->flags & Filesystem::VirtualFilesystemNode::VFS_DIRECTORY) != 0;
}

uint64_t stat_syscall(uint64_t path, uint64_t buf)
{
    if (!is_user_address(path, 1) || !is_user_address(buf, sizeof(FileStatus))) { return -1; }
    auto node = Filesystem::VirtualFilesystemManager::instance().open_file((char*)path, 0);
    if (!node) { return -1; }
    fill_file_status(node, (FileStatus*)buf);
    delete node;
    return 0;
}

uint64_t fstat_syscall(uint64_t fd, uint64_t buf)
{
    if (!is_user_address(buf, sizeof(FileStatus))) { return -1; }
    Task::TaskManager::instance().lock();
    auto& fileDescriptorTable = *Task::TaskManager::instance().get_current_task_pointer()->get_file_descriptor_table();
    auto entry = fileDescriptorTable.get(fd);
    if (entry.is_end())
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    fill_file_status(entry->last.node, (FileStatus*)buf);
    Task::TaskManager::instance().unlock();
    return 0;
}

// Print a number of bytes, which need not end with a null terminator.
uint64_t console_write_syscall(uint64_t buf, uint64_t count)
{
    if (!is_user_address(buf, count)) { return -1; }
    char text[129];
    for (uint64_t done = 0; done < count; done += 128)
    {
        uint64_t length = count - done < 128 ? count - done : 128;
        memcpy(text, (char*)buf + done, length);
        text[length] = '\0';
        printf(text);
    }
    return count;
}

uint64_t fclose_syscall(uint64_t fd)
{
    Task::TaskManager::instance().lock();
    auto& fileDescriptorTable = *Task::TaskManager::instance().get_current_task_pointer()->get_file_descriptor_table();
    auto entry = fileDescriptorTable.get(fd);
    if (entry.is_end())
    {
        Task::TaskManager::instance().unlock();
        return -1;
    }
    // We only have an initrd mounted at the moment, so closing a file only frees its node.
    delete entry->last.node;
    fileDescriptorTable.remove(fd);
    Task::TaskManager::instance().unlock();
    return 0;
}

uint64_t exit_syscall(uint64_t exitCode)
{
    Task::TaskManager::instance().exit_task(exitCode);
    return 0;
}

// Start a program, with an optional string of arguments separated by spaces and an optional environment.
uint64_t spawn_syscall(uint64_t path, uint64_t arguments, uint64_t environment)
{
    if (!is_user_address(path, 1) || (arguments && !is_user_address(arguments, 1)) || (environment && !is_user_address(environment, 1))) { return -1; }
    auto file = Filesystem::VirtualFilesystemManager::instance().open_file((char*)path, 0);
    if (!file) { return -1; }
    delete file;
    return Loader::spawn_user_program((char*)path, (char*)arguments, (char*)environment);
}

// Remove a file or an empty directory.
uint64_t unlink_syscall(uint64_t path)
{
    if (!is_user_address(path, 1)) { return -1; }
    return Filesystem::VirtualFilesystemManager::instance().remove((char*)path) ? 0 : -1;
}

uint64_t mkdir_syscall(uint64_t path)
{
    if (!is_user_address(path, 1)) { return -1; }
    auto directory = Filesystem::VirtualFilesystemManager::instance().create_directory((char*)path);
    if (!directory) { return -1; }
    delete directory;
    return 0;
}

uint64_t wait_syscall(uint64_t tid)
{
    return Task::TaskManager::instance().wait_for_task(tid);
}

// Register the current task to receive messages from an event sender, such as "HID/Keyboard", and return the event descriptor.
uint64_t eregister_syscall(uint64_t event, uint64_t param)
{
    if (!is_user_address(event, 1)) { return -1; }
    return Events::EventDispatcher::register_event_listener(Task::TaskManager::instance().get_current_task(), (char*)event, (void*)param);
}

// Block until the event sender sends a message, unless one is already waiting.
uint64_t elisten_syscall(uint64_t ed)
{
    Events::EventDispatcher::block_event_listen(Task::TaskManager::instance().get_current_task(), ed);
    return 0;
}

uint64_t ederegister_syscall(uint64_t ed)
{
    return Events::EventDispatcher::deregister_event_listener(Task::TaskManager::instance().get_current_task(), ed);
}

// Return the next waiting message, or 0 if there is none. The message is returned directly, so the buffer is not used yet.
uint64_t eread_syscall(uint64_t ed, uint64_t buf, uint64_t count)
{
    return Events::EventDispatcher::read_from_event_queue(Task::TaskManager::instance().get_current_task(), ed);
}

// Copy the name of the entry at an index in a directory into a buffer of at least 256 bytes.
// Returns 1 for a file, 2 for a directory, 0 past the last entry, or -1 if the directory does not exist.
uint64_t readdir_syscall(uint64_t path, uint64_t index, uint64_t buf)
{
    if (!is_user_address(path, 1) || !is_user_address(buf, 256)) { return -1; }
    auto directory = Filesystem::VirtualFilesystemManager::instance().open_file((char*)path, 0);
    if (!directory || !(directory->flags & Filesystem::VirtualFilesystemNode::VFS_DIRECTORY))
    {
        if (directory) { delete directory; }
        return -1;
    }
    uint64_t result = 0;
    auto entry = Filesystem::VirtualFilesystemManager::instance().readdir(directory, index);
    if (entry)
    {
        strcpy((char*)buf, entry->name);
        auto node = Filesystem::VirtualFilesystemManager::instance().finddir(directory, entry->name);
        result = (node && (node->flags & Filesystem::VirtualFilesystemNode::VFS_DIRECTORY)) ? 2 : 1;
        if (node) { delete node; }
        delete entry;
    }
    delete directory;
    return result;
}

uint64_t printtoscreen_syscall(uint64_t arg1)
{
    char* str = (char*)arg1;
    printf(str);
    return 1;
}

uint64_t Processes::syscall_handler(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5, uint64_t arg6)
{
    //asm volatile ("hlt");
    uint64_t rrax;
    asm volatile("mov %%rax, %0" : "=r" (rrax));
    switch (rrax)
    {
        case SYSCALL_DPRINTF:
        return printtoscreen_syscall(arg1);
        case SYSCALL_VMMAP:
        return vmmap_syscall(arg1);
        case SYSCALL_VMUNMAP:
        return vmumap_syscall(arg1, arg2);
        case SYSCALL_FOPEN:
        return fopen_syscall(arg1, arg2);
        case SYSCALL_FREAD:
        return fread_syscall(arg1, arg2, arg3);
        case SYSCALL_FCLOSE:
        return fclose_syscall(arg1);
        case SYSCALL_FWRITE:
        return fwrite_syscall(arg1, arg2, arg3);
        case SYSCALL_EREGISTER:
        return eregister_syscall(arg1, arg2);
        case SYSCALL_ELISTEN:
        return elisten_syscall(arg1);
        case SYSCALL_EDEREGISTER:
        return ederegister_syscall(arg1);
        case SYSCALL_EREAD:
        return eread_syscall(arg1, arg2, arg3);
        case SYSCALL_SLEEP_FOR:
        return sleep_for_syscall(arg1);
        case SYSCALL_EXIT:
        return exit_syscall(arg1);
        case SYSCALL_SPAWN:
        return spawn_syscall(arg1, arg2, arg3);
        case SYSCALL_WAIT:
        return wait_syscall(arg1);
        case SYSCALL_BRK:
        return brk_syscall(arg1);
        case SYSCALL_READDIR:
        return readdir_syscall(arg1, arg2, arg3);
        case SYSCALL_CLEAR:
        clear_screen();
        return 0;
        case SYSCALL_GETTID:
        return Task::TaskManager::instance().get_current_task();
        case SYSCALL_LSEEK:
        return lseek_syscall(arg1, arg2, arg3);
        case SYSCALL_STAT:
        return stat_syscall(arg1, arg2);
        case SYSCALL_FSTAT:
        return fstat_syscall(arg1, arg2);
        case SYSCALL_UPTIME:
        return CPU::PIT::time_since_boot();
        case SYSCALL_CONSOLE_WRITE:
        return console_write_syscall(arg1, arg2);
        case SYSCALL_UNLINK:
        return unlink_syscall(arg1);
        case SYSCALL_MKDIR:
        return mkdir_syscall(arg1);
        case 99:
        printf(arg1);
        return 0;

    }
    return -1;
}
