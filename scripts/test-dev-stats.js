// Standalone Node test for the dev-stats event log.
// Run with: node scripts/test-dev-stats.js
// Exits non-zero on the first failed assertion.

var assert = require('assert');

var store = {};
global.localStorage = {
    getItem: function(key) { return Object.prototype.hasOwnProperty.call(store, key) ? store[key] : null; },
    setItem: function(key, value) { store[key] = String(value); },
    removeItem: function(key) { delete store[key]; }
};

var devStats = require('../src/pkjs/dev-stats.js');
var KEYS = require('../src/pkjs/storage-keys.js');

var DAY_MS = 24 * 60 * 60 * 1000;

// Disabled (default) -> record is a no-op
devStats.record({ type: 'weather', outcome: 'ack', categories: { forecast: 'updated' } });
assert.strictEqual(store[KEYS.DEV_STATS_KEY], undefined, 'disabled record must not write');
assert.deepStrictEqual(devStats.read(), []);

devStats.setEnabled(true);

// Weather ACK event: c map with 1/0, ok=1, numeric timestamp
devStats.record({ type: 'weather', outcome: 'ack', categories: { forecast: 'updated', sun: 'cached' } });
var events = devStats.read();
assert.strictEqual(events.length, 1);
assert.strictEqual(events[0].k, 'weather');
assert.deepStrictEqual(events[0].c, { forecast: 1, sun: 0 });
assert.strictEqual(events[0].ok, 1);
assert.strictEqual(typeof events[0].t, 'number');

// Weather NACK event: ok=0
devStats.record({ type: 'weather', outcome: 'nack', categories: { forecast: 'updated' } });
events = devStats.read();
assert.strictEqual(events[1].ok, 0);

// Weather full skip: no ok field at all
devStats.record({ type: 'weather', outcome: 'skip', categories: { forecast: 'cached' } });
events = devStats.read();
assert.strictEqual(Object.prototype.hasOwnProperty.call(events[2], 'ok'), false, 'skip must omit ok');
assert.deepStrictEqual(events[2].c, { forecast: 0 });

// Setting events use `sent` instead of `c`
devStats.record({ type: 'setting', outcome: 'ack', categories: { clay: 'updated' } });
devStats.record({ type: 'setting', outcome: 'skip', categories: { clay: 'cached' } });
events = devStats.read();
assert.strictEqual(events[3].k, 'setting');
assert.strictEqual(events[3].sent, 1);
assert.strictEqual(events[3].ok, 1);
assert.strictEqual(Object.prototype.hasOwnProperty.call(events[3], 'c'), false, 'setting must omit c');
assert.strictEqual(events[4].sent, 0);
assert.strictEqual(Object.prototype.hasOwnProperty.call(events[4], 'ok'), false);

// Toggling off re-gates recording
devStats.setEnabled(false);
var countBefore = devStats.read().length;
devStats.record({ type: 'weather', outcome: 'ack', categories: { forecast: 'updated' } });
assert.strictEqual(devStats.read().length, countBefore, 'setEnabled(false) must re-gate recording');
devStats.setEnabled(true);

// Pruning: events older than 7 days vanish from read() and on the next record()
var expired = { k: 'weather', t: Date.now() - 8 * DAY_MS, c: { forecast: 1 }, ok: 1 };
store[KEYS.DEV_STATS_KEY] = JSON.stringify([expired].concat(devStats.read()));
assert.strictEqual(devStats.read().length, 5, 'read() must hide expired events');
devStats.record({ type: 'weather', outcome: 'ack', categories: { forecast: 'updated' } });
assert.strictEqual(JSON.parse(store[KEYS.DEV_STATS_KEY]).length, 6, 'record() must prune expired events');

// Corrupt storage recovers cleanly
store[KEYS.DEV_STATS_KEY] = 'not json';
assert.deepStrictEqual(devStats.read(), []);
devStats.record({ type: 'weather', outcome: 'ack', categories: { forecast: 'updated' } });
assert.strictEqual(devStats.read().length, 1);

// Null/garbage array elements are filtered out, never thrown on
store[KEYS.DEV_STATS_KEY] = JSON.stringify([null, 42, { k: 'weather', t: Date.now(), c: { forecast: 1 }, ok: 1 }]);
assert.strictEqual(devStats.read().length, 1, 'null elements must be filtered, not thrown on');

console.log('All dev-stats assertions passed.');
