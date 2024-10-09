#include "start.h"
#include "stdio.h"
#include "unistd.h"

// Prints each file named in the arguments.
int main(int argc, char** argv)
{
    int failures = 0;
    for (int i = argc > 1 ? 1 : 0; i < argc; i++)
    {
        int64_t fd = i ? open(argv[i]) : 0;
        if (fd < 0)
        {
            printf("cat: no such file: %s\n", argv[i]);
            failures++;
            continue;
        }
        // Read the file in pieces, as it may be larger than the buffer.
        char buffer[256];
        int64_t length;
        while ((length = read(fd, buffer, sizeof(buffer))) > 0)
        {
            if (write(1, buffer, length) != length) { failures++; break; }
        }
        if (length < 0) { failures++; }
        if (i) { close(fd); }
    }
    return failures;
}
