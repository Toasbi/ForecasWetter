// src/c/appendix/axis.h
#pragma once

#include <pebble.h>

// Draw num_intervals+1 ticks evenly spaced across [x_start, x_end].
// Tick i lands at: x_start + i * (x_end - x_start) / num_intervals.
// Every big_every-th tick (counting from i=0) is big_len pixels long;
// others are small_len. Ticks anchor at y_anchor and extend downward
// when extend_downward is true, upward otherwise.
//
// Stroke width is 1. Pass the desired tick colour as `color`.
void axis_draw_tick_row(GContext *ctx,
                        int16_t x_start, int16_t x_end,
                        int16_t y_anchor,
                        int num_intervals, int big_every,
                        int16_t big_len, int16_t small_len,
                        GColor color, bool extend_downward);
