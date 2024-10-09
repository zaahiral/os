#include "start.h"
#include "unistd.h"
#include "string.h"

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (i > 1 && write(1, " ", 1) != 1) { return 1; }
        if (write(1, argv[i], strlen(argv[i])) < 0) { return 1; }
    }
    return write(1, "\n", 1) == 1 ? 0 : 1;
}
