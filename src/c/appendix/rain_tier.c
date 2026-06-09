// src/c/appendix/rain_tier.c
#include "rain_tier.h"

#define BAR_COLOR PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)

const int RAIN_TIER_HEIGHT_FIFTHS[RAIN_TIER_COUNT] = { 1, 2, 3, 4, 5 };

// Inclusive upper bounds in wire tenths for tiers 1..4. Tier 5 catches the rest.
static const int RAIN_TIER_MAX_TENTHS[RAIN_TIER_COUNT - 1] = { 1, 5, 20, 100 };

int rain_tier_of_tenths(int tenths) {
    if (tenths <= 0) {
        return 0;
    }
    for (int i = 0; i < RAIN_TIER_COUNT - 1; ++i) {
        if (tenths <= RAIN_TIER_MAX_TENTHS[i]) {
            return i + 1;
        }
    }
    return RAIN_TIER_COUNT;
}

int rain_tier_pixel_height(int tier, int bar_plot_h) {
    if (tier <= 0 || bar_plot_h <= 0) {
        return 0;
    }
    int h = (bar_plot_h * RAIN_TIER_HEIGHT_FIFTHS[tier - 1]) / 5;
    return h > 0 ? h : 1;
}

GColor rain_tier_color(int tier) {
    GColor fill = BAR_COLOR;
#ifdef PBL_COLOR
    switch (tier) {
        case 2: fill = GColorCobaltBlue; break;
        case 3: fill = GColorGreen;      break;
        case 4: fill = GColorOrange;     break;
        case 5: fill = GColorRed;        break;
        default: break;  // tier 1 or 0 -> BAR_COLOR
    }
#endif
    return fill;
}

void rain_bars_draw(GContext *ctx, GRect plot_rect,
                    const uint8_t *tenths, int num_entries) {
    if (num_entries < 2 || !tenths) {
        return;
    }

    const int16_t bar_plot_h = plot_rect.size.h;
    const int16_t bar_plot_bottom = plot_rect.origin.y + bar_plot_h;
    const float entry_w = (float) plot_rect.size.w / (num_entries - 1);
    const int bar_w = (entry_w >= 3.0f) ? (int) entry_w - 2 : 1;

    for (int i = 0; i < num_entries; ++i) {
        const int t = tenths[i];
        if (t <= 0) {
            continue;
        }
        const int tier = rain_tier_of_tenths(t);
        int bar_h = (bar_plot_h * RAIN_TIER_HEIGHT_FIFTHS[tier - 1]) / 5;
        if (bar_h <= 0) {
            bar_h = 1;
        }
        const int bar_x = plot_rect.origin.x + (int)(i * entry_w) + 1;
        const int bar_top_y = bar_plot_bottom - bar_h;
        graphics_context_set_fill_color(ctx, rain_tier_color(tier));
        graphics_fill_rect(ctx, GRect(bar_x, bar_top_y, bar_w, bar_h), 0, GCornerNone);
    }
}
