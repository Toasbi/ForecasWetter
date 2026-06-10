// src/c/appendix/rain_tier.h
#pragma once

#include <pebble.h>
#include "c/appendix/slot_geometry.h"

#define RAIN_TIER_COUNT 5

// Returns 1..RAIN_TIER_COUNT for tenths > 0, or 0 for tenths <= 0.
int rain_tier_of_tenths(int tenths);

// Per-tier slab colour. Colour displays: 1 LightGray, 2 ElectricBlue,
// 3 Green, 4 Yellow, 5 SunsetOrange. B&W: GColorBlack for all tiers.
GColor rain_tier_color(int tier);

// Full pixel height of a bar whose top reaches `tier` (cumulative slab
// top of `tier` as a percent of bar_plot_h). Returns 0 for tier 0;
// clamps to >= 1 for tier >= 1. Used by callers that only need the
// discrete tier top (e.g. the radar area hatch + outline).
int rain_tier_pixel_height(int tier, int bar_plot_h);

// Continuous pixel height for a bar of `tenths` rain, matching the
// visible top edge that rain_tier_bar_draw_slabs would render for the
// same tenths (full lower-tier slabs + a fractional topmost slab).
// Returns 0 for tenths <= 0; clamps to >= 1 otherwise.
int rain_tier_proportional_height(int tenths, int bar_plot_h);

// Draw one stacked-slab bar for `tenths` rain. Renders N slabs bottom-up
// where N = rain_tier_of_tenths(tenths); slab k uses rain_tier_color(k);
// the topmost slab is shortened for continuous height within a tier.
// Skips when tenths <= 0.
void rain_tier_bar_draw_slabs(GContext *ctx,
                              int bar_x, int bar_w,
                              int bar_plot_bottom, int bar_plot_h,
                              int tenths);

// Draw stacked-slab rain bars across plot_rect using a SlotGeometry grid.
// Bars with tenths == 0 are skipped.
void rain_bars_draw(GContext *ctx, GRect plot_rect, SlotGeometry slots,
                    const uint8_t *tenths);
