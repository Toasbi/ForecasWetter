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
