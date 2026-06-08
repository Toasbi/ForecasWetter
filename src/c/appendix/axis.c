// src/c/appendix/axis.c
#include "axis.h"

void axis_draw_tick_row(GContext *ctx,
                        int16_t x_start, int16_t x_end,
                        int16_t y_anchor,
                        int num_intervals, int big_every,
                        int16_t big_len, int16_t small_len,
                        GColor color, bool extend_downward) {
    if (num_intervals < 1 || big_every < 1 || x_end <= x_start) {
        return;
    }

    graphics_context_set_stroke_color(ctx, color);
    graphics_context_set_stroke_width(ctx, 1);

    const int32_t span = (int32_t) x_end - x_start;
    const int16_t sign = extend_downward ? 1 : -1;

    for (int i = 0; i <= num_intervals; ++i) {
        const int16_t x = x_start + (int16_t)((span * i) / num_intervals);
        const int16_t len = (i % big_every == 0) ? big_len : small_len;
        graphics_draw_line(ctx, GPoint(x, y_anchor), GPoint(x, y_anchor + sign * len));
    }
}
