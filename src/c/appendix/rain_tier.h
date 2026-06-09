// src/c/appendix/rain_tier.h
#pragma once

#include <pebble.h>

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

// Draw one stacked-slab bar for `tenths` rain. Renders N slabs bottom-up
// where N = rain_tier_of_tenths(tenths); slab k uses rain_tier_color(k);
// the topmost slab is shortened for continuous height within a tier.
// Skips when tenths <= 0.
void rain_tier_bar_draw_slabs(GContext *ctx,
                              int bar_x, int bar_w,
                              int bar_plot_bottom, int bar_plot_h,
                              int tenths);

// Draw num_entries stacked-slab rain bars across plot_rect.
// Slot width: entry_w = plot_rect.w / (num_entries - 1).
// Bar X: plot_rect.x + i*entry_w + 2 (centred between hour ticks).
// Bar W: max(entry_w - 3, 1).
// Bars with tenths == 0 are skipped.
void rain_bars_draw(GContext *ctx, GRect plot_rect,
                    const uint8_t *tenths, int num_entries);
