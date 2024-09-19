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

