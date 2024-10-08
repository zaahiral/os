//@globals
//@body
    printf("Contents of 0x1000: ");
    printf(*(uint8_t*)Memory::VirtualAddress(Memory::PhysicalAddress(0x1000)).get());
    while (1) asm volatile("hlt");
