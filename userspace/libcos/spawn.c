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
#define MAX_ENVIRONMENT_LENGTH 1024

extern char** environ;

/* Programs share the keyboard and the screen, so their standard input and output cannot be redirected yet. */
struct __posix_spawn_file_actions
{
    int hasRedirection;
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

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* actions, int fd, int newFd)
{
    (*actions)->hasRedirection = 1;
    return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* actions, int fd, const char* path, int flags, mode_t mode)
{
    (*actions)->hasRedirection = 1;
    return 0;
}

/* A new program does not get its parent's open files, so a file the parent asks to close is closed already. */
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* actions, int fd)
{
    return 0;
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
