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
    if (argc > 1 && !strcmp(argv[1], "reader"))
    {
        char c;
        return read(0, &c, 1) == 1 && c == 'x' ? 11 : 2;
    }
    if (argc > 1 && !strcmp(argv[1], "drop")) { return 13; }

    int fds[2];
    struct stat st;
    CHECK(pipe(fds) == 0);
    CHECK(fstat(fds[0], &st) == 0 && S_ISFIFO(st.st_mode));
    CHECK(!isatty(fds[1]) && errno == ENOTTY);
    CHECK(lseek(fds[0], 0, SEEK_SET) == -1 && errno == ESPIPE);
    CHECK(read(fds[1], &st, 1) == -1 && errno == EBADF);
    CHECK(write(fds[0], "x", 1) == -1 && errno == EBADF);
    int writer = dup(fds[1]);
    CHECK(writer >= 3 && close(fds[1]) == 0);
    CHECK(dup2(writer, writer) == writer);
    posix_spawn_file_actions_t actions;
    CHECK(posix_spawn_file_actions_init(&actions) == 0);
    CHECK(posix_spawn_file_actions_adddup2(&actions, writer, 40) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, writer) == 0);
    CHECK(posix_spawn_file_actions_adddup2(&actions, 40, 1) == 0);
    CHECK(posix_spawn_file_actions_adddup2(&actions, 40, 2) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, 40) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, fds[0]) == 0);
    pid_t child;
    char* writerArgs[] = {argv[0], "writer", NULL};
    CHECK(posix_spawn(&child, argv[0], &actions, NULL, writerArgs, environ) == 0);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0);
    CHECK(close(writer) == 0);
    unsigned char data[173];
    int total = 0;
    int length;
    while ((length = read(fds[0], data, sizeof(data))) > 0)
    {
        for (int i = 0; i < length; i++, total++)
        {
            CHECK(total < 8198);
            CHECK(data[i] == (total < 8192 ? total % 251 : "stderr"[total - 8192]));
        }
    }
    /* EOF must arrive while the child is still unreaped, including its duplicated standard error. */
    CHECK(length == 0 && total == 8198);
    CHECK(close(fds[0]) == 0);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 7);

    CHECK(pipe(fds) == 0);
    CHECK(posix_spawn_file_actions_init(&actions) == 0);
    CHECK(posix_spawn_file_actions_adddup2(&actions, fds[0], 0) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, fds[0]) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, fds[1]) == 0);
    char* readerArgs[] = {argv[0], "reader", NULL};
    CHECK(posix_spawn(&child, argv[0], &actions, NULL, readerArgs, environ) == 0);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0);
    CHECK(close(fds[0]) == 0);
    CHECK(write(fds[1], "x", 1) == 1);
    CHECK(waitpid(child, &status, 0) == child && WEXITSTATUS(status) == 11);
    CHECK(write(fds[1], "x", 1) == -1 && errno == EPIPE);
    CHECK(close(fds[1]) == 0);

    CHECK(pipe(fds) == 0);
    CHECK(posix_spawn_file_actions_init(&actions) == 0);
    CHECK(posix_spawn_file_actions_addclose(&actions, fds[1]) == 0);
    char* dropArgs[] = {argv[0], "drop", NULL};
    CHECK(posix_spawn(&child, argv[0], &actions, NULL, dropArgs, environ) == 0);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0);
    CHECK(close(fds[0]) == 0);
    char large[2048] = {0};
    length = write(fds[1], large, sizeof(large));
    CHECK(length == -1 || (length >= 0 && length < sizeof(large)));
    CHECK(write(fds[1], "x", 1) == -1 && errno == EPIPE);
    CHECK(waitpid(child, &status, 0) == child && WEXITSTATUS(status) == 13);
    CHECK(close(fds[1]) == 0);

    CHECK(pipe(fds) == 0);
    int reader = dup(fds[0]);
    writer = dup(fds[1]);
    CHECK(reader >= 3 && writer >= 3);
    CHECK(close(fds[0]) == 0 && close(fds[1]) == 0);
    CHECK(write(writer, "y", 1) == 1 && read(reader, data, 1) == 1 && data[0] == 'y');
    CHECK(close(writer) == 0 && read(reader, data, 1) == 0);
    CHECK(close(reader) == 0);

    int other[2];
    CHECK(pipe(fds) == 0 && pipe(other) == 0);
    CHECK(dup2(other[1], fds[1]) == fds[1]);
    CHECK(read(fds[0], data, 1) == 0 && close(fds[0]) == 0);
    CHECK(close(other[0]) == 0 && close(other[1]) == 0);
    CHECK(write(fds[1], "x", 1) == -1 && errno == EPIPE);
    CHECK(close(fds[1]) == 0);

    int file = open("disk/fds.txt", O_CREAT | O_TRUNC | O_RDWR, 0666);
    CHECK(file >= 3 && write(file, "ab", 2) == 2 && lseek(file, 0, SEEK_SET) == 0);
    int copy = dup(file);
    CHECK(copy >= 3);
    CHECK(read(file, data, 1) == 1 && data[0] == 'a');
    CHECK(read(copy, data, 1) == 1 && data[0] == 'b');
    CHECK(close(copy) == 0);
    int input = dup(0);
    int output = dup(1);
    CHECK(input >= 3 && output >= 3);
    CHECK(dup2(file, 0) == 0 && !isatty(0));
    CHECK(lseek(file, 0, SEEK_SET) == 0 && read(0, data, 2) == 2 && !memcmp(data, "ab", 2));
    CHECK(dup2(file, 1) == 1 && !isatty(1));
    CHECK(fstat(1, &st) == 0 && S_ISREG(st.st_mode));
    CHECK(printf(" redirected") > 0 && fflush(stdout) == 0);
    CHECK(dup2(input, 0) == 0 && dup2(output, 1) == 1);
    CHECK(close(input) == 0 && close(output) == 0 && close(file) == 0);
    CHECK(isatty(1) && fstat(1, &st) == 0 && S_ISCHR(st.st_mode));
    CHECK(dup2(-1, 1) == -1 && errno == EBADF);
    CHECK(!isatty(-1) && errno == EBADF);
    CHECK(close(-1) == -1 && errno == EBADF);

    CHECK(pipe(fds) == 0);
    CHECK(posix_spawn_file_actions_init(&actions) == 0);
    CHECK(posix_spawn_file_actions_adddup2(&actions, 255, 1) == 0);
    CHECK(posix_spawn(&child, argv[0], &actions, NULL, writerArgs, environ) == EBADF);
    CHECK(posix_spawn_file_actions_destroy(&actions) == 0);
    CHECK(close(fds[1]) == 0 && read(fds[0], data, 1) == 0 && close(fds[0]) == 0);
    int copies[253];
    for (int i = 0; i < 253; i++) { CHECK((copies[i] = dup(1)) >= 3); }
    CHECK(dup(1) == -1 && errno == EMFILE);
    CHECK(pipe(fds) == -1 && errno == EMFILE);
    CHECK(close(copies[252]) == 0);
    CHECK(pipe(fds) == -1 && errno == EMFILE);
    for (int i = 0; i < 252; i++) { CHECK(close(copies[i]) == 0); }
    CHECK(pipe(fds) == 0 && close(fds[0]) == 0 && close(fds[1]) == 0);
    puts("Pipe, redirection and waitpid checks passed.");
    return 0;
}
