// src/c/layers/rain_radar_layer.h
#pragma once

#include <pebble.h>

void rain_radar_layer_create(Layer *parent, GRect frame);

void rain_radar_layer_refresh(void);

void rain_radar_layer_destroy(void);

Layer *rain_radar_layer_get_root(void);
