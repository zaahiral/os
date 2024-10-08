#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char** environ;

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: build <command file>\n");
        return 1;
    }
    FILE* commands = fopen(argv[1], "r");
    if (!commands) { perror(argv[1]); return 1; }
    char line[4096];
    unsigned int step = 0;
    while (fgets(line, sizeof(line), commands))
    {
        if (!strchr(line, '\n') && !feof(commands))
        {
            fprintf(stderr, "Build command is too long.\n");
            fclose(commands);
            return 1;
        }
        char* arguments[128];
        unsigned int count = 0;
        char* word = strtok(line, " \t\r\n");
        while (word && count < 127)
        {
            arguments[count++] = word;
            word = strtok(NULL, " \t\r\n");
        }
        if (!count) { continue; }
        if (word)
        {
            fprintf(stderr, "Too many build arguments.\n");
            fclose(commands);
            return 1;
        }
        arguments[count] = NULL;
        printf("[%u]", ++step);
        for (unsigned int i = 0; i < count; i++) { printf(" %s", arguments[i]); }
        printf("\n");
        fflush(stdout);
        pid_t pid;
        int error = posix_spawnp(&pid, arguments[0], NULL, NULL, arguments, environ);
        if (error)
        {
            errno = error;
            perror(arguments[0]);
            fclose(commands);
            return 1;
        }
        int status;
        if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); fclose(commands); return 1; }
        if (!WIFEXITED(status) || WEXITSTATUS(status))
        {
            fprintf(stderr, "Build stopped at step %u (status %d).\n", step, status);
            fclose(commands);
            return 1;
        }
    }
    int failed = ferror(commands);
    fclose(commands);
    if (failed) { fprintf(stderr, "Could not read build commands.\n"); return 1; }
    printf("Build complete: %u commands.\n", step);
    return 0;
}
