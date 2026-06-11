'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { generateFrames, parseHHMM, parseArgs } = require('../scripts/gen-timelapse-fixtures');
const { normalizeWeather } = require('../scripts/lib/fixture-time');

function run() {
  const outDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ww-tl-'));
  const files = generateFrames({ outDir });
  return { outDir, files };
}

test('generates 20 frames named timelapse-00..19', () => {
  const { outDir, files } = run();
  assert.equal(files.length, 20);
  for (let i = 0; i < 20; i++) {
    const nn = String(i).padStart(2, '0');
    assert.ok(fs.existsSync(path.join(outDir, `timelapse-${nn}.json`)), `missing ${nn}`);
  }
});

test('watch.now advances 5 min per frame from 20:45 (same day)', () => {
  const { outDir } = run();
  for (let i = 0; i < 20; i++) {
    const nn = String(i).padStart(2, '0');
    const fx = JSON.parse(fs.readFileSync(path.join(outDir, `timelapse-${nn}.json`), 'utf8'));
    const totalMin = 20 * 60 + 45 + i * 5;
    assert.equal(fx.watch.now.year, 2026);
    assert.equal(fx.watch.now.month, 6);
    assert.equal(fx.watch.now.day, 9);
    assert.equal(fx.watch.now.hour, Math.floor(totalMin / 60));
    assert.equal(fx.watch.now.minute, totalMin % 60);
    assert.equal(fx.watch.now.second, 0);
  }
});

test('startEpoch is fixed across frames, startHour removed, sunEvents+radar present', () => {
  const { outDir } = run();
  const epochs = new Set();
  for (let i = 0; i < 20; i++) {
    const nn = String(i).padStart(2, '0');
    const fx = JSON.parse(fs.readFileSync(path.join(outDir, `timelapse-${nn}.json`), 'utf8'));
    assert.equal(fx.weather.startHour, undefined);
    assert.equal(typeof fx.weather.startEpoch, 'number');
    epochs.add(fx.weather.startEpoch);
    assert.equal(fx.weather.sunEvents.length, 2);
    assert.equal(fx.weather.rainRadarExactMm.length, 18);
    assert.equal(fx.weather.rainRadarAreaMm.length, 18);
  }
  assert.equal(epochs.size, 1, 'startEpoch must be identical across all frames');
});

test('after normalize, every frame resolves to identical sun-event epochs', () => {
  const { outDir } = run();
  let ref = null;
  for (let i = 0; i < 20; i++) {
    const nn = String(i).padStart(2, '0');
    const fx = JSON.parse(fs.readFileSync(path.join(outDir, `timelapse-${nn}.json`), 'utf8'));
    normalizeWeather(fx);
    const epochs = fx.weather.sunEvents.map((e) => e.epoch);
    if (ref === null) ref = epochs;
    assert.deepEqual(epochs, ref);
  }
});

test('parseHHMM throws on malformed input', () => {
  assert.throws(() => parseHHMM('not-a-time'), /Expected HH:MM/);
  assert.throws(() => parseHHMM('2045'), /Expected HH:MM/);
});

test('generateFrames throws when the window crosses midnight', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ww-tl-mid-'));
  assert.throws(
    () => generateFrames({ outDir: dir, windowStart: '23:50', frames: 5, stepMin: 5 }),
    /crosses midnight/
  );
});

test('generateFrames rejects non-positive / non-integer frame counts', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ww-tl-bad-'));
  assert.throws(() => generateFrames({ outDir: dir, frames: 0 }), /positive integer/);
  assert.throws(() => generateFrames({ outDir: dir, frames: NaN }), /positive integer/);
  assert.throws(() => generateFrames({ outDir: dir, stepMin: 0 }), /positive integer/);
});

test('parseArgs throws when a space-separated flag is missing its value', () => {
  assert.throws(() => parseArgs(['--window-start']), /requires a value/);
  assert.throws(() => parseArgs(['--window-start', '--frames']), /requires a value/);
});

test('parseArgs parses valid flags', () => {
  assert.deepEqual(parseArgs(['--frames=3', '--window-start=21:00']),
    { frames: 3, windowStart: '21:00' });
});
