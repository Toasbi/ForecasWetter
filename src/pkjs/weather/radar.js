var WeatherProvider = require('./provider.js');
var request = WeatherProvider.request;

var BRIGHTSKY_BASE = 'https://api.brightsky.dev';
var DISTANCE_METERS = 1000;   // ~3x3 px tile, same point-accuracy as any larger tile
var NUM_BARS = 24;             // 24 frames * 5 min = 120 min

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
 * Fetch 2 hours of 5-minute rainfall from Bright Sky's /radar endpoint at the
 * given lat/lon and pass a 24-entry uint8 array (mm/h * 10) to `onSuccess`.
 *
 * Out-of-coverage (HTTP 200 with `radar: []`) returns all zeros via onSuccess.
 * Network or parse errors invoke onFailure with `{stage: 'radar', code: ...}`.
 *
 * @param {number} lat Latitude in decimal degrees.
 * @param {number} lon Longitude in decimal degrees.
 * @param {Function} onSuccess Receives a 24-entry number array, each 0..255.
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
                // Out of DWD coverage. Spec: return 24 zeros via onSuccess.
                onSuccess(zeroBars());
                return;
            }
            // Drop the first ("now") frame; the remaining frames cover
            // t+5 min .. t+120 min — exactly 24 5-minute bars.
            var frames = body.radar.slice(1);
            var xy = body.latlon_position;
            var out = zeroBars();
            var i;
            var grid;
            var raw;
            var frame;
            var hasXy = xy && isFinite(xy.x) && isFinite(xy.y);
            for (i = 0; i < NUM_BARS && i < frames.length; i += 1) {
                frame = frames[i];
                // Per-frame defensive checks: a malformed frame contributes a
                // zero bar rather than aborting the whole fetch.
                if (!frame) {
                    continue;
                }
                grid = frame.precipitation_5;
                if (!Array.isArray(grid) || grid.length === 0 || !Array.isArray(grid[0]) || grid[0].length === 0) {
                    continue;
                }
                raw = sampleBilinear(grid, hasXy ? xy : gridCentre(grid));
                out[i] = scaleToWireUnits(raw);
            }
            onSuccess(out);
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
