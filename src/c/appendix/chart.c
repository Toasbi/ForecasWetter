#include "chart.h"

GRect graph_frame_content_rect(GraphFrame f, GRect outer) {
    return GRect(outer.origin.x + f.left.width,
                 outer.origin.y + f.top.width,
                 outer.size.w - f.left.width - f.right.width,
                 outer.size.h - f.top.width  - f.bottom.width);
}

void graph_frame_draw(GContext *ctx, GraphFrame f, GRect outer) {
    if (f.left.width > 0) {
        graphics_context_set_fill_color(ctx, f.left.color);
        graphics_fill_rect(ctx,
            GRect(outer.origin.x, outer.origin.y, f.left.width, outer.size.h),
            0, GCornerNone);
    }
    if (f.right.width > 0) {
        graphics_context_set_fill_color(ctx, f.right.color);
        graphics_fill_rect(ctx,
            GRect(outer.origin.x + outer.size.w - f.right.width,
                  outer.origin.y, f.right.width, outer.size.h),
            0, GCornerNone);
    }
    if (f.top.width > 0) {
        graphics_context_set_fill_color(ctx, f.top.color);
        graphics_fill_rect(ctx,
            GRect(outer.origin.x, outer.origin.y, outer.size.w, f.top.width),
            0, GCornerNone);
    }
    if (f.bottom.width > 0) {
        graphics_context_set_fill_color(ctx, f.bottom.color);
        graphics_fill_rect(ctx,
            GRect(outer.origin.x,
                  outer.origin.y + outer.size.h - f.bottom.width,
                  outer.size.w, f.bottom.width),
            0, GCornerNone);
    }
}

void tick_side_draw_at(GContext *ctx, GRect outer, GraphFrame frame,
                       GraphSide side, TickSide style, int idx, int tick_xy) {
    if (style.length == 0) return;

    const bool is_big = style.big_every > 0 && (idx % style.big_every) == 0;
    const int    len  = is_big ? style.big_length : style.length;
    const GColor col  = is_big ? style.big_color  : style.color;
    if (len <= 0) return;

    graphics_context_set_stroke_color(ctx, col);
    graphics_context_set_stroke_width(ctx, 1);

    // length is the offset passed to graphics_draw_line — a line from
    // edge to edge ± length spans `length + 1` pixels (inclusive). When
    // the side has a border, the edge IS the border row and the tick
    // overlaps it; when there's no border, the edge sits one row/col
    // outside `outer` so the tick can't bleed into the content.
    switch (side) {
        case GRAPH_SIDE_BOTTOM: {
            const int y0 = (frame.bottom.width > 0)
                ? (outer.origin.y + outer.size.h - 1)
                : (outer.origin.y + outer.size.h);
            graphics_draw_line(ctx, GPoint(tick_xy, y0),
                                    GPoint(tick_xy, y0 + len));
            break;
        }
        case GRAPH_SIDE_TOP: {
            const int y0 = (frame.top.width > 0)
                ? outer.origin.y
                : (outer.origin.y - 1);
            graphics_draw_line(ctx, GPoint(tick_xy, y0),
                                    GPoint(tick_xy, y0 - len));
            break;
        }
        case GRAPH_SIDE_LEFT: {
            const int x0 = (frame.left.width > 0)
                ? outer.origin.x
                : (outer.origin.x - 1);
            graphics_draw_line(ctx, GPoint(x0,       tick_xy),
                                    GPoint(x0 - len, tick_xy));
            break;
        }
        case GRAPH_SIDE_RIGHT: {
            const int x0 = (frame.right.width > 0)
                ? (outer.origin.x + outer.size.w - 1)
                : (outer.origin.x + outer.size.w);
            graphics_draw_line(ctx, GPoint(x0,       tick_xy),
                                    GPoint(x0 + len, tick_xy));
            break;
        }
    }
}

ChartGeometry chart_compute(ChartConfig cfg, GRect outer, int num_slots) {
    return (ChartGeometry){
        .content = graph_frame_content_rect(cfg.frame, outer),
        .slots   = slot_geometry(num_slots,
                                  cfg.ticks.tick_w,
                                  cfg.slots.pad,
                                  cfg.slots.bar_w),
    };
}

ChartGeometry chart_draw_frame(GContext *ctx, ChartConfig cfg, GRect outer) {
    graph_frame_draw(ctx, cfg.frame, outer);
    return chart_compute(cfg, outer, cfg.slots.num_slots);
}

// --- Engine v2 --------------------------------------------------------

static ChartGeometry chart_geometry(const ChartDef *def, GRect outer) {
    return (ChartGeometry){
        .anchor_x = outer.origin.x,
        .content  = GRect(outer.origin.x + def->inset_left,
                          outer.origin.y + def->inset_top,
                          outer.size.w - def->inset_left - def->inset_right,
                          outer.size.h - def->inset_top  - def->inset_bottom),
        .slots    = slot_geometry(def->num_slots, def->tick_w,
                                  def->bar_pad, def->bar_w),
    };
}

static inline int chart_clamp_count(const ChartRender *r, int count) {
    if (count > r->def->num_slots) return r->def->num_slots;
    return count;
}

// Stage-1 label placement constants — reproduce current pixels exactly.
// Stage 2 may simplify these into one per-side rule (spec §6).
#ifdef PBL_PLATFORM_EMERY
    // emery: digits sit in the reserved strip below the axis row
    #define CHART_LABEL_BOTTOM_DY   6
    #define CHART_LABEL_BOTTOM_H   14
    #define CHART_LABEL_NUDGE_X     0
#else
    #define CHART_LABEL_BOTTOM_DY  (-4)  // GOTHIC_14 top-whitespace pull-up
    #define CHART_LABEL_BOTTOM_H   10
    #define CHART_LABEL_NUDGE_X    (-3)  // small-screen left nudge (stage-2 candidate)
#endif
#define CHART_LABEL_TOP_RAISE 15
#define CHART_LABEL_TOP_H     14

static void chart_draw_tick(const ChartRender *r, GraphSide side,
                            int len, GColor color, int x) {
    if (len <= 0) return;
    graphics_context_set_stroke_color(r->ctx, color);
    graphics_context_set_stroke_width(r->ctx, 1);
    if (side == GRAPH_SIDE_BOTTOM) {
        const int y0 = r->outer.origin.y + r->outer.size.h - 1;
        graphics_draw_line(r->ctx, GPoint(x, y0), GPoint(x, y0 + len));
    } else {  // GRAPH_SIDE_TOP
        const int y0 = r->outer.origin.y - 1;
        graphics_draw_line(r->ctx, GPoint(x, y0), GPoint(x, y0 - len));
    }
}

static void chart_draw_axis_label(const ChartRender *r, GraphSide side,
                                  const char *text, GFont font, int x) {
    GRect box;
    if (side == GRAPH_SIDE_BOTTOM) {
        const int axis_y = r->outer.origin.y + r->outer.size.h - 1;
        box = GRect(x - 20 + CHART_LABEL_NUDGE_X,
                    axis_y + CHART_LABEL_BOTTOM_DY, 40, CHART_LABEL_BOTTOM_H);
    } else {
        box = GRect(x - 20, r->outer.origin.y - CHART_LABEL_TOP_RAISE,
                    40, CHART_LABEL_TOP_H);
    }
    graphics_draw_text(r->ctx, text, font, box,
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
}

static void chart_render_axis(const ChartRender *r, const ChartAxisLayer *a) {
    graphics_context_set_text_color(r->ctx, GColorWhite);
    const GFont font     = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    const int  mid_shift = r->geo.slots.pitch / 2;
    for (int i = 0; i < r->def->num_slots; ++i) {
        const ChartAxisSlot *s = &a->slots[i];
        const int base = chart_slot_tick_x(&r->geo, i);
        if (s->tick != TICK_NONE) {
            const bool big = (s->tick == TICK_BIG);
            chart_draw_tick(r, a->side,
                            big ? a->style.big_length : a->style.length,
                            big ? a->style.big_color  : a->style.color,
                            base + (a->tick_align == ALIGN_MIDDLE ? mid_shift : 0));
        }
        if (s->label[0] != '\0') {
            chart_draw_axis_label(r, a->side, s->label, font,
                                  base + (a->label_align == ALIGN_MIDDLE ? mid_shift : 0));
        }
    }
}

static int chart_scale_h(int v, int lo, int hi, int plot_h) {
    const int range = hi - lo;
    if (range <= 0) return 0;
    return (int)(((int32_t)(v - lo) * plot_h) / range);
}

static void chart_render_bars(const ChartRender *r, const ChartBarsLayer *b) {
    const GRect c          = r->geo.content;
    const int  plot_h      = c.size.h;
    const int  plot_bottom = c.origin.y + c.size.h;
    const int  count       = chart_clamp_count(r, b->count);
    if (plot_h <= 0 || b->num_stops < 1) return;

    for (int i = 0; i < count; ++i) {
        const int v = b->values[i];
        if (v <= b->lo) continue;
        int bar_h = chart_scale_h(v, b->lo, b->hi, plot_h);
        if (bar_h < 1) bar_h = 1;
        const int bar_x   = chart_slot_bar_x(&r->geo, i);
        const int bar_top = plot_bottom - bar_h;

        for (int k = 0; k < b->num_stops; ++k) {
            int seg_bottom = plot_bottom
                           - chart_scale_h(b->stops[k].from, b->lo, b->hi, plot_h);
            int seg_top = (k + 1 < b->num_stops)
                ? plot_bottom - chart_scale_h(b->stops[k + 1].from, b->lo, b->hi, plot_h)
                : bar_top;
            if (seg_top < bar_top)       seg_top    = bar_top;     // clamp at value
            if (seg_bottom > plot_bottom) seg_bottom = plot_bottom;
            const int seg_h = seg_bottom - seg_top;
            if (seg_h <= 0) continue;
            graphics_context_set_fill_color(r->ctx, b->stops[k].color);
            graphics_fill_rect(r->ctx,
                GRect(bar_x, seg_top, r->def->bar_w, seg_h), 0, GCornerNone);
        }

        if (b->style == BAR_OUTLINED) {
            // B&W: white silhouette keeps black bars readable on black
            graphics_context_set_stroke_color(r->ctx, GColorWhite);
            graphics_context_set_stroke_width(r->ctx, 1);
            graphics_draw_rect(r->ctx,
                GRect(bar_x, bar_top, r->def->bar_w, bar_h));
        }
    }
}

void chart_draw(GContext *ctx, const ChartDef *def, GRect outer,
                const ChartLayer *layers, int num_layers) {
    ChartRender r = {
        .ctx   = ctx,
        .def   = def,
        .outer = outer,
        .geo   = chart_geometry(def, outer),
    };
    for (int i = 0; i < num_layers; ++i) {
        const ChartLayer *l = &layers[i];
        switch (l->type) {
            case CHART_LAYER_FRAME:
                graph_frame_draw(ctx, l->frame.frame, outer);
                break;
            case CHART_LAYER_CUSTOM:
                l->custom.fn(&r, l->custom.user);
                break;
            case CHART_LAYER_AXIS:
                chart_render_axis(&r, &l->axis);
                break;
            case CHART_LAYER_BARS:
                chart_render_bars(&r, &l->bars);
                break;
            default:
                break;  // remaining renderers land in follow-up commits
        }
    }
}
