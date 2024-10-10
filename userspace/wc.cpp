#include "start.h"
#include "stdio.h"
#include "unistd.h"

// Counts the lines, words and bytes in each file named in the arguments, as the Unix wc program does.
int main(int argc, char** argv)
{
    uint64_t totalLines = 0;
    uint64_t totalWords = 0;
    uint64_t totalBytes = 0;
    for (int i = argc > 1 ? 1 : 0; i < argc; i++)
    {
        int64_t fd = i ? open(argv[i]) : 0;
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
        if (i) { close(fd); }
        if (length < 0) { return 1; }
        if (i) { printf("%6lu %6lu %6lu %s\n", lines, words, bytes, argv[i]); }
        else { printf("%6lu %6lu %6lu\n", lines, words, bytes); }
        totalLines += lines;
        totalWords += words;
        totalBytes += bytes;
    }
    if (argc > 2) { printf("%6lu %6lu %6lu total\n", totalLines, totalWords, totalBytes); }
    return 0;
}
