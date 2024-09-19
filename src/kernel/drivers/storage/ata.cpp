#include "drivers/storage/ata.h"
#include "cpu/io/io.h"

namespace Drivers
{
    ATADriver::ATADriver()
    {
        // Disable interrupts from the disk, as we poll the status register instead.
        IO::out_8(ATA_PRIMARY_CONTROL, 0x02);

        // A status of 0xFF means that there is no controller on the bus.
        if (IO::in_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_STATUS) == 0xFF) { return; }

        // Select the master drive and send IDENTIFY. A status of 0 means that there is no drive.
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_DRIVE, 0xA0);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_SECTOR_COUNT, 0);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_LOW, 0);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_MID, 0);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_HIGH, 0);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_COMMAND, ATA_COMMAND_IDENTIFY);
        if (!IO::in_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_STATUS)) { return; }
        if (!wait_until_ready()) { return; }

        // ATAPI drives such as CD-ROMs set the middle and high LBA registers, and we only support disks.
        if (IO::in_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_MID) || IO::in_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_HIGH)) { return; }

        // The identify data is 256 words, which we do not need yet.
        for (int i = 0; i < 256; i++) { IO::in_16(ATA_PRIMARY_IO_BASE + ATA_REGISTER_DATA); }
        m_isPresent = true;
    }

    // Wait until the drive is no longer busy and is ready to transfer data, and return false if it reports an error.
    bool ATADriver::wait_until_ready()
    {
        uint8_t status;
        do
        {
            status = IO::in_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_STATUS);
            if (status & ATA_STATUS_ERROR) { return false; }
        }
        while ((status & ATA_STATUS_BUSY) || !(status & ATA_STATUS_DATA_REQUEST));
        return true;
    }

    void ATADriver::select_sector(uint32_t lba)
    {
        // The top four bits of the address go in the drive register, along with the LBA mode bit.
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_SECTOR_COUNT, 1);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_LOW, lba & 0xFF);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_MID, (lba >> 8) & 0xFF);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_LBA_HIGH, (lba >> 16) & 0xFF);
    }

    bool ATADriver::read_sector(uint32_t lba, uint8_t* buffer)
    {
        if (!m_isPresent) { return false; }
        m_spinlock.acquire();
        select_sector(lba);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_COMMAND, ATA_COMMAND_READ_SECTORS);
        if (!wait_until_ready())
        {
            m_spinlock.release();
            return false;
        }
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++)
        {
            uint16_t word = IO::in_16(ATA_PRIMARY_IO_BASE + ATA_REGISTER_DATA);
            buffer[i * 2] = word & 0xFF;
            buffer[i * 2 + 1] = word >> 8;
        }
        m_spinlock.release();
        return true;
    }

    bool ATADriver::write_sector(uint32_t lba, const uint8_t* buffer)
    {
        if (!m_isPresent) { return false; }
        m_spinlock.acquire();
        select_sector(lba);
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_COMMAND, ATA_COMMAND_WRITE_SECTORS);
        if (!wait_until_ready())
        {
            m_spinlock.release();
            return false;
        }
        for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++)
        {
            IO::out_16(ATA_PRIMARY_IO_BASE + ATA_REGISTER_DATA, buffer[i * 2] | (buffer[i * 2 + 1] << 8));
        }
        // Flush the drive's write cache, so that the sector is on the disk before we return.
        IO::out_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_COMMAND, ATA_COMMAND_FLUSH_CACHE);
        while (IO::in_8(ATA_PRIMARY_IO_BASE + ATA_REGISTER_STATUS) & ATA_STATUS_BUSY) {}
        m_spinlock.release();
        return true;
    }
}
