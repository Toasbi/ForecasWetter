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
#define HOUR_LABEL_MIN_SPACING 24 // Minimum horizontal spacing for hour labels
#define FORECAST_BOTTOM_PAD 10
#define EMERY_AXIS_LABEL_TOP 6
#define EMERY_AXIS_LABEL_H 14
#else
#define HOUR_LABEL_MIN_SPACING 20 // Minimum horizontal spacing for hour labels
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
//   144-bucket: 1 + 2 + 2 = 5 → 24*5 + 1 = 121 px after the left axis
//   200-bucket: 1 + 2 + 4 = 7 → 24*7 + 1 = 169 px after the left axis
#define FORECAST_TICK_W 1
#if defined(DISPLAY_WIDTH_200)
    #define FORECAST_BAR_W 4
    #define FORECAST_PAD   1
#elif defined(DISPLAY_WIDTH_144)
    #define FORECAST_BAR_W 2
    #define FORECAST_PAD   1
#endif

// Borders that previously lived as inline graphics_draw_line calls in
// draw_left_axis / draw_bottom_axis. Phase 3 will wrap this into the
// ChartConfig bundle alongside the slot grid and tick config.
// bottom.color is a placeholder — the update proc overrides it with
// render_spec.axis_color so the line tracks the night-overlay state.
static const GraphFrame FORECAST_FRAME = {
    .left   = { 1, GColorWhite },
    .right  = { 0, GColorClear },
    .top    = { 0, GColorClear },
    .bottom = { 1, GColorLightGray },
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
    bool   draw_night_overlay;
    GColor axis_color;          // overrides FORECAST_FRAME.bottom.color
} RenderSpec;

typedef struct
{
    GRect graph_bounds;
    GRect graph_plot_rect;
    int16_t w;
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
    const int raw = persist_get_num_entries();
    ds->num_entries = raw > MAX_FORECAST_ENTRIES ? MAX_FORECAST_ENTRIES : raw;
    ds->forecast_start = persist_get_forecast_start();
    persist_get_temp_trend(ds->temps, ds->num_entries);
    persist_get_precip_trend(ds->precip_probs, ds->num_entries);
    persist_get_rain_trend(ds->rain_tenths, ds->num_entries);
    int lo, hi;
    min_max(ds->temps, ds->num_entries, &lo, &hi);
    ds->temp_lo = lo;
    ds->temp_hi = hi;
}

static Layer *s_forecast_layer;
static int s_axis_left_w = LEFT_AXIS_GRAPH_INSET_DEFAULT;
static int s_label_strip_w = LEFT_AXIS_LABEL_STRIP_MIN_W;
static char s_buffer_lo[12];
static char s_buffer_hi[12];
static GPoint s_points_temp[MAX_FORECAST_ENTRIES];
static GPoint s_points_precip[MAX_FORECAST_ENTRIES + 2];
static GPath s_path_precip_area_under;
static GPath s_path_precip_top;
static GPath s_path_temp;

static RenderSpec make_render_spec()
{
    const bool night = g_config->day_night_shading;
    // Match NIGHT_HATCH_COLOR when shading is on so the bottom axis reads as
    // part of the night region instead of competing with it.
    const GColor axis_color = night
        ? PBL_IF_COLOR_ELSE(GColorDarkGray, GColorWhite)
        : PBL_IF_COLOR_ELSE(GColorOrange,   GColorWhite);
    return (RenderSpec){
        .draw_night_overlay = night,
        .axis_color         = axis_color,
    };
}

static ForecastLayout compute_layout(GRect bounds)
{
    ForecastLayout layout;
    layout.graph_bounds = GRect(s_axis_left_w, 0,
                                bounds.size.w - s_axis_left_w,
                                bounds.size.h - FORECAST_BOTTOM_PAD);
    layout.graph_plot_rect = GRect(layout.graph_bounds.origin.x, 0,
                                   layout.graph_bounds.size.w,
                                   layout.graph_bounds.size.h - BOTTOM_AXIS_H);
    layout.w = layout.graph_bounds.size.w;
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

static void draw_rain_bars(GContext *ctx, GRect plot_rect, SlotGeometry slots,
                           const uint8_t *rain_tenths) {
    rain_bars_draw(ctx, plot_rect, slots, rain_tenths);
}

static void draw_precip_area(GContext *ctx, GRect graph_bounds, int h,
                             SlotGeometry slots, const uint8_t *precips) {
    const int grid_right = slot_geometry_tick_x(slots, slots.num_slots, graph_bounds.origin.x);
    for (int i = 0; i < slots.num_slots; ++i) {
        const int tick_x = slot_geometry_tick_x(slots, i, graph_bounds.origin.x);
        const int precip = precips[i];
        const int precip_h = (float) precip / 100.0 * (h - BOTTOM_AXIS_H);
        s_points_precip[i] = GPoint(tick_x, h - BOTTOM_AXIS_H - precip_h);
    }
    s_points_precip[slots.num_slots]     = GPoint(grid_right, h - BOTTOM_AXIS_H);
    s_points_precip[slots.num_slots + 1] = GPoint(graph_bounds.origin.x, h - BOTTOM_AXIS_H);

    s_path_precip_area_under.num_points = slots.num_slots + 2;
    s_path_precip_area_under.points = s_points_precip;
    graphics_context_set_fill_color(ctx, PRECIP_FILL_COLOR);
    gpath_draw_filled(ctx, &s_path_precip_area_under);
}

static void draw_precip_top_line(GContext *ctx, int num_entries) {
    s_path_precip_top.num_points = num_entries;
    s_path_precip_top.points = s_points_precip;
    graphics_context_set_stroke_color(ctx, GColorPictonBlue);
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline_open(ctx, &s_path_precip_top);
}

static void draw_left_axis(GContext *ctx, int h) {
    // Mask anything drawn into the label strip. The vertical axis line
    // itself is painted by graph_frame_draw(FORECAST_FRAME) earlier in
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

static void draw_bottom_axis(GContext *ctx, int h, GRect graph_bounds,
                             SlotGeometry slots, struct tm *forecast_start_local) {
    // Horizontal axis line is now painted by graph_frame_draw(FORECAST_FRAME)
    // earlier in the update proc; this function owns only ticks and labels.

    const int entries_per_label =
        ((float)HOUR_LABEL_MIN_SPACING + (slots.pitch - 1)) / slots.pitch;

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_context_set_stroke_color(ctx, GColorLightGray);

#ifdef PBL_PLATFORM_EMERY
    for (int i = 0; i < slots.num_slots; ++i) {
        const int tick_x = slot_geometry_tick_x(slots, i, graph_bounds.origin.x);
        const bool is_label_tick = (i % entries_per_label) == 0;
        const GColor tick_color = is_label_tick ? GColorLightGray : GColorDarkGray;
        graphics_context_set_stroke_width(ctx, 1);
        graphics_context_set_stroke_color(ctx, tick_color);
        graphics_draw_line(ctx,
                           GPoint(tick_x, h - BOTTOM_AXIS_H - 0),
                           GPoint(tick_x, h - BOTTOM_AXIS_H + (is_label_tick ? 6 : 4)));
    }
    for (int label_i = 0; label_i < slots.num_slots; label_i += entries_per_label) {
        const int label_x = slot_geometry_tick_x(slots, label_i, graph_bounds.origin.x);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", config_axis_hour(forecast_start_local->tm_hour + label_i));
        const int label_y = h - BOTTOM_AXIS_H + EMERY_AXIS_LABEL_TOP;
        const int label_h = EMERY_AXIS_LABEL_H;
        graphics_draw_text(ctx, buf,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(label_x - 20, label_y, 40, label_h),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    }
#else
    for (int label_i = 0; label_i < slots.num_slots; label_i += entries_per_label) {
        const int label_x = slot_geometry_tick_x(slots, label_i, graph_bounds.origin.x);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", config_axis_hour(forecast_start_local->tm_hour + label_i));
        const int label_y = h - BOTTOM_AXIS_H - BOTTOM_AXIS_FONT_OFFSET;
        const int label_h = BOTTOM_AXIS_H;
        graphics_draw_text(ctx, buf,
                           fonts_get_system_font(FONT_KEY_GOTHIC_14),
                           GRect(label_x - 20, label_y, 40, label_h),
                           GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);

        const int next_label_i = label_i + entries_per_label;
        const int midpoint_i = label_i + entries_per_label / 2;
        if (midpoint_i > label_i && midpoint_i < next_label_i && midpoint_i < slots.num_slots) {
            const int tick_x = slot_geometry_tick_x(slots, midpoint_i, graph_bounds.origin.x);
            graphics_draw_line(ctx,
                               GPoint(tick_x, h - BOTTOM_AXIS_H - 0),
                               GPoint(tick_x, h - BOTTOM_AXIS_H + 4));
        }
    }
#endif
}

static void draw_temp_line(GContext *ctx, GRect graph_bounds, int h,
                           SlotGeometry slots, const int16_t *temps, int lo, int hi) {
    const int temp_plot_h = h - MARGIN_TEMP_H * 2 - BOTTOM_AXIS_H;
    const int range = hi - lo;
    const int range_safe = range > 0 ? range : 1;

    for (int i = 0; i < slots.num_slots; ++i) {
        const int tick_x = slot_geometry_tick_x(slots, i, graph_bounds.origin.x);
        int temp_h = temp_plot_h / 2;
        if (range > 0) {
            temp_h = (int)(((int32_t)(temps[i] - lo) * temp_plot_h) / range_safe);
        }
        s_points_temp[i] = GPoint(tick_x, h - temp_h - MARGIN_TEMP_H - BOTTOM_AXIS_H);
    }

    s_path_temp.num_points = slots.num_slots;
    s_path_temp.points = s_points_temp;
    graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorWhite));
    graphics_context_set_stroke_width(ctx, 3);
    gpath_draw_outline_open(ctx, &s_path_temp);
}

static void forecast_update_proc(Layer *layer, GContext *ctx)
{
    MEMORY_LOG_HEAP("forecast_update:enter");
    GRect bounds = layer_get_bounds(layer);
    RenderSpec render_spec = make_render_spec();
    ForecastLayout layout = compute_layout(bounds);
    GRect graph_bounds = layout.graph_bounds;
    GRect graph_plot_rect = layout.graph_plot_rect;
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
    APP_LOG(APP_LOG_LEVEL_DEBUG, "rain_tenths[0..7]=%u,%u,%u,%u,%u,%u,%u,%u",
            ds.rain_tenths[0], ds.rain_tenths[1], ds.rain_tenths[2], ds.rain_tenths[3],
            ds.rain_tenths[4], ds.rain_tenths[5], ds.rain_tenths[6], ds.rain_tenths[7]);

    NightSegments night_segments = {0};
    if (render_spec.draw_night_overlay)
    {
        night_segments = compute_night_segments(forecast_start, forecast_end);
    }

    const SlotGeometry slots = slot_geometry(ds.num_entries,
                                              FORECAST_TICK_W,
                                              FORECAST_PAD,
                                              FORECAST_BAR_W);

    draw_precip_area(ctx, graph_bounds, h, slots, ds.precip_probs);
    if (render_spec.draw_night_overlay)
    {
        draw_night_shading_under(ctx, graph_plot_rect, forecast_start, forecast_end,
                                 &night_segments, s_points_precip, ds.num_entries);
    }
    draw_rain_bars(ctx, graph_plot_rect, slots, ds.rain_tenths);
    if (render_spec.draw_night_overlay)
    {
        draw_night_shading_over(ctx, graph_plot_rect, forecast_start, forecast_end,
                                &night_segments);
    }
    draw_precip_top_line(ctx, ds.num_entries);
    draw_temp_line(ctx, graph_bounds, h, slots, ds.temps, ds.temp_lo, ds.temp_hi);

    // Frame: left + bottom axis lines. Outer spans the grid columns
    // (graph_bounds.origin.x .. grid_right) and the plot height down to
    // axis_y. The mask + temp-range labels in draw_left_axis sit to the
    // left of this rect and stay untouched. Bottom-axis colour is the
    // one runtime-varying frame attribute (orange normally, darker when
    // the night overlay is on, white on B&W).
    const int16_t axis_y = h - BOTTOM_AXIS_H;
    const int16_t grid_right = slot_geometry_tick_x(slots, slots.num_slots,
                                                     graph_bounds.origin.x);
    const GRect frame_outer = GRect(graph_bounds.origin.x, 0,
                                     grid_right - graph_bounds.origin.x + 1,
                                     axis_y + 1);
    GraphFrame forecast_frame = FORECAST_FRAME;
    forecast_frame.bottom.color = render_spec.axis_color;
    graph_frame_draw(ctx, forecast_frame, frame_outer);

    draw_bottom_axis(ctx, h, graph_bounds, slots, forecast_start_local);
    draw_left_axis(ctx, h);
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
    snprintf(s_buffer_hi, sizeof(s_buffer_hi), "%d", config_localize_temp(persist_get_temp_hi()));
    snprintf(s_buffer_lo, sizeof(s_buffer_lo), "%d", config_localize_temp(persist_get_temp_lo()));

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
#ifdef FCW2_ENABLE_MEMORY_LOGGING
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
