var WeatherProvider = require('./provider.js');
var request = WeatherProvider.request;

var BRIGHTSKY_BASE = 'https://api.brightsky.dev';
var DISTANCE_METERS = 1000;   // must match NEARBY_RADIUS_KM * 1000; Brightsky returns all cells within this radius
var NUM_BARS = 24;             // 24 frames * 5 min = 120 min
var NEARBY_RADIUS_KM = 1;      // disk radius for the "nearby" max signal; radar grid is ~1 km/cell
var SLOT_SECONDS = 5 * 60;     // wire-side slot width; must match RADAR_SLOT_SECONDS on the watch

/**
 * Fallback radar-start epoch when we can't read it off the response: the
 * next 5-minute wall-clock boundary strictly after now. Brightsky frames
 * land on wall-clock 5-min boundaries, so the kept slot-0 frame is at
 * floor(now/5min)*5min + 5min.
 *
 * @returns {number} Unix epoch seconds, multiple of SLOT_SECONDS.
 */
function nextSlotBoundaryEpoch() {
    var nowSec = Math.floor(Date.now() / 1000);
    return Math.floor(nowSec / SLOT_SECONDS) * SLOT_SECONDS + SLOT_SECONDS;
}

/**
 * Build the URL for the Brightsky /radar request.
 *
 * @param {number} lat Latitude in decimal degrees.
 * @param {number} lon Longitude in decimal degrees.
 * @returns {string} Fully-formed request URL.
 */
function buildRadarUrl(lat, lon) {
    return BRIGHTSKY_BASE + '/radar'
        + '?lat=' + lat
        + '&lon=' + lon
        + '&distance=' + DISTANCE_METERS
        + '&format=plain';
}

/**
 * Build a `[0, 0, ..., 0]` array of length NUM_BARS.
 *
 * @returns {number[]} 24-entry zero array.
 */
function zeroBars() {
    var out = new Array(NUM_BARS);
    var i;
    for (i = 0; i < NUM_BARS; i += 1) {
        out[i] = 0;
    }
    return out;
}

/**
 * Clamp `v` to the integer range [lo, hi].
 *
 * @param {number} v Value.
 * @param {number} lo Lower bound (inclusive).
 * @param {number} hi Upper bound (inclusive).
 * @returns {number} Clamped value.
 */
function clampInt(v, lo, hi) {
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/**
 * Bilinear-sample a 2-D grid at sub-pixel coordinates (xy.x, xy.y).
 *
 * `grid` is indexed `grid[row][col]` (outer = rows = y, inner = cols = x).
 * Coordinates outside the grid are clamped so the 2x2 neighbourhood always
 * lies fully inside.
 *
 * @param {number[][]} grid Rectangular 2-D array of numbers.
 * @param {{x: number, y: number}} xy Sub-pixel position.
 * @returns {number} Bilinearly interpolated value.
 */
function sampleBilinear(grid, xy) {
    var rows = grid.length;
    var cols = grid[0].length;
    var ix = Math.floor(xy.x);
    var iy = Math.floor(xy.y);
    var fx = xy.x - ix;
    var fy = xy.y - iy;
    var ix0 = clampInt(ix, 0, cols - 1);
    var ix1 = clampInt(ix + 1, 0, cols - 1);
    var iy0 = clampInt(iy, 0, rows - 1);
    var iy1 = clampInt(iy + 1, 0, rows - 1);
    var v00 = grid[iy0][ix0];
    var v10 = grid[iy0][ix1];
    var v01 = grid[iy1][ix0];
    var v11 = grid[iy1][ix1];
    return v00 * (1 - fx) * (1 - fy)
         + v10 * fx       * (1 - fy)
         + v01 * (1 - fx) * fy
         + v11 * fx       * fy;
}

/**
 * Find the maximum value among all grid cells whose centre lies at or
 * within `radius` grid units of the sub-pixel position (cx, cy)
 * (boundary inclusive — matches the `<=` comparison below).
 *
 * Distance is computed in squared form to avoid a sqrt per cell. The
 * helper is O(rows * cols) — fine on the small grids Brightsky returns
 * (typically 3x3 for distance=1000).
 *
 * @param {number[][]} grid Rectangular 2-D array of cell values.
 * @param {number} cx User sub-pixel column (Brightsky's latlon_position.x).
 * @param {number} cy User sub-pixel row    (Brightsky's latlon_position.y).
 * @param {number} radius Disk radius in grid units (1 grid unit ≈ 1 km).
 * @returns {number} Max cell value among cells inside the disk; 0 if none qualify.
 */
function maxOverDisk(grid, cx, cy, radius) {
    var rows = grid.length;
    var cols = grid[0].length;
    var r2 = radius * radius;
    var best = 0;
    var j;
    var i;
    var dx;
    var dy;
    var v;
    for (j = 0; j < rows; j += 1) {
        for (i = 0; i < cols; i += 1) {
            dx = i - cx;
            dy = j - cy;
            if (dx * dx + dy * dy <= r2) {
                v = grid[j][i];
                if (v > best) {
                    best = v;
                }
            }
        }
    }
    return best;
}

/**
 * Convert a radar cell value (0.01 mm per 5 min) into the watch's wire
 * format for rain bars: uint8 representing mm/h * 10.
 *
 * Factor: 0.01 mm/5min * 12 (5min/h) * 10 = v * 1.2.
 * Saturates at 255 (= 25.5 mm/h).
 *
 * @param {number} v Radar cell value in 0.01 mm / 5 min.
 * @returns {number} Integer in [0, 255].
 */
function scaleToWireUnits(v) {
    var scaled = Math.round(v * 1.2);
    if (scaled < 0) { return 0; }
    if (scaled > 255) { return 255; }
    return scaled;
}

/**
 * Default sub-pixel position when the response omits latlon_position.
 * Falls back to the geometric centre of the supplied grid so we still
 * return a sensible value rather than failing the whole fetch.
 *
 * @param {number[][]} grid Reference grid (used only for its dimensions).
 * @returns {{x: number, y: number}} Centre sub-pixel coordinates.
 */
function gridCentre(grid) {
    var rows = grid.length;
    var cols = grid[0].length;
    return {
        x: (cols - 1) / 2,
        y: (rows - 1) / 2
    };
}

/**
 * Fetch 2 hours of 5-minute rainfall from Bright Sky's /radar endpoint at
 * the given lat/lon. Computes two parallel signals from one response:
 *
 *   - exact:      bilinear sample at the user's sub-pixel position.
 *   - nearby_1km: max value among radar cells whose centre lies within
 *                 NEARBY_RADIUS_KM (1 km) of the user.
 *
 * Both arrays use the watch's existing wire convention (uint8, mm/h * 10).
 * The function guarantees `nearby_1km[i] >= exact[i]` by folding the
 * exact value into the nearby max — the user's own point is, by
 * definition, inside the disk centred on themselves.
 *
 * Out-of-coverage (HTTP 200 with `radar: []`) returns two arrays of
 * zeros via onSuccess. Network or parse errors invoke onFailure with
 * `{stage: 'radar', code: ...}`.
 *
 * `startEpoch` is the wall-clock timestamp of slot 0 (the first kept
 * frame after dropping the "now" frame), in Unix epoch seconds. Read
 * from Brightsky's frame timestamp so it lands on a 5-min boundary;
 * the watch's hour-axis tick suppression relies on this alignment.
 *
 * @param {number} lat Latitude in decimal degrees.
 * @param {number} lon Longitude in decimal degrees.
 * @param {Function} onSuccess Receives `{ exact, nearby_1km, startEpoch }`,
 *   where the two arrays are 24-entry uint8 (mm/h * 10) and startEpoch is
 *   slot 0's Unix epoch seconds.
 * @param {Function} onFailure Receives a `{stage, code}` failure object.
 * @returns {void}
 */
function withRadar2hRain(lat, lon, onSuccess, onFailure) {
    var url = buildRadarUrl(lat, lon);
    console.log('Requesting ' + url);

    request(
        url,
        'GET',
        function(response) {
            var body;
            try {
                body = JSON.parse(response);
            }
            catch (ex) {
                onFailure({ stage: 'radar', code: 'radar_parse_error' });
                return;
            }
            if (!body || !Array.isArray(body.radar)) {
                onFailure({ stage: 'radar', code: 'radar_missing_fields' });
                return;
            }
            if (body.radar.length === 0) {
                // Out of DWD coverage. Return two 24-zero arrays so
                // consumers see a flat signal rather than a
                // coverage-specific error code.
                onSuccess({
                    exact: zeroBars(),
                    nearby_1km: zeroBars(),
                    startEpoch: nextSlotBoundaryEpoch()
                });
                return;
            }
            // Drop the first ("now") frame; the remaining frames cover
            // t+5 min .. t+120 min — exactly 24 5-minute bars.
            var frames = body.radar.slice(1);
            var xy = body.latlon_position;
            var hasXy = Boolean(xy && isFinite(xy.x) && isFinite(xy.y));
            var exactOut = zeroBars();
            var nearbyOut = zeroBars();
            var startEpoch = nextSlotBoundaryEpoch();
            if (frames.length > 0 && frames[0] && typeof frames[0].timestamp === 'string') {
                var parsedMs = Date.parse(frames[0].timestamp);
                if (isFinite(parsedMs)) {
                    startEpoch = Math.floor(parsedMs / 1000);
                }
            }
            var i;
            var frame;
            var grid;
            var samplePos;
            var exactRaw;
            var nearbyRaw;
            for (i = 0; i < NUM_BARS && i < frames.length; i += 1) {
                frame = frames[i];
                // Per-frame defensive checks: a malformed frame contributes
                // a (0, 0) pair rather than aborting the whole fetch.
                if (!frame) {
                    continue;
                }
                grid = frame.precipitation_5;
                if (!Array.isArray(grid) || grid.length === 0 || !Array.isArray(grid[0]) || grid[0].length === 0) {
                    continue;
                }
                samplePos = hasXy ? xy : gridCentre(grid);
                exactRaw = sampleBilinear(grid, samplePos);
                nearbyRaw = maxOverDisk(grid, samplePos.x, samplePos.y, NEARBY_RADIUS_KM);
                // Invariant guard. A pure disk-max can fall below the
                // bilinear sample at corner sub-pixel positions, where
                // the bilinear's diagonal neighbour sits ~sqrt(2) km
                // away (outside the 1 km disk) yet still carries a
                // small weight. Folding exactRaw into nearbyRaw keeps
                // the planned UI invariant `nearby >= exact` true for
                // every frame.
                if (exactRaw > nearbyRaw) {
                    nearbyRaw = exactRaw;
                }
                exactOut[i] = scaleToWireUnits(exactRaw);
                nearbyOut[i] = scaleToWireUnits(nearbyRaw);
            }
            onSuccess({ exact: exactOut, nearby_1km: nearbyOut, startEpoch: startEpoch });
        },
        function(error) {
            console.log('[!] Radar request failed: ' + JSON.stringify(error));
            onFailure({ stage: 'radar', code: 'radar_' + error.code });
        }
    );
}

module.exports = {
    withRadar2hRain: withRadar2hRain
};
