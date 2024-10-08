//@globals
//@body
    Memory::VirtualMemoryAllocationRequest request(Memory::VirtualAddress(0x1000), true, true);
    Memory::MemoryManager::instance().alloc_page(request);
    printf("Value of virtual address 0x1000: ");
    printf(*(uint8_t*)0x1000);
    while (1) asm volatile("hlt");
