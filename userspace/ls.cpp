#include "start.h"
#include "stdio.h"
#include "unistd.h"

// Lists each directory named in the arguments, or initrd if there are none.
int main(int argc, char** argv)
{
    const char* defaultDirectory = "initrd";
    int count = argc > 1 ? argc - 1 : 1;
    char** directories = argc > 1 ? argv + 1 : (char**)&defaultDirectory;
    for (int i = 0; i < count; i++)
    {
        // Name each directory when more than one is listed.
        if (count > 1) { printf("%s:\n", directories[i]); }
        char name[256];
        for (uint64_t index = 0; ; index++)
        {
            int64_t result = readdir(directories[i], index, name);
            if (result < 0)
            {
                printf("ls: no such directory: %s\n", directories[i]);
                return 1;
            }
            if (!result) { break; }
            // Directories are marked with a slash, as in most shells.
            printf("%s%s\n", name, result == 2 ? "/" : "");
        }
    }
    return 0;
}
