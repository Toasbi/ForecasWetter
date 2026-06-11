'use strict';

/**
 * Error thrown when a fixture time field is out of range. CLI callers
 * (prepare-fixture.js) catch this and exit non-zero; library callers let it
 * propagate.
 */
class FixtureError extends Error {}

/**
 * Assert a fixture time field is an integer within [min, max].
 *
 * @param {string} pathLabel Human-readable fixture path.
 * @param {number} value Time field value.
 * @param {number} min Minimum allowed value.
 * @param {number} max Maximum allowed value.
 * @returns {void}
 */
function assertIntInRange(pathLabel, value, min, max) {
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new FixtureError(pathLabel + ' must be an integer from ' + min + '-' + max);
  }
}

/**
 * Convert local fixture fields anchored to watch.now into Unix seconds.
 *
 * @param {Object} watchNow Fixture watch.now value.
 * @param {Object} overrides Local date/time overrides.
 * @returns {number} Unix seconds.
 */
function dateFromWatchNow(watchNow, overrides) {
  const date = new Date(
    watchNow.year,
    watchNow.month - 1,
    watchNow.day + (overrides.dayOffset || 0),
    overrides.hour || 0,
    overrides.minute || 0,
    overrides.second || 0,
    0
  );

  return Math.floor(date.getTime() / 1000);
}

/**
 * Normalize readable fixture weather fields into the runtime shape, in place.
 *
 * @param {Object} fixture Parsed fixture.
 * @returns {void}
 */
function normalizeWeather(fixture) {
  const watchNow = fixture && fixture.watch && fixture.watch.now;
  const weather = fixture && fixture.weather;

  if (!watchNow || !weather) {
    return;
  }

  if (typeof weather.startHour === 'number') {
    assertIntInRange('weather.startHour', weather.startHour, 0, 23);
    weather.startEpoch = dateFromWatchNow(watchNow, {
      dayOffset: weather.startDayOffset || 0,
      hour: weather.startHour,
      minute: 0,
      second: 0,
    });
    delete weather.startHour;
    delete weather.startDayOffset;
  }

  if (Array.isArray(weather.sunEvents)) {
    weather.sunEvents = weather.sunEvents.map((event) => {
      if (typeof event.epoch === 'number') {
        return event;
      }

      assertIntInRange('weather.sunEvents.hour', event.hour, 0, 23);
      assertIntInRange('weather.sunEvents.minute', event.minute || 0, 0, 59);
      return {
        type: event.type,
        epoch: dateFromWatchNow(watchNow, {
          dayOffset: event.dayOffset || 0,
          hour: event.hour,
          minute: event.minute || 0,
          second: event.second || 0,
        }),
      };
    });
  }
}

module.exports = { FixtureError, assertIntInRange, dateFromWatchNow, normalizeWeather };
