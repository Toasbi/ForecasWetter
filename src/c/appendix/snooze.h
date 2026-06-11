// src/c/appendix/snooze.h
#pragma once

#include <pebble.h>

// Draw comic-style snooze glyphs: Z's ascending diagonally, the smallest at
// the lower-left and the largest at the upper-right, scaled to fit `area`.
// Short areas (status row) get a compact 2-glyph variant; taller areas
// (radar slot) get 3 glyphs. Integer math only.
void snooze_draw(GContext *ctx, GRect area, GColor color);
