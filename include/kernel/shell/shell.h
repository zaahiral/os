#ifndef SHELL_H
#define SHELL_H

#include "types.h"

namespace Shell
{
    #define SHELL_MAX_LINE_LENGTH 76

    // An interactive command line that runs as a kernel task and reads the keyboard through the event system.
    class KernelShell
    {
    public:
        static KernelShell& instance()
        {
            static KernelShell instance;
            return instance;
        }
        KernelShell(KernelShell const&) = delete;
        void operator=(KernelShell const&) = delete;
        // The entry point of the shell task.
        static void task();
    private:
        KernelShell() {}
        uint64_t m_tid = 0;
        uint64_t m_keyboardEventDescriptor = 0;
        bool m_shift = false;
        char m_line[SHELL_MAX_LINE_LENGTH + 1];
        uint64_t m_lineLength = 0;
        void handle_scan_code(uint8_t scanCode);
        void run_command();
        void help_command();
        void ls_command(char* path);
        void cat_command(char* path);
        void uptime_command();
        void mem_command();
        void pci_command();
        void net_command();
        void arp_command(char* address);
        void gui_command();
        void run_file_command(char* path);
        void script_command(char* source);
        void exec_command(char* program);
        void write_command(char* argument);
        void reboot_command();
    };
}

#endif
