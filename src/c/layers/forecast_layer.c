#include <string.h>

#include "forecast_layer.h"
#include "c/appendix/persist.h"
#include "c/appendix/math.h"
#include "c/appendix/config.h"
#include "c/appendix/memory_log.h"
#include "c/appendix/rain_tier.h"
#include "c/appendix/hatch.h"
#include "c/appendix/slot_geometry.h"
#include "c/appendix/display_width.h"
#include "c/appendix/chart.h"

#define LEFT_AXIS_LABEL_STRIP_MIN_W 15
#define LEFT_AXIS_LABEL_TO_GRAPH_GAP 2
#define LEFT_AXIS_GRAPH_INSET_DEFAULT (LEFT_AXIS_LABEL_STRIP_MIN_W + LEFT_AXIS_LABEL_TO_GRAPH_GAP)
#define TEMP_LABEL_PAD 2
#define TEMP_LABEL_H 20
#define TEMP_LABEL_MEASURE_BOX_W 200
#define TEMP_LABEL_MEASURE_BOX_H 40
#define BOTTOM_AXIS_FONT_OFFSET 4 // Adjustment for whitespace at top of font
#define BOTTOM_AXIS_H 10          // Height of the bottom axis (hour labels)
#define MARGIN_TEMP_H 7           // Height of margins for the temperature plot
// emery: reserve extra bottom space for larger hour labels and tick marks.
#ifdef PBL_PLATFORM_EMERY
#define FORECAST_BOTTOM_PAD 10
#define EMERY_AXIS_LABEL_TOP 6
#define EMERY_AXIS_LABEL_H 14
#else
#define FORECAST_BOTTOM_PAD 0
#endif
#define NIGHT_HATCH_SPACING PBL_IF_COLOR_ELSE(6, 7)
#define NIGHT_HATCH_COLOR GColorDarkGray
#define PRECIP_FILL_COLOR PBL_IF_COLOR_ELSE(GColorCobaltBlue, GColorLightGray)
#define NIGHT_PRECIP_FILL_COLOR PBL_IF_COLOR_ELSE(GColorDukeBlue, GColorLightGray)
#define NIGHT_HATCH_COLOR_PRECIP PBL_IF_COLOR_ELSE(GColorBlue, GColorWhite)
#define NIGHT_BOUNDARY_COLOR PBL_IF_COLOR_ELSE(GColorDarkGray, GColorLightGray)
#define NIGHT_BOUNDARY_COLOR_PRECIP PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorWhite)
#define FORECAST_STEP_SECONDS (60 * 60)
#define DAY_SECONDS (24 * 60 * 60)
#define MAX_FORECAST_ENTRIES 24

// Slot grid bar dimensions, bucketed by display width.
// pitch = tick_w + 2*pad + bar_w
//   144-bucket: 1 + 2 + 4 = 7 → 24*7 + 1 = 169 px after the left axis
//                              (127 px available → overflows by 42, rightmost slots clip)
//   200-bucket: 1 + 2 + 5 = 8 → 24*8 + 1 = 193 px after the left axis
//                              (181 px available on emery → overflows by 12, rightmost slots clip)
#if defined(DISPLAY_WIDTH_200)
    #define FORECAST_BAR_W 5
    #define FORECAST_PAD   1
#elif defined(DISPLAY_WIDTH_144)
    #define FORECAST_BAR_W 4
    #define FORECAST_PAD   1
#endif

// Chart config: frame + ticks + slots in one block. Two variants because
// the axis colour tracks the night-overlay state — orange (or white on
// B&W) normally, darker grey under night shading so the axis reads as
// part of the night region instead of competing with it. Left and
// bottom share one colour per variant. Ticks and slots are identical
// between variants; only the frame swaps at draw time.
#define FORECAST_AXIS_COLOR_DAY    PBL_IF_COLOR_ELSE(GColorOrange,   GColorWhite)
#define FORECAST_AXIS_COLOR_NIGHT  PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)

// Tick rows: a label every big_every (3) slots. Emery draws every slot
// (small ticks between labels); non-emery only draws the midpoint tick
// between labels.
#ifdef PBL_PLATFORM_EMERY
    #define FORECAST_TICK_SMALL_COLOR  GColorDarkGray
#else
    #define FORECAST_TICK_SMALL_COLOR  GColorLightGray
#endif

static const ChartDef FORECAST_DEF = {
    .num_slots = MAX_FORECAST_ENTRIES,
    .tick_w    = 1,
    .bar_pad   = FORECAST_PAD,
    .bar_w     = FORECAST_BAR_W,
    .inset_left = 1, .inset_bottom = 1,   // 1px left + bottom border rows
};

// Tick style for the bottom axis (kinds come from the per-slot data)
static const TickSide FORECAST_TICK_STYLE = {
    .length     = 4,  .color     = FORECAST_TICK_SMALL_COLOR,
    .big_length = 6,  .big_color = GColorLightGray,
};

typedef struct
{
    time_t start;
    time_t end;
} NightSegment;

typedef struct
{
    int count;
    NightSegment segments[3];
} NightSegments;

typedef struct
{
    time_t timestamp;
    int type; // 0 = sunrise, 1 = sunset
} SunEvent;

typedef struct
{
    bool draw_night_overlay;
} RenderSpec;

typedef struct
{
    GRect   graph_bounds;
    int16_t h;
} ForecastLayout;

typedef struct {
    int num_entries;          // clamped to MAX_FORECAST_ENTRIES
    time_t forecast_start;
    int16_t temps[MAX_FORECAST_ENTRIES];
    uint8_t precip_probs[MAX_FORECAST_ENTRIES];
    uint8_t rain_tenths[MAX_FORECAST_ENTRIES];
    int temp_lo;
    int temp_hi;
} ForecastDataset;

static void load_dataset(ForecastDataset *ds) {
    // No demo data is seeded anymore, so missing persist keys must leave the
    // dataset zeroed instead of reading uninitialized stack memory.
    memset(ds, 0, sizeof(*ds));
    const int raw = persist_get_num_entries();
    ds->num_entries = raw > MAX_FORECAST_ENTRIES ? MAX_FORECAST_ENTRIES : (raw < 0 ? 0 : raw);
    ds->forecast_start = persist_get_forecast_start();
    if (ds->num_entries > 0) {
        persist_get_temp_trend(ds->temps, ds->num_entries);
        persist_get_precip_trend(ds->precip_probs, ds->num_entries);
        persist_get_rain_trend(ds->rain_tenths, ds->num_entries);
        min_max(ds->temps, ds->num_entries, &ds->temp_lo, &ds->temp_hi);
    }
}

static Layer *s_forecast_layer;
static int s_axis_left_w = LEFT_AXIS_GRAPH_INSET_DEFAULT;
static int s_label_strip_w = LEFT_AXIS_LABEL_STRIP_MIN_W;
static char s_buffer_lo[12];
static char s_buffer_hi[12];

static RenderSpec make_render_spec()
{
    return (RenderSpec){
        .draw_night_overlay = g_config->day_night_shading,
    };
}

static ForecastLayout compute_layout(GRect bounds)
{
    ForecastLayout layout;
    layout.graph_bounds = GRect(s_axis_left_w, 0,
                                bounds.size.w - s_axis_left_w,
                                bounds.size.h - FORECAST_BOTTOM_PAD);
    layout.h = layout.graph_bounds.size.h;
    return layout;
}

static void night_segments_add(NightSegments *night_segments, time_t start, time_t end)
{
    if (night_segments->count >= (int)(sizeof(night_segments->segments) / sizeof(night_segments->segments[0])) || end <= start)
    {
        return;
    }

    night_segments->segments[night_segments->count].start = start;
    night_segments->segments[night_segments->count].end = end;
    night_segments->count += 1;
}

static bool get_valid_sun_events(time_t sun_event_times[2], int *sun_event_start_type)
{
    const int num_sun_events = 2;
    const int sun_events_read = persist_get_sun_event_times(sun_event_times, num_sun_events);
    if (sun_events_read < (int)(sizeof(time_t) * num_sun_events))
    {
        return false;
    }

    const int start_type = persist_get_sun_event_start_type();
    if ((start_type != 0 && start_type != 1) || sun_event_times[0] <= 0 || sun_event_times[1] <= 0 || sun_event_times[1] <= sun_event_times[0])
    {
        return false;
    }

    if (sun_event_start_type)
    {
        *sun_event_start_type = start_type;
    }

    return true;
}

static NightSegments compute_night_segments(time_t graph_start, time_t graph_end)
{
    NightSegments night_segments = {0};

    if (graph_end <= graph_start)
    {
        return night_segments;
    }

    time_t sun_event_times[2] = {0, 0};
    int sun_event_start_type;
    if (!get_valid_sun_events(sun_event_times, &sun_event_start_type))
    {
        return night_segments;
    }

    SunEvent events[6];
    int event_count = 0;

    for (int day_offset = -1; day_offset <= 1; ++day_offset)
    {
        const time_t offset_seconds = (time_t)day_offset * DAY_SECONDS;
        events[event_count++] = (SunEvent){
            .timestamp = sun_event_times[0] + offset_seconds,
            .type = sun_event_start_type};
        events[event_count++] = (SunEvent){
            .timestamp = sun_event_times[1] + offset_seconds,
            .type = 1 - sun_event_start_type};
    }

    for (int i = 1; i < event_count; ++i)
    {
        SunEvent current = events[i];
        int j = i - 1;
        while (j >= 0 && events[j].timestamp > current.timestamp)
        {
            events[j + 1] = events[j];
            --j;
        }
        events[j + 1] = current;
    }

    for (int i = 0; i < event_count - 1; ++i)
    {
        const SunEvent event_start = events[i];
        const SunEvent event_end = events[i + 1];
        if (event_start.type != 1 || event_end.type != 0)
        {
            continue;
        }

        night_segments_add(&night_segments, event_start.timestamp, event_end.timestamp);
    }

    return night_segments;
}

static int16_t graph_x_for_time(time_t timestamp, time_t graph_start, time_t graph_end, GRect graph_plot_rect)
{
    const int16_t graph_left = graph_plot_rect.origin.x;
    const int16_t graph_right = graph_plot_rect.origin.x + graph_plot_rect.size.w;

    if (timestamp <= graph_start)
    {
        return graph_left;
    }
    if (timestamp >= graph_end)
    {
        return graph_right;
    }

    const int64_t elapsed = (int64_t)timestamp - graph_start;
    const int64_t total = (int64_t)graph_end - graph_start;
    return graph_left + (int16_t)((elapsed * graph_plot_rect.size.w) / total);
}


static void draw_night_regions(GContext *ctx, GRect graph_plot_rect, time_t graph_start, time_t graph_end,
                               const NightSegments *night_segments)
{
    if (!night_segments || night_segments->count == 0)
    {
        return;
    }

    const int16_t graph_left = graph_plot_rect.origin.x;
    const int16_t graph_right = graph_plot_rect.origin.x + graph_plot_rect.size.w;

    const int16_t hatch_spacing = NIGHT_HATCH_SPACING;
    const bool is_color = PBL_IF_COLOR_ELSE(true, false);
    const GColor hatch_color = is_color ? NIGHT_HATCH_COLOR : GColorWhite;

    for (int i = 0; i < night_segments->count; ++i)
    {
        int16_t x0 = graph_x_for_time(night_segments->segments[i].start, graph_start, graph_end, graph_plot_rect);
        int16_t x1 = graph_x_for_time(night_segments->segments[i].end, graph_start, graph_end, graph_plot_rect);

        if (x0 < graph_left)
        {
            x0 = graph_left;
        }
        if (x1 > graph_right)
        {
            x1 = graph_right;
        }
        if (x1 <= x0)
        {
            continue;
        }

        GRect night_rect = GRect(x0, graph_plot_rect.origin.y, x1 - x0, graph_plot_rect.size.h);
        hatch_fill_rect(ctx, night_rect, hatch_color, hatch_spacing);
    }
}

static int16_t precip_top_y_for_x(const GPoint *points_precip, int num_entries, int16_t x)
{
    if (x <= points_precip[0].x)
    {
        return points_precip[0].y;
    }

    for (int i = 0; i < num_entries - 1; ++i)
    {
        const int16_t x0 = points_precip[i].x;
        const int16_t y0 = points_precip[i].y;
        const int16_t x1 = points_precip[i + 1].x;
        const int16_t y1 = points_precip[i + 1].y;

        if (x > x1)
        {
            continue;
        }

        if (x1 == x0)
        {
            return y0 < y1 ? y0 : y1;
        }

        return y0 + (int16_t)(((int32_t)(y1 - y0) * (x - x0)) / (x1 - x0));
    }

    return points_precip[num_entries - 1].y;
}

static int16_t clamped_precip_top_y_for_x(GRect graph_plot_rect,
                                          const GPoint *points_precip, int num_entries, int16_t x)
{
    const int16_t y_top_limit = graph_plot_rect.origin.y;
    int16_t precip_y = precip_top_y_for_x(points_precip, num_entries, x);
    if (precip_y < y_top_limit)
    {
        precip_y = y_top_limit;
    }

    return precip_y;
}

static void draw_night_hatch_over_precip(GContext *ctx, GRect graph_plot_rect, time_t graph_start, time_t graph_end,
                                         const NightSegments *night_segments,
                                         const GPoint *points_precip, int num_entries)
{
    if (!night_segments || night_segments->count == 0)
    {
        return;
    }

    const int16_t graph_left = graph_plot_rect.origin.x;
    const int16_t graph_right = graph_plot_rect.origin.x + graph_plot_rect.size.w;
    const int16_t y_bottom_exclusive = graph_plot_rect.origin.y + graph_plot_rect.size.h;
    const int16_t y_bottom_inclusive = y_bottom_exclusive - 1;
    const int16_t hatch_spacing = NIGHT_HATCH_SPACING;
    const bool is_color = PBL_IF_COLOR_ELSE(true, false);

    for (int i = 0; i < night_segments->count; ++i)
    {
        int16_t x0 = graph_x_for_time(night_segments->segments[i].start, graph_start, graph_end, graph_plot_rect);
        int16_t x1 = graph_x_for_time(night_segments->segments[i].end, graph_start, graph_end, graph_plot_rect);

        if (x0 < graph_left)
        {
            x0 = graph_left;
        }
        if (x1 > graph_right)
        {
            x1 = graph_right;
        }
        if (x1 <= x0)
        {
            continue;
        }

        if (is_color)
        {
            graphics_context_set_stroke_color(ctx, NIGHT_PRECIP_FILL_COLOR);
            for (int16_t x = x0; x < x1; ++x)
            {
                const int16_t precip_y = clamped_precip_top_y_for_x(graph_plot_rect, points_precip, num_entries, x);
                if (precip_y <= y_bottom_inclusive)
                {
                    graphics_draw_line(ctx, GPoint(x, precip_y), GPoint(x, y_bottom_inclusive));
                }
            }
        }

        const GColor hatch_color = is_color ? NIGHT_HATCH_COLOR_PRECIP : GColorWhite;
        for (int16_t x = x0; x < x1; ++x)
        {
            const int16_t precip_y = clamped_precip_top_y_for_x(graph_plot_rect, points_precip, num_entries, x);
            hatch_fill_rect(ctx, GRect(x, precip_y, 1, y_bottom_exclusive - precip_y), hatch_color, hatch_spacing);
        }
    }
}

static void draw_night_boundaries(GContext *ctx, GRect graph_plot_rect, time_t graph_start, time_t graph_end,
                                  const NightSegments *night_segments)
{
    if (!night_segments || night_segments->count == 0)
    {
        return;
    }

    graphics_context_set_stroke_color(ctx, NIGHT_BOUNDARY_COLOR);
    graphics_context_set_stroke_width(ctx, 1);

    const int16_t y0 = graph_plot_rect.origin.y;
    const int16_t y1 = graph_plot_rect.origin.y + graph_plot_rect.size.h - 1;
    for (int i = 0; i < night_segments->count; ++i)
    {
        const time_t segment_start = night_segments->segments[i].start;
        const time_t segment_end = night_segments->segments[i].end;

        if (segment_start > graph_start && segment_start < graph_end)
        {
            const int16_t start_x = graph_x_for_time(segment_start, graph_start, graph_end, graph_plot_rect);
            graphics_draw_line(ctx, GPoint(start_x, y0), GPoint(start_x, y1));
        }

        if (segment_end > graph_start && segment_end < graph_end)
        {
            const int16_t end_x = graph_x_for_time(segment_end, graph_start, graph_end, graph_plot_rect);
            graphics_draw_line(ctx, GPoint(end_x, y0), GPoint(end_x, y1));
        }
    }
}

static void draw_night_boundaries_over_precip(GContext *ctx, GRect graph_plot_rect, time_t graph_start, time_t graph_end,
                                               const NightSegments *night_segments,
                                               const GPoint *points_precip, int num_entries)
{
    if (!night_segments || night_segments->count == 0)
    {
        return;
    }

    graphics_context_set_stroke_color(ctx, NIGHT_BOUNDARY_COLOR_PRECIP);
    graphics_context_set_stroke_width(ctx, 1);

    const int16_t y_bottom = graph_plot_rect.origin.y + graph_plot_rect.size.h - 1;
    for (int i = 0; i < night_segments->count; ++i)
    {
        const time_t segment_start = night_segments->segments[i].start;
        const time_t segment_end = night_segments->segments[i].end;

        if (segment_start > graph_start && segment_start < graph_end)
        {
            const int16_t start_x = graph_x_for_time(segment_start, graph_start, graph_end, graph_plot_rect);
            const int16_t start_precip_y = clamped_precip_top_y_for_x(graph_plot_rect, points_precip, num_entries, start_x);
            graphics_draw_line(ctx, GPoint(start_x, start_precip_y), GPoint(start_x, y_bottom));
        }

        if (segment_end > graph_start && segment_end < graph_end)
        {
            const int16_t end_x = graph_x_for_time(segment_end, graph_start, graph_end, graph_plot_rect);
            const int16_t end_precip_y = clamped_precip_top_y_for_x(graph_plot_rect, points_precip, num_entries, end_x);
            graphics_draw_line(ctx, GPoint(end_x, end_precip_y), GPoint(end_x, y_bottom));
        }
    }
}

static GSize temp_label_string_size(const char *text);

static void draw_night_shading_under(GContext *ctx, GRect graph_plot_rect,
                                     time_t forecast_start, time_t forecast_end,
                                     const NightSegments *night_segments,
                                     const GPoint *points_precip, int num_entries) {
    draw_night_hatch_over_precip(ctx, graph_plot_rect, forecast_start, forecast_end,
                                 night_segments, points_precip, num_entries);
    draw_night_boundaries_over_precip(ctx, graph_plot_rect, forecast_start, forecast_end,
                                       night_segments, points_precip, num_entries);
}

static void draw_night_shading_over(GContext *ctx, GRect graph_plot_rect,
                                    time_t forecast_start, time_t forecast_end,
                                    const NightSegments *night_segments) {
    draw_night_regions(ctx, graph_plot_rect, forecast_start, forecast_end, night_segments);
    draw_night_boundaries(ctx, graph_plot_rect, forecast_start, forecast_end, night_segments);
}


static void draw_left_axis(GContext *ctx, int h) {
    // Mask anything drawn into the label strip. The vertical axis line
    // itself is painted by graph_frame_draw(cfg->frame, ...) earlier in
    // the update proc.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(0, 0, s_axis_left_w, h - BOTTOM_AXIS_H), 0, GCornerNone);

    graphics_context_set_text_color(ctx, GColorWhite);
    GSize hi_size = temp_label_string_size(s_buffer_hi);
    GSize lo_size = temp_label_string_size(s_buffer_lo);
#ifdef PBL_PLATFORM_EMERY
    const int16_t axis_y = h - BOTTOM_AXIS_H;
    const int hi_y = 0;
    const int lo_y = axis_y - lo_size.h - 2;
#else
    const int hi_y = -3;
    const int lo_y = 22;
#endif
    graphics_draw_text(ctx, s_buffer_hi,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(0, hi_y, s_label_strip_w, hi_size.h),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
    graphics_draw_text(ctx, s_buffer_lo,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(0, lo_y, s_label_strip_w, lo_size.h),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
}


// Bottom-axis slots: hour digit every 3rd slot; small screens mark the
// midpoint between digits with a small tick, emery ticks every slot and
// keeps a big tick under each digit.
static void forecast_fill_axis_slots(ChartAxisSlot *slots, int num_slots,
                                     int origin_x, int pitch, int visible_w,
                                     const struct tm *start_local) {
    for (int i = 0; i < num_slots; ++i) {
        slots[i].label[0] = '\0';
        slots[i].tick     = TICK_NONE;
        if ((i % 3) != 0) {
#ifdef PBL_PLATFORM_EMERY
            // emery: a tick on every slot keeps the dense grid readable
            slots[i].tick = TICK_SMALL;
#else
            if ((i % 3) == 1) slots[i].tick = TICK_SMALL;  // midpoint marker
#endif
            continue;
        }
#ifdef PBL_PLATFORM_EMERY
        slots[i].tick = TICK_BIG;  // emery: digit slots keep their big tick
#endif
        const int hour = config_axis_hour(start_local->tm_hour + i);
#ifndef PBL_PLATFORM_EMERY
        // Two-digit labels sliced by the screen edge are omitted instead of
        // drawing half a number (was clip logic in the tick callback).
        if (hour >= 10 && (origin_x + i * pitch - 3) + 8 > visible_w) continue;
#endif
        snprintf(slots[i].label, sizeof(slots[i].label), "%d", hour);
    }
}

typedef struct {
    GRect                plot_rect;   // spans the full slot grid so night edges
                                      // land on their hour columns (stage-2 fix;
                                      // was the pre-phase-3 compat mapping)
    time_t               start, end;
    const NightSegments *night;
    const GPoint        *precip_pts;
    int                  count;
} NightLayerCtx;

static void night_under_layer(const ChartRender *r, void *user) {
    const NightLayerCtx *c = user;
    draw_night_shading_under(r->ctx, c->plot_rect, c->start, c->end,
                             c->night, c->precip_pts, c->count);
}

static void night_over_layer(const ChartRender *r, void *user) {
    const NightLayerCtx *c = user;
    draw_night_shading_over(r->ctx, c->plot_rect, c->start, c->end, c->night);
}

static void forecast_update_proc(Layer *layer, GContext *ctx)
{
    MEMORY_LOG_HEAP("forecast_update:enter");
    GRect bounds = layer_get_bounds(layer);
    RenderSpec render_spec = make_render_spec();
    ForecastLayout layout = compute_layout(bounds);
    GRect graph_bounds = layout.graph_bounds;
    int h = layout.h;

    ForecastDataset ds;
    load_dataset(&ds);
    MemoryHeapProbe redraw_probe = MEMORY_HEAP_PROBE_START("forecast_update");
    if (ds.num_entries < 2)
    {
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, bounds, 0, GCornerNone);
        MEMORY_LOG_HEAP("forecast_update:exit");
        return;
    }
    const time_t forecast_start = ds.forecast_start;
    const time_t forecast_end = forecast_start + (ds.num_entries - 1) * FORECAST_STEP_SECONDS;
    struct tm *forecast_start_local = localtime(&forecast_start);


    NightSegments night_segments = {0};
    if (render_spec.draw_night_overlay)
    {
        night_segments = compute_night_segments(forecast_start, forecast_end);
    }

    const bool night_on = render_spec.draw_night_overlay;
    const int16_t axis_y     = h - BOTTOM_AXIS_H;
    const int16_t grid_right = graph_bounds.origin.x
                             + ds.num_entries * chart_def_pitch(&FORECAST_DEF);
    const GRect outer = GRect(graph_bounds.origin.x, 0,
                              grid_right - graph_bounds.origin.x + 1,
                              axis_y + 1);

    // Per-redraw data prep + layer list. The scratch arrays are module-static
    // (not stack): aplite's small app stack overflows otherwise (PC=0/LR=0).
    // Safe — single layer instance, single-threaded, all recomputed each redraw.
    static int16_t precip_vals[MAX_FORECAST_ENTRIES];
    static int16_t rain_pm[MAX_FORECAST_ENTRIES];
    static GPoint  precip_pts[MAX_FORECAST_ENTRIES + 2];
    static ChartAxisSlot axis_slots[MAX_FORECAST_ENTRIES];
    for (int i = 0; i < ds.num_entries; ++i) {
        precip_vals[i] = ds.precip_probs[i];           // uint8 -> int16, raw %
    }
    rain_tier_fill_permille(ds.rain_tenths, rain_pm, ds.num_entries);
    forecast_fill_axis_slots(axis_slots, MAX_FORECAST_ENTRIES,
                             outer.origin.x, chart_def_pitch(&FORECAST_DEF),
                             bounds.size.w, forecast_start_local);

    NightLayerCtx night_ctx = {
        .plot_rect  = GRect(outer.origin.x, 0,
                            MAX_FORECAST_ENTRIES
                                * chart_def_pitch(&FORECAST_DEF)
                                + FORECAST_DEF.tick_w,
                            outer.size.h - 1),
        .start      = forecast_start,
        .end        = forecast_end,
        .night      = &night_segments,
        .precip_pts = precip_pts,
        .count      = ds.num_entries,
    };
    const GColor axis_color = night_on ? FORECAST_AXIS_COLOR_NIGHT
                                       : FORECAST_AXIS_COLOR_DAY;

    // Z-order = array order, bottom first. Frame after the data bands so it
    // overwrites curve/area pixels at the border columns.
    static ChartLayer layers[8];  // aplite: largest redraw array — must be static, not stack
    int n = 0;
    layers[n++] = (ChartLayer){ CHART_LAYER_AREA, .area = {
        .values = precip_vals, .export_points = precip_pts,
        .count = ds.num_entries, .lo = 0, .hi = 100,
        .fill_color = PRECIP_FILL_COLOR } };
    if (night_on) {
        layers[n++] = (ChartLayer){ CHART_LAYER_CUSTOM,
                                    .custom = { night_under_layer, &night_ctx } };
    }
    layers[n++] = (ChartLayer){ CHART_LAYER_BARS, .bars = {
        .values = rain_pm, .count = ds.num_entries, .lo = 0, .hi = 1000,
        .stops = RAIN_TIER_STOPS, .num_stops = RAIN_TIER_NUM_STOPS,
        .style = PBL_IF_COLOR_ELSE(BAR_SOLID, BAR_OUTLINED) } };
    if (night_on) {
        layers[n++] = (ChartLayer){ CHART_LAYER_CUSTOM,
                                    .custom = { night_over_layer, &night_ctx } };
    }
    layers[n++] = (ChartLayer){ CHART_LAYER_LINE, .line = {
        .points = precip_pts, .count = ds.num_entries,
        .color = GColorPictonBlue, .width = 1 } };
    layers[n++] = (ChartLayer){ CHART_LAYER_LINE, .line = {
        .values = ds.temps, .count = ds.num_entries,
        .lo = ds.temp_lo, .hi = ds.temp_hi, .inset_y = MARGIN_TEMP_H,
        .color = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite), .width = 3 } };
    layers[n++] = (ChartLayer){ CHART_LAYER_FRAME, .frame = { .frame = {
        .left   = { 1, axis_color },
        .bottom = { 1, axis_color } } } };
    layers[n++] = (ChartLayer){ CHART_LAYER_AXIS, .axis = {
        .side = GRAPH_SIDE_BOTTOM, .style = FORECAST_TICK_STYLE,
        .slots = axis_slots,
        .label_align = ALIGN_START, .tick_align = ALIGN_START } };
    chart_draw(ctx, &FORECAST_DEF, outer, layers, n);

    draw_left_axis(ctx, h);   // hi/lo temp strip: chart-adjacent chrome,
                              // not a chart layer (spec §4 engine boundary)
    MEMORY_HEAP_PROBE_LOG_MIN(&redraw_probe);
    MEMORY_LOG_HEAP("forecast_update:exit");
}

static int temp_label_string_width(const char *text)
{
    const GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    const GRect box = GRect(0, 0, TEMP_LABEL_MEASURE_BOX_W, TEMP_LABEL_MEASURE_BOX_H);
    const GSize sz = graphics_text_layout_get_content_size(text, font, box, GTextOverflowModeFill,
                                                           GTextAlignmentRight);
    return sz.w;
}

static GSize temp_label_string_size(const char *text)
{
    const GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18);
    const GRect box = GRect(0, 0, TEMP_LABEL_MEASURE_BOX_W, TEMP_LABEL_MEASURE_BOX_H);
    return graphics_text_layout_get_content_size(text, font, box, GTextOverflowModeFill,
                                                 GTextAlignmentRight);
}

static void text_labels_refresh()
{
    // Lo/hi are derived from the persisted trend rather than stored separately.
    int16_t temps[MAX_FORECAST_ENTRIES] = {0};
    const int raw = persist_get_num_entries();
    const int num_entries = raw > MAX_FORECAST_ENTRIES ? MAX_FORECAST_ENTRIES : (raw < 0 ? 0 : raw);
    int temp_lo = 0;
    int temp_hi = 0;
    if (num_entries > 0) {
        persist_get_temp_trend(temps, num_entries);
        min_max(temps, num_entries, &temp_lo, &temp_hi);
    }
    snprintf(s_buffer_hi, sizeof(s_buffer_hi), "%d", config_localize_temp(temp_hi));
    snprintf(s_buffer_lo, sizeof(s_buffer_lo), "%d", config_localize_temp(temp_lo));

    int content_w = temp_label_string_width(s_buffer_hi);
    const int w_lo = temp_label_string_width(s_buffer_lo);
    if (w_lo > content_w)
    {
        content_w = w_lo;
    }
    content_w += TEMP_LABEL_PAD;

    int label_strip_w = content_w;
    if (label_strip_w < LEFT_AXIS_LABEL_STRIP_MIN_W)
    {
        label_strip_w = LEFT_AXIS_LABEL_STRIP_MIN_W;
    }
    s_label_strip_w = label_strip_w;
    const int graph_inset_w = label_strip_w + LEFT_AXIS_LABEL_TO_GRAPH_GAP;

    if (graph_inset_w != s_axis_left_w)
    {
        s_axis_left_w = graph_inset_w;
    }
}

void forecast_layer_create(Layer *parent_layer, GRect frame)
{
    s_forecast_layer = layer_create(frame);

    // Fill the contents with values

    layer_set_update_proc(s_forecast_layer, forecast_update_proc);
    text_labels_refresh();

    // Add it as a child layer to the Window's root layer
    layer_add_child(parent_layer, s_forecast_layer);
    MEMORY_LOG_HEAP("after_forecast_layer_create");
}

void forecast_layer_refresh()
{
    text_labels_refresh();
    layer_mark_dirty(s_forecast_layer);
#ifdef WW_ENABLE_MEMORY_LOGGING
    APP_LOG(APP_LOG_LEVEL_DEBUG, "MEM|forecast_refresh|entries=%d|free=%lu|used=%lu",
            persist_get_num_entries(),
            (unsigned long)heap_bytes_free(),
            (unsigned long)heap_bytes_used());
#endif
}

void forecast_layer_destroy()
{
    MEMORY_LOG_HEAP("forecast_layer_destroy:before");
    layer_destroy(s_forecast_layer);
    MEMORY_LOG_HEAP("forecast_layer_destroy:after");
}
