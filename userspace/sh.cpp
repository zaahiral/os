#include "process/syscall.h"
#include "output.h"

// The user-space shell. It reads the keyboard through the event system calls, runs a few built-in
// commands, and runs every other command as a program from initrd/bin, waiting for it to exit.

constexpr uint64_t MAX_LINE_LENGTH = 76;
constexpr uint8_t ENTER_SCAN_CODE = 0x1C;
constexpr uint8_t BACKSPACE_SCAN_CODE = 0x0E;
constexpr uint8_t LEFT_SHIFT_SCAN_CODE = 0x2A;
constexpr uint8_t RIGHT_SHIFT_SCAN_CODE = 0x36;
// A key release sends the key's scan code with the top bit set.
constexpr uint8_t RELEASE_BIT = 0x80;

// Characters for scan code set 1 on a US keyboard, indexed by scan code.
constexpr char LOWER_CASE[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
constexpr char UPPER_CASE[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";
constexpr uint8_t KEY_COUNT = sizeof(LOWER_CASE) - 1;

static uint64_t keyboardEventDescriptor;

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

// Read a line from the keyboard into a buffer, echoing each character as it is typed.
void read_line(char* line)
{
    uint64_t length = 0;
    bool isShiftHeld = false;
    while (1)
    {
        syscall(SYSCALL_ELISTEN, keyboardEventDescriptor);
        uint64_t scanCode;
        while ((scanCode = syscall(SYSCALL_EREAD, keyboardEventDescriptor, 0, 0)))
        {
            if (scanCode == LEFT_SHIFT_SCAN_CODE || scanCode == RIGHT_SHIFT_SCAN_CODE) { isShiftHeld = true; continue; }
            if (scanCode == (LEFT_SHIFT_SCAN_CODE | RELEASE_BIT) || scanCode == (RIGHT_SHIFT_SCAN_CODE | RELEASE_BIT)) { isShiftHeld = false; continue; }
            if (scanCode & RELEASE_BIT) { continue; }
            if (scanCode == ENTER_SCAN_CODE)
            {
                print("\n");
                line[length] = '\0';
                return;
            }
            if (scanCode == BACKSPACE_SCAN_CODE)
            {
                if (length) { length--; print("\b"); }
                continue;
            }
            if (scanCode >= KEY_COUNT) { continue; }
            char c = isShiftHeld ? UPPER_CASE[scanCode] : LOWER_CASE[scanCode];
            if (!c || length == MAX_LINE_LENGTH) { continue; }
            line[length++] = c;
            char echo[2] = {c, '\0'};
            print(echo);
        }
    }
}

void help_command()
{
    print("help             List the commands.\n");
    print("clear            Clear the screen.\n");
    print("echo <text>      Print the text.\n");
    print("ls [directory]   List a directory, initrd by default.\n");
    print("cat <file>       Print a file, such as disk/readme.txt.\n");
    print("write <file> <text>  Add a line to a file on the disk.\n");
    print("exit             Leave the user shell for the kernel shell.\n");
    print("Any other command runs the program of that name in initrd/bin, such as hello.\n");
}

void ls_command(const char* path)
{
    char name[256];
    for (uint64_t index = 0; ; index++)
    {
        uint64_t result = syscall(SYSCALL_READDIR, (uint64_t)path, index, (uint64_t)name);
        if (result == (uint64_t)-1)
        {
            print("No such directory: ");
            print(path);
            print("\n");
            return;
        }
        if (!result) { return; }
        print(name);
        // Directories are marked with a slash, as in most shells.
        if (result == 2) { print("/"); }
        print("\n");
    }
}

void cat_command(const char* path)
{
    uint64_t fd = syscall(SYSCALL_FOPEN, (uint64_t)path, 0);
    if (fd == (uint64_t)-1)
    {
        print("No such file: ");
        print(path);
        print("\n");
        return;
    }
    // Read the file in pieces, as it may be larger than the buffer.
    char buffer[257];
    uint64_t length;
    char last = '\n';
    while ((length = syscall(SYSCALL_FREAD, fd, (uint64_t)buffer, sizeof(buffer) - 1)) && length != (uint64_t)-1)
    {
        buffer[length] = '\0';
        print(buffer);
        last = buffer[length - 1];
    }
    if (last != '\n') { print("\n"); }
    syscall(SYSCALL_FCLOSE, fd);
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
    uint64_t fd = syscall(SYSCALL_FOPEN, (uint64_t)argument, FILE_CREATE | FILE_APPEND);
    if (fd == (uint64_t)-1)
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

void run_program(const char* name)
{
    // A program name without a directory is looked up in initrd/bin.
    char path[MAX_LINE_LENGTH + 12];
    bool hasDirectory = false;
    for (const char* c = name; *c; c++) { if (*c == '/') { hasDirectory = true; } }
    copy_string(path, hasDirectory ? "" : "initrd/bin/");
    copy_string(path + string_length(path), name);

    uint64_t tid = syscall(SYSCALL_SPAWN, (uint64_t)path);
    if (tid == (uint64_t)-1)
    {
        print("No such program: ");
        print(path);
        print("\n");
        return;
    }
    auto exitCode = (int64_t)syscall(SYSCALL_WAIT, tid);
    // Keys typed while the program ran were meant for it, so we discard them.
    while (syscall(SYSCALL_EREAD, keyboardEventDescriptor, 0, 0)) {}
    print("Exited with code ");
    if (exitCode < 0) { print("-"); exitCode = -exitCode; }
    print_number(exitCode);
    print(".\n");
}

extern "C" void _start()
{
    keyboardEventDescriptor = syscall(SYSCALL_EREGISTER, (uint64_t)"HID/Keyboard", 0);
    print("\ncOS user shell. Type help for a list of commands.\n");
    char line[MAX_LINE_LENGTH + 1];
    while (1)
    {
        print("$ ");
        read_line(line);

        // Split the line into the command and its argument at the first space.
        char* argument = line;
        while (*argument && *argument != ' ') { argument++; }
        if (*argument) { *argument++ = '\0'; }
        while (*argument == ' ') { argument++; }

        if (!line[0]) { continue; }
        else if (strings_equal(line, "help")) { help_command(); }
        else if (strings_equal(line, "clear")) { syscall(SYSCALL_CLEAR); }
        else if (strings_equal(line, "echo")) { print(argument); print("\n"); }
        else if (strings_equal(line, "ls")) { ls_command(*argument ? argument : "initrd"); }
        else if (strings_equal(line, "cat")) { cat_command(argument); }
        else if (strings_equal(line, "write")) { write_command(argument); }
        else if (strings_equal(line, "exit"))
        {
            syscall(SYSCALL_EDEREGISTER, keyboardEventDescriptor);
            syscall(SYSCALL_EXIT, 0);
        }
        else { run_program(line); }
    }
}
