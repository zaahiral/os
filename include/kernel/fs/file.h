#ifndef FILE_H
#define FILE_H

#include "process/task.h"
#include "process/syscallnumbers.h"

namespace Filesystem
{
    static constexpr uint64_t PIPE_BUFFER_SIZE = 512;

    struct Pipe
    {
        char buffer[PIPE_BUFFER_SIZE];
        uint64_t start = 0;
        uint64_t length = 0;
        uint64_t readers = 1;
        uint64_t writers = 1;
        Common::DoublyLinkedList<uint64_t> readWaiters;
        Common::DoublyLinkedList<uint64_t> writeWaiters;
    };

    struct ConsoleInput;

    struct OpenFile
    {
        enum Kind { NODE, CONSOLE, PIPE_READ, PIPE_WRITE };
        Kind kind = NODE;
        uint64_t references = 1;
        VirtualFilesystemNode* node = nullptr;
        uint64_t offset = 0;
        bool append = false;
        Pipe* pipe = nullptr;
        ConsoleInput* console = nullptr;
    };

    // File operations run with the task manager lock held, including the check before a pipe sleeps.
    OpenFile* open_console();
    int64_t install_file(Task::FileDescriptorTable& table, OpenFile* file);
    void retain_file(OpenFile* file);
    void release_file(OpenFile* file);
    bool close_file(Task::FileDescriptorTable& table, uint64_t fd);
    void close_files(Task::FileDescriptorTable& table);
    int64_t duplicate_file(Task::FileDescriptorTable& table, uint64_t fd, uint64_t newFd);
    int64_t read_file(OpenFile* file, char* buffer, uint64_t count);
    int64_t write_file(OpenFile* file, const char* buffer, uint64_t count);
}

#endif
