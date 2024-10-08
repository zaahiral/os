//@globals
//@body
    printf("Value of virtual address 0x1000: ");
    printf(*(uint8_t*)0x1000);
    while (1) asm volatile("hlt");
