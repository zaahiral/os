#ifndef SYSCALLNUMBERS_H
#define SYSCALLNUMBERS_H

// The system call numbers, kept apart from syscall.h so that programs built against newlib can use them without
// the kernel's integer types.

#define SYSCALL_DPRINTF 1
#define SYSCALL_VMMAP 2
#define SYSCALL_VMUNMAP 3
#define SYSCALL_FOPEN 4
#define SYSCALL_FCLOSE 5
#define SYSCALL_FREAD 6
#define SYSCALL_FWRITE 7
#define SYSCALL_EREGISTER 8
#define SYSCALL_ELISTEN 9
#define SYSCALL_EDEREGISTER 10
#define SYSCALL_EREAD 11
#define SYSCALL_SLEEP_FOR 12
#define SYSCALL_GETTID 13
#define SYSCALL_EXIT 14
#define SYSCALL_SPAWN 15
#define SYSCALL_WAIT 16
#define SYSCALL_BRK 17
#define SYSCALL_READDIR 18
#define SYSCALL_CLEAR 19
#define SYSCALL_LSEEK 20
#define SYSCALL_STAT 21
#define SYSCALL_FSTAT 22
#define SYSCALL_UPTIME 23
#define SYSCALL_CONSOLE_WRITE 24

// Flags for SYSCALL_FOPEN.
#define FILE_CREATE 0x1
#define FILE_APPEND 0x2

// Whence values for SYSCALL_LSEEK, which match SEEK_SET, SEEK_CUR and SEEK_END.
#define SEEK_FROM_START 0
#define SEEK_FROM_CURRENT 1
#define SEEK_FROM_END 2

// SYSCALL_STAT and SYSCALL_FSTAT fill in this structure.
struct FileStatus
{
    unsigned long long size;
    unsigned long long isDirectory;
};

#endif
