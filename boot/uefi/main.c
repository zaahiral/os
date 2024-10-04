#include "efi.h"
#include "multiboot2.h"

static EfiGuid loadedImageGuid = {0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EfiGuid filesystemGuid = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EfiGuid fileInfoGuid = {0x9576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EfiGuid graphicsGuid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

static EfiSystemTable* systemTable;
static EfiBootServices* bootServices;

typedef struct
{
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, programOffset, sectionOffset;
    uint32_t flags;
    uint16_t headerSize, programSize, programCount, sectionSize, sectionCount, stringIndex;
} ElfHeader;

typedef struct
{
    uint32_t type, flags;
    uint64_t offset, virtualAddress, physicalAddress, fileSize, memorySize, alignment;
} ElfProgram;

static void copy_bytes(void* destination, const void* source, uint64_t size)
{
    uint8_t* to = destination;
    const uint8_t* from = source;
    while (size--) { *to++ = *from++; }
}

static void zero_bytes(void* destination, uint64_t size)
{
    uint8_t* to = destination;
    while (size--) { *to++ = 0; }
}

static void print(uint16_t* message)
{
    systemTable->consoleOut->outputString(systemTable->consoleOut, message);
}

static void fail(uint16_t* message)
{
    print((uint16_t*)L"cOS UEFI: ");
    print(message);
    print((uint16_t*)L"\r\n");
    for (;;) { __asm__ volatile ("cli; hlt"); }
}

static uint64_t allocate_pages(uint64_t size)
{
    // Multiboot2 module and information addresses must fit in 32 bits.
    uint64_t address = 0xFFFFEFFF;
    if (!size || bootServices->allocatePages(1, EFI_LOADER_DATA, (size + 4095) / 4096, &address))
        fail((uint16_t*)L"Unable to allocate memory below 4GiB.");
    zero_bytes((void*)address, (size + 4095) & ~4095ULL);
    return address;
}

static void* read_file(EfiFile* root, uint16_t* path, uint64_t* fileSize)
{
    EfiFile* file;
    if (root->open(root, &file, path, 1, 0)) { fail(path); }
    uint64_t infoSize = 0;
    if (file->getInfo(file, &fileInfoGuid, &infoSize, NULL) != EFI_BUFFER_TOO_SMALL)
        fail((uint16_t*)L"Unable to read file information.");
    void* info;
    if (bootServices->allocatePool(EFI_LOADER_DATA, infoSize, &info))
        fail((uint16_t*)L"Unable to allocate file information.");
    if (file->getInfo(file, &fileInfoGuid, &infoSize, info))
        fail((uint16_t*)L"Unable to read file size.");
    *fileSize = ((uint64_t*)info)[1];
    bootServices->freePool(info);
    if (!*fileSize || *fileSize > 0x40000000)
        fail((uint16_t*)L"Invalid boot file size.");
    void* data = (void*)allocate_pages(*fileSize);
    uint64_t readSize = *fileSize;
    if (file->read(file, &readSize, data) || readSize != *fileSize)
        fail((uint16_t*)L"Unable to read boot file.");
    file->close(file);
    return data;
}

static uint64_t load_kernel(void* file, uint64_t fileSize)
{
    ElfHeader* header = file;
    if (fileSize < sizeof(*header) || header->ident[0] != 0x7F || header->ident[1] != 'E'
        || header->ident[2] != 'L' || header->ident[3] != 'F' || header->ident[4] != 2
        || header->ident[5] != 1 || header->type != 2 || header->machine != 62
        || header->programSize != sizeof(ElfProgram) || !header->programCount
        || header->programOffset > fileSize
        || header->programCount > (fileSize - header->programOffset) / sizeof(ElfProgram))
        fail((uint16_t*)L"Invalid kernel ELF header.");

    ElfProgram* programs = (ElfProgram*)((uint8_t*)file + header->programOffset);
    uint64_t loadStart = 0xFFFFFFFF, loadEnd = 0;
    for (uint16_t i = 0; i < header->programCount; i++)
    {
        ElfProgram* program = &programs[i];
        if (program->type != 1 || !program->memorySize) { continue; }
        if (program->offset > fileSize || program->fileSize > fileSize - program->offset
            || program->memorySize < program->fileSize || program->physicalAddress < 0x100000
            || program->physicalAddress >= 0x40000000
            || program->memorySize > 0x40000000 - program->physicalAddress
            || program->virtualAddress != 0xFFFFFF8000000000ULL + program->physicalAddress)
            fail((uint16_t*)L"Invalid kernel ELF segment.");
        if (program->physicalAddress < loadStart) { loadStart = program->physicalAddress; }
        if (program->physicalAddress + program->memorySize > loadEnd)
            loadEnd = program->physicalAddress + program->memorySize;
    }
    loadStart &= ~4095ULL;
    loadEnd = (loadEnd + 4095) & ~4095ULL;
    if (loadEnd <= loadStart
        || bootServices->allocatePages(2, EFI_LOADER_CODE, (loadEnd - loadStart) / 4096, &loadStart))
        fail((uint16_t*)L"Unable to reserve the kernel load address.");
    zero_bytes((void*)loadStart, loadEnd - loadStart);
    for (uint16_t i = 0; i < header->programCount; i++)
    {
        ElfProgram* program = &programs[i];
        if (program->type == 1 && program->memorySize)
            copy_bytes((void*)program->physicalAddress, (uint8_t*)file + program->offset, program->fileSize);
    }

    // The optional Multiboot2 EFI64 entry is physical, unlike the ELF's protected-mode entry.
    uint64_t searchEnd = fileSize < MULTIBOOT_SEARCH ? fileSize : MULTIBOOT_SEARCH;
    for (uint64_t offset = 0; offset + sizeof(struct multiboot_header) <= searchEnd; offset += 8)
    {
        struct multiboot_header* bootHeader = (void*)((uint8_t*)file + offset);
        if (bootHeader->magic != MULTIBOOT2_HEADER_MAGIC || bootHeader->architecture != 0
            || (uint32_t)(bootHeader->magic + bootHeader->architecture + bootHeader->header_length + bootHeader->checksum))
            continue;
        if (bootHeader->header_length < 24 || bootHeader->header_length > searchEnd - offset) { break; }
        uint64_t end = offset + bootHeader->header_length;
        for (uint64_t tagOffset = offset + 16; tagOffset + 8 <= end;)
        {
            uint16_t* tag = (void*)((uint8_t*)file + tagOffset);
            uint32_t size = ((uint32_t*)tag)[1];
            if (size < 8 || size > end - tagOffset || !tag[0]) { break; }
            if (tag[0] == 9 && size == 12)
            {
                uint64_t entry = ((uint32_t*)tag)[2];
                for (uint16_t i = 0; i < header->programCount; i++)
                {
                    ElfProgram* program = &programs[i];
                    if (program->type == 1 && (program->flags & 1) && entry >= program->physicalAddress
                        && entry - program->physicalAddress < program->fileSize)
                        return entry;
                }
                fail((uint16_t*)L"Invalid EFI64 entry address.");
            }
            tagOffset += (size + 7ULL) & ~7ULL;
        }
    }
    fail((uint16_t*)L"The kernel has no EFI64 entry.");
    return 0;
}

static EfiGraphics* setup_graphics(void)
{
    EfiGraphics* graphics;
    if (bootServices->locateProtocol(&graphicsGuid, NULL, (void**)&graphics))
        fail((uint16_t*)L"No graphics output protocol.");
    for (uint32_t i = 0; i < graphics->mode->maxMode; i++)
    {
        uint64_t infoSize;
        EfiGraphicsInfo* info;
        if (graphics->queryMode(graphics, i, &infoSize, &info)) { continue; }
        int preferred = info->width == 800 && info->height == 600 && info->format <= 1;
        bootServices->freePool(info);
        if (preferred && !graphics->setMode(graphics, i)) { break; }
    }
    EfiGraphicsInfo* info = graphics->mode->info;
    if (info->format > 1 || info->width < 640 || info->height < 400
        || info->pixelsPerLine < info->width || info->pixelsPerLine > 16384 || info->height > 16384
        || graphics->mode->framebuffer >= 0x100000000ULL
        || graphics->mode->framebufferSize > 0x100000000ULL - graphics->mode->framebuffer
        || (uint64_t)info->pixelsPerLine * info->height * 4 > graphics->mode->framebufferSize)
        fail((uint16_t*)L"The console requires a 32-bit RGB or BGR framebuffer below 4GiB.");
    return graphics;
}

EfiStatus EFIAPI efi_main(EfiHandle image, EfiSystemTable* table)
{
    systemTable = table;
    bootServices = table->bootServices;
    bootServices->setWatchdogTimer(0, 0, 0, NULL);
    print((uint16_t*)L"cOS UEFI bootloader\r\n");
    EfiLoadedImage* loadedImage;
    EfiFilesystem* filesystem;
    EfiFile* root;
    if (bootServices->handleProtocol(image, &loadedImageGuid, (void**)&loadedImage)
        || bootServices->handleProtocol(loadedImage->device, &filesystemGuid, (void**)&filesystem)
        || filesystem->openVolume(filesystem, &root))
        fail((uint16_t*)L"Unable to open the boot volume.");
    uint64_t kernelSize, initrdSize;
    void* kernel = read_file(root, (uint16_t*)L"\\kernel.bin", &kernelSize);
    uint64_t entry = load_kernel(kernel, kernelSize);
    bootServices->freePages((uint64_t)kernel, (kernelSize + 4095) / 4096);
    uint64_t initrd = (uint64_t)read_file(root, (uint16_t*)L"\\initrd.tar", &initrdSize);
    root->close(root);
    EfiGraphics* graphics = setup_graphics();

    uint64_t mapSize = 0, mapKey, descriptorSize;
    uint32_t descriptorVersion;
    if (bootServices->getMemoryMap(&mapSize, NULL, &mapKey, &descriptorSize, &descriptorVersion) != EFI_BUFFER_TOO_SMALL)
        fail((uint16_t*)L"Unable to size the memory map.");
    uint64_t mapCapacity = mapSize + 4096;
    void* map = (void*)allocate_pages(mapCapacity);
    uint64_t infoCapacity = mapCapacity + 4096;
    uint8_t* info = (void*)allocate_pages(infoCapacity);

    struct multiboot_tag_module* module = (void*)(info + 8);
    module->type = MULTIBOOT_TAG_TYPE_MODULE;
    module->size = 16 + sizeof("initrd");
    module->mod_start = initrd;
    module->mod_end = initrd + ((initrdSize + 4095) & ~4095ULL);
    copy_bytes(module->cmdline, "initrd", sizeof("initrd"));
    struct multiboot_tag_framebuffer* framebuffer = (void*)(info + 32);
    framebuffer->type = MULTIBOOT_TAG_TYPE_FRAMEBUFFER;
    framebuffer->size = sizeof(*framebuffer);
    framebuffer->address = graphics->mode->framebuffer;
    framebuffer->pitch = graphics->mode->info->pixelsPerLine * 4;
    framebuffer->width = graphics->mode->info->width;
    framebuffer->height = graphics->mode->info->height;
    framebuffer->bpp = 32;
    framebuffer->framebufferType = 1;
    framebuffer->redPosition = graphics->mode->info->format == 0 ? 0 : 16;
    framebuffer->greenPosition = 8;
    framebuffer->bluePosition = graphics->mode->info->format == 0 ? 16 : 0;
    framebuffer->redSize = framebuffer->greenSize = framebuffer->blueSize = 8;

    print((uint16_t*)L"Kernel and initrd loaded. Leaving firmware.\r\n");
    for (uint32_t attempt = 0; attempt < 3; attempt++)
    {
        mapSize = mapCapacity;
        if (bootServices->getMemoryMap(&mapSize, map, &mapKey, &descriptorSize, &descriptorVersion)
            || descriptorSize < sizeof(EfiMemoryDescriptor) || mapSize % descriptorSize) { break; }
        struct multiboot_tag_mmap* mmap = (void*)(info + 72);
        mmap->type = MULTIBOOT_TAG_TYPE_MMAP;
        mmap->entry_size = sizeof(struct multiboot_mmap_entry);
        mmap->entry_version = 0;
        uint64_t count = mapSize / descriptorSize;
        mmap->size = 16 + count * mmap->entry_size;
        if (72 + mmap->size + 8 > infoCapacity) { break; }
        for (uint64_t i = 0; i < count; i++)
        {
            EfiMemoryDescriptor* descriptor = (void*)((uint8_t*)map + i * descriptorSize);
            struct multiboot_mmap_entry* region = &mmap->entries[i];
            region->addr = descriptor->physical;
            region->len = descriptor->pages * 4096;
            // Boot-services memory is reusable after ExitBootServices. Loader allocations remain reserved.
            region->type = descriptor->type == 7 || descriptor->type == 3 || descriptor->type == 4 ? 1 : 2;
            if (descriptor->type == 9) { region->type = 3; }
            if (descriptor->type == 10) { region->type = 4; }
            if (descriptor->type == 8) { region->type = 5; }
        }
        uint32_t totalSize = 72 + mmap->size + 8;
        struct multiboot_tag* end = (void*)(info + totalSize - 8);
        end->type = MULTIBOOT_TAG_TYPE_END;
        end->size = 8;
        ((uint32_t*)info)[0] = totalSize;
        EfiStatus status = bootServices->exitBootServices(image, mapKey);
        if (!status)
        {
            __asm__ volatile ("cli; cld; jmp *%0" : : "r"(entry), "b"((uint32_t)(uint64_t)info), "a"(MULTIBOOT2_BOOTLOADER_MAGIC) : "memory");
            __builtin_unreachable();
        }
        // A failed exit may already have stopped firmware devices. Only memory-map and exit calls are retried.
        if (status != EFI_INVALID_PARAMETER) { break; }
    }
    for (;;) { __asm__ volatile ("cli; hlt"); }
}
