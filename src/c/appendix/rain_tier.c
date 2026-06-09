// src/c/appendix/rain_tier.c
#include "rain_tier.h"

#define BAR_COLOR PBL_IF_COLOR_ELSE(GColorWhite, GColorBlack)

// Inclusive upper bounds in wire tenths for tiers 1..4. Tier 5 catches the rest.
static const int RAIN_TIER_MAX_TENTHS[RAIN_TIER_COUNT - 1] = { 1, 5, 20, 100 };

// Top of each slab as a cumulative percentage of plot_h, indexed by tier
// (0 = axis, RAIN_TIER_TOP_PCT[k] = top of slab k). Tier 1 occupies the
// bottom 14%, tier 2 the next 20%, and tiers 3..5 22% each.
static const int RAIN_TIER_TOP_PCT_ARR[RAIN_TIER_COUNT + 1] = { 0, 14, 34, 56, 78, 100 };

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

static int rain_tier_fill_q8(int tenths, int tier) {
    int low, high;
    switch (tier) {
        case 1: return 256;
        case 2: low = 2;   high = 5;   break;
        case 3: low = 6;   high = 20;  break;
        case 4: low = 21;  high = 100; break;
        case 5: low = 101; high = 255; break;
        default: return 256;
    }
    if (tenths >= high) { return 256; }
    if (tenths <= low)  { return 0; }
    return ((tenths - low) * 256) / (high - low);
}

int rain_tier_pixel_height(int tier, int bar_plot_h) {
    if (tier <= 0 || bar_plot_h <= 0) {
        return 0;
    }
    int h = (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[tier]) / 100;
    return h > 0 ? h : 1;
}

int rain_tier_proportional_height(int tenths, int bar_plot_h) {
    if (tenths <= 0 || bar_plot_h <= 0) {
        return 0;
    }
    const int tier    = rain_tier_of_tenths(tenths);
    const int fill_q8 = rain_tier_fill_q8(tenths, tier);

    // Lower tiers contribute their full segment height; the topmost
    // tier contributes fill_q8/256 of its segment so the top edge moves
    // continuously across the wire-tenths domain — same math as the
    // slab loop in rain_tier_bar_draw_slabs.
    const int below_h          = (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[tier - 1]) / 100;
    const int slab_top_full    = (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[tier])     / 100;
    const int slab_h_full      = slab_top_full - below_h;
    int slab_h_top = (slab_h_full * fill_q8) / 256;
    if (slab_h_top == 0 && fill_q8 > 0) { slab_h_top = 1; }

    const int total = below_h + slab_h_top;
    return total > 0 ? total : 1;
}

GColor rain_tier_color(int tier) {
    GColor fill = BAR_COLOR;
#ifdef PBL_COLOR
    switch (tier) {
        case 1: fill = GColorLightGray;    break;
        case 2: fill = GColorElectricBlue; break;
        case 3: fill = GColorGreen;        break;
        case 4: fill = GColorYellow;       break;
        case 5: fill = GColorSunsetOrange; break;
        default: break;  // tier 0 -> BAR_COLOR
    }
#endif
    return fill;
}

void rain_tier_bar_draw_slabs(GContext *ctx,
                              int bar_x, int bar_w,
                              int bar_plot_bottom, int bar_plot_h,
                              int tenths) {
    if (tenths <= 0 || bar_w <= 0 || bar_plot_h <= 0) {
        return;
    }
    const int tier    = rain_tier_of_tenths(tenths);
    const int fill_q8 = rain_tier_fill_q8(tenths, tier);

    // Lower slabs (k < tier) fill their segment fully. The topmost slab
    // (k == tier) grows upward from its segment bottom by fill_q8/256 so
    // bar height varies continuously across the wire-tenths domain while
    // colours stay discrete per tier.
    for (int k = 1; k <= tier; ++k) {
        const int slab_top_full    = bar_plot_bottom - (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[k])     / 100;
        const int slab_bottom_full = bar_plot_bottom - (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[k - 1]) / 100;
        const int slab_h_full      = slab_bottom_full - slab_top_full;

        int slab_h;
        if (k < tier) {
            slab_h = slab_h_full;
        } else {
            slab_h = (slab_h_full * fill_q8) / 256;
            if (slab_h == 0 && fill_q8 > 0) { slab_h = 1; }
        }
        if (slab_h <= 0) { continue; }

        const int slab_top = slab_bottom_full - slab_h;
        graphics_context_set_fill_color(ctx, rain_tier_color(k));
        graphics_fill_rect(ctx, GRect(bar_x, slab_top, bar_w, slab_h), 0, GCornerNone);
    }
}

void rain_bars_draw(GContext *ctx, GRect plot_rect,
                    const uint8_t *tenths, int num_entries) {
    if (num_entries < 2 || !tenths) {
        return;
    }

    const int16_t bar_plot_h = plot_rect.size.h;
    const int16_t bar_plot_bottom = plot_rect.origin.y + bar_plot_h;
    const float entry_w = (float) plot_rect.size.w / (num_entries - 1);
    // Bar starts 2 px right of the hour-marker column so the tick at
    // entry_x stays clear, and is 3 px thinner than the slot so the next
    // slot's tick also stays clear. Falls back to 1 px when the slot
    // is too narrow.
    const int bar_w = (entry_w >= 4.0f) ? (int) entry_w - 3 : 1;

    for (int i = 0; i < num_entries; ++i) {
        const int t = tenths[i];
        if (t <= 0) {
            continue;
        }
        const int bar_x = plot_rect.origin.x + (int)(i * entry_w) + 2;
        rain_tier_bar_draw_slabs(ctx, bar_x, bar_w, bar_plot_bottom, bar_plot_h, t);
    }
}
