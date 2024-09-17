#include "cpu/io/pci.h"
#include "print.h"

namespace PeripheralComponentInterconnect {
    PCIManager* PCIManager::m_self;
    PCIManager* PCIManager::instance() { return m_self; }

    PCIManager::PCIManager()
    {
        m_self = this;
        find_devices();
    }

    uint32_t PCIManager::read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
    {
        uint32_t address;
        address = (bus&0xFF)<<16 | (device&0x1F)<<11 | (function&0x7)<<8 | offset&0xFC | 1<<31;
        IO::out_32(CONFIG_ADDRESS_PORT, address);
        return IO::in_32(CONFIG_DATA_PORT) >> (8*(offset&3));
    }

    void PCIManager::write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value)
    {
        uint32_t address;
        address = (bus&0xFF)<<16 | (device&0x1F)<<11 | (function&0x7)<<8 | offset&0xFC | 1<<31;
        IO::out_32(CONFIG_ADDRESS_PORT, address);
        IO::out_32(CONFIG_DATA_PORT, value);
    }

    uint8_t PCIManager::device_functions_count(uint8_t bus, uint8_t device)
    {
        return (read_dword(bus, device, 0, 0x0E)&(1<<7)) ? 8 : 1;
    }

    PCIDevice PCIManager::get_pci_device(uint8_t bus, uint8_t device, uint8_t function)
    {
        PCIDevice dev;
        dev.bus = bus;
        dev.device = device;
        dev.function = function;
        dev.vendorId = read_dword(bus, device, function, 0x00);
        dev.deviceId = read_dword(bus, device, function, 0x02);
        dev.classCode = read_dword(bus, device, function, 0x0b);
        dev.subclassCode = read_dword(bus, device, function, 0x0a);
        dev.revision = read_dword(bus, device, function, 0x08);
        dev.interruptLine = read_dword(bus, device, function, 0x3c);
        if (dev.vendorId != 0 && dev.vendorId != 0xFFFF)
        {
            for (int bar = 0; bar < 6; bar++)
            {
                uint32_t baraddress = read_dword(bus, device, function, 0x10 + 4*bar);
                if (baraddress & IO_SPACE_BAR)
                {
                    dev.ioPortBase = baraddress&(~0x3);
                }
            }
        }
        return dev;
    }

    void PCIManager::find_devices()
    {
        for (int bus=0; bus<8; bus++)
        {
            for (int device = 0; device < 32; device++)
            {
                int functionCount = device_functions_count(bus, device);
                for (int func = 0; func < functionCount; func++)
                {
                    auto dev = get_pci_device(bus, device, func);
                    if (dev.vendorId == 0x0000 || dev.vendorId == 0xFFFF) { continue; }
                    m_devices.append(dev);
                }
            }
        }
    }

    PCIDevice* PCIManager::get_device(uint16_t vendorId, uint16_t deviceId)
    {
        for (auto it = m_devices.first(); !it.is_end(); ++it)
        {
            if (it->deviceId == deviceId && it->vendorId == vendorId) { return &*it; }
        }
        return nullptr;
    }
}
