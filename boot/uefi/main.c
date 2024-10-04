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
