// src/c/layers/rain_radar_layer.c
#include "rain_radar_layer.h"
#include "c/appendix/persist.h"
#include "c/appendix/config.h"
#include "c/appendix/rain_tier.h"
#include "c/appendix/hatch.h"
#include "c/appendix/memory_log.h"
#include "c/appendix/slot_geometry.h"
#include "c/appendix/display_width.h"
#include "c/appendix/chart.h"
#include "c/appendix/snooze.h"

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
#define RADAR_NUM_SLOTS         24
#define RADAR_SLOT_SECONDS      (5 * 60)
#define RADAR_WINDOW_SECONDS    (RADAR_NUM_SLOTS * RADAR_SLOT_SECONDS)
#define HOUR_SECONDS            3600
// Grace after a grid fetch boundary before the watch synthesizes an advance,
// giving PKJS time to deliver the real frame. 55s (not 60) so the gate clears
// strictly before the next minute tick, which then reliably redraws.
#define RADAR_ADVANCE_BUFFER_SECONDS 55

// Bar growth direction. true flips the radar top-down (rain hanging
// from the axis); false keeps the conventional bottom-up layout. Both
// branches live in draw_radar_area_bars and draw_radar_exact_bars; the
// unused side is dead-stripped at the constant value below. Wire to a
// runtime setting when desired.
#define RADAR_INVERT_BARS false

// Slot grid bar dimensions, bucketed by display width.
// pitch = tick_w + 2*pad + bar_w
//   144-bucket: 1 + 2 + 3 = 6 → 24*6 + 1 = 145 px (overflows 144 by 1 → 1 px clip)
//   200-bucket: 1 + 4 + 4 = 9 → 24*9 + 1 = 217 px (overflows emery's 196 by 21 → rightmost slots clip)
#if defined(DISPLAY_WIDTH_200)
    #define RADAR_BAR_W 5
    #define RADAR_PAD   1
#elif defined(DISPLAY_WIDTH_144)
    #define RADAR_BAR_W 3
    #define RADAR_PAD   1
#endif

// Hatch line spacing for the 1km background bars. Matches the night-shading
// stride for visual consistency.
#define RADAR_HATCH_SPACING PBL_IF_COLOR_ELSE(6, 7)

// Hatch fill colour for the 1km nearby-rain shape. Matches the
// night-region hatch (DarkGray on colour, White on B&W) so the fill
// reads as low-emphasis context; tier intensity is conveyed by the
// outline + the exact bars on top.
#define RADAR_AREA_HATCH_COLOR PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)

// Chart config: no-border frame; top tick row sits in the axis strip
// above the bar plot. Small ticks every 5-min slot, big ticks every
// 15 min (3 × 5min). Outer for the radar is the bar plot rect — top
// ticks extend upward from there into the axis strip.
#define RADAR_TICK_COLOR PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)

// Breathing room around the snooze glyphs inside the layer bounds.
#define RADAR_SNOOZE_INSET 4

static const ChartConfig RADAR_CHART = {
    .frame = {
        .left   = { 0, GColorClear },
        .right  = { 0, GColorClear },
        // Top divider only renders in inverted (top-down) layout, where
        // it reads as the axis line that bars hang from. Bottom-up mode
        // doesn't need it — the layer's bottom edge implicitly anchors
        // the bars.
        .top    = { RADAR_INVERT_BARS ? 1 : 0, RADAR_AREA_HATCH_COLOR },
        .bottom = { 0, GColorClear },
    },
    .ticks = {
        .tick_w = 1,
        .top    = { .length     = 2,
                    .color      = RADAR_TICK_COLOR,
                    .big_length = 5,
                    .big_color  = RADAR_TICK_COLOR,
                    .big_every  = 3 },   // every 15min (3 × 5min slots)
    },
    .slots = { .pad = RADAR_PAD, .bar_w = RADAR_BAR_W,
               .num_slots = RADAR_NUM_SLOTS },
};

static Layer *s_radar_layer;

// True when tick index `i` (0..RADAR_NUM_SLOTS) lands exactly on a
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

typedef struct {
    GRect    outer;
    time_t   radar_start;
} RadarTickCtx;

static void radar_tick_callback(GContext *ctx, int idx, int tick_x, void *user) {
    RadarTickCtx *c = user;
    if (tick_index_is_on_hour(idx, c->radar_start)) {
        return;  // hour digit takes this column instead
    }
    tick_side_draw_at(ctx, c->outer, RADAR_CHART.frame, GRAPH_SIDE_TOP,
                      RADAR_CHART.ticks.top, idx, tick_x);
}

static void draw_radar_axis(GContext *ctx, GRect outer, ChartGeometry chart) {
    const time_t radar_start = persist_get_rain_radar_start();

    RadarTickCtx tick_ctx = {
        .outer       = outer,
        .radar_start = radar_start,
    };
    slot_geometry_visit_ticks(chart.slots, ctx, chart.content.origin.x,
                              radar_tick_callback, &tick_ctx);

    if (radar_start <= 0) {
        return;
    }

    // Hour labels centred on the slot column where the tick was removed.
    // Whole-hour boundaries strictly inside the 2h radar window get a
    // label; the start hour at slot 0 is implied by reading "+1" off the
    // first label. Label x uses the slot grid width so labels and bars
    // share the same time→x mapping.
    const int32_t grid_w = (int32_t)chart.slots.num_slots * chart.slots.pitch
                         + chart.slots.tick_w;
    const time_t radar_end = radar_start + RADAR_WINDOW_SECONDS;
    const time_t first_hour = (radar_start / HOUR_SECONDS + 1) * HOUR_SECONDS;

    graphics_context_set_text_color(ctx, GColorWhite);
    const GFont font = fonts_get_system_font(RADAR_LABEL_FONT);
    const int label_y = outer.origin.y - RADAR_AXIS_H - RADAR_LABEL_FONT_OFFSET;

    for (time_t hr = first_hour; hr < radar_end; hr += HOUR_SECONDS) {
        struct tm *hr_local = localtime(&hr);
        const int hour_disp = config_axis_hour(hr_local->tm_hour);
        const int16_t x = chart.content.origin.x
                        + (int16_t)(((int64_t)(hr - radar_start) * grid_w) / RADAR_WINDOW_SECONDS);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", hour_disp);
        graphics_draw_text(ctx, buf, font,
                           GRect(x - 20, label_y, 40, RADAR_LABEL_H),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
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

// Draw a dotted horizontal/vertical line segment for the nearby border on
// B&W devices; fall back to a solid line on colour devices.
static void nearby_border_h_line(GContext *ctx, int16_t x0, int16_t x1, int16_t y) {
#ifdef PBL_COLOR
    graphics_draw_line(ctx, GPoint(x0, y), GPoint(x1, y));
#else
    graphics_context_set_stroke_color(ctx, GColorWhite);
    if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; }
    for (int16_t x = x0; x <= x1; x += 2) {
        graphics_draw_pixel(ctx, GPoint(x, y));
    }
#endif
}

static void nearby_border_v_line(GContext *ctx, int16_t x, int16_t y0, int16_t y1) {
#ifdef PBL_COLOR
    graphics_draw_line(ctx, GPoint(x, y0), GPoint(x, y1));
#else
    graphics_context_set_stroke_color(ctx, GColorWhite);
    if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; }
    for (int16_t y = y0; y <= y1; y += 2) {
        graphics_draw_pixel(ctx, GPoint(x, y));
    }
#endif
}

// Pass 1: 1km background bars. Per slot with area > 0, hatch-fill a
// full-slot-width rect in the muted RADAR_AREA_HATCH_COLOR; tier
// intensity is conveyed by the outline + the exact bars on top.
// Contiguous runs of nonzero slots get a 1-px outline tracing the
// perimeter, with each segment coloured by its slot's exact tier.
//
// Direction is gated on RADAR_INVERT_BARS:
//   false → bottom-up: hatch rects anchor at plot_bottom and grow
//           upward; outline traces the run's top edge plus the
//           left/right verticals from plot_bottom up.
//   true  → top-down (radar rain "falls" from the axis): hatch rects
//           anchor at plot_top; outline traces the run's lower lip
//           plus the verticals from plot_top down.
// The unused branch is dead-stripped at the macro's current value.
static void draw_radar_area_bars(GContext *ctx, GRect bar_plot_rect,
                                  SlotGeometry slots,
                                  const uint8_t *area_tenths,
                                  const uint8_t *exact_tenths) {
    if (bar_plot_rect.size.w <= 0 || bar_plot_rect.size.h <= 0) {
        return;
    }
    const bool invert = RADAR_INVERT_BARS;
    const int16_t plot_x      = bar_plot_rect.origin.x;
    const int16_t plot_top    = bar_plot_rect.origin.y;
    const int16_t plot_bottom = bar_plot_rect.origin.y + bar_plot_rect.size.h;
    const int16_t bar_h       = bar_plot_rect.size.h;

    graphics_context_set_stroke_width(ctx, 1);

    int i = 0;
    while (i < slots.num_slots) {
        // Skip zero-area runs.
        if (area_tenths[i] == 0) { ++i; continue; }

        int run_start = i;
        int run_end = i;
        while (run_end < slots.num_slots && area_tenths[run_end] != 0) {
            ++run_end;
        }

        // Hatch-fill each slot in the run.
        for (int s = run_start; s < run_end; ++s) {
            const int slot_h = slot_height_px(area_tenths[s], bar_h);
            if (slot_h <= 0) { continue; }
            const int16_t x_a = slot_geometry_tick_x(slots, s,     plot_x);
            const int16_t x_b = slot_geometry_tick_x(slots, s + 1, plot_x);
            const int16_t slot_w = x_b - x_a;
            const int16_t y = invert ? plot_top : (plot_bottom - slot_h);
            const GRect r = GRect(x_a, y, slot_w, slot_h);
            hatch_fill_rect(ctx, r, RADAR_AREA_HATCH_COLOR, RADAR_HATCH_SPACING);
        }

        // Left vertical outline at the run's left edge.
        {
            const int h0 = slot_height_px(area_tenths[run_start], bar_h);
            const int16_t lx = slot_geometry_tick_x(slots, run_start, plot_x);
            const int16_t y0 = invert ? plot_top                : (plot_bottom - 1);
            const int16_t y1 = invert ? (plot_top + h0 - 1)     : (plot_bottom - h0);
            graphics_context_set_stroke_color(ctx, border_color_for_slot(exact_tenths[run_start], area_tenths[run_start]));
            nearby_border_v_line(ctx, lx, y0, y1);
        }

        // Visible inner edge across the run — top edge (bottom-up) or
        // lower lip (top-down).
        for (int s = run_start; s < run_end; ++s) {
            const int h_s = slot_height_px(area_tenths[s], bar_h);
            if (h_s <= 0) { continue; }
            const int16_t x_a = slot_geometry_tick_x(slots, s,     plot_x);
            const int16_t x_b = slot_geometry_tick_x(slots, s + 1, plot_x);
            const int16_t y = invert ? (plot_top + h_s - 1) : (plot_bottom - h_s);
            graphics_context_set_stroke_color(ctx, border_color_for_slot(exact_tenths[s], area_tenths[s]));
            nearby_border_h_line(ctx, x_a, x_b - 1, y);
        }

        // Internal vertical steps where adjacent slot heights differ.
        for (int s = run_start; s + 1 < run_end; ++s) {
            const int h_a = slot_height_px(area_tenths[s],     bar_h);
            const int h_b = slot_height_px(area_tenths[s + 1], bar_h);
            if (h_a == h_b) { continue; }
            const int16_t bx = slot_geometry_tick_x(slots, s + 1, plot_x);
            const int min_h = (h_a > h_b) ? h_b : h_a;
            const int max_h = (h_a > h_b) ? h_a : h_b;
            const int y_near = invert ? (plot_top + min_h) : (plot_bottom - min_h);
            const int y_far  = invert ? (plot_top + max_h) : (plot_bottom - max_h);
            graphics_context_set_stroke_color(ctx, border_color_for_slot(exact_tenths[s + 1], area_tenths[s + 1]));
            nearby_border_v_line(ctx, bx, y_near, y_far);
        }

        // Right vertical outline at the run's right edge.
        {
            const int h_last = slot_height_px(area_tenths[run_end - 1], bar_h);
            const int16_t rx = slot_geometry_tick_x(slots, run_end, plot_x) - 1;
            const int16_t y0 = invert ? plot_top                : (plot_bottom - 1);
            const int16_t y1 = invert ? (plot_top + h_last - 1) : (plot_bottom - h_last);
            graphics_context_set_stroke_color(ctx, border_color_for_slot(exact_tenths[run_end - 1], area_tenths[run_end - 1]));
            nearby_border_v_line(ctx, rx, y0, y1);
        }

        i = run_end;
    }
}

// Pass 2: exact-location foreground bars. Position and width come from
// the slot geometry — bar sits between the two ticks with a 1 px pad
// on each side (set by RADAR_PAD in the per-bucket constants block).
// Drawn on top of the area pass — slab colours carry the tier intensity.
static void draw_radar_exact_bars(GContext *ctx, GRect bar_plot_rect,
                                   SlotGeometry slots,
                                   const uint8_t *exact_tenths) {
    if (bar_plot_rect.size.w <= 0 || bar_plot_rect.size.h <= 0) {
        return;
    }
    const int16_t plot_x = bar_plot_rect.origin.x;
    const int16_t plot_bottom = bar_plot_rect.origin.y + bar_plot_rect.size.h;
    const int16_t bar_h = bar_plot_rect.size.h;

    for (int s = 0; s < slots.num_slots; ++s) {
        if (exact_tenths[s] == 0) { continue; }
        const int16_t bar_x = slot_geometry_bar_x(slots, s, plot_x);
        rain_tier_bar_draw_slabs(ctx, bar_x, slots.bar_w, plot_bottom, bar_h,
                                 exact_tenths[s], RADAR_INVERT_BARS);
    }
}

static void radar_or_snooze_update_proc(Layer *layer, GContext *ctx) {
    MEMORY_LOG_HEAP("radar_update:enter");
    GRect bounds = layer_get_bounds(layer);

    if (persist_get_radar_snooze()) {
        // Sleep mode: big snooze glyphs instead of the chart. Latched until
        // fresh radar data arrives after waking (see app_message.c).
        snooze_draw(ctx, grect_inset(bounds, GEdgeInsets(RADAR_SNOOZE_INSET)), RADAR_TICK_COLOR);
        MEMORY_LOG_HEAP("radar_update:exit");
        return;
    }

    // Zero-init: missing persist keys (fresh install) leave the buffers untouched.
    uint8_t exact_tenths[RADAR_NUM_SLOTS] = {0};
    uint8_t area_tenths[RADAR_NUM_SLOTS] = {0};
    persist_get_rain_radar_trend(exact_tenths, RADAR_NUM_SLOTS);
    persist_get_rain_radar_trend_area(area_tenths, RADAR_NUM_SLOTS);

    // Chart outer is the bar plot area. The axis strip above it hosts
    // hour labels and the tick row that extends upward from chart.content.
    const GRect outer = GRect(bounds.origin.x,
                              bounds.origin.y + RADAR_AXIS_H,
                              bounds.size.w,
                              bounds.size.h - RADAR_AXIS_H);
    const ChartGeometry chart = chart_draw_frame(ctx, RADAR_CHART, outer);

    draw_radar_axis(ctx, outer, chart);
    draw_radar_area_bars(ctx, chart.content, chart.slots, area_tenths, exact_tenths);
    draw_radar_exact_bars(ctx, chart.content, chart.slots, exact_tenths);

    MEMORY_LOG_HEAP("radar_update:exit");
}

void rain_radar_layer_create(Layer *parent, GRect frame) {
    s_radar_layer = layer_create(frame);
    layer_set_update_proc(s_radar_layer, radar_or_snooze_update_proc);
    layer_set_hidden(s_radar_layer, true);  // calendar wins by default until toggle wiring lands
    layer_add_child(parent, s_radar_layer);
    MEMORY_LOG_HEAP("after_rain_radar_layer_create");
}

void rain_radar_layer_refresh(void) {
    layer_mark_dirty(s_radar_layer);
}

bool rain_radar_layer_tick(time_t now) {
    const time_t start = persist_get_rain_radar_start();
    if (start <= 0) {
        return false;  // no radar window to advance
    }
    if (!connection_service_peek_pebble_app_connection()) {
        return false;  // Bluetooth down: freeze the last real window
    }

    int interval_min = (g_config && g_config->fetch_interval_min > 0)
                     ? g_config->fetch_interval_min : 30;
    const time_t interval_sec = (time_t)interval_min * 60;

    // PKJS fetches on an aligned grid (index.js shouldFetch): the boundary that
    // should have delivered the current frame is floor(now/interval)*interval.
    // If the persisted window already starts there the real fetch landed, and
    // between grid boundaries no fetch was due — so we hold. The watch only
    // stands in for a fetch PKJS *skipped* (deduped), and a skip means PKJS
    // already validated the freshly-revealed tail slots are dry, so zero-padding
    // them on advance is correct.
    const time_t grid = (now / interval_sec) * interval_sec;
    if (start >= grid) {
        return false;  // current grid fetch already applied; no boundary to cover
    }
    // Grace past the boundary for PKJS to deliver before we stand in. Anchored to
    // the grid (not arrival time), so the cadence never drifts or compounds.
    if (now < grid + RADAR_ADVANCE_BUFFER_SECONDS) {
        return false;
    }

    // grid and start are both interval-grid (hence 5-min) aligned, so this is a
    // whole slot count: interval/5 slots per skipped fetch.
    int count = (int)((grid - start) / RADAR_SLOT_SECONDS);
    if (count <= 0) {
        return false;  // start not slot-aligned (corrupt persist) — don't shear
    }
    if (count > RADAR_NUM_SLOTS) {
        count = RADAR_NUM_SLOTS;  // window fully rolled to empty
    }

    uint8_t exact[RADAR_NUM_SLOTS] = {0};
    uint8_t area[RADAR_NUM_SLOTS] = {0};
    persist_get_rain_radar_trend(exact, RADAR_NUM_SLOTS);
    persist_get_rain_radar_trend_area(area, RADAR_NUM_SLOTS);

    uint8_t new_exact[RADAR_NUM_SLOTS] = {0};
    uint8_t new_area[RADAR_NUM_SLOTS] = {0};
    for (int i = 0; i + count < RADAR_NUM_SLOTS; i += 1) {
        new_exact[i] = exact[i + count];
        new_area[i] = area[i + count];
    }
    // Slots [RADAR_NUM_SLOTS - count .. ] stay zero (the zero-initialised tail).

    persist_set_rain_radar_trend(new_exact, RADAR_NUM_SLOTS);
    persist_set_rain_radar_trend_area(new_area, RADAR_NUM_SLOTS);
    persist_set_rain_radar_start(grid);
    rain_radar_layer_refresh();
    return true;
}

void rain_radar_layer_destroy(void) {
    layer_destroy(s_radar_layer);
}

Layer *rain_radar_layer_get_root(void) {
    return s_radar_layer;
}
