#include "print.h"
#include "memory/address.h"
#include "cpu/io/io.h"
#include "boot/multibootManager.h"
#include "drivers/graphics/font8x8.h"

namespace
{
    constexpr uint64_t VGA_TEXT_BUFFER = 0xB8000;
    constexpr uint64_t VGA_WIDTH = 80;
    constexpr uint64_t VGA_HEIGHT = 25;
    // The colour attribute is light grey on black.
    constexpr uint16_t VGA_COLOUR = 0x07 << 8;

    uint64_t cursorX = 0;
    uint64_t cursorY = 0;
    uint16_t textBuffer[VGA_WIDTH * VGA_HEIGHT];
    uint32_t* framebuffer = nullptr;
    uint32_t framebufferPitch = 0;
    uint32_t framebufferHeight = 0;

    uint16_t* get_buffer()
    {
        if (framebuffer) { return textBuffer; }
        // The VGA buffer is accessed through the physical memory map, as the lower half is not always mapped.
        return static_cast<uint16_t*>(Memory::VirtualAddress(Memory::PhysicalAddress(VGA_TEXT_BUFFER)).get());
    }

    void draw_cell(uint64_t x, uint64_t y, bool cursor = false)
    {
        if (!framebuffer) { return; }
        uint8_t character = textBuffer[y * VGA_WIDTH + x];
        if (character >= 128) { character = '?'; }
        for (uint64_t row = 0; row < 16; row++)
        {
            uint8_t pixels = CONSOLE_FONT[character][row / 2];
            for (uint64_t column = 0; column < 8; column++)
            {
                bool foreground = pixels & (1 << column);
                if (cursor && row >= 14) { foreground = !foreground; }
                framebuffer[(y * 16 + row) * framebufferPitch + x * 8 + column] = foreground ? 0xAAAAAA : 0;
            }
        }
    }

    void scroll()
    {
        auto buffer = get_buffer();
        for (uint64_t i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++)
            buffer[i] = buffer[i + VGA_WIDTH];
        for (uint64_t i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++)
            buffer[i] = VGA_COLOUR | ' ';
        cursorY = VGA_HEIGHT - 1;
        if (framebuffer)
            for (uint64_t y = 0; y < VGA_HEIGHT; y++)
                for (uint64_t x = 0; x < VGA_WIDTH; x++)
                    draw_cell(x, y);
    }

    void update_cursor()
    {
        if (framebuffer)
        {
            draw_cell(cursorX, cursorY, true);
            return;
        }
        // The VGA controller takes the cursor position as a character offset into the text buffer.
        uint16_t position = cursorY * VGA_WIDTH + cursorX;
        IO::out_8(0x3D4, 0x0F);
        IO::out_8(0x3D5, position & 0xFF);
        IO::out_8(0x3D4, 0x0E);
        IO::out_8(0x3D5, (position >> 8) & 0xFF);
    }

    void putc(char c)
    {
        draw_cell(cursorX, cursorY);
        if (c == '\n')
        {
            cursorX = 0;
            cursorY++;
        }
        else if (c == '\b')
        {
            if (!cursorX) { return; }
            cursorX--;
            get_buffer()[cursorY * VGA_WIDTH + cursorX] = VGA_COLOUR | ' ';
            draw_cell(cursorX, cursorY);
        }
        else
        {
            get_buffer()[cursorY * VGA_WIDTH + cursorX] = VGA_COLOUR | static_cast<uint8_t>(c);
            draw_cell(cursorX, cursorY);
            cursorX++;
        }

        if (cursorX == VGA_WIDTH)
        {
            cursorX = 0;
            cursorY++;
        }
        if (cursorY == VGA_HEIGHT) { scroll(); }
    }
}

void initialise_console()
{
    auto it = Multiboot::MultibootManager::instance().first(MULTIBOOT_TAG_TYPE_FRAMEBUFFER);
    if (it.is_end() || it->size < sizeof(multiboot_tag_framebuffer)) { return; }
    auto tag = reinterpret_cast<multiboot_tag_framebuffer*>(&*it);
    if (tag->framebufferType != 1 || tag->bpp != 32 || tag->width < 640 || tag->height < 400
        || tag->pitch < tag->width * 4 || tag->pitch % 4) { return; }
    framebuffer = static_cast<uint32_t*>(Memory::VirtualAddress(Memory::PhysicalAddress(tag->address)).get());
    framebufferPitch = tag->pitch / 4;
    framebufferHeight = tag->height;
}

bool has_framebuffer_console() { return framebuffer; }

void clear_screen()
{
    if (framebuffer)
        for (uint64_t i = 0; i < static_cast<uint64_t>(framebufferPitch) * framebufferHeight; i++)
            framebuffer[i] = 0;
    auto buffer = get_buffer();
    for (uint64_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        buffer[i] = VGA_COLOUR | ' ';
    cursorX = 0;
    cursorY = 0;
    update_cursor();
}

void print_bytes(const char* buffer, uint64_t count)
{
    for (uint64_t i = 0; i < count; i++) { putc(buffer[i]); }
    update_cursor();
}

void printf(const char* str)
{
    while (*str)
        putc(*str++);
    update_cursor();
}

void printf(uint64_t num)
{
    // The largest 64-bit integer has 20 decimal digits.
    char digits[21];
    uint64_t i = 20;
    digits[i] = '\0';
    do
    {
        digits[--i] = '0' + num % 10;
        num /= 10;
    }
    while (num);
    printf(&digits[i]);
}
