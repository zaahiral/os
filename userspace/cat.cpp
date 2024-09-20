#include "start.h"
#include "stdio.h"
#include "unistd.h"

// Prints each file named in the arguments.
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        puts("Usage: cat <file>...");
        return 1;
    }
    int failures = 0;
    for (int i = 1; i < argc; i++)
    {
        int64_t fd = open(argv[i]);
        if (fd < 0)
        {
            printf("cat: no such file: %s\n", argv[i]);
            failures++;
            continue;
        }
        // Read the file in pieces, as it may be larger than the buffer.
        char buffer[257];
        int64_t length;
        char last = '\n';
        while ((length = read(fd, buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[length] = '\0';
            printf("%s", buffer);
            last = buffer[length - 1];
        }
        if (last != '\n') { puts(""); }
        close(fd);
    }
    return failures;
}
