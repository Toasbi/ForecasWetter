/**
 * Outbox with per-category dedupe.
 *
 * Payload keys are grouped into categories that match the screen areas on the
 * watch (forecast chart, status row, sun events, rain radar, sleep state, Clay
 * config). PayloadComparator checks each category's content against the
 * last-sent serialization cached in localStorage; only categories whose
 * content actually changed are bundled into the outgoing AppMessage. When
 * nothing changed, no message is sent at all — Bluetooth is the most
 * battery-expensive part of a fetch cycle.
 *
 * All changed categories ride in ONE Pebble.sendAppMessage call: the channel
 * is half-duplex, so back-to-back sends would collide. Caches are committed
 * only in the ACK callback, so a NACKed category is retried on the next send.
 *
 * Every compare/send cycle is reported to dev-stats as a semantic descriptor
 * (skip immediately; ack/nack from the AppMessage callbacks).
 */

var KEYS = require('./storage-keys');
var PayloadComparator = require('./payload-comparator');
var devStats = require('./dev-stats');

/** Weather categories, each mapping a cache key to its AppMessage keys. */
var WEATHER_CATEGORIES = [
    {
        name: 'forecast',
        cacheKey: KEYS.LAST_SENT_FORECAST_KEY,
        keys: ['TEMP_TREND_INT16', 'PRECIP_TREND_UINT8', 'RAIN_TREND_UINT8', 'FORECAST_START', 'NUM_ENTRIES']
    },
    {
        name: 'status',
        cacheKey: KEYS.LAST_SENT_STATUS_KEY,
        keys: ['CURRENT_TEMP', 'CITY']
    },
    {
        name: 'sun',
        cacheKey: KEYS.LAST_SENT_SUN_KEY,
        keys: ['SUN_EVENTS']
    },
    {
        name: 'radar',
        cacheKey: KEYS.LAST_SENT_RADAR_KEY,
        keys: ['RAIN_RADAR_TREND_UINT8', 'RAIN_RADAR_TREND_AREA_UINT8', 'RAIN_RADAR_START']
    },
    {
        name: 'sleep',
        cacheKey: KEYS.LAST_SENT_SLEEP_KEY,
        keys: ['IS_SLEEPING']
    }
];

/**
 * Distill a comparison result into the dev-stats descriptor.
 *
 * @param {string} type 'weather' or 'setting'.
 * @param {Object} result PayloadComparator result.
 * @param {string} outcome 'ack', 'nack', or 'skip'.
 * @returns {{type: string, outcome: string, categories: Object}} Descriptor for devStats.record().
 */
function statsDescriptor(type, result, outcome) {
    var categories = {};
    result.categories.forEach(function(entry) {
        categories[entry.name] = entry.changed ? 'updated' : 'cached';
    });
    return { type: type, outcome: outcome, categories: categories };
}

/**
 * Send the changed parts of a payload as one AppMessage.
 *
 * @param {Object} payload Full candidate payload.
 * @param {Array} categories Category descriptors to dedupe against.
 * @param {string} label Log prefix ('weather' or 'clay').
 * @param {Function} [onSuccess] Called after ACK, or immediately when nothing changed.
 * @param {Function} [onFailure] Called on NACK.
 * @returns {void}
 */
function sendChangedCategories(payload, categories, label, onSuccess, onFailure) {
    var statsType = label === 'clay' ? 'setting' : 'weather';
    var result = new PayloadComparator(categories).compare(payload);
    var changed = result.categories.filter(function(entry) {
        return entry.changed;
    });
    var outgoing = {};
    var changedNames = [];

    changed.forEach(function(entry) {
        var key;
        for (key in entry.subset) {
            if (Object.prototype.hasOwnProperty.call(entry.subset, key)) {
                outgoing[key] = entry.subset[key];
            }
        }
        changedNames.push(entry.name);
    });

    if (changed.length === 0) {
        console.log('Outbox: ' + label + ' unchanged, skipping send.');
        devStats.record(statsDescriptor(statsType, result, 'skip'));
        if (typeof onSuccess === 'function') {
            onSuccess();
        }
        return;
    }

    console.log('Outbox: sending ' + label + ' categories: ' + changedNames.join(', '));
    Pebble.sendAppMessage(outgoing, function() {
        // Commit caches only after the watch ACKed, so NACKs retry next time.
        changed.forEach(function(entry) {
            localStorage.setItem(entry.cacheKey, entry.serialized);
        });
        devStats.record(statsDescriptor(statsType, result, 'ack'));
        console.log('Outbox: ' + label + ' sent successfully.');
        if (typeof onSuccess === 'function') {
            onSuccess();
        }
    }, function(e) {
        devStats.record(statsDescriptor(statsType, result, 'nack'));
        console.log('Outbox: ' + label + ' send failed: ' + JSON.stringify(e));
        if (typeof onFailure === 'function') {
            onFailure(e);
        }
    });
}

/**
 * Send the changed categories of a weather payload (forecast, status, sun,
 * radar, sleep) as a single AppMessage; skip the send when nothing changed.
 *
 * @param {Object} payload Full weather payload (radar/sleep keys optional).
 * @param {Function} [onSuccess] Called after ACK, or immediately when nothing changed.
 * @param {Function} [onFailure] Called on NACK.
 * @returns {void}
 */
function sendWeather(payload, onSuccess, onFailure) {
    sendChangedCategories(payload, WEATHER_CATEGORIES, 'weather', onSuccess, onFailure);
}

/**
 * Send a Clay settings payload, skipping the send when it matches the last
 * ACKed one.
 *
 * @param {Object} payload Clay settings payload.
 * @param {Function} [onSuccess] Called after ACK, or immediately when nothing changed.
 * @param {Function} [onFailure] Called on NACK.
 * @returns {void}
 */
function sendClay(payload, onSuccess, onFailure) {
    var clayCategory = {
        name: 'clay',
        cacheKey: KEYS.LAST_SENT_CLAY_KEY,
        keys: Object.keys(payload)
    };
    sendChangedCategories(payload, [clayCategory], 'clay', onSuccess, onFailure);
}

/**
 * Forget all last-sent weather categories so the next fetch resends
 * everything. Used when the watch reports it has no (or stale) forecast data.
 *
 * @returns {void}
 */
function clearWeatherCaches() {
    WEATHER_CATEGORIES.forEach(function(category) {
        localStorage.removeItem(category.cacheKey);
    });
}

/**
 * Forget the last-sent Clay settings so the next Clay send goes through.
 * Used when the watch reports it has no persisted config.
 *
 * @returns {void}
 */
function clearClayCache() {
    localStorage.removeItem(KEYS.LAST_SENT_CLAY_KEY);
}

module.exports = {
    sendWeather: sendWeather,
    sendClay: sendClay,
    clearWeatherCaches: clearWeatherCaches,
    clearClayCache: clearClayCache
};
