#include <stdio.h>

/* Compile this inside cOS with gcc disk/hello.c -o disk/hello. */
int main(int argc, char** argv)
{
    printf("Hello from a program compiled on cOS, with %d arguments.\n", argc);
    return 0;
}
