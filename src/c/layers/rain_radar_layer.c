// src/c/layers/rain_radar_layer.c
#include "rain_radar_layer.h"
#include "c/appendix/persist.h"
#include "c/appendix/rain_tier.h"
#include "c/appendix/axis.h"
#include "c/appendix/hatch.h"
#include "c/appendix/memory_log.h"

// Layout constants. Top axis row hosts ticks + numeric labels.
#define RADAR_AXIS_H        12
#define RADAR_LABEL_FONT    FONT_KEY_GOTHIC_14
#define RADAR_TICK_BIG_LEN  5
#define RADAR_TICK_SMALL_LEN 2
#define RADAR_NUM_ENTRIES   24
#define RADAR_BIG_EVERY     3   // every 15min (3 * 5min)

// Hatch line spacing for the 1km background bars. Matches the night-shading
// stride for visual consistency.
#define RADAR_HATCH_SPACING PBL_IF_COLOR_ELSE(6, 7)

// 1km area background hatch colour — matches the night-region hatch so the
// area pass reads as "non-foreground intensity context" rather than as
// tier-coloured fill. Tier colour still lives in the outline + exact bars.
#define RADAR_AREA_HATCH_COLOR PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)

// Minimum px between text labels. Drives label cadence per platform.
#define RADAR_LABEL_MIN_SPACING_PX 24

static Layer *s_radar_layer;

static int radar_label_stride(int16_t plot_w) {
    const float entry_w = (float) plot_w / RADAR_NUM_ENTRIES;
    const float big_slot_w = entry_w * RADAR_BIG_EVERY;
    if (big_slot_w >= RADAR_LABEL_MIN_SPACING_PX) {
        return 1;
    }
    return 2;
}

static void format_radar_label(int minutes_offset, char *buf, size_t buf_size) {
    if (minutes_offset == 0) {
        snprintf(buf, buf_size, "now");
    } else {
        snprintf(buf, buf_size, "+%d", minutes_offset);
    }
}

static void draw_radar_axis(GContext *ctx, GRect bounds) {
    const int16_t x_start = bounds.origin.x;
    const int16_t x_end = bounds.origin.x + bounds.size.w;
    const int16_t y_axis = bounds.origin.y + RADAR_AXIS_H - 1;

    axis_draw_tick_row(ctx, x_start, x_end, y_axis,
                       RADAR_NUM_ENTRIES, RADAR_BIG_EVERY,
                       RADAR_TICK_BIG_LEN, RADAR_TICK_SMALL_LEN,
                       PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite),
                       false);

    graphics_context_set_text_color(ctx, GColorWhite);
    const int stride = radar_label_stride(bounds.size.w);
    const GFont font = fonts_get_system_font(RADAR_LABEL_FONT);
    const int label_y = bounds.origin.y - 4;
    const int label_h = RADAR_AXIS_H;

    for (int big_i = 0; big_i * RADAR_BIG_EVERY <= RADAR_NUM_ENTRIES; ++big_i) {
        if (big_i % stride != 0) {
            continue;
        }
        const int tick_i = big_i * RADAR_BIG_EVERY;
        const int minutes = tick_i * 5;
        const int16_t x = x_start + (int16_t)(((int32_t)(x_end - x_start) * tick_i) / RADAR_NUM_ENTRIES);
        char buf[8];
        format_radar_label(minutes, buf, sizeof(buf));
        graphics_draw_text(ctx, buf, font,
                           GRect(x - 20, label_y, 40, label_h),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

// Per-slot helpers used by both passes.
static inline int16_t slot_x(int s, int16_t plot_x, float entry_w) {
    return plot_x + (int16_t)(s * entry_w);
}

static inline int slot_height_px(uint8_t tenths, int16_t bar_plot_h) {
    const int tier = rain_tier_of_tenths(tenths);
    return rain_tier_pixel_height(tier, bar_plot_h);
}

// Pass 1: 1km background bars. Per slot with area > 0, hatch-fill a
// full-slot-width rect in RADAR_AREA_HATCH_COLOR (matches night hatch so
// the area pass reads as low-emphasis context). Contiguous runs of
// nonzero slots get a 1-px outline tracing the perimeter, colour =
// max area tier in the run — that's where tier intensity is conveyed.
static void draw_radar_area_bars(GContext *ctx, GRect bar_plot_rect,
                                  const uint8_t *area_tenths) {
    if (bar_plot_rect.size.w <= 0 || bar_plot_rect.size.h <= 0) {
        return;
    }
    const int16_t plot_x = bar_plot_rect.origin.x;
    const int16_t plot_bottom = bar_plot_rect.origin.y + bar_plot_rect.size.h;
    const int16_t bar_h = bar_plot_rect.size.h;
    const float entry_w = (float) bar_plot_rect.size.w / RADAR_NUM_ENTRIES;

    int i = 0;
    while (i < RADAR_NUM_ENTRIES) {
        if (area_tenths[i] == 0) { i++; continue; }

        const int run_start = i;
        int run_end = i;
        int max_tier = 0;
        while (run_end < RADAR_NUM_ENTRIES && area_tenths[run_end] != 0) {
            const int tier = rain_tier_of_tenths(area_tenths[run_end]);
            if (tier > max_tier) { max_tier = tier; }
            run_end++;
        }

        for (int s = run_start; s < run_end; ++s) {
            const int slot_h = slot_height_px(area_tenths[s], bar_h);
            const int16_t x_a = slot_x(s,     plot_x, entry_w);
            const int16_t x_b = slot_x(s + 1, plot_x, entry_w);
            const GRect r = GRect(x_a, plot_bottom - slot_h, x_b - x_a, slot_h);
            hatch_fill_rect(ctx, r, RADAR_AREA_HATCH_COLOR, RADAR_HATCH_SPACING);
        }

        graphics_context_set_stroke_color(ctx, rain_tier_color(max_tier));
        graphics_context_set_stroke_width(ctx, 1);

        // Left vertical edge: baseline up to slot run_start's top.
        {
            const int h0 = slot_height_px(area_tenths[run_start], bar_h);
            const int16_t lx = slot_x(run_start, plot_x, entry_w);
            graphics_draw_line(ctx,
                GPoint(lx, plot_bottom - 1),
                GPoint(lx, plot_bottom - h0));
        }
        // Stepped top: per-slot horizontals at each slot's own height.
        for (int s = run_start; s < run_end; ++s) {
            const int h_s = slot_height_px(area_tenths[s], bar_h);
            const int16_t x_a = slot_x(s,     plot_x, entry_w);
            const int16_t x_b = slot_x(s + 1, plot_x, entry_w);
            graphics_draw_line(ctx,
                GPoint(x_a,     plot_bottom - h_s),
                GPoint(x_b - 1, plot_bottom - h_s));
        }
        // Step verticals between adjacent slots inside the run.
        for (int s = run_start; s < run_end - 1; ++s) {
            const int h_a = slot_height_px(area_tenths[s],     bar_h);
            const int h_b = slot_height_px(area_tenths[s + 1], bar_h);
            if (h_a == h_b) { continue; }
            const int16_t bx = slot_x(s + 1, plot_x, entry_w);
            const int16_t y_lo = plot_bottom - (h_a < h_b ? h_a : h_b);
            const int16_t y_hi = plot_bottom - (h_a < h_b ? h_b : h_a);
            graphics_draw_line(ctx, GPoint(bx, y_lo), GPoint(bx, y_hi));
        }
        // Right vertical edge: top of last slot down to baseline.
        {
            const int h_last = slot_height_px(area_tenths[run_end - 1], bar_h);
            const int16_t rx = slot_x(run_end, plot_x, entry_w) - 1;
            graphics_draw_line(ctx,
                GPoint(rx, plot_bottom - h_last),
                GPoint(rx, plot_bottom - 1));
        }

        i = run_end;
    }
}

// Pass 2: exact-location foreground bars. Narrow solid bars centred in
// each slot, drawn on top of the area pass.
static void draw_radar_exact_bars(GContext *ctx, GRect bar_plot_rect,
                                   const uint8_t *exact_tenths) {
    if (bar_plot_rect.size.w <= 0 || bar_plot_rect.size.h <= 0) {
        return;
    }
    const int16_t plot_x = bar_plot_rect.origin.x;
    const int16_t plot_bottom = bar_plot_rect.origin.y + bar_plot_rect.size.h;
    const int16_t bar_h = bar_plot_rect.size.h;
    const float entry_w = (float) bar_plot_rect.size.w / RADAR_NUM_ENTRIES;

    int fg_w = (int)(entry_w / 3.0f);
    if (fg_w < 1) { fg_w = 1; }

    for (int s = 0; s < RADAR_NUM_ENTRIES; ++s) {
        if (exact_tenths[s] == 0) { continue; }
        const int16_t x_a = slot_x(s, plot_x, entry_w);
        const int16_t bar_x = x_a + (int16_t)((entry_w - fg_w) / 2.0f);
        rain_tier_bar_draw_slabs(ctx, bar_x, fg_w, plot_bottom, bar_h,
                                 exact_tenths[s]);
    }
}

static void radar_update_proc(Layer *layer, GContext *ctx) {
    MEMORY_LOG_HEAP("radar_update:enter");
    GRect bounds = layer_get_bounds(layer);

    uint8_t exact_tenths[RADAR_NUM_ENTRIES];
    uint8_t area_tenths[RADAR_NUM_ENTRIES];
    persist_get_rain_radar_trend(exact_tenths, RADAR_NUM_ENTRIES);
    persist_get_rain_radar_trend_area(area_tenths, RADAR_NUM_ENTRIES);

    const GRect axis_rect = GRect(bounds.origin.x, bounds.origin.y,
                                  bounds.size.w, RADAR_AXIS_H);
    const GRect bar_plot_rect = GRect(bounds.origin.x,
                                      bounds.origin.y + RADAR_AXIS_H,
                                      bounds.size.w,
                                      bounds.size.h - RADAR_AXIS_H);

    draw_radar_axis(ctx, axis_rect);
    draw_radar_area_bars(ctx, bar_plot_rect, area_tenths);
    draw_radar_exact_bars(ctx, bar_plot_rect, exact_tenths);

    MEMORY_LOG_HEAP("radar_update:exit");
}

void rain_radar_layer_create(Layer *parent, GRect frame) {
    s_radar_layer = layer_create(frame);
    layer_set_update_proc(s_radar_layer, radar_update_proc);
    layer_set_hidden(s_radar_layer, true);  // calendar wins by default until toggle wiring lands
    layer_add_child(parent, s_radar_layer);
    MEMORY_LOG_HEAP("after_rain_radar_layer_create");
}

void rain_radar_layer_refresh(void) {
    layer_mark_dirty(s_radar_layer);
}

void rain_radar_layer_destroy(void) {
    layer_destroy(s_radar_layer);
}

Layer *rain_radar_layer_get_root(void) {
    return s_radar_layer;
}
