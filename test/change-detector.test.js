const test = require('node:test');
const assert = require('node:assert/strict');
const ChangeDetector = require('../src/pkjs/change-detector');

function withLocalStorage(map) {
  global.localStorage = {
    getItem: function(k) {
      return Object.prototype.hasOwnProperty.call(map, k) ? map[k] : null;
    },
    setItem: function(k, v) { map[k] = String(v); },
    removeItem: function(k) { delete map[k]; }
  };
}

test('a category comparator decides changed and receives the parsed cache', () => {
  withLocalStorage({ testKey: JSON.stringify({ A: 1 }) });
  let received;
  const cat = {
    name: 'test', cacheKey: 'testKey', keys: ['A'],
    comparator: function(subset, cached) { received = cached; return false; }
  };
  const result = new ChangeDetector([cat]).detect({ A: 2 });
  assert.equal(result.categories.length, 1);
  assert.equal(result.categories[0].changed, false);
  assert.deepEqual(received, { A: 1 });
});

test('a comparator receives null when no cache is present', () => {
  withLocalStorage({});
  let received = 'sentinel';
  const cat = {
    name: 'test', cacheKey: 'testKey', keys: ['A'],
    comparator: function(subset, cached) { received = cached; return true; }
  };
  const result = new ChangeDetector([cat]).detect({ A: 2 });
  assert.equal(result.categories[0].changed, true);
  assert.equal(received, null);
});

test('categories without a comparator use the default exact comparator', () => {
  withLocalStorage({ k: JSON.stringify({ A: 2 }) });
  const cat = { name: 'plain', cacheKey: 'k', keys: ['A'] };
  assert.equal(new ChangeDetector([cat]).detect({ A: 2 }).categories[0].changed, false);
  assert.equal(new ChangeDetector([cat]).detect({ A: 3 }).categories[0].changed, true);
});
