#ifndef FAT32_H
#define FAT32_H

#include "types.h"
#include "fs/vfs.h"
#include "process/spinlock.h"
#include "drivers/storage/ata.h"

namespace Filesystem
{
    #define FAT32_END_OF_CHAIN 0x0FFFFFF8
    #define FAT32_CLUSTER_MASK 0x0FFFFFFF
    #define FAT32_ENTRIES_PER_SECTOR (ATA_SECTOR_SIZE / sizeof(FAT32DirectoryEntry))

    #define FAT32_ATTRIBUTE_VOLUME_ID 0x08
    #define FAT32_ATTRIBUTE_DIRECTORY 0x10
    #define FAT32_ATTRIBUTE_ARCHIVE 0x20
    #define FAT32_ATTRIBUTE_LONG_NAME 0x0F
    #define FAT32_ENTRY_END 0x00
    #define FAT32_ENTRY_DELETED 0xE5

    struct FAT32BootSector
    {
        uint8_t jump[3];
        char oemName[8];
        uint16_t bytesPerSector;
        uint8_t sectorsPerCluster;
        uint16_t reservedSectors;
        uint8_t fatCount;
        uint16_t rootEntryCount;
        uint16_t totalSectors16;
        uint8_t mediaType;
        uint16_t fatSize16;
        uint16_t sectorsPerTrack;
        uint16_t headCount;
        uint32_t hiddenSectors;
        uint32_t totalSectors32;
        uint32_t fatSize32;
        uint16_t extendedFlags;
        uint16_t version;
        uint32_t rootCluster;
        uint16_t fsInfoSector;
    } __attribute__((packed));

