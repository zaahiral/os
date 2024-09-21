#include "start.h"
#include "stdio.h"
#include "unistd.h"

// Counts the lines, words and bytes in each file named in the arguments, as the Unix wc program does.
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        puts("Usage: wc <file>...");
        return 1;
    }
    uint64_t totalLines = 0;
    uint64_t totalWords = 0;
    uint64_t totalBytes = 0;
    for (int i = 1; i < argc; i++)
    {
        int64_t fd = open(argv[i]);
        if (fd < 0)
        {
            printf("wc: no such file: %s\n", argv[i]);
            return 1;
        }
        uint64_t lines = 0;
        uint64_t words = 0;
        uint64_t bytes = 0;
        bool isInWord = false;
        char buffer[256];
        int64_t length;
        while ((length = read(fd, buffer, sizeof(buffer))) > 0)
        {
            bytes += length;
            for (int64_t j = 0; j < length; j++)
            {
                char c = buffer[j];
                if (c == '\n') { lines++; }
                // A word starts at the first character that is not a space after a space.
                bool isSpace = c == ' ' || c == '\n' || c == '\t';
                if (!isSpace && !isInWord) { words++; }
                isInWord = !isSpace;
            }
        }
        close(fd);
        printf("%6lu %6lu %6lu %s\n", lines, words, bytes, argv[i]);
        totalLines += lines;
        totalWords += words;
        totalBytes += bytes;
    }
    if (argc > 2) { printf("%6lu %6lu %6lu total\n", totalLines, totalWords, totalBytes); }
    return 0;
}
