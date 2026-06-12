/**
 * Detects which categories of a candidate AppMessage payload changed versus the
 * last-sent state cached in localStorage. Each category decides "changed" via
 * its own comparator (defaulting to exact-serialized equality), so radar can
 * plug in alignment-aware logic without the detector knowing radar's wire shape.
 *
 * The result object is the single source for everything downstream in the
 * outbox: the outgoing AppMessage (changed subsets), the ACK-time cache writes
 * (changed {cacheKey, serialized}), and the dev-stats event descriptor.
 */

/**
 * Extract the subset of `payload` belonging to a category, in the category's
 * fixed key order so the serialization is stable across calls.
 *
 * @param {Object} payload Full candidate payload.
 * @param {Object} category Category descriptor ({name, cacheKey, keys}).
 * @returns {Object|null} Subset object, or null when none of the keys are present.
 */
function categorySubset(payload, category) {
    var subset = {};
    var present = false;
    category.keys.forEach(function(key) {
        if (Object.prototype.hasOwnProperty.call(payload, key)) {
            subset[key] = payload[key];
            present = true;
        }
    });
    return present ? subset : null;
}

/**
 * The default comparator: a category changed when its serialized form differs
 * from the cached one. Structural equality via JSON.stringify (categorySubset
 * emits keys in a fixed order, so the serialization is stable).
 *
 * @param {Object} newSubset Candidate category subset.
 * @param {Object|null} cachedSubset Last-sent subset, or null when none.
 * @returns {boolean} true when the subset changed.
 */
function exactComparator(newSubset, cachedSubset) {
    return JSON.stringify(newSubset) !== JSON.stringify(cachedSubset);
}

/**
 * @param {Object[]} categories Category descriptors
 *   ({name, cacheKey, keys, comparator?}). `comparator(newSubset, cachedSubset)`
 *   returns whether the category changed; omitted → exactComparator.
 * @constructor
 */
function ChangeDetector(categories) {
    this.categories = categories;
}

/**
 * Detect which categories changed versus their last-sent cache.
 *
 * @param {Object} payload Full candidate payload.
 * @returns {{categories: Array<{name: string, cacheKey: string, subset: Object, serialized: string, changed: boolean}>}}
 *     One entry per category present in the payload; `changed` is the result of
 *     the category's comparator against the parsed cache.
 */
ChangeDetector.prototype.detect = function(payload) {
    var entries = [];
    this.categories.forEach(function(category) {
        var subset = categorySubset(payload, category);
        if (subset === null) {
            return;  // Category absent from this payload.
        }
        var serialized = JSON.stringify(subset);
        var cached = localStorage.getItem(category.cacheKey);
        var cachedSubset = null;
        if (cached !== null) {
            try {
                cachedSubset = JSON.parse(cached);
            }
            catch (e) {
                cachedSubset = null;  // corrupt cache — treat as no cache
            }
        }
        var comparator = category.comparator || exactComparator;
        entries.push({
            name: category.name,
            cacheKey: category.cacheKey,
            subset: subset,
            serialized: serialized,
            changed: comparator(subset, cachedSubset)
        });
    });
    return { categories: entries };
};

module.exports = ChangeDetector;
