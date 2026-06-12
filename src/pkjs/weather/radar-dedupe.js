/**
 * Alignment-aware comparator for the rain-radar category. Unlike the default
 * exact comparator the other categories use, radar frames shift by one 5-min
 * slot each cycle, so a naive compare always reports "changed". This compares
 * the candidate against the last *sent* radar after aligning them in time.
 *
 * Let k = (newStart - oldStart) / SLOT_SECONDS be the slots advanced since the
 * last send. The overlap is old[k..23] vs new[0..overlapCount-1]; the
 * non-overlapping new tail new[overlapCount..23] is the part the watch does not
 * yet hold (it zero-pads those via its own self-advance). We treat the radar as
 * UNCHANGED (skip the send) iff the overlap matches and the tail is dry.
 */

var SLOT_SECONDS = 5 * 60;   // must match radar.js SLOT_SECONDS / RADAR_SLOT_SECONDS on the watch
var NUM_BARS = 24;

/**
 * Decide whether a candidate radar subset differs from the last-sent one.
 *
 * @param {Object} newSubset Candidate subset: {RAIN_RADAR_TREND_UINT8: number[],
 *   RAIN_RADAR_TREND_AREA_UINT8: number[], RAIN_RADAR_START: number}.
 * @param {Object|null} cachedSubset Last-sent subset in the same shape, or null
 *   when nothing was sent yet.
 * @returns {boolean} true when the radar should be sent (changed), false to skip.
 */
function radarComparator(newSubset, cachedSubset) {
    if (!cachedSubset) {
        return true;  // nothing sent yet
    }
    var newExact = newSubset.RAIN_RADAR_TREND_UINT8;
    var newArea = newSubset.RAIN_RADAR_TREND_AREA_UINT8;
    var newStart = newSubset.RAIN_RADAR_START;
    var oldExact = cachedSubset.RAIN_RADAR_TREND_UINT8;
    var oldArea = cachedSubset.RAIN_RADAR_TREND_AREA_UINT8;
    var oldStart = cachedSubset.RAIN_RADAR_START;

    if (!Array.isArray(newExact) || !Array.isArray(newArea)
        || !Array.isArray(oldExact) || !Array.isArray(oldArea)
        || typeof newStart !== 'number' || typeof oldStart !== 'number') {
        return true;  // malformed cache/candidate — resend to be safe
    }

    var deltaSec = newStart - oldStart;
    if (deltaSec < 0 || deltaSec % SLOT_SECONDS !== 0) {
        return true;  // clock moved backwards or not on a slot boundary — resend
    }
    var k = deltaSec / SLOT_SECONDS;
    var overlapCount = Math.max(0, NUM_BARS - k);
    var i;

    // Overlap must match for both arrays (new[i] is the same wall-clock slot as old[i+k]).
    for (i = 0; i < overlapCount; i += 1) {
        if (oldExact[i + k] !== newExact[i] || oldArea[i + k] !== newArea[i]) {
            return true;
        }
    }
    // Non-overlapping new tail must be dry. area >= exact, so checking area suffices.
    for (i = overlapCount; i < NUM_BARS; i += 1) {
        if (newArea[i] !== 0) {
            return true;
        }
    }
    return false;
}

module.exports = { radarComparator: radarComparator };
