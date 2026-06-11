#pragma once

#include <pebble.h>

#include "config.h"

int persist_get_temp_trend(int16_t *buffer, const size_t buffer_size);

int persist_get_precip_trend(uint8_t *buffer, const size_t buffer_size);

int persist_get_rain_trend(uint8_t *buffer, const size_t buffer_size);

time_t persist_get_forecast_start();

int persist_get_num_entries();

int persist_get_current_temp();

int persist_get_city(char *buffer, const size_t buffer_size);

int persist_get_sun_event_start_type();

int persist_get_sun_event_times(time_t *buffer, const size_t buffer_size);

int persist_get_config(Config *config);

bool persist_has_config();

bool persist_set_temp_trend(int16_t *data, const size_t size);

bool persist_set_precip_trend(uint8_t *data, const size_t size);

bool persist_set_rain_trend(uint8_t *data, const size_t size);

int persist_get_rain_radar_trend(uint8_t *buffer, const size_t buffer_size);

int persist_get_rain_radar_trend_area(uint8_t *buffer, const size_t buffer_size);

time_t persist_get_rain_radar_start();

bool persist_set_rain_radar_trend(uint8_t *data, const size_t size);

bool persist_set_rain_radar_trend_area(uint8_t *data, const size_t size);

bool persist_set_rain_radar_start(time_t val);

bool persist_set_forecast_start(time_t val);

bool persist_set_num_entries(int val);

bool persist_set_current_temp(int val);

bool persist_set_city(char *val);

bool persist_set_sun_event_start_type(int val);

bool persist_set_sun_event_times(time_t *data, const size_t size);

bool persist_set_config(Config config);

bool persist_get_is_sleeping();

bool persist_set_is_sleeping(bool sleeping);

bool persist_get_radar_snooze();

bool persist_set_radar_snooze(bool snooze);
