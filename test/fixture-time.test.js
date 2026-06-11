'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { dateFromWatchNow, assertIntInRange, normalizeWeather, FixtureError } =
  require('../scripts/lib/fixture-time');

const WATCH_NOW = { year: 2026, month: 6, day: 9, hour: 15, minute: 24, second: 0 };

test('dateFromWatchNow matches new Date local epoch with overrides', () => {
  const expected = Math.floor(new Date(2026, 5, 9, 21, 29, 0, 0).getTime() / 1000);
  assert.equal(dateFromWatchNow(WATCH_NOW, { hour: 21, minute: 29 }), expected);
});

test('dateFromWatchNow applies dayOffset', () => {
  const expected = Math.floor(new Date(2026, 5, 10, 4, 46, 0, 0).getTime() / 1000);
  assert.equal(dateFromWatchNow(WATCH_NOW, { dayOffset: 1, hour: 4, minute: 46 }), expected);
});

test('assertIntInRange throws FixtureError out of range', () => {
  assert.throws(() => assertIntInRange('weather.startHour', 24, 0, 23), FixtureError);  // above max
  assert.throws(() => assertIntInRange('weather.startHour', -1, 0, 23), FixtureError);  // below min
  assert.throws(() => assertIntInRange('weather.startHour', 0.5, 0, 23), FixtureError); // non-integer
  assert.doesNotThrow(() => assertIntInRange('weather.startHour', 0, 0, 23));  // low boundary
  assert.doesNotThrow(() => assertIntInRange('weather.startHour', 23, 0, 23)); // high boundary
});

test('normalizeWeather converts startHour and sunEvents to epochs in place', () => {
  const fixture = {
    watch: { now: { ...WATCH_NOW } },
    weather: {
      startHour: 15,
      sunEvents: [{ type: 'sunset', dayOffset: 0, hour: 21, minute: 29 }],
    },
  };
  normalizeWeather(fixture);
  assert.equal(fixture.weather.startHour, undefined);
  assert.equal(fixture.weather.startEpoch,
    Math.floor(new Date(2026, 5, 9, 15, 0, 0, 0).getTime() / 1000));
  assert.equal(fixture.weather.sunEvents[0].epoch,
    Math.floor(new Date(2026, 5, 9, 21, 29, 0, 0).getTime() / 1000));
});

test('normalizeWeather passes through epoch sunEvents unchanged', () => {
  const fixture = {
    watch: { now: { ...WATCH_NOW } },
    weather: { sunEvents: [{ type: 'sunset', epoch: 1234567890 }] },
  };
  normalizeWeather(fixture);
  assert.equal(fixture.weather.sunEvents[0].epoch, 1234567890);
});

test('normalizeWeather is a no-op when watch.now or weather is missing', () => {
  const noWeather = { watch: { now: { ...WATCH_NOW } } };
  assert.doesNotThrow(() => normalizeWeather(noWeather));
  assert.deepEqual(noWeather, { watch: { now: { ...WATCH_NOW } } });

  const noWatch = { weather: { startHour: 15 } };
  normalizeWeather(noWatch);
  assert.equal(noWatch.weather.startHour, 15);  // unchanged: no watch.now to anchor against

  assert.doesNotThrow(() => normalizeWeather(null));
  assert.doesNotThrow(() => normalizeWeather({}));
});
