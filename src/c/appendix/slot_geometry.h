#pragma once
#include <pebble.h>

// Integer-pitched slot grid. Bars sit between two ticks with a fixed
// pad on each side and an integer pitch — no fractional entry_w drift,
// so the gap to the left and right tick is symmetric on every slot.
typedef struct {
    int num_slots;   // bars to draw (echoes the requested count)
    int pitch;       // px per slot = tick_w + 2*pad + bar_w
    int bar_dx;      // bar's left col, offset from its slot's left tick = tick_w + pad
    int bar_w;
    int tick_w;
} SlotGeometry;

static inline SlotGeometry slot_geometry(int num_slots, int tick_w,
                                          int pad, int bar_w) {
    return (SlotGeometry){
        .num_slots = num_slots,
        .pitch     = tick_w + 2 * pad + bar_w,
        .bar_dx    = tick_w + pad,
        .bar_w     = bar_w,
        .tick_w    = tick_w,
    };
}

static inline int slot_geometry_tick_x(SlotGeometry geo, int i, int origin_x) {
    return origin_x + i * geo.pitch;
}

static inline int slot_geometry_bar_x(SlotGeometry geo, int i, int origin_x) {
    return origin_x + i * geo.pitch + geo.bar_dx;
}

