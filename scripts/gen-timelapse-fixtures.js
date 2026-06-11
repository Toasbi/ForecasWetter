#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const { dateFromWatchNow } = require('./lib/fixture-time');

const BASE_PATH = path.join('fixtures', 'berlin.json');

/**
 * Parse an "HH:MM" string into {hour, minute}.
 *
 * @param {string} hhmm Time string like "20:45".
 * @returns {{hour: number, minute: number}} Parsed components.
 */
function parseHHMM(hhmm) {
  const m = /^(\d{1,2}):(\d{2})$/.exec(hhmm);
  if (!m) {
    throw new Error('Expected HH:MM, got "' + hhmm + '"');
  }
  return { hour: Number(m[1]), minute: Number(m[2]) };
}

/**
 * Build the time-lapse frame fixtures and write them to disk.
 *
 * @param {Object} [opts] Options.
 * @param {string} [opts.outDir="fixtures"] Directory to write timelapse-NN.json into.
 * @param {number} [opts.frames=20] Number of frames.
 * @param {number} [opts.stepMin=5] Minutes between frames (matches radar 5-min cadence).
 * @param {string} [opts.windowStart="20:45"] First frame's watch.now time on the base date.
 * @param {string} [opts.forecastStart="20:45"] Fixed forecast/radar anchor time on the base date.
 * @returns {string[]} Paths of the written fixture files.
 */
function generateFrames(opts = {}) {
  const outDir = opts.outDir ?? 'fixtures';
  const frames = opts.frames ?? 20;
  const stepMin = opts.stepMin ?? 5;
  const windowStart = parseHHMM(opts.windowStart ?? '20:45');
  const forecastStart = parseHHMM(opts.forecastStart ?? '20:45');

  if (!Number.isInteger(frames) || frames < 1) {
    throw new Error('frames must be a positive integer, got ' + opts.frames);
  }
  if (!Number.isInteger(stepMin) || stepMin < 1) {
    throw new Error('stepMin must be a positive integer, got ' + opts.stepMin);
  }

  const base = JSON.parse(fs.readFileSync(BASE_PATH, 'utf8'));
  const baseNow = base.watch.now;

  // Fixed forecast/radar anchor (RAIN_RADAR_START === weather.startEpoch). Holding
  // this constant while watch.now advances makes the now-marker slide across the
  // forecast and the radar current-slot step forward.
  const startEpoch = dateFromWatchNow(baseNow, {
    hour: forecastStart.hour,
    minute: forecastStart.minute,
  });

  fs.mkdirSync(outDir, { recursive: true });
  const written = [];

  for (let i = 0; i < frames; i++) {
    const totalMin = windowStart.hour * 60 + windowStart.minute + i * stepMin;
    if (totalMin >= 24 * 60) {
      throw new Error('Frame ' + i + ' crosses midnight; narrow the window or frame count');
    }
    const frame = JSON.parse(JSON.stringify(base));
    frame.watch.now = {
      ...baseNow,
      hour: Math.floor(totalMin / 60),
      minute: totalMin % 60,
      second: 0,
    };
    // Pin the forecast/radar anchor and drop the readable startHour so
    // prepare-fixture.js leaves startEpoch untouched. sunEvents stay in
    // dayOffset/hour/minute form: same calendar day => identical epochs.
    delete frame.weather.startHour;
    delete frame.weather.startDayOffset;
    frame.weather.startEpoch = startEpoch;

    const nn = String(i).padStart(2, '0');
    const outPath = path.join(outDir, `timelapse-${nn}.json`);
    fs.writeFileSync(outPath, JSON.stringify(frame, null, 2) + '\n');
    written.push(outPath);
  }

  return written;
}

/**
 * Read --key=value / --key value flags from argv into an options object.
 *
 * @param {string[]} argv Process args (process.argv.slice(2)).
 * @returns {Object} Parsed options for generateFrames.
 */
function parseArgs(argv) {
  const opts = {};
  const map = {
    'out-dir': 'outDir',
    'frames': 'frames',
    'step-min': 'stepMin',
    'window-start': 'windowStart',
    'forecast-start': 'forecastStart',
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    const eq = arg.indexOf('=');
    let key;
    let val;
    if (arg.startsWith('--') && eq !== -1) {
      key = arg.slice(2, eq);
      val = arg.slice(eq + 1);
    } else if (arg.startsWith('--')) {
      key = arg.slice(2);
      val = argv[++i];
      if (val === undefined || val.startsWith('--')) {
        throw new Error('Flag --' + key + ' requires a value');
      }
    } else {
      continue;
    }
    const optKey = map[key];
    if (!optKey) {
      continue;
    }
    opts[optKey] = (optKey === 'frames' || optKey === 'stepMin') ? Number(val) : val;
  }
  return opts;
}

if (require.main === module) {
  const written = generateFrames(parseArgs(process.argv.slice(2)));
  console.log('Wrote ' + written.length + ' time-lapse fixtures to ' + path.dirname(written[0]));
}

module.exports = { generateFrames, parseHHMM, parseArgs };
