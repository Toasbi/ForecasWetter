
var WundergroundProvider = require('./weather/wunderground.js');
var OpenWeatherMapProvider = require('./weather/openweathermap.js')
var DwdProvider = require('./weather/dwd.js');
var WeatherProvider = require('./weather/provider.js');
var createTelemetryClient = require('./telemetry.js');
var Clay = require('./clay/_source.js');
var clayConfig = require('./clay/config.js');
var customClay = require('./clay/inject.js');
var storageKeys = require('./storage-keys.js');
var pkg = require('../../package.json');
var activeFixture = require('./active-fixture.generated.js');
var pebbleColors = require('./pebble-colors.js');

/**
 * Full release-notification manifest (dev: force-show by version). Omitted from bundle if missing.
 *
 * @returns {Object|null} Parsed release-notifications.json or null.
 */
function loadReleaseNotificationsManifest() {
    try {
        return require('../../release-notifications.json');
    }
    catch (ex) {
        return null;
    }
}

var releaseNotificationsManifest = loadReleaseNotificationsManifest();
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false });
/**
 * @type {{
 *     fetchInProgress: boolean,
 *     pendingStartupFetch: boolean,
 *     settings?: Object,
 *     telemetry?: Object,
 *     provider?: Object,
 *     watchInfo?: Object,
 *     devConfig?: Object
 * }}
 */
var app = {};  // Namespace for global app variables
var KEY_MAX_NOTIFIED_VERSION = 'max_notified_version';
var KEY_FETCH_ATTEMPT = storageKeys.FETCH_ATTEMPT_KEY;
var KEY_LAST_FETCH_SUCCESS = storageKeys.LAST_FETCH_SUCCESS_KEY;
var KEY_LAST_FETCH_ATTEMPT = storageKeys.LAST_FETCH_ATTEMPT_KEY;
var KEY_GEOCODE_CACHE = storageKeys.GEOCODE_CACHE_KEY;
var KEY_GEOCODE_BACKOFF = storageKeys.GEOCODE_BACKOFF_KEY;
var KEY_V1_34_0_WEEKEND_HOLIDAY_COLOR_MIGRATION = 'v1.34.0_weekend_holiday_color_migration';
var KEY_LAST_IS_SLEEPING = storageKeys.LAST_IS_SLEEPING_KEY;
var DEFAULT_COLOR_WHITE = pebbleColors.GColorWhite;
var DEFAULT_COLOR_FOLLY = pebbleColors.GColorFolly;

app.fetchInProgress = false;
app.pendingStartupFetch = false;

(function initLastIsSleeping() {
    var raw = localStorage.getItem(KEY_LAST_IS_SLEEPING);
    app.lastIsSleeping = raw === 'true';   // default false when missing
})();

Pebble.addEventListener('appmessage', function(e) {
    var payload = e && e.payload;

    if (!payload || !Object.prototype.hasOwnProperty.call(payload, 'WATCH_HAS_FORECAST_DATA')) {
        return;
    }

    var hasForecastData = Boolean(payload.WATCH_HAS_FORECAST_DATA);

    if (hasForecastData) {
        console.log('Watch reported valid forecast data at startup.');
        app.pendingStartupFetch = false;
        return;
    }

    console.log('Watch reported no forecast data at startup.');
    app.pendingStartupFetch = true;

    if (app.provider) {
        app.pendingStartupFetch = false;
        fetch(app.provider, true);
    }
});

Pebble.addEventListener('showConfiguration', function(e) {
    // Set the userData here rather than in the Clay() constructor so it's actually up to date
    clay.meta.userData.lastFetchSuccess = localStorage.getItem(KEY_LAST_FETCH_SUCCESS);
    clay.meta.userData.lastFetchAttempt = localStorage.getItem(KEY_LAST_FETCH_ATTEMPT);
    Pebble.openURL(clay.generateUrl());
    console.log('Showing clay: ' + JSON.stringify(getClaySettings()));
});

Pebble.addEventListener('webviewclosed', function(e) {
    if (e && !e.response) {
        return;
    }

    clay.getSettings(e.response, false);  // This triggers the update in localStorage
    app.settings = getClaySettings();  // This reads from localStorage in sensible format
    app.telemetry = createTelemetryClient(getRuntimeTelemetryConfig());
    refreshProvider();
    sendClaySettings();

    // Fetching goes last, after other settings have been handled
    if (app.settings.fetch === true) {
        console.log('Force fetch!');
        fetch(app.provider, true);
    }
    console.log('Closing clay: ' + JSON.stringify(getClaySettings()));
});

// Listen for when the watchface is opened
Pebble.addEventListener('ready',
    function (e) {
        var migratedWeekendHolidayColors;

        app.devConfig = getDevConfig();
        maybeHandleDevStorageReset(app.devConfig);
        var hadExistingInstall = localStorage.getItem('clay-settings') !== null;
        maybeShowReleaseNotification(
            hadExistingInstall,
            app.devConfig.forceShowReleaseNotificationOnBoot
        );
        clayTryDefaults();
        migratedWeekendHolidayColors = clayTryWeekendHolidayColorMigration();
        clayTryDevConfig(app.devConfig);
        clayTryFixtureSettings(activeFixture);
        console.log('PebbleKit JS ready!');
        app.settings = getClaySettings();
        try {
            app.watchInfo = Pebble.getActiveWatchInfo();
        }
        catch (ex) {
            app.watchInfo = null;
            console.log('Unable to read watch info: ' + ex.message);
        }
        app.telemetry = createTelemetryClient(getRuntimeTelemetryConfig());
        refreshProvider();
        if (activeFixture) {
            sendClaySettings(function() {
                sendFixtureWeather(activeFixture);
            }, function() {
                sendFixtureWeather(activeFixture);
            });
            return;
        }
        if (migratedWeekendHolidayColors) {
            sendClaySettings(markWeekendHolidayColorMigrationComplete);
        }
        if (app.pendingStartupFetch) {
            app.pendingStartupFetch = false;
            fetch(app.provider, true);
        }
        startTick();
    }
);

/**
 * Build telemetry runtime config from package.json.
 *
 * @returns {{enabled: boolean, endpoint: string, appVersion: string, buildProfile: string}} Runtime telemetry config.
 */
function getRuntimeTelemetryConfig() {
    var telemetry = pkg.telemetry || {};
    var endpoint = typeof telemetry.endpoint === 'string' ? telemetry.endpoint : '';
    var telemetryEnabled = !app.settings || app.settings.telemetryEnabled !== false;

    return {
        enabled: telemetryEnabled,
        endpoint: endpoint,
        appVersion: pkg.version,
        buildProfile: pkg.buildProfile
    };
}

/**
 * Parse a semver-like string into numeric major/minor/patch parts.
 *
 * @param {string} v Version string such as "1.25.0" or "v1.25.0-beta+build".
 * @returns {number[]} Tuple-like array: [major, minor, patch].
 */
function parseSemver(v) {
    var core = String(v || '0.0.0').replace(/^v/, '').split('-')[0].split('+')[0];
    var p = core.split('.');
    return [
        parseInt(p[0], 10) || 0,
        parseInt(p[1], 10) || 0,
        parseInt(p[2], 10) || 0
    ];
}

/**
 * Compare two semver-like version strings.
 *
 * @param {string} a Left-hand version.
 * @param {string} b Right-hand version.
 * @returns {number} 1 when a>b, -1 when a<b, 0 when equal.
 */
function compareSemver(a, b) {
    var pa = parseSemver(a);
    var pb = parseSemver(b);
    if (pa[0] !== pb[0]) return pa[0] > pb[0] ? 1 : -1;
    if (pa[1] !== pb[1]) return pa[1] > pb[1] ? 1 : -1;
    if (pa[2] !== pb[2]) return pa[2] > pb[2] ? 1 : -1;
    return 0;
}

/**
 * Normalize a release notification entry into title/body or null.
 *
 * @param {*} releaseNotification Field from package.json.
 * @returns {{title: string, body: string}|null} Payload or null when disabled/empty.
 */
function normalizeReleaseNotificationPayload(releaseNotification) {
    if (!releaseNotification || typeof releaseNotification !== 'object' || Array.isArray(releaseNotification)) {
        return null;
    }
    var title = releaseNotification.title ? String(releaseNotification.title).trim() : '';
    var body = releaseNotification.body ? String(releaseNotification.body).trim() : '';
    if (title === '' || body === '') {
        return null;
    }
    return { title: title, body: body };
}

/**
 * Normalize bundled pkg.releaseNotification into title/body or null.
 *
 * @param {Object|undefined} releaseNotification Field from package.json.
 * @returns {{title: string, body: string}|null} Payload or null when disabled/empty.
 */
function getBundledReleaseNotificationPayload(releaseNotification) {
    if (
        !releaseNotification ||
        releaseNotification.enabled !== true
    ) {
        return null;
    }
    return normalizeReleaseNotificationPayload(releaseNotification);
}

/**
 * Read package.json releaseNotifications, with legacy releaseNotification fallback.
 *
 * @returns {Object} Version-keyed release notification payloads.
 */
function getBundledReleaseNotifications() {
    var notifications = {};
    var bundled = pkg.releaseNotifications;
    var versionKey;
    var payload;

    if (bundled && typeof bundled === 'object' && !Array.isArray(bundled)) {
        for (versionKey in bundled) {
            if (Object.prototype.hasOwnProperty.call(bundled, versionKey)) {
                payload = normalizeReleaseNotificationPayload(bundled[versionKey]);
                if (payload !== null) {
                    notifications[versionKey] = payload;
                }
            }
        }
    }

    payload = getBundledReleaseNotificationPayload(pkg.releaseNotification);
    if (payload !== null && typeof pkg.version === 'string') {
        notifications[pkg.version] = payload;
    }

    return notifications;
}

/**
 * Find the newest bundled release notification that has not been shown yet.
 *
 * @param {string} maxNotified Highest notification version already shown.
 * @param {string} appVersion Running app version.
 * @returns {{version: string, title: string, body: string}|null} Latest unseen payload, or null.
 */
function getLatestUnseenReleaseNotification(maxNotified, appVersion) {
    var notifications = getBundledReleaseNotifications();
    var versions = Object.keys(notifications).filter(function(versionKey) {
        return (
            compareSemver(versionKey, maxNotified) > 0 &&
            compareSemver(versionKey, appVersion) <= 0
        );
    }).sort(compareSemver);
    var latestVersion;
    var payload;

    if (versions.length === 0) {
        return null;
    }

    latestVersion = versions[versions.length - 1];
    payload = notifications[latestVersion];
    return {
        version: latestVersion,
        title: payload.title,
        body: payload.body
    };
}

/**
 * Look up a release notification in release-notifications.json (dev force-show).
 *
 * @param {string} versionKey Exact version key, e.g. "1.26.0".
 * @returns {{title: string, body: string}|null} Payload or null when missing/invalid.
 */
function getReleaseNotificationFromManifest(versionKey) {
    var manifest = releaseNotificationsManifest;
    if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
        return null;
    }
    var entry = Object.prototype.hasOwnProperty.call(manifest, versionKey)
        ? manifest[versionKey]
        : undefined;
    if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
        return null;
    }
    var title = entry.title ? String(entry.title).trim() : '';
    var body = entry.body ? String(entry.body).trim() : '';
    if (title === '' || body === '') {
        return null;
    }
    return { title: title, body: body };
}

/**
 * Parse dev-config force-show value: non-empty string = manifest version key.
 *
 * @param {*} forceVersionSpec From dev-config.forceShowReleaseNotificationOnBoot.
 * @returns {string} Trimmed version key or '' when disabled.
 */
function normalizeForceReleaseVersionSpec(forceVersionSpec) {
    if (typeof forceVersionSpec !== 'string') {
        return '';
    }
    return forceVersionSpec.trim();
}

/**
 * Show the release notification exactly once for eligible upgrades, or every boot when dev forces a manifest version.
 *
 * @param {boolean} hadExistingInstall True when this launch is not first install.
 * @param {*} forceVersionSpec Dev: exact version key in release-notifications.json (e.g. "1.26.0"), or falsy.
 * @returns {void}
 */
function maybeShowReleaseNotification(hadExistingInstall, forceVersionSpec) {
    var appVersion = pkg.version;
    var forceKey = normalizeForceReleaseVersionSpec(forceVersionSpec);
    var forcePayload = forceKey !== '' ? getReleaseNotificationFromManifest(forceKey) : null;
    if (forceKey !== '' && !forcePayload) {
        console.log(
            '[release-notification] force version ' + JSON.stringify(forceKey) +
            ' not found or invalid in release-notifications.json'
        );
    }

    var maxNotified = localStorage.getItem(KEY_MAX_NOTIFIED_VERSION) || '0.0.0';
    var unseenNotification = getLatestUnseenReleaseNotification(maxNotified, appVersion);
    var isNewer = compareSemver(appVersion, maxNotified) > 0;
    var shouldNotifyUpgrade = hadExistingInstall && isNewer && unseenNotification !== null;
    var shouldNotifyForce = forcePayload !== null;
    var shouldNotify = shouldNotifyUpgrade || shouldNotifyForce;
    var title = '';
    var body = '';
    if (shouldNotifyForce) {
        title = forcePayload.title;
        body = forcePayload.body;
    }
    else if (shouldNotifyUpgrade) {
        title = unseenNotification.title;
        body = unseenNotification.body;
    }

    console.log(
        '[release-notification] appVersion=' + appVersion +
        ' hadExistingInstall=' + hadExistingInstall +
        ' maxNotified=' + maxNotified +
        ' isNewer=' + isNewer +
        ' forceVersionKey=' + (forceKey !== '' ? forceKey : '(none)') +
        ' shouldNotify=' + shouldNotify +
        ' shouldNotifyUpgrade=' + shouldNotifyUpgrade +
        ' shouldNotifyForce=' + shouldNotifyForce +
        ' unseenVersion=' + (unseenNotification ? unseenNotification.version : '(none)')
    );

    if (!shouldNotify) {
        console.log('[release-notification] skip');
    }

    if (shouldNotify) {
        console.log('[release-notification] showing notification');
        Pebble.showSimpleNotificationOnPebble(title, body);
    }

    if (shouldNotifyUpgrade) {
        localStorage.setItem(KEY_MAX_NOTIFIED_VERSION, unseenNotification.version);
        console.log('[release-notification] set max_notified_version=' + unseenNotification.version);
    }
    else if (!hadExistingInstall && isNewer) {
        localStorage.setItem(KEY_MAX_NOTIFIED_VERSION, appVersion);
        console.log('[release-notification] first install, set max_notified_version=' + appVersion);
    }
    else {
        console.log('[release-notification] keep max_notified_version=' + maxNotified);
    }
}

/**
 * Optionally edit PKJS localStorage on boot when enabled in dev-config.js.
 *
 * @param {Object} devConfig Developer configuration object.
 * @returns {void}
 */
function maybeHandleDevStorageReset(devConfig) {
    var shouldClear = Boolean(devConfig && devConfig.clearPkjsStorageOnBoot);
    var shouldResetV134WeekendHolidayColorMigration = Boolean(
        devConfig &&
        devConfig.resetV134WeekendHolidayColorMigration
    );
    var forcedMaxNotifiedVersion = devConfig &&
        typeof devConfig.maxNotifiedVersion === 'string'
        ? devConfig.maxNotifiedVersion.trim()
        : '';

    if (shouldClear) {
        console.log('[dev] clearPkjsStorageOnBoot=true, clearing localStorage');
        localStorage.clear();
    }

    if (forcedMaxNotifiedVersion !== '') {
        console.log('[dev] maxNotifiedVersion=' + forcedMaxNotifiedVersion + ', setting release notification marker');
        localStorage.setItem(KEY_MAX_NOTIFIED_VERSION, forcedMaxNotifiedVersion);
    }

    if (shouldResetV134WeekendHolidayColorMigration) {
        console.log('[dev] resetV134WeekendHolidayColorMigration=true, clearing migration marker');
        localStorage.removeItem(KEY_V1_34_0_WEEKEND_HOLIDAY_COLOR_MIGRATION);
    }
}

/**
 * Read the persisted weather fetch attempt counter.
 *
 * @returns {number} Non-negative integer attempt counter.
 */
function getFetchAttemptCounter() {
    var raw = localStorage.getItem(KEY_FETCH_ATTEMPT);
    var parsed = Number(raw);

    if (!isFinite(parsed) || parsed < 0) {
        return 0;
    }

    return Math.floor(parsed);
}

/**
 * Increment and persist the weather fetch attempt counter.
 *
 * @returns {number} New attempt number after increment.
 */
function incrementFetchAttemptCounter() {
    var nextAttempt = getFetchAttemptCounter() + 1;
    localStorage.setItem(KEY_FETCH_ATTEMPT, String(nextAttempt));
    return nextAttempt;
}

/**
 * Reset the weather fetch attempt counter after success.
 *
 * @returns {void}
 */
function resetFetchAttemptCounter() {
    localStorage.setItem(KEY_FETCH_ATTEMPT, '0');
}

function startTick() {
    console.log('Tick from PKJS!');
    tryFetch(app.provider);
    setTimeout(startTick, 60 * 1000); // 60 * 1000 milsec = 1 minute
}

function sendClaySettings(onSuccess, onFailure) {
    var payload = {
        "CLAY_CELSIUS": app.settings.temperatureUnits === 'c',
        "CLAY_TIME_LEAD_ZERO": app.settings.timeLeadingZero,
        "CLAY_AXIS_12H": app.settings.axisTimeFormat === '12h',
        "CLAY_COLOR_TODAY": app.settings.hasOwnProperty('colorToday') ? app.settings.colorToday : DEFAULT_COLOR_WHITE,
        "CLAY_START_MON": app.settings.weekStartDay === 'mon',
        "CLAY_PREV_WEEK": app.settings.firstWeek === 'prev',
        "CLAY_TIME_FONT": ['roboto', 'leco', 'bitham'].indexOf(app.settings.timeFont),
        "CLAY_SHOW_QT": app.settings.showQt,
        "CLAY_SHOW_BT": app.settings.btIcons === "connected" || app.settings.btIcons === "both",
        "CLAY_SHOW_BT_DISCONNECT": app.settings.btIcons === "disconnected" || app.settings.btIcons === "both",
        "CLAY_VIBE": app.settings.vibe,
        "CLAY_SHOW_AM_PM": app.settings.timeShowAmPm,
        "CLAY_COLOR_SUNDAY": app.settings.hasOwnProperty('colorSunday') ? app.settings.colorSunday : DEFAULT_COLOR_FOLLY,
        "CLAY_COLOR_SATURDAY": app.settings.hasOwnProperty('colorSaturday') ? app.settings.colorSaturday : DEFAULT_COLOR_FOLLY,
        "CLAY_COLOR_US_FEDERAL": app.settings.hasOwnProperty('colorUSFederal') ? app.settings.colorUSFederal : DEFAULT_COLOR_FOLLY,
        "CLAY_COLOR_TIME": app.settings.hasOwnProperty('colorTime') ? app.settings.colorTime : DEFAULT_COLOR_WHITE,
        "CLAY_DAY_NIGHT_SHADING": app.settings.hasOwnProperty('dayNightShading') ? app.settings.dayNightShading : true,
        "CLAY_TOP_VIEW_DEFAULT": app.settings.topViewDefault === 'rain_radar' ? 1 : 0,
    }
    Pebble.sendAppMessage(payload, function() {
        console.log('Message sent successfully: ' + JSON.stringify(payload));
        if (typeof onSuccess === 'function') {
            onSuccess();
        }
    }, function(e) {
        console.log('Message failed: ' + JSON.stringify(e));
        if (typeof onFailure === 'function') {
            onFailure(e);
        }
    });
}

function refreshProvider() {
    var oldLocation = app.provider ? app.provider.location : null;
    setProvider(app.settings.provider);
    app.provider.location = app.settings.location === '' ? null : app.settings.location;

    // Clear geocode cache when location changes so a fresh lookup always happens
    if (oldLocation !== app.provider.location) {
        localStorage.removeItem(KEY_GEOCODE_CACHE);
        localStorage.removeItem(KEY_GEOCODE_BACKOFF);
    }
}

function setProvider(providerId) {
    switch (providerId) {
        case 'openweathermap':
            app.provider = new OpenWeatherMapProvider(app.settings.owmApiKey);
            break;
        case 'dwd':
            app.provider = new DwdProvider();
            break;
        case 'wunderground':
            app.provider = new WundergroundProvider();
            break;
        default:
            console.log('Unknown provider: "' + providerId + '", defaulting to wunderground');
            clay.setSettings("provider", "wunderground");
            app.provider = new WundergroundProvider();
    }
    console.log('Set provider: ' + app.provider.name);
}

function clayTryDefaults() {
    /* Clay only considers `defaultValue` upon first startup, but we need
     * defaults set even if the user has not made a custom config
     */
    var persistClayString = localStorage.getItem('clay-settings');
    var defaults = getDefaultClaySettings();
    var persistClay;
    var prop;
    if (persistClayString === null) {
        console.log('No clay settings found, setting defaults');
        localStorage.setItem('clay-settings', JSON.stringify(defaults));
        return;
    }

    try {
        persistClay = JSON.parse(persistClayString);
    }
    catch (ex) {
        console.log('Malformed clay settings found, resetting defaults');
        localStorage.setItem('clay-settings', JSON.stringify(defaults));
        return;
    }

    for (prop in defaults) {
        if (
            Object.prototype.hasOwnProperty.call(defaults, prop) &&
            !Object.prototype.hasOwnProperty.call(persistClay, prop)
        ) {
            persistClay[prop] = defaults[prop];
        }
    }
    localStorage.setItem('clay-settings', JSON.stringify(persistClay));

}

/**
 * Get the full Clay settings defaults needed to send a complete config payload.
 *
 * @returns {Object} Default Clay-compatible settings.
 */
function getDefaultClaySettings() {
    return {
        provider: 'wunderground',
        owmApiKey: '',
        fetch: false,
        location: '',
        temperatureUnits: 'f',
        dayNightShading: true,
        timeLeadingZero: false,
        timeShowAmPm: false,
        axisTimeFormat: '24h',
        timeFont: 'roboto',
        colorTime: DEFAULT_COLOR_WHITE,
        weekStartDay: 'sun',
        firstWeek: 'prev',
        colorToday: 0,
        colorSunday: DEFAULT_COLOR_FOLLY,
        colorSaturday: DEFAULT_COLOR_FOLLY,
        colorUSFederal: DEFAULT_COLOR_FOLLY,
        showQt: true,
        vibe: false,
        btIcons: 'both',
        telemetryEnabled: true
    };
}

/**
 * Move existing installs from the old all-white weekend/holiday defaults to the
 * current highlighted default while preserving any customized color set.
 *
 * @returns {boolean} True when the migrated settings should be sent to the watch.
 */
function clayTryWeekendHolidayColorMigration() {
    var persistClayString = localStorage.getItem('clay-settings');
    var persistClay;

    if (
        persistClayString === null ||
        localStorage.getItem(KEY_V1_34_0_WEEKEND_HOLIDAY_COLOR_MIGRATION) !== null
    ) {
        return false;
    }

    try {
        persistClay = JSON.parse(persistClayString);
    }
    catch (ex) {
        console.log('Malformed clay settings found, skipping weekend/holiday color migration');
        return false;
    }

    if (
        persistClay.colorSunday === DEFAULT_COLOR_WHITE &&
        persistClay.colorSaturday === DEFAULT_COLOR_WHITE &&
        persistClay.colorUSFederal === DEFAULT_COLOR_WHITE
    ) {
        persistClay.colorSunday = DEFAULT_COLOR_FOLLY;
        persistClay.colorSaturday = DEFAULT_COLOR_FOLLY;
        persistClay.colorUSFederal = DEFAULT_COLOR_FOLLY;
        localStorage.setItem('clay-settings', JSON.stringify(persistClay));
        console.log('Migrated weekend/holiday color defaults to Folly');
        return true;
    }

    if (
        persistClay.colorSunday === DEFAULT_COLOR_FOLLY &&
        persistClay.colorSaturday === DEFAULT_COLOR_FOLLY &&
        persistClay.colorUSFederal === DEFAULT_COLOR_FOLLY
    ) {
        return true;
    }

    markWeekendHolidayColorMigrationComplete();
    return false;
}

/**
 * Mark the v1.34.0 weekend/holiday color migration as complete.
 *
 * @returns {void}
 */
function markWeekendHolidayColorMigrationComplete() {
    localStorage.setItem(KEY_V1_34_0_WEEKEND_HOLIDAY_COLOR_MIGRATION, '1');
}

function getDevConfig() {
    try {
        return require('./dev-config.js');
    }
    catch (ex) {
        console.log('No developer configuration file found');
        return {};
    }
}

function clayTryDevConfig(devConfig) {
    /* Use values from a dev-config.js file to configure clay settings
     * by iterating over the exported properties
     */
    var persistClay;
    var prop;

    var localOnlyDevConfigKeys = {
        clearPkjsStorageOnBoot: true,
        forceShowReleaseNotificationOnBoot: true,
        maxNotifiedVersion: true,
        resetV134WeekendHolidayColorMigration: true,
    };

    persistClay = getClaySettings();
    for (prop in devConfig) {
        if (Object.prototype.hasOwnProperty.call(devConfig, prop)) {
            if (Object.prototype.hasOwnProperty.call(localOnlyDevConfigKeys, prop)) {
                console.log('Found local-only dev setting: ' + prop);
                continue;
            }
            persistClay[prop] = devConfig[prop];
            console.log('Found dev setting: ' + prop + '=' + devConfig[prop]);
        }
    }
    localStorage.setItem('clay-settings', JSON.stringify(persistClay));
}

/**
 * Apply Clay-compatible settings from the active fixture.
 *
 * @param {Object|null} fixture Active fixture, or null when fixtures are disabled.
 * @returns {void}
 */
function clayTryFixtureSettings(fixture) {
    var persistClay;
    var settings;
    var prop;

    if (!fixture || !fixture.claySettings || typeof fixture.claySettings !== 'object' || Array.isArray(fixture.claySettings)) {
        return;
    }

    settings = fixture.claySettings;
    persistClay = getClaySettings();
    for (prop in settings) {
        if (Object.prototype.hasOwnProperty.call(settings, prop)) {
            persistClay[prop] = normalizeFixtureSetting(prop, settings[prop]);
        }
    }
    localStorage.setItem('clay-settings', JSON.stringify(persistClay));
}

/**
 * Normalize fixture settings into the same shape Clay stores locally.
 *
 * @param {string} key Clay setting key.
 * @param {*} value Fixture setting value.
 * @returns {*} Normalized setting value.
 */
function normalizeFixtureSetting(key, value) {
    if (isColorSettingKey(key)) {
        return normalizeFixtureColor(value);
    }

    return value;
}

/**
 * Determine whether a Clay setting is a color value.
 *
 * @param {string} key Clay setting key.
 * @returns {boolean} True for color settings.
 */
function isColorSettingKey(key) {
    return key === 'colorTime' ||
        key === 'colorToday' ||
        key === 'colorSunday' ||
        key === 'colorSaturday' ||
        key === 'colorUSFederal';
}

/**
 * Normalize fixture colors from SDK color constant names.
 *
 * @param {*} value Fixture color value.
 * @returns {number} Clay-compatible RGB integer.
 */
function normalizeFixtureColor(value) {
    if (typeof value === 'string') {
        if (Object.prototype.hasOwnProperty.call(pebbleColors, value)) {
            return pebbleColors[value];
        }
    }

    return value;
}

function getClaySettings() {
    return JSON.parse(localStorage.getItem('clay-settings'));
}

/**
 * Determine whether a watch is currently connected.
 *
 * @returns {boolean} True when a watch is connected.
 */
function isWatchConnected() {
    try {
        return Boolean(Pebble.getActiveWatchInfo());
    }
    catch (ex) {
        console.log('Unable to read active watch info: ' + ex.message);
        return false;
    }
}

/**
 * Convert a fixture weather object into the real watch weather AppMessage payload.
 *
 * @param {Object} fixture Active fixture loaded from fixtures/<name>.json.
 * @returns {Object|null} Pebble weather payload, or null when invalid.
 */
function getFixtureWeatherPayload(fixture) {
    var weather;
    var provider;
    var sunEvents;

    if (!fixture || typeof fixture !== 'object') {
        return null;
    }

    weather = fixture.weather;
    if (!weather || typeof weather !== 'object') {
        console.log('[fixture] Missing weather block');
        return null;
    }

    sunEvents = Array.isArray(weather.sunEvents) ? weather.sunEvents.map(function(event) {
        return {
            type: event.type,
            date: new Date(event.epoch * 1000)
        };
    }) : [];

    provider = new WeatherProvider();
    provider.name = 'Fixture';
    provider.id = 'fixture';
    provider.numEntries = Array.isArray(weather.temps) ? weather.temps.length : 0;
    provider.cityName = weather.city || 'Fixture City';
    provider.currentTemp = weather.currentTemp;
    provider.startTime = weather.startEpoch;
    provider.tempTrend = Array.isArray(weather.temps) ? weather.temps.slice(0) : [];
    provider.precipTrend = Array.isArray(weather.precipPct) ? weather.precipPct.map(function(probabilityPercent) {
        return probabilityPercent / 100.0;
    }) : [];
    provider.rainTrend = Array.isArray(weather.rainMm) ? weather.rainMm.slice(0) : new Array(provider.numEntries);
    if (!Array.isArray(weather.rainMm)) {
        for (var rainFillIdx = 0; rainFillIdx < provider.numEntries; rainFillIdx += 1) {
            provider.rainTrend[rainFillIdx] = 0;
        }
    }
    provider.sunEvents = sunEvents;

    if (provider.numEntries <= 0 || sunEvents.length < 2 || !provider.hasValidData()) {
        console.log('[fixture] Invalid weather data in fixture ' + (fixture.name || '(unknown)'));
        return null;
    }

    return provider.getPayload();
}

/**
 * Send fixture weather directly to the watch, bypassing live provider fetch logic.
 *
 * @param {Object} fixture Active fixture loaded from fixtures/<name>.json.
 * @returns {void}
 */
/**
 * Read rainRadarExactMm + rainRadarAreaMm from the fixture's weather
 * block and convert to wire tenths (same mm/h * 10 scaling as RAIN_TREND).
 * Returns null when either array is missing — callers ship the weather
 * payload without radar tuples in that case.
 *
 * @param {Object} fixture Active fixture.
 * @returns {Object|null} Object of three radar AppMessage tuples, or null.
 */
function getFixtureRadarTuples(fixture) {
    var weather = fixture && fixture.weather;
    if (!weather || !Array.isArray(weather.rainRadarExactMm) || !Array.isArray(weather.rainRadarAreaMm)) {
        return null;
    }
    var toTenths = function(mmPerHour) {
        var scaled = Math.round((mmPerHour || 0) * 10);
        if (scaled < 0) { return 0; }
        if (scaled > 255) { return 255; }
        return scaled;
    };
    // Align radar start with the fixture clock (weather.startEpoch) so the
    // watch's hour-axis labels render relative to fixture time, not real
    // wall-clock time. Falls back to Date.now() if the fixture predates
    // startEpoch.
    var radarStart = typeof weather.startEpoch === 'number'
        ? weather.startEpoch
        : Math.floor(Date.now() / 1000);
    return {
        RAIN_RADAR_TREND_UINT8: weather.rainRadarExactMm.map(toTenths),
        RAIN_RADAR_TREND_AREA_UINT8: weather.rainRadarAreaMm.map(toTenths),
        RAIN_RADAR_START: radarStart
    };
}

function sendFixtureWeather(fixture) {
    var payload = getFixtureWeatherPayload(fixture);
    var radarTuples;
    var radarKey;

    if (!payload) {
        return;
    }

    // Bundle radar tuples into the same AppMessage so they ride the
    // inbox handler's bundled forecast+radar branch. Sending them as a
    // follow-up Pebble.sendAppMessage during startup races on the
    // half-duplex outbox channel.
    radarTuples = getFixtureRadarTuples(fixture);
    if (radarTuples) {
        for (radarKey in radarTuples) {
            if (Object.prototype.hasOwnProperty.call(radarTuples, radarKey)) {
                payload[radarKey] = radarTuples[radarKey];
            }
        }
    }

    console.log('[fixture] Sending weather fixture: ' + (fixture.name || '(unknown)'));
    Pebble.sendAppMessage(payload, function() {
        console.log('[fixture] Weather fixture sent successfully');
    }, function(e) {
        console.log('[fixture] Weather fixture failed: ' + JSON.stringify(e));
    });
}

/**
 * Fetch rain-radar data via withRadar2hRain and invoke `callback` with
 * an object of three AppMessage tuples ready to be merged into the
 * weather payload. On any failure (no coordinates, network error,
 * radar parse error) calls `callback(null)` after logging; the weather
 * payload still ships without radar tuples.
 *
 * Out-of-coverage (DWD returns radar: []) is NOT a failure — it
 * produces zero arrays via withRadar2hRain, which is shipped normally.
 *
 * Relies on the existing GPS cache (maximumAge: 10000 ms in
 * WeatherProvider.withGpsCoordinates) to absorb the second
 * getCurrentPosition call that provider.fetch makes shortly after.
 *
 * @param {Object} provider Active WeatherProvider (has .withCoordinates).
 * @param {Function} callback Receives `{tuples}` or `null`.
 * @returns {void}
 */
function withRainRadarTuples(provider, callback) {
    var radar = require('./weather/radar.js');
    // RAIN_RADAR_START is the watch's "5-min pinned" slot-0 epoch: the
    // most recent wall-clock 5-min boundary at or before dispatch (so at
    // 16:53 the radar starts at 16:50). Computed here rather than read
    // off the Brightsky response because Brightsky's /radar window is
    // centred ~1h behind real time, so picking slot-0 off the response
    // would peg the radar to past data under live-looking labels.
    var RADAR_SLOT_SECONDS = 5 * 60;
    var slotZeroEpoch = Math.floor(Date.now() / 1000 / RADAR_SLOT_SECONDS) * RADAR_SLOT_SECONDS;
    provider.withCoordinates(function(lat, lon) {
        radar.withRadar2hRain(lat, lon, slotZeroEpoch, function(result) {
            callback({
                "RAIN_RADAR_TREND_UINT8": result.exact,
                "RAIN_RADAR_TREND_AREA_UINT8": result.nearby_1km,
                "RAIN_RADAR_START": slotZeroEpoch
            });
        }, function(err) {
            console.log('Rain-radar fetch failed: ' + JSON.stringify(err));
            callback(null);
        });
    }, function(err) {
        console.log('Rain-radar coords failed: ' + JSON.stringify(err));
        callback(null);
    });
}

/**
 * @typedef {import("./weather/provider")} WeatherProvider
 * @param {WeatherProvider} provider
 * @param {boolean} force
 */
function fetch(provider, force) {
    if (!isWatchConnected()) {
        console.log('Skipping weather fetch: no watch connected.');
        return;
    }

    if (app.fetchInProgress) {
        console.log('Skipping weather fetch: another fetch is already in progress.');
        return;
    }

    if (typeof provider.isGeocodeBackoffActive === 'function' && provider.isGeocodeBackoffActive()) {
        console.log('Skipping weather fetch: geocoding is in backoff cooldown.');
        return;
    }

    console.log('Fetching from ' + provider.name);
    app.fetchInProgress = true;
    var fetchStart = Date.now();
    var attempt = incrementFetchAttemptCounter();
    var fetchStatus = {
        time: new Date(),
        id: provider.id,
        name: provider.name
    }
    localStorage.setItem(KEY_LAST_FETCH_ATTEMPT, JSON.stringify(fetchStatus));
    try {
        withRainRadarTuples(provider, function(radarTuples) {
            provider.fetch(
                function() {
                    // Sucess, update recent fetch time
                    app.fetchInProgress = false;
                    localStorage.setItem(KEY_LAST_FETCH_SUCCESS, JSON.stringify(fetchStatus));
                    resetFetchAttemptCounter();
                    console.log('Successfully fetched weather!');
                    maybeTrackWeatherFetch({
                        provider: provider.id,
                        success: true,
                        attempt: attempt,
                        usedGpsCache: provider.usedGpsCache,
                        gpsErrorCode: provider.gpsErrorCode,
                        locationMode: provider.locationMode,
                        countryCode: provider.countryCode,
                        settings: app.settings,
                        watchInfo: app.watchInfo,
                        durationMs: Date.now() - fetchStart
                    });
                },
                function(failure) {
                    // Failure
                    app.fetchInProgress = false;
                    console.log('[!] Provider failed to update weather: ' + JSON.stringify(failure));
                    var attemptStatus = {
                        time: fetchStatus.time,
                        id: fetchStatus.id,
                        name: fetchStatus.name,
                        error: failure
                    };
                    localStorage.setItem(KEY_LAST_FETCH_ATTEMPT, JSON.stringify(attemptStatus));
                    maybeTrackWeatherFetch({
                        provider: provider.id,
                        success: false,
                        attempt: attempt,
                        usedGpsCache: provider.usedGpsCache,
                        gpsErrorCode: provider.gpsErrorCode,
                        locationMode: provider.locationMode,
                        countryCode: provider.countryCode,
                        error: failure,
                        settings: app.settings,
                        watchInfo: app.watchInfo,
                        durationMs: Date.now() - fetchStart
                    });
                },
                force,
                radarTuples
            );
        });
    }
    catch (e) {
        app.fetchInProgress = false;
        console.log('Weather fetch threw synchronously: ' + e.message);
    }
}

/**
 * Send a weather fetch telemetry event when telemetry is enabled.
 *
 * @param {Object} event Telemetry event details.
 * @returns {void}
 */
function maybeTrackWeatherFetch(event) {
    if (!app.telemetry || app.telemetry.enabled !== true) {
        return;
    }
    app.telemetry.trackWeatherFetch(event || {});
}

function tryFetch(provider) {
    if (needRefresh()) {
        fetch(provider, false);
    };
}

/**
 * Test whether `now` falls inside the user-configured sleep window.
 *
 * Schedule values come from Clay as strings (the select stores '0'..'23').
 * We parse and clamp at use-site rather than mutating settings, matching
 * how fetchIntervalMin is handled elsewhere in this file.
 *
 * @param {Date} now Current time to evaluate.
 * @returns {boolean} True when the toggle is on and `now` is inside the window.
 */
function inCustomWindow(now) {
    if (!app.settings || !app.settings.sleepNightEnabled) return false;
    var h = now.getHours();
    var start = parseInt(app.settings.sleepStartHour, 10);
    var end = parseInt(app.settings.sleepEndHour, 10);
    if (isNaN(start) || start < 0 || start > 23) start = 22;
    if (isNaN(end)   || end   < 0 || end   > 23) end   = 7;
    if (start === end) return false;          // zero-length window
    if (start < end)   return h >= start && h < end;
    return h >= start || h < end;             // wraps midnight
}

function isSleepingNow() {
    return inCustomWindow(new Date());
}

/**
 * Compute the current sleep state, persist it for the next needRefresh()
 * call, and return it so the caller can include it in a payload.
 *
 * Call this exactly once per outbound message that carries IS_SLEEPING.
 *
 * @returns {boolean} Current sleep state.
 */
function refreshLastIsSleeping() {
    var sleeping = isSleepingNow();
    app.lastIsSleeping = sleeping;
    localStorage.setItem(KEY_LAST_IS_SLEEPING, sleeping ? 'true' : 'false');
    return sleeping;
}

function needRefresh() {
    // Slot-based boundary check: a "slot" is a chunk of length intervalMs since the
    // Unix epoch. Refresh whenever Date.now() sits in a later slot than the last
    // successful fetch. Slots are UTC-aligned, which matches local clock :NN
    // boundaries in whole-hour timezones (see spec for half-hour-offset caveat).
    var raw = localStorage.getItem(KEY_LAST_FETCH_SUCCESS);
    if (raw === null) {
        return true;
    }
    var last = JSON.parse(raw);
    if (!last || !last.time) {
        return true;
    }
    var lastTimeMs = new Date(last.time).getTime();
    if (isNaN(lastTimeMs)) {
        return true;
    }
    var intervalMs = app.settings.fetchIntervalMin * 60 * 1000;
    var lastSlot = Math.floor(lastTimeMs / intervalMs);
    var nowSlot = Math.floor(Date.now() / intervalMs);
    if (nowSlot <= lastSlot) return false;
    if (isSleepingNow() && app.lastIsSleeping === true) return false;
    return true;
}
