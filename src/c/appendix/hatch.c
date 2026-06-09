// src/c/appendix/hatch.c
#include "hatch.h"

// Returns the first y >= y_start in column x where (x + y) % spacing == 0.
// Handles negative modulo so the pattern is correct for negative coordinates.
static int16_t aligned_hatch_start_y(int16_t x, int16_t y_start, int16_t spacing)
{
    int16_t modulo = (x + y_start) % spacing;
    if (modulo < 0)
    {
        modulo += spacing;
    }

    if (modulo == 0)
    {
        return y_start;
    }

    return y_start + (spacing - modulo);
}

void hatch_fill_rect(GContext *ctx, GRect rect, GColor color, int stride)
{
    if (stride <= 0 || rect.size.w <= 0 || rect.size.h <= 0)
    {
        return;
    }

    graphics_context_set_stroke_color(ctx, color);

    const int16_t x_end = rect.origin.x + rect.size.w;
    const int16_t y_end = rect.origin.y + rect.size.h;
    for (int16_t x = rect.origin.x; x < x_end; ++x)
    {
        int16_t hatch_y = aligned_hatch_start_y(x, rect.origin.y, (int16_t)stride);
        for (int16_t y = hatch_y; y < y_end; y += stride)
        {
            graphics_draw_pixel(ctx, GPoint(x, y));
        }
    }
}
