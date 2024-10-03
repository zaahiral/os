#ifndef COS_EFI_H
#define COS_EFI_H

#include <stdint.h>
#include <stddef.h>

// These are the UEFI x64 interfaces used by the loader. All calls use the Microsoft ABI.
#define EFIAPI __attribute__((ms_abi))
typedef uint64_t EfiStatus;
typedef void* EfiHandle;
typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } EfiGuid;
typedef struct { uint64_t signature; uint32_t revision, size, crc, reserved; } EfiTableHeader;
typedef struct { uint32_t type; uint64_t physical, virtualAddress, pages, attributes; } EfiMemoryDescriptor;

typedef struct EfiFile EfiFile;
struct EfiFile
{
    uint64_t revision;
    EfiStatus (EFIAPI *open)(EfiFile*, EfiFile**, uint16_t*, uint64_t, uint64_t);
    EfiStatus (EFIAPI *close)(EfiFile*);
    void* deleteFile;
    EfiStatus (EFIAPI *read)(EfiFile*, uint64_t*, void*);
    void* write;
    void* getPosition;
    void* setPosition;
    EfiStatus (EFIAPI *getInfo)(EfiFile*, EfiGuid*, uint64_t*, void*);
};

typedef struct EfiFilesystem EfiFilesystem;
struct EfiFilesystem
{
    uint64_t revision;
    EfiStatus (EFIAPI *openVolume)(EfiFilesystem*, EfiFile**);
};

typedef struct
{
    uint32_t revision;
    EfiHandle parent;
    void* systemTable;
    EfiHandle device;
    void* filePath;
    void* reserved;
    uint32_t optionsSize;
    void* options;
    void* imageBase;
    uint64_t imageSize;
    uint32_t codeType, dataType;
    void* unload;
} EfiLoadedImage;

