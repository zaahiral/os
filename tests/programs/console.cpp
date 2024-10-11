#include "process/syscall.h"
#include "output.h"
#include "unistd.h"

extern "C" void _start()
{
    print("Queue console input now.\n");
    syscall(SYSCALL_SLEEP_FOR, 5000);
    const char expected[] = "reader input\n";
    char line[sizeof(expected)];
    auto length = read(0, line, sizeof(line));
    bool matches = length == sizeof(expected) - 1;
    for (int64_t i = 0; i < length && matches; i++) { matches = line[i] == expected[i]; }
    print(matches ? "Console line received.\n" : "Console input differs.\n");
    syscall(SYSCALL_EXIT, matches ? 0 : 1);
}
