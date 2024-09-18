#ifndef BOCHSVBE_H
#define BOCHSVBE_H

#include "types.h"

namespace Drivers
{
    #define VBE_DISPI_IOPORT_INDEX 0x01CE
    #define VBE_DISPI_IOPORT_DATA 0x01CF

    #define VBE_DISPI_INDEX_ID 0
    #define VBE_DISPI_INDEX_XRES 1
    #define VBE_DISPI_INDEX_YRES 2
    #define VBE_DISPI_INDEX_BPP 3
    #define VBE_DISPI_INDEX_ENABLE 4

    #define VBE_DISPI_ID_MIN 0xB0C0
    #define VBE_DISPI_ENABLED 0x01
    #define VBE_DISPI_LFB_ENABLED 0x40

    #define BOCHS_VGA_VENDOR_ID 0x1234
    #define BOCHS_VGA_DEVICE_ID 0x1111

    #define VGA_FONT_GLYPHS 256
    #define VGA_FONT_HEIGHT 16

    #define VGA_SEQUENCER_REGISTERS 5
    #define VGA_CRTC_REGISTERS 25
    #define VGA_GRAPHICS_REGISTERS 9
    #define VGA_ATTRIBUTE_REGISTERS 21

    // The VGA registers that describe the current text mode.
    struct VGARegisters
    {
        uint8_t miscellaneous;
        uint8_t sequencer[VGA_SEQUENCER_REGISTERS];
        uint8_t crtc[VGA_CRTC_REGISTERS];
        uint8_t graphics[VGA_GRAPHICS_REGISTERS];
        uint8_t attribute[VGA_ATTRIBUTE_REGISTERS];
    };

    // Drives the Bochs VBE extensions, which QEMU's standard VGA adapter provides.
    // These let us switch to a high resolution mode with a linear framebuffer without calling the BIOS.
    class BochsVBEDriver
    {
    public:
        static BochsVBEDriver& instance()
        {
            static BochsVBEDriver instance;
            return instance;
        }
        BochsVBEDriver(BochsVBEDriver const&) = delete;
        void operator=(BochsVBEDriver const&) = delete;

        bool is_present() { return m_framebuffer; }
        void enter_graphics_mode(uint16_t width, uint16_t height);
        void leave_graphics_mode();
        uint32_t* get_framebuffer() { return m_framebuffer; }
        uint16_t get_width() { return m_width; }
        uint16_t get_height() { return m_height; }
        // Each glyph is 16 rows of 8 pixels, one byte per row.
        uint8_t* get_glyph(uint8_t c) { return &m_font[c * VGA_FONT_HEIGHT]; }
    private:
        BochsVBEDriver();
        uint32_t* m_framebuffer = nullptr;
        uint16_t m_width = 0;
        uint16_t m_height = 0;
        uint8_t m_font[VGA_FONT_GLYPHS * VGA_FONT_HEIGHT];
        uint16_t m_textBuffer[80 * 25];
        VGARegisters m_textModeRegisters;
        void save_vga_registers(VGARegisters& registers);
        void restore_vga_registers(VGARegisters& registers);
        void write_register(uint16_t index, uint16_t value);
        uint16_t read_register(uint16_t index);
        void select_font_plane();
        void deselect_font_plane();
    };
}

#endif
