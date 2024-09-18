#ifndef DESKTOP_H
#define DESKTOP_H

#include "types.h"

namespace GUI
{
    // A graphical demo of cOS, drawn straight into the framebuffer.
    class Desktop
    {
    public:
        // Draws the desktop and animates it until Escape is pressed. Keys are read from the given event descriptor.
        static void run(uint64_t tid, uint64_t keyboardEventDescriptor);
    private:
        static void fill_rect(int64_t x, int64_t y, int64_t width, int64_t height, uint32_t colour);
        static void draw_line(int64_t x0, int64_t y0, int64_t x1, int64_t y1, uint32_t colour);
        static void draw_text(int64_t x, int64_t y, const char* text, uint32_t colour);
        static void draw_window(int64_t x, int64_t y, int64_t width, int64_t height, const char* title);
        static void draw_top_bar();
        static void draw_cube(int64_t x, int64_t y, int64_t width, int64_t height, uint8_t angle);
    };
}

#endif
