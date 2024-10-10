#include "process/syscall.h"
#include "output.h"
#include "unistd.h"

// The user-space shell runs built-in commands and programs from initrd/bin through standard file descriptors.

constexpr uint64_t MAX_LINE_LENGTH = 76;
bool strings_equal(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

void copy_string(char* destination, const char* source)
{
    while ((*destination++ = *source++));
}

uint64_t string_length(const char* text)
{
    uint64_t length = 0;
    while (text[length]) { length++; }
    return length;
}

// Read through standard input so a shell can itself receive a redirected command stream.
bool read_line(char* line)
{
    uint64_t length = 0;
    bool overflow = false;
    char c;
    int64_t result;
    while ((result = read(0, &c, 1)) > 0)
    {
        if (c == '\n') { break; }
        if (length < MAX_LINE_LENGTH) { line[length++] = c; }
        else { overflow = true; }
    }
    if (overflow)
    {
        print("Command line too long.\n");
        length = 0;
    }
    line[length] = '\0';
    return result > 0 || length || overflow;
}

void help_command()
{
    print("help             List the commands.\n");
    print("clear            Clear the screen.\n");
    print("echo <text>      Print the text.\n");
    print("write <file> <text>  Add a line to a file on the disk.\n");
    print("exit             Leave the user shell for the kernel shell.\n");
    print("Use | between programs, > or >> to write a file, and < to read a file.\n");
    print("Any other command runs the program of that name in initrd/bin with its arguments,\n");
    print("such as ls, cat disk/readme.txt, wc initrd/hello.txt or memtest.\n");
}

void write_command(char* argument)
{
    // The file name ends at the first space, and the rest of the line is the text.
    char* text = argument;
    while (*text && *text != ' ') { text++; }
    if (*text) { *text++ = '\0'; }
    if (!argument[0])
    {
        print("Usage: write <file> <text>, such as write disk/notes.txt hello\n");
        return;
    }
    int64_t fd = syscall(SYSCALL_FOPEN, (uint64_t)argument, FILE_CREATE | FILE_APPEND);
    if (fd < 0)
    {
        print("Cannot write to ");
        print(argument);
        print(", only files on the disk can be written to.\n");
        return;
    }
    syscall(SYSCALL_FWRITE, fd, (uint64_t)text, string_length(text));
    syscall(SYSCALL_FWRITE, fd, (uint64_t)"\n", 1);
    syscall(SYSCALL_FCLOSE, fd);
}

bool run_builtin(char* name, char* arguments)
{
    if (strings_equal(name, "help")) { help_command(); }
    else if (strings_equal(name, "clear")) { syscall(SYSCALL_CLEAR); }
    else if (strings_equal(name, "echo")) { print(arguments); print("\n"); }
    else if (strings_equal(name, "write")) { write_command(arguments); }
    else if (strings_equal(name, "exit")) { syscall(SYSCALL_EXIT, 0); }
    else { return false; }
    return true;
}

void run_program(const char* name, const char* arguments)
{
    // A program name without a directory is looked up in initrd/bin.
    char path[MAX_LINE_LENGTH + 12];
    bool hasDirectory = false;
    for (const char* c = name; *c; c++) { if (*c == '/') { hasDirectory = true; } }
    copy_string(path, hasDirectory ? "" : "initrd/bin/");
    copy_string(path + string_length(path), name);

    uint64_t tid = syscall(SYSCALL_SPAWN, (uint64_t)path, (uint64_t)arguments, 0);
    if (tid == (uint64_t)-1)
    {
        print("No such program: ");
        print(path);
        print("\n");
        return;
    }
    auto exitCode = (int64_t)syscall(SYSCALL_WAIT, tid);
    // As in most shells, only an exit code that reports a failure is printed.
    if (!exitCode) { return; }
    print("Exited with code ");
    if (exitCode < 0) { print("-"); exitCode = -exitCode; }
    print_number(exitCode);
    print(".\n");
}

constexpr uint64_t MAX_PIPELINE_COMMANDS = 16;

struct Command
{
    char text[MAX_LINE_LENGTH + 1] = {};
    uint64_t length = 0;
    char input[MAX_LINE_LENGTH + 1] = {};
    char output[MAX_LINE_LENGTH + 1] = {};
    bool append = false;
    int64_t inputFd = -1;
    int64_t outputFd = -1;
};

// Temporary descriptors must survive the child's dup2 actions without occupying their targets.
int64_t move_temporary_fd(int64_t fd)
{
    if (fd < 0 || fd > 2) { return fd; }
    auto copy = move_temporary_fd(dup(fd));
    close(fd);
    return copy;
}

// Operators are tokens even without surrounding spaces. File names and arguments cannot contain spaces.
bool run_pipeline(char* line)
{
    Command commands[MAX_PIPELINE_COMMANDS];
    uint64_t commandCount = 1;
    bool isValid = true;
    char pending = 0;
    bool append = false;
    for (char* c = line; *c && isValid;)
    {
        if (*c == ' ') { c++; continue; }
        if (*c == '|' || *c == '<' || *c == '>')
        {
            char operation = *c++;
            if (pending) { isValid = false; break; }
            if (operation == '|')
            {
                if (!commands[commandCount - 1].length || commandCount == MAX_PIPELINE_COMMANDS) { isValid = false; break; }
                commandCount++;
            }
            else
            {
                pending = operation;
                append = operation == '>' && *c == '>';
                if (append) { c++; }
            }
            continue;
        }
        char* word = c;
        while (*c && *c != ' ' && *c != '|' && *c != '<' && *c != '>') { c++; }
        auto& command = commands[commandCount - 1];
        auto length = c - word;
        if (pending)
        {
            if (pending == '<')
            {
                if (command.input[0]) { isValid = false; break; }
                for (int64_t i = 0; i < length; i++) { command.input[i] = word[i]; }
            }
            else
            {
                if (command.output[0]) { isValid = false; break; }
                for (int64_t i = 0; i < length; i++) { command.output[i] = word[i]; }
                command.append = append;
            }
            pending = 0;
        }
        else
        {
            if (command.length) { command.text[command.length++] = ' '; }
            for (int64_t i = 0; i < length; i++) { command.text[command.length++] = word[i]; }
            command.text[command.length] = '\0';
        }
    }
    if (pending || !commands[commandCount - 1].length) { isValid = false; }
    if (!isValid) { print("Invalid pipeline or redirection.\n"); return false; }

    int pipes[MAX_PIPELINE_COMMANDS - 1][2];
    uint64_t pipeCount = 0;
    for (; pipeCount + 1 < commandCount; pipeCount++)
    {
        if (pipe(pipes[pipeCount]) < 0) { isValid = false; break; }
        pipes[pipeCount][0] = move_temporary_fd(pipes[pipeCount][0]);
        pipes[pipeCount][1] = move_temporary_fd(pipes[pipeCount][1]);
        if (pipes[pipeCount][0] < 0 || pipes[pipeCount][1] < 0) { pipeCount++; isValid = false; break; }
    }
    for (uint64_t i = 0; i < commandCount && isValid; i++)
    {
        auto& command = commands[i];
        if (command.input[0])
        {
            command.inputFd = move_temporary_fd(open(command.input));
            if (command.inputFd < 0) { isValid = false; }
        }
        if (command.output[0])
        {
            command.outputFd = move_temporary_fd(open(command.output, FILE_CREATE | (command.append ? FILE_APPEND : FILE_TRUNCATE)));
            if (command.outputFd < 0) { isValid = false; }
        }
    }
    uint64_t tids[MAX_PIPELINE_COMMANDS];
    uint64_t started = 0;
    for (uint64_t i = 0; i < commandCount && isValid; i++)
    {
        auto& command = commands[i];
        char* arguments = command.text;
        while (*arguments && *arguments != ' ') { arguments++; }
        if (*arguments) { *arguments++ = '\0'; }
        if (commandCount == 1)
        {
            FileStatus status;
            bool hasInput = !fstat(0, &status);
            bool hasOutput = !fstat(1, &status);
            auto input = hasInput ? move_temporary_fd(dup(0)) : -1;
            auto output = hasOutput ? move_temporary_fd(dup(1)) : -1;
            if ((hasInput && input < 0) || (hasOutput && output < 0))
            {
                if (input >= 0) { close(input); }
                if (output >= 0) { close(output); }
                isValid = false;
                break;
            }
            if (command.inputFd >= 0) { dup2(command.inputFd, 0); }
            if (command.outputFd >= 0) { dup2(command.outputFd, 1); }
            bool isBuiltin = run_builtin(command.text, arguments);
            if (input >= 0) { dup2(input, 0); close(input); }
            else { close(0); }
            if (output >= 0) { dup2(output, 1); close(output); }
            else { close(1); }
            if (isBuiltin) { break; }
        }
        char path[MAX_LINE_LENGTH + 12];
        bool hasDirectory = false;
        for (char* c = command.text; *c; c++) { if (*c == '/') { hasDirectory = true; } }
        copy_string(path, hasDirectory ? "" : "initrd/bin/");
        copy_string(path + string_length(path), command.text);
        SpawnFileAction actions[MAX_SPAWN_ACTIONS];
        uint64_t actionCount = 0;
        int64_t inputFd = command.inputFd >= 0 ? command.inputFd : (i ? pipes[i - 1][0] : 0);
        int64_t outputFd = command.outputFd >= 0 ? command.outputFd : (i + 1 < commandCount ? pipes[i][1] : 1);
        if (inputFd != 0) { actions[actionCount++] = {SPAWN_DUP2, (int)inputFd, 0}; }
        if (outputFd != 1) { actions[actionCount++] = {SPAWN_DUP2, (int)outputFd, 1}; }
        for (uint64_t j = 0; j < pipeCount; j++)
        {
            actions[actionCount++] = {SPAWN_CLOSE, pipes[j][0], 0};
            actions[actionCount++] = {SPAWN_CLOSE, pipes[j][1], 0};
        }
        for (uint64_t j = 0; j < commandCount; j++)
        {
            if (commands[j].inputFd >= 0) { actions[actionCount++] = {SPAWN_CLOSE, (int)commands[j].inputFd, 0}; }
            if (commands[j].outputFd >= 0) { actions[actionCount++] = {SPAWN_CLOSE, (int)commands[j].outputFd, 0}; }
        }
        SpawnRequest request = {path, arguments, nullptr, actions, actionCount};
        auto tid = (int64_t)syscall(SYSCALL_SPAWN_ACTIONS, (uint64_t)&request);
        if (tid < 0) { isValid = false; }
        else { tids[started++] = tid; }
    }
    // The parent must close its copies before waiting, so readers can see EOF and writers can see a closed reader.
    for (uint64_t i = 0; i < pipeCount; i++) { close(pipes[i][0]); close(pipes[i][1]); }
    for (uint64_t i = 0; i < commandCount; i++)
    {
        if (commands[i].inputFd >= 0) { close(commands[i].inputFd); }
        if (commands[i].outputFd >= 0) { close(commands[i].outputFd); }
    }
    for (uint64_t i = 0; i < started; i++) { wait(tids[i]); }
    if (!isValid) { print("Cannot open redirection or start pipeline.\n"); }
    return isValid;
}

extern "C" void _start()
{
    print("\ncOS user shell. Type help for a list of commands.\n");
    char line[MAX_LINE_LENGTH + 1];
    while (1)
    {
        print("$ ");
        if (!read_line(line)) { syscall(SYSCALL_EXIT, 0); }

        bool hasOperators = false;
        for (char* c = line; *c; c++) { if (*c == '|' || *c == '<' || *c == '>') { hasOperators = true; } }
        if (hasOperators) { run_pipeline(line); continue; }

        // Split the line into the command and its argument at the first space.
        char* argument = line;
        while (*argument && *argument != ' ') { argument++; }
        if (*argument) { *argument++ = '\0'; }
        while (*argument == ' ') { argument++; }

        if (!line[0]) { continue; }
        if (!run_builtin(line, argument)) { run_program(line, argument); }
    }
}
