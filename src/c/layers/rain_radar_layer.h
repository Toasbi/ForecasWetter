// src/c/layers/rain_radar_layer.h
#pragma once

#include <pebble.h>

void rain_radar_layer_create(Layer *parent, GRect frame);

void rain_radar_layer_refresh(void);

// On the minute tick: if PKJS skipped a grid fetch (deduped) and Bluetooth is
// connected, advance the persisted radar window to the missed fetch boundary
// (shift left + zero-pad) and redraw. Returns true if the window was advanced.
bool rain_radar_layer_tick(time_t now);

void rain_radar_layer_destroy(void);

Layer *rain_radar_layer_get_root(void);
