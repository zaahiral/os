#include "fs/file.h"
#include "process/taskManager.h"
#include "drivers/hid/keyboard.h"
#include "print.h"

namespace Filesystem
{
    struct ConsoleInput : Drivers::KeyboardListener
    {
        Common::DoublyLinkedList<uint8_t> queue;
        uint64_t waitingTask = 0;
        bool reading = false;
        bool shiftHeld = false;
        Common::DoublyLinkedList<uint64_t> waiters;
        char line[128];
        uint64_t lineLength = 0;
        uint64_t lineOffset = 0;

        void on_scan_code(uint8_t scanCode) override
        {
            queue.append(scanCode);
            if (waitingTask)
            {
                Task::TaskManager::instance().unblock_task_locked(waitingTask);
                waitingTask = 0;
            }
        }
    };

    OpenFile* open_console()
    {
        auto file = new OpenFile();
        file->kind = OpenFile::CONSOLE;
        file->console = new ConsoleInput();
        // Input belongs to the console, so a reader exiting cannot discard queued keys.
        Drivers::KeyboardDriver::instance()->register_listener(file->console);
        return file;
    }

    int64_t install_file(Task::FileDescriptorTable& table, OpenFile* file)
    {
        for (uint64_t fd = 0; fd < MAX_FILE_DESCRIPTORS; fd++)
        {
            if (table.get(fd).is_end())
            {
                table.insert_at(fd, file);
                return fd;
            }
        }
        return -1;
    }

    static void wake_waiters(Common::DoublyLinkedList<uint64_t>& waiters)
    {
        for (auto it = waiters.first(); !it.is_end(); ++it)
        {
            Task::TaskManager::instance().unblock_task_locked(*it);
        }
        waiters.clear();
    }

    void retain_file(OpenFile* file)
    {
        file->references++;
    }

    void release_file(OpenFile* file)
    {
        if (--file->references) { return; }
        if (file->kind == OpenFile::PIPE_READ || file->kind == OpenFile::PIPE_WRITE)
        {
            auto pipe = file->pipe;
            if (file->kind == OpenFile::PIPE_READ)
            {
                pipe->readers--;
                wake_waiters(pipe->writeWaiters);
            }
            else
            {
                pipe->writers--;
                wake_waiters(pipe->readWaiters);
            }
            if (!pipe->readers && !pipe->writers) { delete pipe; }
        }
        if (file->console)
        {
            Drivers::KeyboardDriver::instance()->deregister_listener(file->console);
            delete file->console;
        }
        delete file->node;
        delete file;
    }

    bool close_file(Task::FileDescriptorTable& table, uint64_t fd)
    {
        auto entry = table.get(fd);
        if (entry.is_end()) { return false; }
        release_file(entry->last);
        table.remove(fd);
        return true;
    }

    void close_files(Task::FileDescriptorTable& table)
    {
        for (uint64_t fd = 0; fd < table.get_id_limit(); fd++) { close_file(table, fd); }
    }

    int64_t duplicate_file(Task::FileDescriptorTable& table, uint64_t fd, uint64_t newFd)
    {
        auto entry = table.get(fd);
        if (entry.is_end() || newFd >= MAX_FILE_DESCRIPTORS) { return -1; }
        if (fd == newFd) { return fd; }
        auto file = entry->last;
        retain_file(file);
        close_file(table, newFd);
        table.insert_at(newFd, file);
        return newFd;
    }

    static int64_t read_console(OpenFile* file, char* buffer, uint64_t count)
    {
        auto& manager = Task::TaskManager::instance();
        auto console = file->console;
        // Inherited console descriptors share a line buffer, so only one reader may fill or drain it at a time.
        while (console->reading)
        {
            console->waiters.append(manager.get_current_task());
            manager.block_task();
            manager.lock();
        }
        console->reading = true;
        if (console->lineOffset == console->lineLength)
        {
            console->lineOffset = console->lineLength = 0;
            const char lowerCase[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
            const char upperCase[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";
            bool isComplete = false;
            while (!isComplete)
            {
                while (!console->queue.size())
                {
                    console->waitingTask = manager.get_current_task();
                    manager.block_task();
                    manager.lock();
                }
                console->waitingTask = 0;
                while (console->queue.size())
                {
                    auto event = console->queue.first();
                    auto scanCode = *event;
                    console->queue.remove(event);
                    if (scanCode == 0x2A || scanCode == 0x36) { console->shiftHeld = true; continue; }
                    if (scanCode == 0xAA || scanCode == 0xB6) { console->shiftHeld = false; continue; }
                    if (scanCode & 0x80) { continue; }
                    if (scanCode == 0x1C)
                    {
                        console->line[console->lineLength++] = '\n';
                        print_bytes("\n", 1);
                        isComplete = true;
                        break;
                    }
                    if (scanCode == 0x0E)
                    {
                        if (console->lineLength) { console->lineLength--; print_bytes("\b", 1); }
                        continue;
                    }
                    if (scanCode >= sizeof(lowerCase) - 1) { continue; }
                    char c = console->shiftHeld ? upperCase[scanCode] : lowerCase[scanCode];
                    if (!c || console->lineLength == sizeof(console->line) - 1) { continue; }
                    console->line[console->lineLength++] = c;
                    print_bytes(&c, 1);
                }
            }
        }
        if (count > console->lineLength - console->lineOffset) { count = console->lineLength - console->lineOffset; }
        memcpy(buffer, console->line + console->lineOffset, count);
        console->lineOffset += count;
        console->reading = false;
        wake_waiters(console->waiters);
        return count;
    }

    int64_t read_file(OpenFile* file, char* buffer, uint64_t count)
    {
        if (file->kind == OpenFile::PIPE_WRITE) { return -1; }
        if (!count) { return 0; }
        if (file->kind == OpenFile::CONSOLE) { return read_console(file, buffer, count); }
        if (file->kind == OpenFile::PIPE_READ)
        {
            auto pipe = file->pipe;
            while (!pipe->length && pipe->writers)
            {
                pipe->readWaiters.append(Task::TaskManager::get_current_task());
                Task::TaskManager::instance().block_task();
                Task::TaskManager::instance().lock();
            }
            if (count > pipe->length) { count = pipe->length; }
            for (uint64_t i = 0; i < count; i++)
            {
                buffer[i] = pipe->buffer[pipe->start];
                pipe->start = (pipe->start + 1) % PIPE_BUFFER_SIZE;
            }
            pipe->length -= count;
            wake_waiters(pipe->writeWaiters);
            return count;
        }
        auto length = VirtualFilesystemManager::instance().filelen(file->node);
        if (file->offset >= length) { count = 0; }
        else if (count > length - file->offset) { count = length - file->offset; }
        auto result = VirtualFilesystemManager::instance().read(file->node, file->offset, count, buffer);
        file->offset += result;
        return result;
    }

    int64_t write_file(OpenFile* file, const char* buffer, uint64_t count)
    {
        if (file->kind == OpenFile::PIPE_READ) { return -1; }
        if (file->kind == OpenFile::CONSOLE) { print_bytes(buffer, count); return count; }
        if (file->kind == OpenFile::PIPE_WRITE)
        {
            auto pipe = file->pipe;
            uint64_t done = 0;
            while (done < count)
            {
                if (!pipe->readers) { return done ? (int64_t)done : -2; }
                if (pipe->length == PIPE_BUFFER_SIZE)
                {
                    pipe->writeWaiters.append(Task::TaskManager::get_current_task());
                    Task::TaskManager::instance().block_task();
                    Task::TaskManager::instance().lock();
                    continue;
                }
                uint64_t length = PIPE_BUFFER_SIZE - pipe->length;
                if (length > count - done) { length = count - done; }
                for (uint64_t i = 0; i < length; i++)
                {
                    pipe->buffer[(pipe->start + pipe->length++) % PIPE_BUFFER_SIZE] = buffer[done++];
                }
                wake_waiters(pipe->readWaiters);
            }
            return done;
        }
        if (file->append) { file->offset = VirtualFilesystemManager::instance().filelen(file->node); }
        auto result = VirtualFilesystemManager::instance().write(file->node, file->offset, count, (void*)buffer);
        file->offset += result;
        return result;
    }
}
