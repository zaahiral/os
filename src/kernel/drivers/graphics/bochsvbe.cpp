#include "drivers/graphics/bochsvbe.h"
#include "cpu/io/io.h"
#include "cpu/io/pci.h"
#include "memory/address.h"
#include "memory/memory.h"

namespace Drivers
{
    BochsVBEDriver::BochsVBEDriver()
    {
        if (read_register(VBE_DISPI_INDEX_ID) < VBE_DISPI_ID_MIN) { return; }
        auto device = PeripheralComponentInterconnect::PCIManager::instance()->get_device(BOCHS_VGA_VENDOR_ID, BOCHS_VGA_DEVICE_ID);
        if (!device) { return; }

        // The framebuffer is the memory region in BAR0. All of physical memory is mapped, so we can use the physical memory map.
        uint64_t framebufferBase = PeripheralComponentInterconnect::PCIManager::instance()->read_dword(device->bus, device->device, device->function, 0x10) & ~0xF;
        m_framebuffer = static_cast<uint32_t*>(Memory::VirtualAddress(Memory::PhysicalAddress(framebufferBase)).get());
    }

    void BochsVBEDriver::write_register(uint16_t index, uint16_t value)
    {
        IO::out_16(VBE_DISPI_IOPORT_INDEX, index);
        IO::out_16(VBE_DISPI_IOPORT_DATA, value);
    }

    uint16_t BochsVBEDriver::read_register(uint16_t index)
    {
        IO::out_16(VBE_DISPI_IOPORT_INDEX, index);
        return IO::in_16(VBE_DISPI_IOPORT_DATA);
    }

    // In text mode, the font is stored in plane 2 of video memory. These registers let us access plane 2 at 0xA0000.
    void BochsVBEDriver::select_font_plane()
    {
        IO::out_16(0x3C4, 0x0402);
        IO::out_16(0x3C4, 0x0704);
        IO::out_16(0x3CE, 0x0204);
        IO::out_16(0x3CE, 0x0005);
        IO::out_16(0x3CE, 0x0406);
    }

    // Restore the registers that text mode uses.
    void BochsVBEDriver::deselect_font_plane()
    {
        IO::out_16(0x3C4, 0x0302);
        IO::out_16(0x3C4, 0x0304);
        IO::out_16(0x3CE, 0x0004);
        IO::out_16(0x3CE, 0x1005);
        IO::out_16(0x3CE, 0x0E06);
    }

    void BochsVBEDriver::save_vga_registers(VGARegisters& registers)
    {
        registers.miscellaneous = IO::in_8(0x3CC);
        for (uint8_t i = 0; i < VGA_SEQUENCER_REGISTERS; i++) { IO::out_8(0x3C4, i); registers.sequencer[i] = IO::in_8(0x3C5); }
        for (uint8_t i = 0; i < VGA_CRTC_REGISTERS; i++) { IO::out_8(0x3D4, i); registers.crtc[i] = IO::in_8(0x3D5); }
        for (uint8_t i = 0; i < VGA_GRAPHICS_REGISTERS; i++) { IO::out_8(0x3CE, i); registers.graphics[i] = IO::in_8(0x3CF); }
        // Reading the input status register resets the attribute controller to expect an index.
        for (uint8_t i = 0; i < VGA_ATTRIBUTE_REGISTERS; i++) { IO::in_8(0x3DA); IO::out_8(0x3C0, i); registers.attribute[i] = IO::in_8(0x3C1); }
        IO::in_8(0x3DA);
        IO::out_8(0x3C0, 0x20);
    }

    void BochsVBEDriver::restore_vga_registers(VGARegisters& registers)
    {
        IO::out_8(0x3C2, registers.miscellaneous);
        for (uint8_t i = 0; i < VGA_SEQUENCER_REGISTERS; i++) { IO::out_8(0x3C4, i); IO::out_8(0x3C5, registers.sequencer[i]); }
        // The first CRTC registers are write protected until bit 7 of register 0x11 is cleared.
        IO::out_8(0x3D4, 0x11);
        IO::out_8(0x3D5, registers.crtc[0x11] & 0x7F);
        for (uint8_t i = 0; i < VGA_CRTC_REGISTERS; i++) { IO::out_8(0x3D4, i); IO::out_8(0x3D5, registers.crtc[i]); }
        for (uint8_t i = 0; i < VGA_GRAPHICS_REGISTERS; i++) { IO::out_8(0x3CE, i); IO::out_8(0x3CF, registers.graphics[i]); }
        for (uint8_t i = 0; i < VGA_ATTRIBUTE_REGISTERS; i++) { IO::in_8(0x3DA); IO::out_8(0x3C0, i); IO::out_8(0x3C0, registers.attribute[i]); }
        // Setting bit 5 of the attribute index turns the display back on.
        IO::in_8(0x3DA);
        IO::out_8(0x3C0, 0x20);
    }

    void BochsVBEDriver::enter_graphics_mode(uint16_t width, uint16_t height)
    {
        save_vga_registers(m_textModeRegisters);
        // The framebuffer shares video memory with text mode, so we save the font and the screen before drawing over them.
        auto fontPlane = static_cast<uint8_t*>(Memory::VirtualAddress(Memory::PhysicalAddress(0xA0000)).get());
        select_font_plane();
        for (uint64_t glyph = 0; glyph < VGA_FONT_GLYPHS; glyph++)
        {
            // Each glyph takes 32 bytes in plane 2, but only the first 16 rows are used.
            memcpy(&m_font[glyph * VGA_FONT_HEIGHT], &fontPlane[glyph * 32], VGA_FONT_HEIGHT);
        }
        deselect_font_plane();
        memcpy(m_textBuffer, Memory::VirtualAddress(Memory::PhysicalAddress(0xB8000)).get(), sizeof(m_textBuffer));

        write_register(VBE_DISPI_INDEX_ENABLE, 0);
        write_register(VBE_DISPI_INDEX_XRES, width);
        write_register(VBE_DISPI_INDEX_YRES, height);
        write_register(VBE_DISPI_INDEX_BPP, 32);
        write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
        m_width = width;
        m_height = height;
    }

    void BochsVBEDriver::leave_graphics_mode()
    {
        write_register(VBE_DISPI_INDEX_ENABLE, 0);
        // Enabling the VBE extensions changes the VGA registers, so we put back the ones that text mode used.
        restore_vga_registers(m_textModeRegisters);

        // Put the font and the screen back where text mode expects them.
        auto fontPlane = static_cast<uint8_t*>(Memory::VirtualAddress(Memory::PhysicalAddress(0xA0000)).get());
        select_font_plane();
        for (uint64_t glyph = 0; glyph < VGA_FONT_GLYPHS; glyph++)
        {
            memcpy(&fontPlane[glyph * 32], &m_font[glyph * VGA_FONT_HEIGHT], VGA_FONT_HEIGHT);
        }
        deselect_font_plane();
        memcpy(Memory::VirtualAddress(Memory::PhysicalAddress(0xB8000)).get(), m_textBuffer, sizeof(m_textBuffer));
    }
}
