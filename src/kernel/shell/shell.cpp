#include "shell/shell.h"
#include "print.h"
#include "libc/string.h"
#include "memory/kheap.h"
#include "cpu/io/io.h"
#include "cpu/io/pci.h"
#include "cpu/io/pit.h"
#include "fs/vfs.h"
#include "event/dispatcher.h"
#include "process/taskManager.h"
#include "drivers/network/amdpcnetiii.h"
#include "drivers/graphics/bochsvbe.h"
#include "network/arp.h"
#include "gui/desktop.h"
#include "script/interpreter.h"
#include "loader/elf.h"

namespace Shell
{
    namespace
    {
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

        void print_hex(uint64_t value, uint64_t digits)
        {
            char buffer[17];
            buffer[digits] = '\0';
            for (uint64_t i = digits; i > 0; i--, value >>= 4)
                buffer[i - 1] = "0123456789ABCDEF"[value & 0xF];
            printf(buffer);
        }

        // IP and MAC addresses are stored with their first byte in the lowest byte.
        void print_IP_address(uint32_t address)
        {
            for (int i = 0; i < 4; i++)
            {
                printf((address >> (8 * i)) & 0xFF);
                if (i < 3) { printf("."); }
            }
        }

        void print_MAC_address(uint64_t address)
        {
            for (int i = 0; i < 6; i++)
            {
                print_hex((address >> (8 * i)) & 0xFF, 2);
                if (i < 5) { printf(":"); }
            }
        }

        // Parses an address such as 10.0.2.2, and returns 0 if it is not valid.
        uint32_t parse_IP_address(char* text)
        {
            uint32_t address = 0;
            for (int i = 0; i < 4; i++)
            {
                uint64_t octet = 0;
                if (*text < '0' || *text > '9') { return 0; }
                while (*text >= '0' && *text <= '9')
                    octet = octet * 10 + (*text++ - '0');
                if (octet > 255 || (i < 3 && *text++ != '.')) { return 0; }
                address |= octet << (8 * i);
            }
            return *text ? 0 : address;
        }
    }

    void KernelShell::task()
    {
        auto& shell = KernelShell::instance();
        shell.m_tid = Task::TaskManager::instance().get_current_task();
        shell.m_keyboardEventDescriptor = Events::EventDispatcher::instance().register_event_listener(shell.m_tid, "HID/Keyboard", 0);
        printf("\nWelcome to cOS. Type help for a list of commands.\ncOS> ");
        while (1)
        {
            Events::EventDispatcher::instance().block_event_listen(shell.m_tid, shell.m_keyboardEventDescriptor);
            uint64_t scanCode;
            while ((scanCode = Events::EventDispatcher::instance().read_from_event_queue(shell.m_tid, shell.m_keyboardEventDescriptor)))
            {
                shell.handle_scan_code(scanCode);
            }
        }
    }

    void KernelShell::handle_scan_code(uint8_t scanCode)
    {
        if (scanCode == LEFT_SHIFT_SCAN_CODE || scanCode == RIGHT_SHIFT_SCAN_CODE) { m_shift = true; return; }
        if (scanCode == (LEFT_SHIFT_SCAN_CODE | RELEASE_BIT) || scanCode == (RIGHT_SHIFT_SCAN_CODE | RELEASE_BIT)) { m_shift = false; return; }
        if (scanCode & RELEASE_BIT) { return; }

        if (scanCode == ENTER_SCAN_CODE)
        {
            printf("\n");
            m_line[m_lineLength] = '\0';
            run_command();
            m_lineLength = 0;
            printf("cOS> ");
            return;
        }
        if (scanCode == BACKSPACE_SCAN_CODE)
        {
            if (!m_lineLength) { return; }
            m_lineLength--;
            printf("\b");
            return;
        }
        if (scanCode >= KEY_COUNT) { return; }

        char c = m_shift ? UPPER_CASE[scanCode] : LOWER_CASE[scanCode];
        if (!c || m_lineLength == SHELL_MAX_LINE_LENGTH) { return; }
        m_line[m_lineLength++] = c;
        char echo[2] = {c, '\0'};
        printf(echo);
    }

    void KernelShell::run_command()
    {
        // Split the line into the command and its argument at the first space.
        char* argument = m_line;
        while (*argument && *argument != ' ') { argument++; }
        if (*argument) { *argument++ = '\0'; }
        while (*argument == ' ') { argument++; }

        if (!m_line[0]) { return; }
        else if (!strcmp(m_line, "help")) { help_command(); }
        else if (!strcmp(m_line, "clear")) { clear_screen(); }
        else if (!strcmp(m_line, "echo")) { printf(argument); printf("\n"); }
        else if (!strcmp(m_line, "ls")) { ls_command(*argument ? argument : (char*)"initrd"); }
        else if (!strcmp(m_line, "cat")) { cat_command(argument); }
        else if (!strcmp(m_line, "uptime")) { uptime_command(); }
        else if (!strcmp(m_line, "pci")) { pci_command(); }
        else if (!strcmp(m_line, "net")) { net_command(); }
        else if (!strcmp(m_line, "arp")) { arp_command(argument); }
        else if (!strcmp(m_line, "gui")) { gui_command(); }
        else if (!strcmp(m_line, "run")) { run_file_command(argument); }
        else if (!strcmp(m_line, "cs")) { script_command(argument); }
        else if (!strcmp(m_line, "exec")) { exec_command(argument); }
        else if (!strcmp(m_line, "reboot")) { reboot_command(); }
        else
        {
            printf("Unknown command: ");
            printf(m_line);
            printf("\n");
        }
    }

    void KernelShell::help_command()
    {
        printf("help             List the commands.\n");
        printf("clear            Clear the screen.\n");
        printf("echo <text>      Print the text.\n");
        printf("ls [directory]   List a directory in the initrd, initrd by default.\n");
        printf("cat <file>       Print a file, such as initrd/hello.txt.\n");
        printf("uptime           Print the time since boot.\n");
        printf("pci              List the devices on the PCI bus.\n");
        printf("net              Print the network card's MAC and IP addresses.\n");
        printf("arp <address>    Resolve an IP address to a MAC address, such as 10.0.2.2.\n");
        printf("gui              Open the graphical desktop.\n");
        printf("run <file>       Run a cScript file, such as initrd/scripts/fibonacci.cs.\n");
        printf("cs <code>        Run one line of cScript, such as cs print(6 * 7);\n");
        printf("exec <program>   Run a user-space program from initrd/bin, such as exec hello.\n");
        printf("reboot           Restart the computer.\n");
    }

    void KernelShell::ls_command(char* path)
    {
        auto directory = Filesystem::VirtualFilesystemManager::instance().open_file(path, 0);
        if (!directory || !(directory->flags & Filesystem::VirtualFilesystemNode::VFS_DIRECTORY))
        {
            printf("No such directory: ");
            printf(path);
            printf("\n");
            if (directory) { delete directory; }
            return;
        }
        for (uint64_t index = 0; ; index++)
        {
            auto entry = Filesystem::VirtualFilesystemManager::instance().readdir(directory, index);
            if (!entry) { break; }
            printf(entry->name);
            // Directories are marked with a slash, as in most shells.
            auto node = Filesystem::VirtualFilesystemManager::instance().finddir(directory, entry->name);
            if (node && (node->flags & Filesystem::VirtualFilesystemNode::VFS_DIRECTORY)) { printf("/"); }
            printf("\n");
            if (node) { delete node; }
            delete entry;
        }
        delete directory;
    }

    void KernelShell::cat_command(char* path)
    {
        auto file = Filesystem::VirtualFilesystemManager::instance().open_file(path, 0);
        if (!file || !(file->flags & Filesystem::VirtualFilesystemNode::VFS_FILE))
        {
            printf("No such file: ");
            printf(path);
            printf("\n");
            if (file) { delete file; }
            return;
        }
        auto length = Filesystem::VirtualFilesystemManager::instance().filelen(file);
        // We add one to the length to add a null terminator.
        auto buffer = (char*)kmalloc(length + 1, 0);
        Filesystem::VirtualFilesystemManager::instance().read(file, 0, length, buffer);
        buffer[length] = '\0';
        printf(buffer);
        if (length && buffer[length - 1] != '\n') { printf("\n"); }
        kfree(buffer);
        delete file;
    }

    void KernelShell::uptime_command()
    {
        auto milliseconds = CPU::PIT::time_since_boot();
        printf("Up for ");
        printf(milliseconds / 1000);
        printf(".");
        // Print the tenths of a second.
        printf((milliseconds / 100) % 10);
        printf(" seconds.\n");
    }

    void KernelShell::pci_command()
    {
        printf("Bus  Device  Function  Vendor:Device  Revision\n");
        for (auto it = PeripheralComponentInterconnect::PCIManager::instance()->m_devices.first(); !it.is_end(); ++it)
        {
            printf(" ");
            printf(it->bus);
            printf("     ");
            print_hex(it->device, 2);
            printf("       ");
            printf(it->function);
            printf("       ");
            print_hex(it->vendorId, 4);
            printf(":");
            print_hex(it->deviceId, 4);
            printf("      ");
            printf(it->revision);
            printf("\n");
        }
    }

    void KernelShell::net_command()
    {
        printf("MAC address: ");
        print_MAC_address(Drivers::AMDPCNetIIIDriver::instance()->get_MAC_address());
        printf("\nIP address:  ");
        print_IP_address(Drivers::AMDPCNetIIIDriver::instance()->get_IP_address());
        printf("\n");
    }

    void KernelShell::arp_command(char* address)
    {
        auto IPAddress = parse_IP_address(address);
        if (!IPAddress)
        {
            printf("Usage: arp <address>, such as arp 10.0.2.2\n");
            return;
        }
        auto ARPManager = Networking::AddressResolutionProtocol::AddressResolutionProtocolManager::instance();
        ARPManager->send_ARP_request(IPAddress);
        // Wait up to a second for the reply, which the network task adds to the ARP cache.
        uint64_t MACAddress = 0;
        for (int attempt = 0; attempt < 20 && !MACAddress; attempt++)
        {
            Task::TaskManager::instance().sleep_for(50);
            MACAddress = ARPManager->find_cached_IP_address(IPAddress);
        }
        print_IP_address(IPAddress);
        if (!MACAddress)
        {
            printf(" did not reply.\n");
            return;
        }
        printf(" is at ");
        print_MAC_address(MACAddress);
        printf("\n");
    }

    void KernelShell::gui_command()
    {
        if (!Drivers::BochsVBEDriver::instance().is_present())
        {
            printf("The display adapter does not support the Bochs VBE extensions.\n");
            return;
        }
        GUI::Desktop::run(m_tid, m_keyboardEventDescriptor);
    }

    void KernelShell::run_file_command(char* path)
    {
        auto file = Filesystem::VirtualFilesystemManager::instance().open_file(path, 0);
        if (!file || !(file->flags & Filesystem::VirtualFilesystemNode::VFS_FILE))
        {
            printf("No such file: ");
            printf(path);
            printf("\n");
            if (file) { delete file; }
            return;
        }
        auto length = Filesystem::VirtualFilesystemManager::instance().filelen(file);
        auto source = (char*)kmalloc(length + 1, 0);
        Filesystem::VirtualFilesystemManager::instance().read(file, 0, length, source);
        source[length] = '\0';
        delete file;
        script_command(source);
        kfree(source);
    }

    void KernelShell::script_command(char* source)
    {
        Script::Interpreter interpreter(m_tid, m_keyboardEventDescriptor);
        interpreter.run(source);
    }

    void KernelShell::exec_command(char* program)
    {
        // A program name without a directory is looked up in initrd/bin.
        char path[SHELL_MAX_LINE_LENGTH + 12];
        bool hasDirectory = false;
        for (char* c = program; *c; c++) { if (*c == '/') { hasDirectory = true; } }
        strcpy(path, hasDirectory ? (char*)"" : (char*)"initrd/bin/");
        strcpy(path + strlen(path), program);

        auto file = Filesystem::VirtualFilesystemManager::instance().open_file(path, 0);
        if (!file || !(file->flags & Filesystem::VirtualFilesystemNode::VFS_FILE))
        {
            printf("No such program: ");
            printf(path);
            printf("\n");
            if (file) { delete file; }
            return;
        }
        delete file;

        auto tid = Loader::spawn_user_program(path);
        auto exitCode = (int64_t)Task::TaskManager::instance().wait_for_task(tid);
        printf("Exited with code ");
        if (exitCode < 0) { printf("-"); exitCode = -exitCode; }
        printf(exitCode);
        printf(".\n");
    }

    void KernelShell::reboot_command()
    {
        // Pulsing the CPU reset line through the keyboard controller restarts the computer.
        while (IO::in_8(0x64) & 0x02) {}
        IO::out_8(0x64, 0xFE);
    }
}
