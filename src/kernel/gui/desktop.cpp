#include "gui/desktop.h"
#include "drivers/graphics/bochsvbe.h"
#include "cpu/io/pci.h"
#include "cpu/io/pit.h"
#include "event/dispatcher.h"
#include "process/taskManager.h"

namespace GUI
{
    namespace
    {
        constexpr uint16_t SCREEN_WIDTH = 1024;
        constexpr uint16_t SCREEN_HEIGHT = 768;
        constexpr uint64_t ESCAPE_SCAN_CODE = 0x01;

        constexpr uint32_t BACKGROUND_COLOUR = 0x2E3236;
        constexpr uint32_t SHADOW_COLOUR = 0x1E2124;
        constexpr uint32_t BORDER_COLOUR = 0x5A5A5A;
        constexpr uint32_t TITLE_BAR_COLOUR = 0xE9E9E9;
        constexpr uint32_t WINDOW_COLOUR = 0xFFFFFF;
        constexpr uint32_t TEXT_COLOUR = 0x000000;
        constexpr uint32_t TOP_BAR_COLOUR = 0x111111;
        constexpr uint32_t TOP_BAR_TEXT_COLOUR = 0xF0F0F0;
        constexpr uint32_t TOP_BAR_HINT_COLOUR = 0xAAAAAA;
        constexpr int64_t TITLE_BAR_HEIGHT = 24;

        // The window that holds the spinning cube.
        constexpr int64_t CUBE_WINDOW_X = 520;
        constexpr int64_t CUBE_WINDOW_Y = 70;
        constexpr int64_t CUBE_WINDOW_WIDTH = 440;
        constexpr int64_t CUBE_WINDOW_HEIGHT = 420;

        // The kernel is compiled without SSE, so the cube uses fixed point maths with 14 fractional bits.
        constexpr int64_t FIXED_ONE = 16384;
        constexpr int16_t SINE_TABLE[256] = {
        0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
        6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
        11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
        15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379,
        16384, 16379, 16364, 16340, 16305, 16261, 16207, 16143, 16069, 15986, 15893, 15791, 15679, 15557, 15426, 15286,
        15137, 14978, 14811, 14635, 14449, 14256, 14053, 13842, 13623, 13395, 13160, 12916, 12665, 12406, 12140, 11866,
        11585, 11297, 11003, 10702, 10394, 10080, 9760, 9434, 9102, 8765, 8423, 8076, 7723, 7366, 7005, 6639,
        6270, 5897, 5520, 5139, 4756, 4370, 3981, 3590, 3196, 2801, 2404, 2006, 1606, 1205, 804, 402,
        0, -402, -804, -1205, -1606, -2006, -2404, -2801, -3196, -3590, -3981, -4370, -4756, -5139, -5520, -5897,
        -6270, -6639, -7005, -7366, -7723, -8076, -8423, -8765, -9102, -9434, -9760, -10080, -10394, -10702, -11003, -11297,
        -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395, -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
        -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986, -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
        -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143, -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
        -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842, -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
        -11585, -11297, -11003, -10702, -10394, -10080, -9760, -9434, -9102, -8765, -8423, -8076, -7723, -7366, -7005, -6639,
        -6270, -5897, -5520, -5139, -4756, -4370, -3981, -3590, -3196, -2801, -2404, -2006, -1606, -1205, -804, -402,
        };

        int64_t sine(uint8_t angle) { return SINE_TABLE[angle]; }
        int64_t cosine(uint8_t angle) { return SINE_TABLE[(uint8_t)(angle + 64)]; }

        // Writes a number into a buffer in the given base, and returns the buffer.
        char* format_number(uint64_t number, uint64_t base, char* buffer, uint64_t minimumDigits = 1)
        {
            char digits[21];
            uint64_t count = 0;
            do
            {
                digits[count++] = "0123456789ABCDEF"[number % base];
                number /= base;
            }
            while (number || count < minimumDigits);
            for (uint64_t i = 0; i < count; i++)
                buffer[i] = digits[count - 1 - i];
            buffer[count] = '\0';
            return buffer;
        }
    }

    void Desktop::fill_rect(int64_t x, int64_t y, int64_t width, int64_t height, uint32_t colour)
    {
        auto framebuffer = Drivers::BochsVBEDriver::instance().get_framebuffer();
        for (int64_t row = y; row < y + height; row++)
        {
            if (row < 0 || row >= SCREEN_HEIGHT) { continue; }
            for (int64_t column = x; column < x + width; column++)
            {
                if (column < 0 || column >= SCREEN_WIDTH) { continue; }
                framebuffer[row * SCREEN_WIDTH + column] = colour;
            }
        }
    }

    // Bresenham's line algorithm, which only uses integer arithmetic.
    void Desktop::draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, uint32_t colour)
    {
        auto framebuffer = Drivers::BochsVBEDriver::instance().get_framebuffer();
        int64_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
        int64_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
        int64_t stepX = x0 < x1 ? 1 : -1;
        int64_t stepY = y0 < y1 ? 1 : -1;
        int64_t error = dx + dy;
        while (1)
        {
            if (x0 >= 0 && x0 < SCREEN_WIDTH && y0 >= 0 && y0 < SCREEN_HEIGHT) { framebuffer[y0 * SCREEN_WIDTH + x0] = colour; }
            if (x0 == x1 && y0 == y1) { break; }
            int64_t doubleError = 2 * error;
            if (doubleError >= dy) { error += dy; x0 += stepX; }
            if (doubleError <= dx) { error += dx; y0 += stepY; }
        }
    }

    // Text is drawn with the VGA font, which the graphics driver saved before leaving text mode.
    void Desktop::draw_text(int64_t x, int64_t y, const char* text, uint32_t colour)
    {
        for (; *text; text++, x += 8)
        {
            auto glyph = Drivers::BochsVBEDriver::instance().get_glyph(*text);
            for (int64_t row = 0; row < VGA_FONT_HEIGHT; row++)
            {
                for (int64_t column = 0; column < 8; column++)
                {
                    if (glyph[row] & (0x80 >> column)) { fill_rect(x + column, y + row, 1, 1, colour); }
                }
            }
        }
    }

    void Desktop::draw_window(int64_t x, int64_t y, int64_t width, int64_t height, const char* title)
    {
        fill_rect(x + 5, y + 5, width, height, SHADOW_COLOUR);
        fill_rect(x, y, width, height, BORDER_COLOUR);
        fill_rect(x + 1, y + 1, width - 2, TITLE_BAR_HEIGHT - 1, TITLE_BAR_COLOUR);
        fill_rect(x + 1, y + TITLE_BAR_HEIGHT, width - 2, height - TITLE_BAR_HEIGHT - 1, WINDOW_COLOUR);
        draw_text(x + 10, y + 4, title, TEXT_COLOUR);
    }

    void Desktop::draw_top_bar()
    {
        char number[21];
        fill_rect(0, 0, SCREEN_WIDTH, 28, TOP_BAR_COLOUR);
        draw_text(14, 6, "cOS", TOP_BAR_TEXT_COLOUR);
        draw_text(64, 6, "Uptime:", TOP_BAR_HINT_COLOUR);
        draw_text(128, 6, format_number(CPU::PIT::time_since_boot() / 1000, 10, number), TOP_BAR_TEXT_COLOUR);
        draw_text(SCREEN_WIDTH - 8 * 33, 6, "Press Esc to return to the shell", TOP_BAR_HINT_COLOUR);
    }

    void Desktop::draw_cube(int64_t x, int64_t y, int64_t width, int64_t height, uint8_t angle)
    {
        constexpr int64_t VERTICES[8][3] = {
            {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
            {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
        };
        constexpr uint8_t EDGES[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0},
            {4, 5}, {5, 6}, {6, 7}, {7, 4},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        int64_t projected[8][2];
        int64_t centreX = x + width / 2;
        int64_t centreY = y + height / 2;
        // The cube turns about the x axis at half the speed it turns about the y axis.
        uint8_t tilt = angle / 2;
        for (int vertex = 0; vertex < 8; vertex++)
        {
            int64_t vx = VERTICES[vertex][0] * FIXED_ONE;
            int64_t vy = VERTICES[vertex][1] * FIXED_ONE;
            int64_t vz = VERTICES[vertex][2] * FIXED_ONE;
            int64_t rotatedX = (vx * cosine(angle) - vz * sine(angle)) / FIXED_ONE;
            int64_t rotatedZ = (vx * sine(angle) + vz * cosine(angle)) / FIXED_ONE;
            int64_t rotatedY = (vy * cosine(tilt) - rotatedZ * sine(tilt)) / FIXED_ONE;
            rotatedZ = (vy * sine(tilt) + rotatedZ * cosine(tilt)) / FIXED_ONE;
            // Perspective projection, so that further vertices are drawn closer to the centre.
            int64_t depth = rotatedZ + 4 * FIXED_ONE;
            projected[vertex][0] = centreX + rotatedX * 220 / depth;
            projected[vertex][1] = centreY + rotatedY * 220 / depth;
        }
        fill_rect(x, y, width, height, WINDOW_COLOUR);
        for (int edge = 0; edge < 12; edge++)
        {
            auto from = projected[EDGES[edge][0]];
            auto to = projected[EDGES[edge][1]];
            draw_line(from[0], from[1], to[0], to[1], TEXT_COLOUR);
        }
    }

    void Desktop::run(uint64_t tid, uint64_t keyboardEventDescriptor)
    {
        Drivers::BochsVBEDriver::instance().enter_graphics_mode(SCREEN_WIDTH, SCREEN_HEIGHT);
        fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, BACKGROUND_COLOUR);
        draw_top_bar();

        draw_window(60, 70, 400, 200, "About cOS");
        draw_text(80, 110, "cOS v1.0.0b", TEXT_COLOUR);
        draw_text(80, 134, "An x86-64 operating system in C++.", TEXT_COLOUR);
        draw_text(80, 158, "This desktop is drawn by the kernel", TEXT_COLOUR);
        draw_text(80, 182, "into a 1024x768 framebuffer.", TEXT_COLOUR);
        draw_text(80, 222, "Press Esc to return to the shell.", TEXT_COLOUR);

        draw_window(60, 300, 400, 260, "PCI devices");
        char number[21];
        int64_t row = 340;
        for (auto it = PeripheralComponentInterconnect::PCIManager::instance()->m_devices.first(); !it.is_end(); ++it, row += 24)
        {
            draw_text(80, row, format_number(it->vendorId, 16, number, 4), TEXT_COLOUR);
            draw_text(112, row, ":", TEXT_COLOUR);
            draw_text(120, row, format_number(it->deviceId, 16, number, 4), TEXT_COLOUR);
        }

        draw_window(CUBE_WINDOW_X, CUBE_WINDOW_Y, CUBE_WINDOW_WIDTH, CUBE_WINDOW_HEIGHT, "Cube");
        uint8_t angle = 0;
        while (1)
        {
            draw_cube(CUBE_WINDOW_X + 1, CUBE_WINDOW_Y + TITLE_BAR_HEIGHT, CUBE_WINDOW_WIDTH - 2, CUBE_WINDOW_HEIGHT - TITLE_BAR_HEIGHT - 1, angle);
            draw_top_bar();
            angle += 2;
            Task::TaskManager::instance().sleep_for(30);

            // Read every key pressed since the last frame, and return to the shell if one of them was Escape.
            uint64_t scanCode;
            while ((scanCode = Events::EventDispatcher::instance().read_from_event_queue(tid, keyboardEventDescriptor)))
            {
                if (scanCode == ESCAPE_SCAN_CODE)
                {
                    Drivers::BochsVBEDriver::instance().leave_graphics_mode();
                    return;
                }
            }
        }
    }
}
