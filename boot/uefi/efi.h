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

typedef struct
{
    uint32_t version, width, height, format;
    uint32_t redMask, greenMask, blueMask, reservedMask;
    uint32_t pixelsPerLine;
} EfiGraphicsInfo;

typedef struct
{
    uint32_t maxMode, mode;
    EfiGraphicsInfo* info;
    uint64_t infoSize, framebuffer, framebufferSize;
} EfiGraphicsMode;

typedef struct EfiGraphics EfiGraphics;
struct EfiGraphics
{
    EfiStatus (EFIAPI *queryMode)(EfiGraphics*, uint32_t, uint64_t*, EfiGraphicsInfo**);
    EfiStatus (EFIAPI *setMode)(EfiGraphics*, uint32_t);
    void* blt;
    EfiGraphicsMode* mode;
};

typedef struct EfiText EfiText;
struct EfiText
{
    void* reset;
    EfiStatus (EFIAPI *outputString)(EfiText*, uint16_t*);
};

typedef struct
{
    EfiTableHeader header;
    void* raiseTpl;
    void* restoreTpl;
    EfiStatus (EFIAPI *allocatePages)(uint32_t, uint32_t, uint64_t, uint64_t*);
    EfiStatus (EFIAPI *freePages)(uint64_t, uint64_t);
    EfiStatus (EFIAPI *getMemoryMap)(uint64_t*, void*, uint64_t*, uint64_t*, uint32_t*);
    EfiStatus (EFIAPI *allocatePool)(uint32_t, uint64_t, void**);
    EfiStatus (EFIAPI *freePool)(void*);
    void* events[6];
    void* installProtocol;
    void* reinstallProtocol;
    void* uninstallProtocol;
    EfiStatus (EFIAPI *handleProtocol)(EfiHandle, EfiGuid*, void**);
    void* reserved;
    void* registerProtocolNotify;
    void* locateHandle;
    void* locateDevicePath;
    void* installConfigurationTable;
    void* loadImage;
    void* startImage;
    void* exit;
    void* unloadImage;
    EfiStatus (EFIAPI *exitBootServices)(EfiHandle, uint64_t);
    void* getNextMonotonicCount;
    void* stall;
    EfiStatus (EFIAPI *setWatchdogTimer)(uint64_t, uint64_t, uint64_t, uint16_t*);
    void* connectController;
    void* disconnectController;
    void* openProtocol;
    void* closeProtocol;
    void* openProtocolInformation;
    void* protocolsPerHandle;
    void* locateHandleBuffer;
    EfiStatus (EFIAPI *locateProtocol)(EfiGuid*, void*, void**);
} EfiBootServices;

typedef struct
{
    EfiTableHeader header;
    uint16_t* vendor;
    uint32_t revision;
    EfiHandle consoleInHandle;
    void* consoleIn;
    EfiHandle consoleOutHandle;
    EfiText* consoleOut;
    EfiHandle errorHandle;
    void* error;
    void* runtimeServices;
    EfiBootServices* bootServices;
} EfiSystemTable;

#define EFI_ERROR(code) (0x8000000000000000ULL | (code))
#define EFI_BUFFER_TOO_SMALL EFI_ERROR(5)
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_LOADER_CODE 1
#define EFI_LOADER_DATA 2

#endif
