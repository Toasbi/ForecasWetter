#pragma once
#include <pebble.h>

// Chart layout primitives. Phase 2 introduces the frame; phase 3 will
// add TickConfig + the ChartConfig bundle and the chart_compute helper.
// One header carries the small structs that make up a chart config so a
// layer can declare its whole visual shape in one block instead of a
// scattered set of inline constants.

// --- Frame ----------------------------------------------------------

typedef struct {
    int    width;     // border thickness in px; 0 = no border on this side
    GColor color;
} Border;

typedef struct {
    Border left;
    Border right;
    Border top;
    Border bottom;
} GraphFrame;

// Borders sit inside the outer rect: a 1-px left border occupies the
// leftmost column of outer, a 1-px bottom border occupies the bottom row.
// outer.size shrinks accordingly — graph_frame_content_rect returns the
// rectangle inside the borders.
GRect graph_frame_content_rect(GraphFrame f, GRect outer);

// Paints each non-zero-width side as a single graphics_fill_rect.
// Sides with width == 0 are skipped (color is irrelevant for them).
void  graph_frame_draw(GContext *ctx, GraphFrame f, GRect outer);
