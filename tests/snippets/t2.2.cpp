//@globals
//@body
    // Allocate 8192 bytes.
    auto allocation = kmalloc(8192, 0);
    printf("Allocated 8192 bytes.");
    while (1) asm volatile("hlt");
