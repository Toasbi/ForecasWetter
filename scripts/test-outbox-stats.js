// Standalone Node integration test: outbox + PayloadComparator + dev-stats.
// Run with: node scripts/test-outbox-stats.js
// Exits non-zero on the first failed assertion.

var assert = require('assert');

var store = {};
global.localStorage = {
    getItem: function(key) { return Object.prototype.hasOwnProperty.call(store, key) ? store[key] : null; },
    setItem: function(key, value) { store[key] = String(value); },
    removeItem: function(key) { delete store[key]; }
};

var transmitted = [];
var nextSendOutcome = 'ack';
global.Pebble = {
    sendAppMessage: function(payload, onAck, onNack) {
        transmitted.push(payload);
        if (nextSendOutcome === 'ack') {
            onAck();
        }
        else {
            onNack({ error: 'test nack' });
        }
    }
};

var outbox = require('../src/pkjs/outbox.js');
// Same module instance as the one outbox required (Node module cache).
var devStats = require('../src/pkjs/dev-stats.js');
var KEYS = require('../src/pkjs/storage-keys.js');

devStats.setEnabled(true);

var payload = {
    TEMP_TREND_INT16: [1, 2],
    PRECIP_TREND_UINT8: [3],
    RAIN_TREND_UINT8: [4],
    FORECAST_START: 100,
    NUM_ENTRIES: 24,
    CURRENT_TEMP: 20,
    CITY: 'Berlin',
    SUN_EVENTS: [0, 1, 2]
};

// First send: everything changed -> one AppMessage, caches committed, ack event
outbox.sendWeather(payload);
assert.strictEqual(transmitted.length, 1);
assert.strictEqual(transmitted[0].CITY, 'Berlin');
assert.strictEqual(transmitted[0].FORECAST_START, 100);
assert.strictEqual(
    localStorage.getItem(KEYS.LAST_SENT_STATUS_KEY),
    JSON.stringify({ CURRENT_TEMP: 20, CITY: 'Berlin' }),
    'ACK must commit category caches'
);
var events = devStats.read();
assert.strictEqual(events.length, 1);
assert.strictEqual(events[0].k, 'weather');
assert.strictEqual(events[0].ok, 1);
assert.deepStrictEqual(events[0].c, { forecast: 1, status: 1, sun: 1 });

// Second identical send: full skip -> no AppMessage, skip event, all cached
var skipSuccess = false;
outbox.sendWeather(payload, function() { skipSuccess = true; });
assert.strictEqual(transmitted.length, 1, 'unchanged payload must not transmit');
assert.strictEqual(skipSuccess, true, 'skip still calls onSuccess');
events = devStats.read();
assert.strictEqual(events.length, 2);
assert.strictEqual(Object.prototype.hasOwnProperty.call(events[1], 'ok'), false);
assert.deepStrictEqual(events[1].c, { forecast: 0, status: 0, sun: 0 });

// NACK: changed payload, send fails -> caches untouched, nack event
nextSendOutcome = 'nack';
payload.CURRENT_TEMP = 21;
var failed = false;
outbox.sendWeather(payload, null, function() { failed = true; });
assert.strictEqual(failed, true);
assert.strictEqual(transmitted.length, 2);
assert.strictEqual(
    localStorage.getItem(KEYS.LAST_SENT_STATUS_KEY),
    JSON.stringify({ CURRENT_TEMP: 20, CITY: 'Berlin' }),
    'NACK must not commit caches'
);
events = devStats.read();
assert.strictEqual(events[2].ok, 0);
assert.deepStrictEqual(events[2].c, { forecast: 0, status: 1, sun: 0 });

// Clay: first send transmits (setting/ack), identical second send skips
nextSendOutcome = 'ack';
outbox.sendClay({ CLAY_VIBE: true });
outbox.sendClay({ CLAY_VIBE: true });
assert.strictEqual(transmitted.length, 3);
events = devStats.read();
assert.strictEqual(events.length, 5);
assert.strictEqual(events[3].k, 'setting');
assert.strictEqual(events[3].sent, 1);
assert.strictEqual(events[3].ok, 1);
assert.strictEqual(events[4].sent, 0);
assert.strictEqual(Object.prototype.hasOwnProperty.call(events[4], 'ok'), false);

console.log('All outbox stats assertions passed.');
