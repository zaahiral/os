#include "fs/file.h"
#include "process/taskManager.h"
#include "event/dispatcher.h"
#include "print.h"

namespace Filesystem
{
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
        // Inherited console descriptors share a line buffer, so only one reader may fill or drain it at a time.
        while (file->consoleReading)
        {
            file->consoleWaiters.append(manager.get_current_task());
            manager.block_task();
            manager.lock();
        }
        file->consoleReading = true;
        if (file->lineOffset == file->lineLength)
        {
            file->lineOffset = file->lineLength = 0;
            auto tid = manager.get_current_task();
            manager.unlock();
            auto ed = Events::EventDispatcher::register_event_listener(tid, (char*)"HID/Keyboard", nullptr);
            const char lowerCase[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
            const char upperCase[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";
            bool isShiftHeld = false;
            bool isComplete = false;
            while (!isComplete)
            {
                Events::EventDispatcher::block_event_listen(tid, ed);
                uint64_t scanCode;
                while ((scanCode = Events::EventDispatcher::read_from_event_queue(tid, ed)))
                {
                    if (scanCode == 0x2A || scanCode == 0x36) { isShiftHeld = true; continue; }
                    if (scanCode == 0xAA || scanCode == 0xB6) { isShiftHeld = false; continue; }
                    if (scanCode & 0x80) { continue; }
                    if (scanCode == 0x1C)
                    {
                        file->line[file->lineLength++] = '\n';
                        print_bytes("\n", 1);
                        isComplete = true;
                        break;
                    }
                    if (scanCode == 0x0E)
                    {
                        if (file->lineLength) { file->lineLength--; print_bytes("\b", 1); }
                        continue;
                    }
                    if (scanCode >= sizeof(lowerCase) - 1) { continue; }
                    char c = isShiftHeld ? upperCase[scanCode] : lowerCase[scanCode];
                    if (!c || file->lineLength == sizeof(file->line) - 1) { continue; }
                    file->line[file->lineLength++] = c;
                    print_bytes(&c, 1);
                }
            }
            Events::EventDispatcher::deregister_event_listener(tid, ed);
            manager.lock();
        }
        if (count > file->lineLength - file->lineOffset) { count = file->lineLength - file->lineOffset; }
        memcpy(buffer, file->line + file->lineOffset, count);
        file->lineOffset += count;
        file->consoleReading = false;
        wake_waiters(file->consoleWaiters);
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
