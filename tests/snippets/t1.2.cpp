//@globals
//@body
    for (auto it = Multiboot::MultibootManager::instance().first(MULTIBOOT_TAG_TYPE_MMAP); !it.is_end(); ++it)
    {
        auto tag = reinterpret_cast<multiboot_tag_mmap*>(&(*it));
        for (auto entry = tag->entries;
            reinterpret_cast<uint64_t>(entry) < reinterpret_cast<uint64_t>(tag) + tag->size;
            entry = reinterpret_cast<multiboot_mmap_entry*>((reinterpret_cast<uint64_t>(entry) + tag->entry_size)))
        {
            if (entry->type != 1) { continue; }
            printf("MEMORY DETECTED: ");
            printf(entry->len);
            printf(" bytes at ");
            printf(entry->addr);
            printf("\n");
        }
    }
    while (1) asm volatile("hlt");
