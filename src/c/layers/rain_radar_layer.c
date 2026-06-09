// src/c/layers/rain_radar_layer.c
#include "rain_radar_layer.h"
#include "c/appendix/persist.h"
#include "c/appendix/config.h"
#include "c/appendix/rain_tier.h"
#include "c/appendix/hatch.h"
#include "c/appendix/memory_log.h"

// Layout constants. The axis area sits above the bar plot. Hour labels
// share a single vertical strip with the tick row: at hour-aligned slot
// positions the tick is suppressed and the hour digit is drawn centred
// on that column instead.
//
// RADAR_LABEL_FONT_OFFSET nudges the GOTHIC_14 text box up so its
// internal top padding doesn't push the digit pixels down past the
// bottom of the axis area into the bar plot. The box itself spills a
// couple of px above bounds, but only padding pixels live there.
#define RADAR_LABEL_FONT        FONT_KEY_GOTHIC_14
#define RADAR_LABEL_H           14
#define RADAR_LABEL_FONT_OFFSET 3
#define RADAR_AXIS_H            12
#define RADAR_TICK_BIG_LEN      5
#define RADAR_TICK_SMALL_LEN    2
#define RADAR_NUM_ENTRIES    24
#define RADAR_BIG_EVERY      3      // every 15min (3 * 5min)
#define RADAR_SLOT_SECONDS   (5 * 60)
#define RADAR_WINDOW_SECONDS (RADAR_NUM_ENTRIES * RADAR_SLOT_SECONDS)
#define HOUR_SECONDS         3600

// Hatch line spacing for the 1km background bars. Matches the night-shading
// stride for visual consistency.
#define RADAR_HATCH_SPACING PBL_IF_COLOR_ELSE(6, 7)

// Hatch fill colour for the 1km nearby-rain shape. Matches the
// night-region hatch (DarkGray on colour, White on B&W) so the fill
// reads as low-emphasis context; tier intensity is conveyed by the
// outline + the exact bars on top.
#define RADAR_AREA_HATCH_COLOR PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)

static Layer *s_radar_layer;

// True when tick index `i` (0..RADAR_NUM_ENTRIES) lands exactly on a
// whole-hour wall-clock boundary that will actually carry a label,
// given `radar_start`. The label loop draws hours strictly inside the
// window (radar_start < hr < radar_end), so the slot-0 and slot-24
// ticks at the window edges aren't suppressed even when radar_start
// happens to be hour-aligned. Returns false when radar_start <= 0 (no
// time info available) so all ticks render.
static bool tick_index_is_on_hour(int i, time_t radar_start) {
    if (radar_start <= 0) {
        return false;
    }
    const time_t tick_time = radar_start + (time_t)i * RADAR_SLOT_SECONDS;
    if ((tick_time % HOUR_SECONDS) != 0) {
        return false;
    }
    return tick_time > radar_start && tick_time < radar_start + RADAR_WINDOW_SECONDS;
}

static void draw_radar_axis(GContext *ctx, GRect bounds) {
    const int16_t x_start = bounds.origin.x;
    const int16_t x_end = bounds.origin.x + bounds.size.w;
    const int16_t y_axis = bounds.origin.y + RADAR_AXIS_H - 1;
    const int32_t span_px = (int32_t) x_end - x_start;
    const time_t radar_start = persist_get_rain_radar_start();

    // Tick row, but skip ticks whose column will host a hour label so
    // the hour digit can sit in the same vertical strip without a tick
    // collision underneath it.
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite));
    graphics_context_set_stroke_width(ctx, 1);
    for (int i = 0; i <= RADAR_NUM_ENTRIES; ++i) {
        if (tick_index_is_on_hour(i, radar_start)) {
            continue;
        }
        const int16_t x = x_start + (int16_t)((span_px * i) / RADAR_NUM_ENTRIES);
        const int16_t len = (i % RADAR_BIG_EVERY == 0) ? RADAR_TICK_BIG_LEN : RADAR_TICK_SMALL_LEN;
        graphics_draw_line(ctx, GPoint(x, y_axis), GPoint(x, y_axis - len));
    }

    if (radar_start <= 0) {
        return;
    }

    // Hour labels centred on the slot column where the tick was removed.
    // Whole-hour boundaries strictly inside the 2h radar window get a
    // label; the start hour at slot 0 is implied by reading "+1" off the
    // first label.
    const time_t radar_end = radar_start + RADAR_WINDOW_SECONDS;
    const time_t first_hour = (radar_start / HOUR_SECONDS + 1) * HOUR_SECONDS;

    graphics_context_set_text_color(ctx, GColorWhite);
    const GFont font = fonts_get_system_font(RADAR_LABEL_FONT);
    // Shift the GOTHIC_14 text box up by RADAR_LABEL_FONT_OFFSET so the
    // font's internal top padding doesn't push the digit pixels past the
    // axis bottom into the bar plot. The box itself spills above
    // bounds.origin.y but only top-padding pixels live there.
    const int label_y = bounds.origin.y - RADAR_LABEL_FONT_OFFSET;

    for (time_t hr = first_hour; hr < radar_end; hr += HOUR_SECONDS) {
        struct tm *hr_local = localtime(&hr);
        const int hour_disp = config_axis_hour(hr_local->tm_hour);
        const int16_t x = x_start + (int16_t)(((int64_t)(hr - radar_start) * span_px) / RADAR_WINDOW_SECONDS);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", hour_disp);
        graphics_draw_text(ctx, buf, font,
                           GRect(x - 20, label_y, 40, RADAR_LABEL_H),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
}

// Per-slot helpers used by both passes.
static inline int16_t slot_x(int s, int16_t plot_x, float entry_w) {
    return plot_x + (int16_t)(s * entry_w);
}

static inline int slot_height_px(uint8_t tenths, int16_t bar_plot_h) {
    return rain_tier_proportional_height((int) tenths, bar_plot_h);
}

// Per-slot outline colour: tier of the exact (foreground) bar at that
// slot — the border at column k matches the colour of the topmost slab
// of the exact bar in column k. Falls back to the area tier when the
// slot has no exact bar so the border still has a colour.
static GColor border_color_for_slot(uint8_t exact_t, uint8_t area_t) {
    int tier = rain_tier_of_tenths(exact_t);
    if (tier == 0) {
        tier = rain_tier_of_tenths(area_t);
    }
    return rain_tier_color(tier);
}

// Pass 1: 1km background bars. Per slot with area > 0, hatch-fill a
// full-slot-width rect in the muted RADAR_AREA_HATCH_COLOR; tier
// intensity is conveyed by the outline + the exact bars on top.
// Contiguous runs of nonzero slots get a 1-px outline tracing the
// perimeter, with each segment coloured by its slot's exact tier.
static void draw_radar_area_bars(GContext *ctx, GRect bar_plot_rect,
                                  const uint8_t *area_tenths,
                                  const uint8_t *exact_tenths) {
    if (bar_plot_rect.size.w <= 0 || bar_plot_rect.size.h <= 0) {
        return;
    }
    const int16_t plot_x = bar_plot_rect.origin.x;
    const int16_t plot_bottom = bar_plot_rect.origin.y + bar_plot_rect.size.h;
    const int16_t bar_h = bar_plot_rect.size.h;
    const float entry_w = (float) bar_plot_rect.size.w / RADAR_NUM_ENTRIES;

    graphics_context_set_stroke_width(ctx, 1);

    int i = 0;
    while (i < RADAR_NUM_ENTRIES) {
        if (area_tenths[i] == 0) { i++; continue; }

        const int run_start = i;
        int run_end = i;
        while (run_end < RADAR_NUM_ENTRIES && area_tenths[run_end] != 0) {
            run_end++;
        }

        for (int s = run_start; s < run_end; ++s) {
            const int slot_h = slot_height_px(area_tenths[s], bar_h);
            const int16_t x_a = slot_x(s,     plot_x, entry_w);
            const int16_t x_b = slot_x(s + 1, plot_x, entry_w);
            const GRect r = GRect(x_a, plot_bottom - slot_h, x_b - x_a, slot_h);
            hatch_fill_rect(ctx, r, RADAR_AREA_HATCH_COLOR, RADAR_HATCH_SPACING);
        }

        // Left vertical edge: baseline up to slot run_start's top, in
        // that slot's exact-tier colour.
        {
            const int h0 = slot_height_px(area_tenths[run_start], bar_h);
            const int16_t lx = slot_x(run_start, plot_x, entry_w);
            graphics_context_set_stroke_color(ctx,
                border_color_for_slot(exact_tenths[run_start], area_tenths[run_start]));
            graphics_draw_line(ctx,
                GPoint(lx, plot_bottom - 1),
                GPoint(lx, plot_bottom - h0));
        }
        // Stepped top: per-slot horizontals each in the slot's own colour.
        for (int s = run_start; s < run_end; ++s) {
            const int h_s = slot_height_px(area_tenths[s], bar_h);
            const int16_t x_a = slot_x(s,     plot_x, entry_w);
            const int16_t x_b = slot_x(s + 1, plot_x, entry_w);
            graphics_context_set_stroke_color(ctx,
                border_color_for_slot(exact_tenths[s], area_tenths[s]));
            graphics_draw_line(ctx,
                GPoint(x_a,     plot_bottom - h_s),
                GPoint(x_b - 1, plot_bottom - h_s));
        }
        // Step verticals between adjacent slots — coloured by the taller
        // slot (the one whose top the vertical reaches up to).
        for (int s = run_start; s < run_end - 1; ++s) {
            const int h_a = slot_height_px(area_tenths[s],     bar_h);
            const int h_b = slot_height_px(area_tenths[s + 1], bar_h);
            if (h_a == h_b) { continue; }
            const int taller = (h_a > h_b) ? s : s + 1;
            const int16_t bx = slot_x(s + 1, plot_x, entry_w);
            const int16_t y_lo = plot_bottom - (h_a < h_b ? h_a : h_b);
            const int16_t y_hi = plot_bottom - (h_a < h_b ? h_b : h_a);
            graphics_context_set_stroke_color(ctx,
                border_color_for_slot(exact_tenths[taller], area_tenths[taller]));
            graphics_draw_line(ctx, GPoint(bx, y_lo), GPoint(bx, y_hi));
        }
        // Right vertical edge: top of last slot down to baseline, in
        // that slot's exact-tier colour.
        {
            const int h_last = slot_height_px(area_tenths[run_end - 1], bar_h);
            const int16_t rx = slot_x(run_end, plot_x, entry_w) - 1;
            graphics_context_set_stroke_color(ctx,
                border_color_for_slot(exact_tenths[run_end - 1], area_tenths[run_end - 1]));
            graphics_draw_line(ctx,
                GPoint(rx, plot_bottom - h_last),
                GPoint(rx, plot_bottom - 1));
        }

        i = run_end;
    }
}

// Pass 2: exact-location foreground bars. Sized one px narrower than
// the forecast rain bars (bar_w = entry_w - 4 vs forecast's -3); the
// radar layer fills the full layer width with 24 slots while the
// forecast plot is narrower after the left axis, so matching the
// forecast formula 1:1 makes the radar bars look fatter. Drawn on top
// of the area pass — slab colours carry the tier intensity.
static void draw_radar_exact_bars(GContext *ctx, GRect bar_plot_rect,
                                   const uint8_t *exact_tenths) {
    if (bar_plot_rect.size.w <= 0 || bar_plot_rect.size.h <= 0) {
        return;
    }
    const int16_t plot_x = bar_plot_rect.origin.x;
    const int16_t plot_bottom = bar_plot_rect.origin.y + bar_plot_rect.size.h;
    const int16_t bar_h = bar_plot_rect.size.h;
    const float entry_w = (float) bar_plot_rect.size.w / RADAR_NUM_ENTRIES;

    const int fg_w = (entry_w >= 5.0f) ? (int) entry_w - 4 : 1;

    for (int s = 0; s < RADAR_NUM_ENTRIES; ++s) {
        if (exact_tenths[s] == 0) { continue; }
        const int16_t bar_x = slot_x(s, plot_x, entry_w) + 2;
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
    draw_radar_area_bars(ctx, bar_plot_rect, area_tenths, exact_tenths);
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
