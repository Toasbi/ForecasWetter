// src/c/appendix/rain_tier.h
#pragma once

#include <pebble.h>
#include "c/appendix/chart.h"

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

// Continuous pixel height for a bar of `tenths` rain: full lower-tier
// slabs plus a fractional topmost slab. Returns 0 for tenths <= 0;
// clamps to >= 1 otherwise.
int rain_tier_proportional_height(int tenths, int bar_plot_h);

// Per-mille (0..1000 of plot height) for a rain value in wire tenths —
// the chart-engine BARS contract. Same tier allocation math as
// rain_tier_proportional_height (this is that function with h = 1000).
int16_t rain_tier_permille(int tenths);

// Fill `out` (>= count entries) with per-mille values for a tenths series.
void rain_tier_fill_permille(const uint8_t *tenths, int16_t *out, int count);

// Color stops for the chart engine, in per-mille value space. Colour
// displays: 5 tier stops. B&W: a single black stop (callers pair it with
// BAR_OUTLINED for the white silhouette).
extern const ChartColorStop RAIN_TIER_STOPS[];
extern const int RAIN_TIER_NUM_STOPS;
