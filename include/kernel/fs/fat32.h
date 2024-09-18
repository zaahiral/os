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

    struct FAT32DirectoryEntry
    {
        // Names are stored as 8 characters and a 3 character extension, padded with spaces.
        char name[11];
        uint8_t attributes;
        uint8_t reserved;
        uint8_t creationTimeTenths;
        uint16_t creationTime;
        uint16_t creationDate;
        uint16_t accessedDate;
        uint16_t firstClusterHigh;
        uint16_t modificationTime;
        uint16_t modificationDate;
        uint16_t firstClusterLow;
        uint32_t size;
    } __attribute__((packed));

    // A driver for FAT32 filesystems on the ATA disk, which supports reading, writing and creating files with 8.3 names.
    // A node's inode number is the position of its directory entry on the disk, and the root directory has inode 0.
    class FAT32Driver : VFSDriver
    {
    public:
        static FAT32Driver& instance()
        {
            static FAT32Driver instance;
            return instance;
        }
        FAT32Driver(FAT32Driver const&) = delete;
        void operator=(FAT32Driver const&) = delete;
        // Read the boot sector and mount the filesystem at the given name, and return false if it is not FAT32.
        bool mount(const char* mountPoint);

