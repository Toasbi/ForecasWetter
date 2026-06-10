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

ChartGeometry chart_compute(ChartConfig cfg, GRect outer) {
    return (ChartGeometry){
        .content = graph_frame_content_rect(cfg.frame, outer),
        .slots   = slot_geometry(cfg.slots.num_slots,
                                  cfg.ticks.tick_w,
                                  cfg.slots.pad,
                                  cfg.slots.bar_w),
    };
}

ChartGeometry chart_draw_frame(GContext *ctx, ChartConfig cfg, GRect outer) {
    graph_frame_draw(ctx, cfg.frame, outer);
    return chart_compute(cfg, outer);
}
