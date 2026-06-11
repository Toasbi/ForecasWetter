// src/c/appendix/snooze.c
#include "snooze.h"

// Areas shorter than this get the compact 2-glyph variant. The status row is
// ~20px tall (a bit more on emery); the radar slot is well above this.
#define SNOOZE_COMPACT_AREA_H 26

// Minimum glyph side so the diagonal stays readable at status-row scale.
#define SNOOZE_MIN_SIDE 4

// One Z = three strokes: top bar, diagonal, bottom bar.
static void draw_z(GContext *ctx, GRect box) {
    const int16_t x0 = box.origin.x;
    const int16_t x1 = box.origin.x + box.size.w - 1;
    const int16_t y0 = box.origin.y;
    const int16_t y1 = box.origin.y + box.size.h - 1;
    graphics_draw_line(ctx, GPoint(x0, y0), GPoint(x1, y0));
    graphics_draw_line(ctx, GPoint(x1, y0), GPoint(x0, y1));
    graphics_draw_line(ctx, GPoint(x0, y1), GPoint(x1, y1));
}

void snooze_draw(GContext *ctx, GRect area, GColor color) {
    // Glyph side lengths in twelfths of the sizing basis, smallest first.
    static const int16_t COMPACT_TWELFTHS[] = {6, 9};
    static const int16_t FULL_TWELFTHS[] = {4, 6, 9};

    if (area.size.w <= 0 || area.size.h < SNOOZE_MIN_SIDE) {
        return;
    }

    const bool compact = area.size.h < SNOOZE_COMPACT_AREA_H;
    const int n = compact ? 2 : 3;
    const int16_t *twelfths = compact ? COMPACT_TWELFTHS : FULL_TWELFTHS;
    const int16_t h = area.size.h;

    // Sizing basis: the area height, capped so the glyph group (sides plus
    // gaps) always fits the area width — the status-row box is narrower
    // than it is implied by its height on taller rows.
    int16_t sum_twelfths = 0;
    for (int i = 0; i < n; ++i) {
        sum_twelfths += twelfths[i];
    }
    int16_t basis = (int16_t)(((area.size.w - (n - 1)) * 12)
                              / (sum_twelfths + (n - 1)));
    if (h < basis) {
        basis = h;
    }
    const int16_t gap = basis / 12 + 1;

    int16_t sides[3];
    int16_t total_w = (int16_t)((n - 1) * gap);
    for (int i = 0; i < n; ++i) {
        sides[i] = (int16_t)(basis * twelfths[i] / 12);
        if (sides[i] < SNOOZE_MIN_SIDE) {
            sides[i] = SNOOZE_MIN_SIDE;
        }
        total_w += sides[i];
    }

    graphics_context_set_stroke_color(ctx, color);
    // The SDK supports odd stroke widths only (even values round down).
    graphics_context_set_stroke_width(ctx, compact ? 1 : 3);

    // The smallest glyph's bottom touches the area bottom, the largest
    // glyph's top touches the area top; tops in between interpolate
    // linearly. The group is centred horizontally.
    int16_t x = area.origin.x + (area.size.w - total_w) / 2;
    for (int i = 0; i < n; ++i) {
        const int16_t top = area.origin.y
            + (int16_t)((n - 1 - i) * (h - sides[i]) / (n - 1));
        draw_z(ctx, GRect(x, top, sides[i], sides[i]));
        x += sides[i] + gap;
    }
}
