// src/c/appendix/rain_tier.c
#include "rain_tier.h"
#include "c/appendix/slot_geometry.h"

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
                              int tenths, bool invert) {
    if (tenths <= 0 || bar_w <= 0 || bar_plot_h <= 0) {
        return;
    }
    const int tier         = rain_tier_of_tenths(tenths);
    const int fill_q8      = rain_tier_fill_q8(tenths, tier);
    const int bar_plot_top = bar_plot_bottom - bar_plot_h;

    // Lower slabs (k < tier) fill their segment fully. The topmost slab
    // (k == tier) grows from its segment base by fill_q8/256 so bar
    // height varies continuously across the wire-tenths domain while
    // colours stay discrete per tier.
    //
    // Invert: slab y math anchors at bar_plot_top instead of
    // bar_plot_bottom so the bar hangs downward. Tier 1 stays nearest
    // the anchor edge (which is now the top).
    for (int k = 1; k <= tier; ++k) {
        int slab_top_full, slab_bottom_full;
        if (invert) {
            slab_top_full    = bar_plot_top + (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[k - 1]) / 100;
            slab_bottom_full = bar_plot_top + (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[k])     / 100;
        } else {
            slab_top_full    = bar_plot_bottom - (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[k])     / 100;
            slab_bottom_full = bar_plot_bottom - (bar_plot_h * RAIN_TIER_TOP_PCT_ARR[k - 1]) / 100;
        }
        const int slab_h_full = slab_bottom_full - slab_top_full;

        int slab_h;
        if (k < tier) {
            slab_h = slab_h_full;
        } else {
            slab_h = (slab_h_full * fill_q8) / 256;
            if (slab_h == 0 && fill_q8 > 0) { slab_h = 1; }
        }
        if (slab_h <= 0) { continue; }

        const int slab_top = invert ? slab_top_full : (slab_bottom_full - slab_h);
        graphics_context_set_fill_color(ctx, rain_tier_color(k));
        graphics_fill_rect(ctx, GRect(bar_x, slab_top, bar_w, slab_h), 0, GCornerNone);
    }

#ifndef PBL_COLOR
    // B&W: every tier reduces to a single BAR_COLOR (black), so a tall bar
    // that extends above the precip area or outside the radar hatch sits
    // black-on-black and disappears. A 1 px white outline around the bar's
    // silhouette keeps the shape readable on either background.
    const int bar_h = rain_tier_proportional_height(tenths, bar_plot_h);
    if (bar_h > 0) {
        const int outline_top = invert ? bar_plot_top : (bar_plot_bottom - bar_h);
        graphics_context_set_stroke_color(ctx, GColorWhite);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_rect(ctx, GRect(bar_x, outline_top, bar_w, bar_h));
    }
#endif
}

void rain_bars_draw(GContext *ctx, GRect plot_rect, SlotGeometry slots,
                    const uint8_t *tenths) {
    if (slots.num_slots < 1 || !tenths) {
        return;
    }

    const int16_t bar_plot_h = plot_rect.size.h;
    const int16_t bar_plot_bottom = plot_rect.origin.y + bar_plot_h;

    for (int i = 0; i < slots.num_slots; ++i) {
        const int t = tenths[i];
        if (t <= 0) {
            continue;
        }
        const int bar_x = slot_geometry_bar_x(slots, i, plot_rect.origin.x);
        rain_tier_bar_draw_slabs(ctx, bar_x, slots.bar_w, bar_plot_bottom, bar_plot_h, t, false);
    }
}
