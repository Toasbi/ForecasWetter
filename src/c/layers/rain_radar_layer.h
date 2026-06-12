// src/c/layers/rain_radar_layer.h
#pragma once

#include <pebble.h>

void rain_radar_layer_create(Layer *parent, GRect frame);

void rain_radar_layer_refresh(void);

// Record that fresh radar arrived; resets the self-advance timer.
void rain_radar_layer_note_update(void);

// On the minute tick: if a scheduled update hasn't arrived and Bluetooth is
// connected, advance the persisted radar window to `now` (shift left + zero-pad)
// and redraw. Returns true if the window was advanced.
bool rain_radar_layer_tick(time_t now);

void rain_radar_layer_destroy(void);

Layer *rain_radar_layer_get_root(void);
