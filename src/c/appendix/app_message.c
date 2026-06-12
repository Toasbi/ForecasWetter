#include <string.h>

#include "app_message.h"
#include "persist.h"
#include "c/layers/forecast_layer.h"
#include "c/layers/weather_status_layer.h"
#include "c/layers/loading_layer.h"
#include "c/layers/rain_radar_layer.h"
#include "c/windows/main_window.h"
#include "memory_log.h"

// Payloads arrive split into categories that map onto screen areas (forecast
// chart, status row, radar). Each category is processed independently and the
// persist setters report real changes, so only the affected layers redraw.

static bool handle_forecast(DictionaryIterator *iterator, bool *forecast_dirty) {
    Tuple *temp_trend_tuple = dict_find(iterator, MESSAGE_KEY_TEMP_TREND_INT16);
    Tuple *precip_trend_tuple = dict_find(iterator, MESSAGE_KEY_PRECIP_TREND_UINT8);
    Tuple *rain_trend_tuple = dict_find(iterator, MESSAGE_KEY_RAIN_TREND_UINT8);
    Tuple *forecast_start_tuple = dict_find(iterator, MESSAGE_KEY_FORECAST_START);
    Tuple *num_entries_tuple = dict_find(iterator, MESSAGE_KEY_NUM_ENTRIES);

    if (!(temp_trend_tuple && precip_trend_tuple && forecast_start_tuple && num_entries_tuple)) {
        if (temp_trend_tuple || precip_trend_tuple || forecast_start_tuple || num_entries_tuple) {
            APP_LOG(APP_LOG_LEVEL_WARNING,
                    "Forecast payload incomplete (temp=%d precip=%d start=%d entries=%d) — skipping",
                    temp_trend_tuple != NULL,
                    precip_trend_tuple != NULL,
                    forecast_start_tuple != NULL,
                    num_entries_tuple != NULL);
        }
        return false;
    }

    const int num_entries = (int) num_entries_tuple->value->int32;
#ifdef WW_ENABLE_MEMORY_LOGGING
    APP_LOG(APP_LOG_LEVEL_DEBUG, "MEM|forecast_payload|entries=%d|free=%lu|used=%lu",
            num_entries,
            (unsigned long)heap_bytes_free(),
            (unsigned long)heap_bytes_used());
#endif
    bool changed = false;
    changed |= persist_set_forecast_start((time_t) forecast_start_tuple->value->int32);
    changed |= persist_set_num_entries(num_entries);
    changed |= persist_set_temp_trend((int16_t*) temp_trend_tuple->value->data, num_entries);
    changed |= persist_set_precip_trend((uint8_t*) precip_trend_tuple->value->data, num_entries);
    if (rain_trend_tuple) {
        changed |= persist_set_rain_trend((uint8_t*) rain_trend_tuple->value->data, num_entries);
    }

    *forecast_dirty |= changed;
    return true;
}

static bool handle_status(DictionaryIterator *iterator, bool *status_dirty, bool *radar_dirty) {
    Tuple *current_temp_tuple = dict_find(iterator, MESSAGE_KEY_CURRENT_TEMP);
    Tuple *city_tuple = dict_find(iterator, MESSAGE_KEY_CITY);
    Tuple *is_sleeping_tuple = dict_find(iterator, MESSAGE_KEY_IS_SLEEPING);

    if (!(current_temp_tuple || city_tuple || is_sleeping_tuple)) {
        return false;
    }

    bool changed = false;
    if (current_temp_tuple) {
        changed |= persist_set_current_temp((int) current_temp_tuple->value->int32);
    }
    if (city_tuple) {
        changed |= persist_set_city((char*) city_tuple->value->cstring);
    }
    if (is_sleeping_tuple) {
        const bool sleeping = (bool) is_sleeping_tuple->value->int16;
        changed |= persist_set_is_sleeping(sleeping);
        if (sleeping) {
            // Sleep onset latches the radar area into snooze immediately.
            // The latch is only released by a radar payload while awake
            // (see inbox_received_callback) so waking never reveals a
            // stale chart.
            *radar_dirty |= persist_set_radar_snooze(true);
        }
    }

    *status_dirty |= changed;
    return true;
}

static bool handle_sun_events(DictionaryIterator *iterator, bool *forecast_dirty, bool *status_dirty) {
    Tuple *sun_events_tuple = dict_find(iterator, MESSAGE_KEY_SUN_EVENTS);
    if (!sun_events_tuple) {
        return false;
    }

    // Packed as one start-type byte followed by two epoch timestamps.
    uint8_t sun_event_start_type = (uint8_t) sun_events_tuple->value->uint8;
    time_t *sun_event_times = (time_t*) (sun_events_tuple->value->data + 1);
    bool changed = false;
    changed |= persist_set_sun_event_start_type(sun_event_start_type);
    changed |= persist_set_sun_event_times(sun_event_times, 2);

    // Sun events feed the chart's day/night shading and the status row.
    *forecast_dirty |= changed;
    *status_dirty |= changed;
    return true;
}

static bool handle_rain_radar(DictionaryIterator *iterator, bool *radar_dirty, bool *radar_payload) {
    Tuple *rain_radar_exact_tuple = dict_find(iterator, MESSAGE_KEY_RAIN_RADAR_TREND_UINT8);
    Tuple *rain_radar_area_tuple  = dict_find(iterator, MESSAGE_KEY_RAIN_RADAR_TREND_AREA_UINT8);
    Tuple *rain_radar_start_tuple = dict_find(iterator, MESSAGE_KEY_RAIN_RADAR_START);

    if (!(rain_radar_exact_tuple && rain_radar_area_tuple && rain_radar_start_tuple)) {
        if (rain_radar_exact_tuple || rain_radar_area_tuple) {
            // Partial radar payload — log and discard so persist never holds half-state.
            // RAIN_RADAR_START alone is too generic an int32 to count as radar-flavoured.
            APP_LOG(APP_LOG_LEVEL_WARNING,
                    "Rain-radar payload incomplete (exact=%d area=%d start=%d) — skipping",
                    rain_radar_exact_tuple != NULL,
                    rain_radar_area_tuple  != NULL,
                    rain_radar_start_tuple != NULL);
            return true;
        }
        return false;
    }

    bool changed = false;
    if (rain_radar_exact_tuple->length == 0) {
        // Empty array from a non-DWD provider — clear persisted radar data.
        uint8_t zeros[24] = {0};
        changed |= persist_set_rain_radar_trend(zeros, 24);
        changed |= persist_set_rain_radar_trend_area(zeros, 24);
        changed |= persist_set_rain_radar_start(0);
    } else {
        changed |= persist_set_rain_radar_trend(
            (uint8_t*) rain_radar_exact_tuple->value->data, 24);
        changed |= persist_set_rain_radar_trend_area(
            (uint8_t*) rain_radar_area_tuple->value->data, 24);
        changed |= persist_set_rain_radar_start(
            (time_t) rain_radar_start_tuple->value->int32);
    }
    *radar_payload = true;

    *radar_dirty |= changed;
    return true;
}

static bool handle_clay_config(DictionaryIterator *iterator, bool *config_dirty) {
    Tuple *clay_celsius_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_CELSIUS);
    Tuple *clay_time_lead_zero_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_TIME_LEAD_ZERO);
    Tuple *clay_axis_12h_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_AXIS_12H);
    Tuple *clay_start_mon_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_START_MON);
    Tuple *clay_prev_week_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_PREV_WEEK);
    Tuple *clay_color_today_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_COLOR_TODAY);
    Tuple *clay_time_font_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_TIME_FONT);
    Tuple *clay_vibe_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_VIBE);
    Tuple *clay_show_qt_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_SHOW_QT);
    Tuple *clay_show_bt_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_SHOW_BT);
    Tuple *clay_show_bt_disconnect_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_SHOW_BT_DISCONNECT);
    Tuple *clay_show_am_pm_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_SHOW_AM_PM);
    Tuple *clay_color_saturday_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_COLOR_SATURDAY);
    Tuple *clay_color_sunday_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_COLOR_SUNDAY);
    Tuple *clay_color_us_federal_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_COLOR_US_FEDERAL);
    Tuple *clay_color_time_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_COLOR_TIME);
    Tuple *clay_day_night_shading_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_DAY_NIGHT_SHADING);
    Tuple *clay_fetch_interval_tuple = dict_find(iterator, MESSAGE_KEY_CLAY_FETCH_INTERVAL_MIN);

    if (!(clay_celsius_tuple && clay_time_lead_zero_tuple && clay_axis_12h_tuple && clay_start_mon_tuple
        && clay_prev_week_tuple && clay_color_today_tuple && clay_time_font_tuple && clay_vibe_tuple
        && clay_show_qt_tuple && clay_show_bt_tuple && clay_show_bt_disconnect_tuple && clay_show_am_pm_tuple
        && clay_color_saturday_tuple && clay_color_sunday_tuple && clay_color_us_federal_tuple
        && clay_color_time_tuple && clay_day_night_shading_tuple && clay_fetch_interval_tuple)) {
        return false;
    }

    // Zero the struct first so padding bytes compare deterministically in
    // persist_set_config's change detection.
    Config config;
    memset(&config, 0, sizeof(config));
    config.celsius = (bool) (clay_celsius_tuple->value->int16);
    config.time_lead_zero = (bool) (clay_time_lead_zero_tuple->value->int16);
    config.axis_12h = (bool) (clay_axis_12h_tuple->value->int16);
    config.start_mon = (bool) (clay_start_mon_tuple->value->int16);
    config.prev_week = (bool) (clay_prev_week_tuple->value->int16);
    config.vibe = (bool) (clay_vibe_tuple->value->int16);
    config.show_qt = (bool) (clay_show_qt_tuple->value->int16);
    config.show_bt = (bool) (clay_show_bt_tuple->value->int16);
    config.show_bt_disconnect = (bool) (clay_show_bt_disconnect_tuple->value->int16);
    config.show_am_pm = (bool) (clay_show_am_pm_tuple->value->int16);
    config.day_night_shading = (bool) (clay_day_night_shading_tuple->value->int16);
    config.fetch_interval_min = clay_fetch_interval_tuple->value->int16;
    config.time_font = clay_time_font_tuple->value->int16;
    config.color_today = GColorFromHEX(clay_color_today_tuple->value->int32);
    config.color_saturday = GColorFromHEX(clay_color_saturday_tuple->value->int32);
    config.color_sunday = GColorFromHEX(clay_color_sunday_tuple->value->int32);
    config.color_us_federal = GColorFromHEX(clay_color_us_federal_tuple->value->int32);
    config.color_time = GColorFromHEX(clay_color_time_tuple->value->int32);

    *config_dirty |= persist_set_config(config);
    return true;
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Inbox received: %u bytes",
            (unsigned) dict_size(iterator));

    bool handled = false;
    bool forecast_dirty = false;  // chart + loading overlay
    bool status_dirty = false;    // status row
    bool radar_dirty = false;     // radar chart + top-view availability
    bool config_dirty = false;    // whole window (config feeds every layer)
    bool radar_payload = false;   // complete radar tuples accepted this inbox
    handled |= handle_forecast(iterator, &forecast_dirty);
    handled |= handle_status(iterator, &status_dirty, &radar_dirty);
    handled |= handle_sun_events(iterator, &forecast_dirty, &status_dirty);
    handled |= handle_rain_radar(iterator, &radar_dirty, &radar_payload);
    handled |= handle_clay_config(iterator, &config_dirty);

    // Release the radar-snooze latch only when a complete radar payload
    // was accepted while awake. Runs after every handler so it can't race
    // the IS_SLEEPING tuple in the same payload. Also release when awake
    // with no radar data at all: radarless providers send a constant empty
    // radar payload that the outbox dedupe suppresses after the first ACK,
    // so no releasing payload would ever arrive — and with no data there
    // is no stale chart to reveal (the top view falls back to the calendar).
    if ((radar_payload || persist_get_rain_radar_start() == 0)
            && !persist_get_is_sleeping()) {
        radar_dirty |= persist_set_radar_snooze(false);
    }

    if (config_dirty) {
        main_window_refresh();
    }
    if (forecast_dirty) {
        loading_layer_refresh();
        forecast_layer_refresh();
    }
    if (status_dirty) {
        weather_status_layer_refresh();
    }
    if (radar_dirty) {
        rain_radar_layer_refresh();
        // Radar availability may have switched — re-evaluate the top view so
        // a cleared radar falls back to the calendar.
        main_window_apply_top_view();
    }
    if (!handled) {
        APP_LOG(APP_LOG_LEVEL_WARNING, "Bad payload received in app_message.c");
    }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

void app_message_send_startup_state(bool has_forecast_data) {
    DictionaryIterator *outbox;
    AppMessageResult result = app_message_outbox_begin(&outbox);

    if (result != APP_MSG_OK) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Unable to begin startup outbox: %d", result);
        return;
    }

    dict_write_uint8(outbox, MESSAGE_KEY_WATCH_HAS_FORECAST_DATA, has_forecast_data ? 1 : 0);
    dict_write_uint8(outbox, MESSAGE_KEY_WATCH_HAS_CONFIG, persist_has_config() ? 1 : 0);
    result = app_message_outbox_send();

    if (result != APP_MSG_OK) {
        APP_LOG(APP_LOG_LEVEL_ERROR, "Unable to send startup state: %d", result);
    }
}

void app_message_init() {
    // Register callbacks
    app_message_register_inbox_received(inbox_received_callback);
    app_message_register_inbox_dropped(inbox_dropped_callback);

    // Open AppMessage
    const int inbox_size = 384;
    const int outbox_size = dict_calc_buffer_size(2, sizeof(uint8_t), sizeof(uint8_t));
    APP_LOG(APP_LOG_LEVEL_INFO, "AppMessage buffer sizes: inbox=%d outbox=%d", inbox_size, outbox_size);
    app_message_open(inbox_size, outbox_size);
    MEMORY_LOG_HEAP("after_app_message_open");
}
