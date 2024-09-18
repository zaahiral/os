#include "guard-abi.h"
#include "print.h"
#include "cpu/interrupts/interrupts.h"
#include "boot/multibootManager.h"
#include "cpu/gdt.h"
#include "cpu/tss.h"
#include "memory/kheap.h"
#include "memory/MemoryManager.h"
#include "fs/vfs.h"
#include "drivers/initrd/tarinitrd.h"
#include "cpu/io/pit.h"
#include "boot/modules/moduleManager.h"
#include "process/scheduler.h"
#include "loader/elf.h"
#include "common/hashmap.h"
#include "drivers/hid/keyboard.h"
#include "drivers/network/amdpcnetiii.h"
#include "network/icmp.h"
#include "network/tcp.h"
#include "cpu/io/pci.h"
#include "network/arp.h"
#include "network/ethernet.h"
#include "network/ipv4.h"
#include "network/udp.h"
#include "process/task.h"
#include "process/taskManager.h"
#include "process/mutex.h"
#include "x86_64.h"
#include "process/masterScheduler.h"
#include "shell/shell.h"

extern "C" void __cxa_pure_virtual() { printf("This should not be called."); }

typedef void (*constructor)();
extern "C" constructor start_ctors;
extern "C" constructor end_ctors;
extern "C" uint64_t start_of_kernel_image;
extern "C" uint64_t end_of_kernel_image;
extern "C" void call_constructors()
{
    for(constructor* i = &start_ctors; i != &end_ctors; i++)
        (*i)();
}

extern "C" void jump_to_usermode_entry();
extern "C" void start_userspace_task_iretq(uint64_t entryPoint, uint64_t userStackPointer);
extern "C" void start_userspace_task_sysret(uint64_t entryPoint, uint64_t userStackPointer);

void task_one()
{
    while (1)
    {
        printf("Task one. ");
        Task::TaskManager::instance().sleep_for(1000);
    }
}

// Pass frames received by the network driver up to the Ethernet layer.
void network_task()
{
    auto tid = Task::TaskManager::instance().get_current_task();
    auto ed = Events::EventDispatcher::instance().register_event_listener(tid, "Network/Event", 0);
    while (1)
    {
        Events::EventDispatcher::instance().block_event_listen(tid, ed);
        auto data = (Drivers::NetworkDataReceived*)Events::EventDispatcher::instance().read_from_event_queue(tid, ed);
        while (data)
        {
            Networking::Ethernet::EthernetLayerManager::instance()->on_receive_data(data->buf, data->size);
            delete data;
            data = (Drivers::NetworkDataReceived*)Events::EventDispatcher::instance().read_from_event_queue(tid, ed);
        }
    }
}

extern "C" void pre_kernel(const Multiboot::multiboot_info* multibootStructure, const uint64_t gdtp,
                            const uint64_t gdt, const uint64_t tss)
{
    clear_screen();
    printf("cOS v1.0.0b - COPYRIGHT Zaahir Ali 2024\n");
    printf("WARNING: This version of cOS has not been compiled with a GUI.\n");
    Multiboot::MultibootManager::instance().set_structure((Multiboot::multiboot_info*)(Memory::VirtualAddress(Memory::PhysicalAddress((Multiboot::multiboot_info*)multibootStructure))).get());
    printf("---------- MEMORY MAP ----------------------------------------------------------");
    Memory::MemoryManager::instance().protect_physical_regions(Memory::VirtualAddress(reinterpret_cast<uint64_t>(&end_of_kernel_image)).get_low_physical());
    Modules::protect_pages();
    printf("--------------------------------------------------------------------------------");
    Memory::MemoryManager::instance().remap_pages();
    Modules::map_virtual();
    CPU::set_global_descriptor_table(Memory::VirtualAddress(gdt).get());
CPU::set_task_state_segment(static_cast<CPU::TaskStateSegment64*>(Memory::VirtualAddress(tss).get()));
    CPU::InterruptManager::instance().initialise();
    CPU::PIT::instance().initialise();
    Memory::KernelHeapManager::instance();
    printf("-> Kernel heap allocated successfully.\n");
    Filesystem::VirtualFilesystemManager::instance();
    printf("-> Virtual filesystem initialised successfully.\n");
    Filesystem::TarReader::instance().load_initrd();
    printf("-> Read initrd successfully.\n");
    Task::TaskManager::instance();
    Events::EventDispatcher::instance();
    Drivers::KeyboardDriver keyboardDriver;
    PeripheralComponentInterconnect::PCIManager PCIManager;
    printf("---------- PCI DEVICES ---------------------------------------------------------");
    for (auto it = PCIManager.m_devices.first(); !it.is_end(); ++it)
    {
        printf("Device ID: ");
        printf(it->deviceId);
        printf(", Vendor ID: ");
        printf(it->vendorId);
        printf(", Rev: ");
        printf(it->revision);
        printf("\n");
    }
    printf("--------------------------------------------------------------------------------");
    Drivers::AMDPCNetIIIDriver AMDPCNETIIIDriver;
    Networking::Ethernet::EthernetLayerManager ELM(&AMDPCNETIIIDriver);
    Networking::AddressResolutionProtocol::AddressResolutionProtocolManager ARPManager(&ELM);
    uint8_t ip1 = 10, ip2 = 0, ip3 = 2, ip4 = 15;
    uint32_t ip_be = ((uint32_t)ip4 << 24)
                | ((uint32_t)ip3 << 16)
                | ((uint32_t)ip2 << 8)
                | (uint32_t)ip1;
    uint8_t gip1 = 10, gip2 = 0, gip3 = 2, gip4 = 2;
    uint32_t gip_be = ((uint32_t)gip4 << 24)
                   | ((uint32_t)gip3 << 16)
                   | ((uint32_t)gip2 << 8)
                   | (uint32_t)gip1;

    

    uint8_t sip1 = 142, sip2 = 250, sip3 = 200, sip4 = 46;
    uint32_t sip_be = ((uint32_t)sip4 << 24)
                   | ((uint32_t)sip3 << 16)
                   | ((uint32_t)sip2 << 8)
                   | (uint32_t)sip1;

                   uint8_t subnet1 = 255, subnet2 = 255, subnet3 = 255, subnet4 = 0;
    uint32_t subnet_be = ((uint32_t)subnet4 << 24)
                   | ((uint32_t)subnet3 << 16)
                   | ((uint32_t)subnet2 << 8)
                   | (uint32_t)subnet1;
    AMDPCNETIIIDriver.set_IP_address(ip_be);
    Networking::InternetProtocolV4::InternetProtocolManager im(&ELM, gip_be, subnet_be);

    // Scheduler queue 1 (priority 256-511) is for network stack processing.
    auto networkTask = Task::TaskManager::instance().create_new_kernel_task(256);
    networkTask.set_entry_point((void*)& network_task);
    Task::TaskManager::instance().add_task(networkTask);

    auto shellTask = Task::TaskManager::instance().create_new_kernel_task();
    shellTask.set_entry_point((void*)& Shell::KernelShell::task);

    Task::TaskManager::instance().add_task(shellTask);
    Task::TaskManager::instance().run();
}
