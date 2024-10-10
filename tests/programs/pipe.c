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

