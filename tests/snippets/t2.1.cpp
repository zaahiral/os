//@globals
//@body
    // Allocate 10 bytes.
    auto allocation = kmalloc(10, 0);
    uint8_t magicBytes[10] = {0x03, 0x11, 0x3F, 0x5C, 0x91, 0x11, 0x44, 0x53, 0xDE, 0xB0};
    // Copy magicBytes into allocation.
    memcpy(allocation, magicBytes, 10);
    // Allocate more bytes.
    auto secondAllocation = kmalloc(100, 0);
    kfree(secondAllocation);
    // Check first allocation has not been overwritten.
    bool overwritten = false;
    for (int i = 0; i < 10; i++)
    {
        if (static_cast<uint8_t*>(allocation)[i] != magicBytes[i])
        {
            overwritten = true;
            break;
        }
    }
    if (!overwritten)
        printf("Allocation was not overwritten.");
    while (1) asm volatile("hlt");
