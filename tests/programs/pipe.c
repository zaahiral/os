#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

#define CHECK(condition) do { if (!(condition)) { printf("Pipe check failed at line %d, errno %d.\n", __LINE__, errno); return 1; } } while (0)

int main(int argc, char** argv)
{
    if (argc > 1 && !strcmp(argv[1], "writer"))
    {
        unsigned char data[8192];
        for (int i = 0; i < sizeof(data); i++) { data[i] = i % 251; }
        if (write(1, data, sizeof(data)) != sizeof(data)) { return 2; }
        if (write(2, "stderr", 6) != 6) { return 3; }
        return 7;
    }
