module.exports = function (minified) {
    clayConfig = this;
    var $ = minified.$;

    /**
     * Parse stored JSON safely.
     *
     * @param {string|null} value Raw JSON string.
     * @returns {Object|null} Parsed object or null.
     */
    function parseStoredJson(value) {
        if (value === null) {
            return null;
        }

        try {
            return JSON.parse(value);
        }
        catch (ex) {
            return null;
        }
    }

    /**
     * Render the dev stats daily rollup table and raw event list as HTML.
     *
     * @param {Object[]} events Stored dev-stats events, oldest first.
     * @returns {string} HTML for the stats block.
     */
    function renderDevStats(events) {
        var CATEGORIES = ['forecast', 'status', 'sun', 'radar', 'sleep'];
        var CATEGORY_HEADERS = ['fcst', 'stat', 'sun', 'radr', 'slp'];
        var RAW_EVENT_CAP = 100;
        var TABLE_STYLE = 'border-collapse:collapse;font-size:0.72em;margin:6px 0;width:100%;text-align:center;';
        var CELL_STYLE = 'border:1px solid #555;padding:1px 3px;';
        var days = {};
        var dayOrder = [];
        var raw;
        var html;

        /**
         * Zero-pad a number to two digits.
         *
         * @param {number} value Number in [0, 99].
         * @returns {string} Two-digit string.
         */
        function pad2(value) {
            return value < 10 ? '0' + value : String(value);
        }

        /**
         * Format an epoch as a local MM-DD day key.
         *
         * @param {number} t Epoch milliseconds.
         * @returns {string} Day key.
         */
        function dayOf(t) {
            var d = new Date(t);
            return pad2(d.getMonth() + 1) + '-' + pad2(d.getDate());
        }

        /**
         * Format an epoch as local "MM-DD HH:MM".
         *
         * @param {number} t Epoch milliseconds.
         * @returns {string} Timestamp string.
         */
        function timeOf(t) {
            var d = new Date(t);
            return dayOf(t) + ' ' + pad2(d.getHours()) + ':' + pad2(d.getMinutes());
        }

        /**
         * Wrap content in a styled table cell.
         *
         * @param {string} content Cell content.
         * @returns {string} HTML td.
         */
        function cell(content) {
            return '<td style="' + CELL_STYLE + '">' + content + '</td>';
        }

        /**
         * Build a styled table header row.
         *
         * @param {string[]} labels Column labels.
         * @returns {string} HTML tr.
         */
        function headerRow(labels) {
            return '<tr>' + labels.map(function(label) {
                return '<th style="' + CELL_STYLE + '">' + label + '</th>';
            }).join('') + '</tr>';
        }

        /**
         * Format event-level outcome counts like "5✓ 1✗ 4c" (zeros omitted).
         *
         * @param {{ack: number, nack: number, skip: number}} counts Outcome counts.
         * @returns {string} Cell text.
         */
        function outcomeCell(counts) {
            var parts = [];
            if (counts.ack > 0) {
                parts.push(counts.ack + '✓');
            }
            if (counts.nack > 0) {
                parts.push(counts.nack + '✗');
            }
            if (counts.skip > 0) {
                parts.push(counts.skip + 'c');
            }
            return parts.length > 0 ? parts.join(' ') : '–';
        }

        // Aggregate per local day. Outcome: ok=1 → ack, ok=0 → nack,
        // no ok field → skip (nothing transmitted).
        events.forEach(function(ev) {
            var day = dayOf(ev.t);
            var bucket = days[day];
            var outcome = ev.ok === 1 ? 'ack' : (ev.ok === 0 ? 'nack' : 'skip');
            if (!bucket) {
                bucket = {
                    weather: { ack: 0, nack: 0, skip: 0 },
                    setting: { ack: 0, nack: 0, skip: 0 },
                    cats: {}
                };
                CATEGORIES.forEach(function(name) {
                    bucket.cats[name] = { sent: 0, cached: 0 };
                });
                days[day] = bucket;
                dayOrder.push(day);
            }
            if (ev.k === 'weather') {
                bucket.weather[outcome] += 1;
                CATEGORIES.forEach(function(name) {
                    if (!ev.c || typeof ev.c[name] === 'undefined' || outcome === 'nack') {
                        return;  // Absent category, or NACK: counts in neither column.
                    }
                    if (ev.c[name] === 1) {
                        bucket.cats[name].sent += 1;
                    }
                    else {
                        bucket.cats[name].cached += 1;
                    }
                });
            }
            else {
                bucket.setting[outcome] += 1;
            }
        });
        dayOrder.reverse();  // Newest day first.

        // Daily rollup table.
        html = '<table style="' + TABLE_STYLE + '">';
        html += headerRow(['Day', 'weather'].concat(CATEGORY_HEADERS).concat(['set']));
        dayOrder.forEach(function(day) {
            var bucket = days[day];
            html += '<tr>' + cell(day) + cell(outcomeCell(bucket.weather));
            CATEGORIES.forEach(function(name) {
                html += cell(bucket.cats[name].sent + '/' + bucket.cats[name].cached);
            });
            html += cell(outcomeCell(bucket.setting)) + '</tr>';
        });
        html += '</table>';

        // Raw event list, newest first, capped for page sanity.
        raw = events.slice(-RAW_EVENT_CAP).reverse();
        html += '<table style="' + TABLE_STYLE + '">';
        html += headerRow(['Time', 'ok'].concat(CATEGORY_HEADERS).concat(['set']));
        raw.forEach(function(ev) {
            var okMark = ev.ok === 1 ? '✓' : (ev.ok === 0 ? '✗' : '');
            html += '<tr>' + cell(timeOf(ev.t)) + cell(okMark);
            CATEGORIES.forEach(function(name) {
                var mark = '';
                if (ev.k === 'weather' && ev.c && typeof ev.c[name] !== 'undefined') {
                    mark = ev.c[name] === 1 ? '●' : '–';
                }
                html += cell(mark);
            });
            html += cell(ev.k === 'setting' ? (ev.sent === 1 ? '●' : '–') : '') + '</tr>';
        });
        html += '</table>';
        if (events.length > RAW_EVENT_CAP) {
            html += '<div style="font-size:0.72em;">Showing last ' + RAW_EVENT_CAP + ' of ' + events.length + ' events.</div>';
        }
        return html;
    }

    clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
        var clayFetch;
        var clayOwmApiKey;
        var clayProvider;
        var clayLocation;
        var initProvider;
        var initOwmApiKey;
        var initLocation;
        var lastFetchSuccessString;
        var lastFetchSuccess;
        var date;
        var lastFetchSuccessTime;
        var lastFetchAttemptString;
        var lastFetchAttempt;
        var attemptDate;
        var attemptTime;
        var attemptText;
        var shouldShowLastAttempt;
        var devStatsToggle;
        var devStatsEvents;

        clayFetch = clayConfig.getItemByMessageKey('fetch');
        clayFetch.set(false);

        // Save initial states to detect changes to provider
        clayOwmApiKey = clayConfig.getItemByMessageKey('owmApiKey');
        clayProvider = clayConfig.getItemByMessageKey('provider');
        clayLocation = clayConfig.getItemByMessageKey('location');
        initProvider = clayProvider.get();
        initOwmApiKey = clayOwmApiKey.get();
        initLocation = clayLocation.get();

        // Configure default provider section layout
        if (initProvider !== 'openweathermap') {
            clayOwmApiKey.hide();
        }

        // Configure logic for updating the provider section layout
        clayProvider.on('change', function() {
            if (this.get() === 'openweathermap') {
                clayOwmApiKey.show();
            } else {
                clayOwmApiKey.hide();
            }
            console.log('Provider set to ' + this.get());
        });

        // Show last weather fetch status
        lastFetchSuccessString = clayConfig.meta.userData.lastFetchSuccess;
        lastFetchSuccessTime = null;
        lastFetchSuccess = parseStoredJson(lastFetchSuccessString);
        if (lastFetchSuccess !== null) {
            date = new Date(lastFetchSuccess.time);
            lastFetchSuccessTime = date.getTime();
            $('#lastFetchSpan').ht(date.toLocaleDateString() + ' ' + date.toLocaleTimeString() + ' with ' + lastFetchSuccess.name);
        }

        lastFetchAttemptString = clayConfig.meta.userData.lastFetchAttempt;
        lastFetchAttempt = parseStoredJson(lastFetchAttemptString);
        if (lastFetchAttempt !== null) {
            if (lastFetchAttempt.error) {
                attemptDate = new Date(lastFetchAttempt.time);
                attemptTime = attemptDate.getTime();
                shouldShowLastAttempt = !Boolean(lastFetchSuccessTime) || attemptTime > lastFetchSuccessTime;

                if (shouldShowLastAttempt) {
                    attemptText = '<br>Last failed attempt:<br>';
                    attemptText += attemptDate.toLocaleDateString() + ' ' + attemptDate.toLocaleTimeString() + ' with ' + lastFetchAttempt.name;
                    attemptText += '<br>Error: ' + lastFetchAttempt.error.stage + ': ' + lastFetchAttempt.error.code;
                    $('#lastAttemptBlock').ht(attemptText);
                }
            }
        }

        // Render dev stats when recording is enabled and events exist.
        devStatsToggle = clayConfig.getItemByMessageKey('devStatsEnabled');
        devStatsEvents = parseStoredJson(
            typeof clayConfig.meta.userData.devStats === 'string'
                ? clayConfig.meta.userData.devStats
                : null
        );
        if (devStatsToggle && Boolean(devStatsToggle.get())
                && Array.isArray(devStatsEvents) && devStatsEvents.length > 0) {
            $('#devStatsBlock').ht(renderDevStats(devStatsEvents));
        }

        // Override submit handler to force re-fetch if provider config changed
        $('#main-form').on('submit', function() {
            var returnTo;
            if (clayProvider.get() !== initProvider
                || clayOwmApiKey.get() !== initOwmApiKey
                || clayLocation.get() !== initLocation) {
                clayFetch.set(true);
            }

            // Copied from original handler ($.off requires non-anonymous handler)
            returnTo = window.returnTo || 'pebblejs://close#';
            location.href = returnTo +
                encodeURIComponent(JSON.stringify(clayConfig.serialize()));
        })
    });
};
