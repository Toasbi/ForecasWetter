#pragma once
#include <pebble.h>
#include "c/appendix/slot_geometry.h"

// Chart layout primitives. Phase 2 introduced the frame; phase 3 adds
// TickConfig + SlotConfig and wraps everything into ChartConfig so each
// layer declares its visual shape in one block.

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

// --- Ticks ----------------------------------------------------------

typedef struct {
    int    length;       // px perpendicular to the side; 0 = side disabled
    GColor color;
    int    big_length;   // length for "big" ticks (every big_every'th index)
    GColor big_color;
    int    big_every;    // every Nth tick is big; 0 = none are big
} TickSide;

typedef struct {
    int      tick_w;     // horizontal px the tick occupies at each slot
                         // boundary; shared across all sides — also feeds
                         // the SlotGeometry pitch
    TickSide left;
    TickSide right;
    TickSide top;
    TickSide bottom;
} TickConfig;

typedef enum {
    GRAPH_SIDE_LEFT,
    GRAPH_SIDE_RIGHT,
    GRAPH_SIDE_TOP,
    GRAPH_SIDE_BOTTOM,
} GraphSide;

// Default tick line drawer. Picks big vs small from idx % style.big_every;
// no-op when style.length == 0. Ticks extend outward from the border
// (away from the chart content) by `length` (or `big_length`) px.
// Callers needing bespoke behaviour (suppressing the tick, drawing a
// label in its place) write their own SlotTickFn and delegate here for
// the default case.
void tick_side_draw_at(GContext *ctx, GRect outer, GraphFrame frame,
                       GraphSide side, TickSide style, int idx, int tick_xy);

// --- Slot config ----------------------------------------------------

typedef struct {
    int pad;          // px on each side of a bar inside its slot
    int bar_w;        // bar width in px
    int num_slots;    // requested slot count
} SlotConfig;

// --- Bundle ---------------------------------------------------------

typedef struct {
    GraphFrame  frame;
    TickConfig  ticks;
    SlotConfig  slots;
} ChartConfig;

typedef struct {
    GRect        content;   // rect inside the frame's borders
    SlotGeometry slots;     // num_slots, pitch, bar_dx, bar_w, tick_w
} ChartGeometry;

// Compute the content rect + slot geometry from a static config and the
// caller's outer rect. Pitch math lives here exactly once.
ChartGeometry chart_compute(ChartConfig cfg, GRect outer);

// Convenience: paints the frame, then returns the derived geometry.
ChartGeometry chart_draw_frame(GContext *ctx, ChartConfig cfg, GRect outer);
