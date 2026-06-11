/**
 * Dev stats: a 7-day rolling log of outbox send/skip events, recorded only
 * while the Clay "Record diagnostics" toggle is enabled. The stored event
 * shape belongs to this module: callers hand over a semantic descriptor
 * (see record()) and never construct or read the compact storage form.
 *
 * Stored event shapes:
 *   weather: { k: 'weather', t: <epoch ms>, c: {forecast: 1, sun: 0, ...}, ok: 1|0 }
 *   setting: { k: 'setting', t: <epoch ms>, sent: 1|0, ok: 1|0 }
 * `ok` is omitted when nothing was transmitted (full skip). `c` lists only
 * categories present in that payload: 1 = updated (transmitted), 0 = cached.
 */

var KEYS = require('./storage-keys');

var MAX_AGE_MS = 7 * 24 * 60 * 60 * 1000;

var enabled = false;

/**
 * Read the raw event array from localStorage, tolerating corrupt JSON.
 *
 * @returns {Object[]} Stored events, oldest first (possibly empty).
 */
function readEvents() {
    var raw = localStorage.getItem(KEYS.DEV_STATS_KEY);
    var parsed;

    if (raw === null) {
        return [];
    }

    try {
        parsed = JSON.parse(raw);
    }
    catch (ex) {
        localStorage.removeItem(KEYS.DEV_STATS_KEY);
        return [];
    }

    return Array.isArray(parsed) ? parsed : [];
}

/**
 * Drop events older than the rolling window.
 *
 * @param {Object[]} events Stored events.
 * @param {number} now Epoch milliseconds.
 * @returns {Object[]} Events within the window.
 */
function prune(events, now) {
    return events.filter(function(event) {
        return Boolean(event) && typeof event.t === 'number' && now - event.t <= MAX_AGE_MS;
    });
}

/**
 * Enable or disable recording (synced from the Clay toggle).
 *
 * @param {boolean} value New enabled state.
 * @returns {void}
 */
function setEnabled(value) {
    enabled = Boolean(value);
}

/**
 * Record one outbox send/skip event.
 *
 * @param {Object} info Event descriptor.
 * @param {string} info.type 'weather' or 'setting'.
 * @param {string} info.outcome 'ack', 'nack', or 'skip' (nothing transmitted).
 * @param {Object} info.categories Map of category name to 'updated'|'cached',
 *     covering only the categories present in the payload.
 * @returns {void}
 */
function record(info) {
    var now;
    var event;
    var name;
    var anyUpdated;
    var events;

    if (!enabled) {
        return;
    }

    try {
        now = Date.now();
        event = { k: info.type, t: now };
        if (info.type === 'weather') {
            event.c = {};
            for (name in info.categories) {
                if (Object.prototype.hasOwnProperty.call(info.categories, name)) {
                    event.c[name] = info.categories[name] === 'updated' ? 1 : 0;
                }
            }
        }
        else {
            anyUpdated = false;
            for (name in info.categories) {
                if (Object.prototype.hasOwnProperty.call(info.categories, name)
                        && info.categories[name] === 'updated') {
                    anyUpdated = true;
                }
            }
            event.sent = anyUpdated ? 1 : 0;
        }
        if (info.outcome === 'ack') {
            event.ok = 1;
        }
        else if (info.outcome === 'nack') {
            event.ok = 0;
        }

        events = prune(readEvents(), now);
        events.push(event);
        localStorage.setItem(KEYS.DEV_STATS_KEY, JSON.stringify(events));
    }
    catch (ex) {
        // Recording must never break a send or its ACK handling.
        console.log('[dev-stats] record failed: ' + ex.message);
    }
}

/**
 * Read all events inside the rolling window, oldest first.
 *
 * @returns {Object[]} Pruned event array.
 */
function read() {
    return prune(readEvents(), Date.now());
}

module.exports = {
    setEnabled: setEnabled,
    record: record,
    read: read
};
