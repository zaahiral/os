#include <errno.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "cossyscall.h"

/* cOS starts programs with a spawn system call instead of fork and exec, which is all that posix_spawn needs.
   The kernel takes the arguments as one string of words separated by spaces, so an argument cannot contain a space. */

#define MAX_COMMAND_LINE_LENGTH 4096
#define MAX_PROGRAM_ARGUMENTS 128
#define MAX_ENVIRONMENT_LENGTH 4096

extern char** environ;

struct __posix_spawn_file_actions
{
    struct SpawnFileAction entries[MAX_SPAWN_ACTIONS];
    unsigned long long count;
    int hasOpen;
};

struct __posix_spawnattr
{
    short flags;
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t* actions)
{
    *actions = calloc(1, sizeof(struct __posix_spawn_file_actions));
    return *actions ? 0 : ENOMEM;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* actions)
{
    free(*actions);
    *actions = NULL;
    return 0;
}

static int add_action(posix_spawn_file_actions_t* actions, int operation, int fd, int newFd)
{
    if (fd < 0 || fd >= MAX_FILE_DESCRIPTORS || newFd < 0 || newFd >= MAX_FILE_DESCRIPTORS) { return EBADF; }
    if ((*actions)->count == MAX_SPAWN_ACTIONS) { return ENOMEM; }
    struct SpawnFileAction* action = &(*actions)->entries[(*actions)->count++];
    action->operation = operation;
    action->fd = fd;
    action->newFd = newFd;
    return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* actions, int fd, int newFd)
{
    return add_action(actions, SPAWN_DUP2, fd, newFd);
}

/* Opening files as a spawn action is still unsupported. Open a file in the parent and adddup2 it instead. */
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* actions, int fd, const char* path, int flags, mode_t mode)
{
    (*actions)->hasOpen = 1;
    return 0;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* actions, int fd)
{
    return add_action(actions, SPAWN_CLOSE, fd, 0);
}

int posix_spawnattr_init(posix_spawnattr_t* attributes)
{
    *attributes = calloc(1, sizeof(struct __posix_spawnattr));
    return *attributes ? 0 : ENOMEM;
}

int posix_spawnattr_destroy(posix_spawnattr_t* attributes)
{
    free(*attributes);
    *attributes = NULL;
    return 0;
}

/* cOS has no process groups, signals or scheduling policies, so the flags are kept but have no effect. */
int posix_spawnattr_setflags(posix_spawnattr_t* attributes, short flags)
{
    (*attributes)->flags = flags;
    return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t* attributes, short* flags)
{
    *flags = (*attributes)->flags;
    return 0;
}

/* Join the arguments after the program's name into one string, separated by spaces. The kernel puts the program's
   path in front of them, so the path counts towards the limits too. */
static int join_arguments(const char* path, char* const argv[], char* arguments)
{
    size_t length = 0;
    arguments[0] = '\0';
    for (int i = 1; argv[i]; i++)
    {
        size_t argumentLength = strlen(argv[i]);
        if (!argumentLength || strchr(argv[i], ' ')) { return EINVAL; }
        if (i >= MAX_PROGRAM_ARGUMENTS || strlen(path) + 1 + length + argumentLength + 2 > MAX_COMMAND_LINE_LENGTH) { return E2BIG; }
        if (length) { arguments[length++] = ' '; }
        memcpy(arguments + length, argv[i], argumentLength + 1);
        length += argumentLength;
    }
    return 0;
}

/* Copy the environment into one block of strings, each with a null terminator, with an empty string after the last one. */
static int join_environment(char* const envp[], char* environment)
{
    size_t length = 0;
    for (int i = 0; envp && envp[i]; i++)
    {
        size_t variableLength = strlen(envp[i]) + 1;
        if (length + variableLength + 1 > MAX_ENVIRONMENT_LENGTH) { return E2BIG; }
        memcpy(environment + length, envp[i], variableLength);
        length += variableLength;
    }
    environment[length] = '\0';
    return 0;
}

int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* actions, const posix_spawnattr_t* attributes,
                char* const argv[], char* const envp[])
{
    if (actions && *actions && (*actions)->hasOpen) { return ENOTSUP; }
    char arguments[MAX_COMMAND_LINE_LENGTH];
    char environment[MAX_ENVIRONMENT_LENGTH];
    int error = join_arguments(path, argv, arguments);
    if (!error) { error = join_environment(envp, environment); }
    if (error) { return error; }

    struct SpawnRequest request = {path, arguments, environment, NULL, 0};
    if (actions && *actions)
    {
        request.actions = (*actions)->entries;
        request.actionCount = (*actions)->count;
    }
    long tid = cos_syscall(SYSCALL_SPAWN_ACTIONS, (long)&request, 0, 0);
    if (tid < 0) { return tid == -2 ? EBADF : ENOENT; }
    if (pid) { *pid = tid; }
    return 0;
}

/* A program name without a / is looked for in each directory in PATH, which is initrd/bin if it is not set. */
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* actions, const posix_spawnattr_t* attributes,
                 char* const argv[], char* const envp[])
{
    if (strchr(file, '/')) { return posix_spawn(pid, file, actions, attributes, argv, envp); }
    const char* directory = getenv("PATH");
    if (!directory || !*directory) { directory = "initrd/bin"; }
    char path[MAX_COMMAND_LINE_LENGTH];
    while (*directory)
    {
        size_t directoryLength = strcspn(directory, ":");
        if (directoryLength + strlen(file) + 2 <= sizeof(path))
        {
            memcpy(path, directory, directoryLength);
            path[directoryLength] = '/';
            strcpy(path + directoryLength + 1, file);
            struct stat st;
            if (!stat(path, &st) && !S_ISDIR(st.st_mode)) { return posix_spawn(pid, path, actions, attributes, argv, envp); }
        }
        directory += directoryLength;
        if (*directory == ':') { directory++; }
    }
    return ENOENT;
}

/* cOS can only wait for one program at a time, and cannot check on a program without waiting for it.
   A program that was stopped by an exception exits with code 255. */
pid_t waitpid(pid_t pid, int* status, int options)
{
    if (options)
    {
        errno = EINVAL;
        return -1;
    }
    long exitCode = pid > 0 ? cos_syscall(SYSCALL_WAIT, pid, 0, 0) : -1;
    if (exitCode < 0)
    {
        errno = ECHILD;
        return -1;
    }
    if (status) { *status = exitCode << 8; }
    return pid;
}
