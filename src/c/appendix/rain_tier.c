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
    // continuously across the wire-tenths domain.
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

int16_t rain_tier_permille(int tenths) {
    return (int16_t)rain_tier_proportional_height(tenths, 1000);
}

void rain_tier_fill_permille(const uint8_t *tenths, int16_t *out, int count) {
    for (int i = 0; i < count; ++i) {
        out[i] = rain_tier_permille(tenths[i]);
    }
}

#ifdef PBL_COLOR
const ChartColorStop RAIN_TIER_STOPS[] = {
    { 0,   GColorLightGray    },   // tier 1
    { 140, GColorElectricBlue },   // tier 2 — RAIN_TIER_TOP_PCT_ARR * 10
    { 340, GColorGreen        },   // tier 3
    { 560, GColorYellow       },   // tier 4
    { 780, GColorSunsetOrange },   // tier 5
};
#else
const ChartColorStop RAIN_TIER_STOPS[] = {
    { 0, GColorBlack },            // single stop; outline carries the shape
};
#endif
const int RAIN_TIER_NUM_STOPS =
    (int)(sizeof(RAIN_TIER_STOPS) / sizeof(RAIN_TIER_STOPS[0]));
