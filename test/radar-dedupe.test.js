const test = require('node:test');
const assert = require('node:assert/strict');
const { radarComparator } = require('../src/pkjs/weather/radar-dedupe');

const SLOT = 5 * 60;
const N = 24;

function zeros() { return new Array(N).fill(0); }

function subset(exact, area, start) {
  return {
    RAIN_RADAR_TREND_UINT8: exact,
    RAIN_RADAR_TREND_AREA_UINT8: area,
    RAIN_RADAR_START: start
  };
}

test('no cache → changed (send the first time)', () => {
  assert.equal(radarComparator(subset(zeros(), zeros(), 0), null), true);
});

test('k=1, identical overlap, dry tail → unchanged (skip)', () => {
  const cached = subset(zeros(), zeros(), 0);
  const next = subset(zeros(), zeros(), SLOT);
  assert.equal(radarComparator(next, cached), false);
});

test('k=1, identical overlap, rain in tail → changed', () => {
  const cached = subset(zeros(), zeros(), 0);
  const area = zeros(); area[N - 1] = 7;   // newly-exposed last slot has rain
  assert.equal(radarComparator(subset(zeros(), area, SLOT), cached), true);
});

test('k=1, overlap differs (forecast revised) → changed', () => {
  const oldArea = zeros(); oldArea[5] = 4;
  const cached = subset(zeros(), oldArea, 0);
  // new[4] should mirror old[5]=4 but is 0 → mismatch
  assert.equal(radarComparator(subset(zeros(), zeros(), SLOT), cached), true);
});

test('k=1, overlap matches on a shifted rain bar, dry tail → unchanged', () => {
  const oldArea = zeros(); oldArea[5] = 4;
  const cached = subset(zeros(), oldArea, 0);
  const newArea = zeros(); newArea[4] = 4;  // same wall-clock slot, shifted left
  assert.equal(radarComparator(subset(zeros(), newArea, SLOT), cached), false);
});

test('k=0, identical → unchanged; differing → changed', () => {
  const cached = subset(zeros(), zeros(), 0);
  assert.equal(radarComparator(subset(zeros(), zeros(), 0), cached), false);
  const area = zeros(); area[0] = 1;
  assert.equal(radarComparator(subset(zeros(), area, 0), cached), true);
});

test('k>=24 (fully rolled), entire window dry → unchanged', () => {
  const cached = subset(zeros(), zeros(), 0);
  assert.equal(radarComparator(subset(zeros(), zeros(), 30 * SLOT), cached), false);
});

test('k>=24 (fully rolled), any rain in window → changed', () => {
  const cached = subset(zeros(), zeros(), 0);
  const area = zeros(); area[10] = 3;
  assert.equal(radarComparator(subset(zeros(), area, 30 * SLOT), cached), true);
});

test('newStart earlier than oldStart → changed', () => {
  const cached = subset(zeros(), zeros(), 10 * SLOT);
  assert.equal(radarComparator(subset(zeros(), zeros(), 9 * SLOT), cached), true);
});

test('newStart not an integer number of slots after oldStart → changed', () => {
  const cached = subset(zeros(), zeros(), 0);
  assert.equal(radarComparator(subset(zeros(), zeros(), SLOT + 30), cached), true);
});

test('HEADLINE: 3h of fetch-every-5-min with no rain → exactly one send', () => {
  // The first fetch has no cache → send, then cache it.
  const first = subset(zeros(), zeros(), 0);
  assert.equal(radarComparator(first, null), true);
  const cached = subset(zeros(), zeros(), 0);   // what was cached after the ACK
  let sends = 0;
  for (let k = 1; k <= 36; k += 1) {            // 36 slots = 3 hours
    if (radarComparator(subset(zeros(), zeros(), k * SLOT), cached)) {
      sends += 1;
    }
  }
  assert.equal(sends, 0);  // every subsequent fetch skips
});
