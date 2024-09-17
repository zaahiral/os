#include "print.h"
#include "memory/address.h"

namespace
{
    constexpr uint64_t VGA_TEXT_BUFFER = 0xB8000;
    constexpr uint64_t VGA_WIDTH = 80;
    constexpr uint64_t VGA_HEIGHT = 25;
    // The colour attribute is light grey on black.
    constexpr uint16_t VGA_COLOUR = 0x07 << 8;

    uint64_t cursorX = 0;
    uint64_t cursorY = 0;

    uint16_t* get_buffer()
    {
        // The VGA buffer is accessed through the physical memory map, as the lower half is not always mapped.
        return static_cast<uint16_t*>(Memory::VirtualAddress(Memory::PhysicalAddress(VGA_TEXT_BUFFER)).get());
    }

    void scroll()
    {
        auto buffer = get_buffer();
        for (uint64_t i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++)
            buffer[i] = buffer[i + VGA_WIDTH];
        for (uint64_t i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++)
            buffer[i] = VGA_COLOUR | ' ';
        cursorY = VGA_HEIGHT - 1;
    }

    void putc(char c)
    {
        if (c == '\n')
        {
            cursorX = 0;
            cursorY++;
        }
        else
        {
            get_buffer()[cursorY * VGA_WIDTH + cursorX] = VGA_COLOUR | static_cast<uint8_t>(c);
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

void clear_screen()
{
    auto buffer = get_buffer();
    for (uint64_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        buffer[i] = VGA_COLOUR | ' ';
    cursorX = 0;
    cursorY = 0;
}

void printf(const char* str)
{
    while (*str)
        putc(*str++);
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
