var WeatherProvider = require('./provider.js');
var request = WeatherProvider.request;

var BRIGHTSKY_BASE = 'https://api.brightsky.dev';
var MAX_DIST_METERS = 500000;
var FORECAST_HOURS = 24;
var HOUR_MS = 60 * 60 * 1000;

function celsiusToFahrenheit(celsius) {
    return celsius * 9 / 5 + 32;
}

/**
 * ISO 8601 forecast window starting at the current wall-clock hour and
 * covering FORECAST_HOURS buckets. Brightsky returns `hourly[0]` as the
 * bucket whose timestamp >= `date`, so anchoring `date` at the hour
 * boundary keeps `hourly[0]` on the bucket the user is currently inside.
 *
 * @returns {{ start: string, end: string }} ISO timestamps.
 */
function forecastWindow() {
    var startMs = Math.floor(Date.now() / HOUR_MS) * HOUR_MS;
    return {
        start: new Date(startMs).toISOString(),
        end: new Date(startMs + (FORECAST_HOURS - 1) * HOUR_MS).toISOString()
    };
}

var DwdProvider = function() {
    this._super.call(this);
    this.name = 'Brightsky (Deutscher Wetterdienst)';
    this.id = 'dwd';
};

DwdProvider.prototype = Object.create(WeatherProvider.prototype);
DwdProvider.prototype.constructor = DwdProvider;
DwdProvider.prototype._super = WeatherProvider;

DwdProvider.prototype.withDwdForecast = function(lat, lon, callback, onFailure) {
    var win = forecastWindow();
    var url = BRIGHTSKY_BASE + '/weather'
        + '?lat=' + lat
        + '&lon=' + lon
        + '&date=' + encodeURIComponent(win.start)
        + '&last_date=' + encodeURIComponent(win.end)
        + '&max_dist=' + MAX_DIST_METERS;
    console.log('Requesting ' + url);
    request(url, 'GET', function(response) {
        try {
            callback(JSON.parse(response).weather);
        }
        catch (ex) {
            onFailure({ stage: 'provider_data', code: 'dwd_forecast_parse_error' });
        }
    }, function(error) {
        console.log('[!] DWD forecast request failed: ' + JSON.stringify(error));
        onFailure({ stage: 'provider_data', code: 'dwd_forecast_' + error.code });
    });
};

DwdProvider.prototype.withDwdCurrent = function(lat, lon, callback, onFailure) {
    var url = BRIGHTSKY_BASE + '/current_weather'
        + '?lat=' + lat
        + '&lon=' + lon
        + '&max_dist=' + MAX_DIST_METERS;
    console.log('Requesting ' + url);
    request(url, 'GET', function(response) {
        try {
            callback(celsiusToFahrenheit(JSON.parse(response).weather.temperature));
        }
        catch (ex) {
            onFailure({ stage: 'provider_data', code: 'dwd_current_parse_error' });
        }
    }, function(error) {
        console.log('[!] DWD current request failed: ' + JSON.stringify(error));
        onFailure({ stage: 'provider_data', code: 'dwd_current_' + error.code });
    });
};

DwdProvider.prototype.withProviderData = function(lat, lon, force, onSuccess, onFailure) {
    this.withDwdForecast(lat, lon, (function(hourly) {
        this.withDwdCurrent(lat, lon, (function(currentTempF) {
            this.tempTrend = hourly.map(function(e) { return celsiusToFahrenheit(e.temperature); });
            this.precipTrend = hourly.map(function(e) { return e.precipitation_probability / 100; });
            this.rainTrend = hourly.map(function(e) { return e.precipitation; });
            this.startTime = Math.floor(Date.parse(hourly[0].timestamp) / 1000);
            this.currentTemp = currentTempF;
            onSuccess();
        }).bind(this), onFailure);
    }).bind(this), onFailure);
};

module.exports = DwdProvider;
