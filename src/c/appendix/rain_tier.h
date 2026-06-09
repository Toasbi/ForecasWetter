// src/c/appendix/rain_tier.h
#pragma once

#include <pebble.h>

#define RAIN_TIER_COUNT 5

// 1..5 fifths of plot height per tier.
extern const int RAIN_TIER_HEIGHT_FIFTHS[RAIN_TIER_COUNT];

// Returns 1..RAIN_TIER_COUNT for tenths > 0, or 0 for tenths <= 0.
int rain_tier_of_tenths(int tenths);

// Per-tier fill colour. On colour displays: tier 1 white, 2 cobalt,
// 3 green, 4 orange, 5 red. On B&W: GColorBlack for all tiers
// (matches the previous behaviour in forecast_layer.c; intentional
// preservation by Task 2 extraction).
GColor rain_tier_color(int tier);

// Tier height in pixels given a bar-plot height. Returns 0 for tier 0;
// clamps to >= 1 for tier >= 1. Used by callers that build bar rects
// outside of rain_bars_draw (e.g. the radar layer's dual-pass draw).
int rain_tier_pixel_height(int tier, int bar_plot_h);

// Draw num_entries tier-coloured rain bars across plot_rect.
// Slot width: entry_w = plot_rect.w / (num_entries - 1).
// Bar X: plot_rect.x + i * entry_w + 1.
// Bar W: max(entry_w - 2, 1).
// Bar H: plot_rect.h * RAIN_TIER_HEIGHT_FIFTHS[tier-1] / 5  (clamped to >= 1).
// Bars with tenths == 0 are skipped.
void rain_bars_draw(GContext *ctx, GRect plot_rect,
                    const uint8_t *tenths, int num_entries);
