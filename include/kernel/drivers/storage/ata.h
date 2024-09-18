#ifndef ATA_H
#define ATA_H

#include "types.h"
#include "process/spinlock.h"

namespace Drivers
{
    #define ATA_PRIMARY_IO_BASE 0x1F0
    #define ATA_PRIMARY_CONTROL 0x3F6

    #define ATA_REGISTER_DATA 0
    #define ATA_REGISTER_SECTOR_COUNT 2
    #define ATA_REGISTER_LBA_LOW 3
    #define ATA_REGISTER_LBA_MID 4
    #define ATA_REGISTER_LBA_HIGH 5
    #define ATA_REGISTER_DRIVE 6
    #define ATA_REGISTER_STATUS 7
    #define ATA_REGISTER_COMMAND 7

    #define ATA_STATUS_ERROR 0x01
    #define ATA_STATUS_DATA_REQUEST 0x08
    #define ATA_STATUS_BUSY 0x80

    #define ATA_COMMAND_READ_SECTORS 0x20
    #define ATA_COMMAND_WRITE_SECTORS 0x30
    #define ATA_COMMAND_FLUSH_CACHE 0xE7
    #define ATA_COMMAND_IDENTIFY 0xEC

    #define ATA_SECTOR_SIZE 512

    // Reads and writes sectors on the primary master ATA disk using programmed I/O.
    // The driver polls the status register rather than using interrupts, which is slower but simple.
    class ATADriver
    {
    public:
        static ATADriver& instance()
        {
            static ATADriver instance;
            return instance;
        }
        ATADriver(ATADriver const&) = delete;
        void operator=(ATADriver const&) = delete;

        bool is_present() { return m_isPresent; }
        // These use 28-bit logical block addresses, so they can reach the first 128GiB of the disk.
        bool read_sector(uint32_t lba, uint8_t* buffer);
        bool write_sector(uint32_t lba, const uint8_t* buffer);
    private:
        ATADriver();
        bool m_isPresent = false;
        Spinlock m_spinlock;
        bool wait_until_ready();
        void select_sector(uint32_t lba);
    };
}

#endif
