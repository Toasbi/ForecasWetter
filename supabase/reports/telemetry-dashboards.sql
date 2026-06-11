-- Telemetry dashboards for public.telemetry_weather_fetch
--
-- Each section is one chart-ready query: paste it into a SQL block of a
-- Supabase custom report (Dashboard -> Reports -> New custom report) or run
-- it in the SQL editor. Queries return few, flat columns so Studio's chart
-- view can map them to x/y directly. All dates are UTC, matching the
-- telemetry_weather_fetch_dau_idx index.

-- ============================================================
-- 1. Daily active users (distinct accounts per day, last 30 days)
-- ============================================================
select
  (received_at at time zone 'UTC')::date as day,
  count(distinct account_token_hash) as active_users
from telemetry_weather_fetch
where received_at >= now() - interval '30 days'
group by day
order by day;

-- ============================================================
-- 2. Fetches per day, split by outcome (last 30 days)
-- ============================================================
select
  (received_at at time zone 'UTC')::date as day,
  count(*) filter (where success) as ok,
  count(*) filter (where not success) as failed
from telemetry_weather_fetch
where received_at >= now() - interval '30 days'
group by day
order by day;

-- ============================================================
-- 3. Success rate by provider (last 7 days)
-- ============================================================
select
  provider,
  count(*) as fetches,
  round(100.0 * count(*) filter (where success) / count(*), 1) as success_pct
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by provider
order by fetches desc;

-- ============================================================
-- 4. Top errors (last 7 days)
-- ============================================================
select
  left(error, 80) as error,
  count(*) as occurrences,
  count(distinct account_token_hash) as affected_users
from telemetry_weather_fetch
where not success
  and received_at >= now() - interval '7 days'
group by left(error, 80)
order by occurrences desc
limit 20;

-- ============================================================
-- 5. Fetch duration percentiles per day (ms, last 30 days)
-- ============================================================
select
  (received_at at time zone 'UTC')::date as day,
  percentile_cont(0.5) within group (order by duration_ms)::int as p50_ms,
  percentile_cont(0.9) within group (order by duration_ms)::int as p90_ms,
  percentile_cont(0.99) within group (order by duration_ms)::int as p99_ms
from telemetry_weather_fetch
where duration_ms is not null
  and received_at >= now() - interval '30 days'
group by day
order by day;

-- ============================================================
-- 6. App version adoption (distinct users, last 7 days)
-- ============================================================
select
  app_version,
  count(distinct account_token_hash) as users
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by app_version
order by users desc;

-- ============================================================
-- 7. Watch platform breakdown (distinct users, last 7 days)
-- ============================================================
select
  coalesce(watch_info ->> 'platform', 'unknown') as platform,
  count(distinct account_token_hash) as users
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by platform
order by users desc;

-- ============================================================
-- 8. Users by country (last 7 days)
-- ============================================================
select
  coalesce(country_code, 'unknown') as country,
  count(distinct account_token_hash) as users
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by country
order by users desc;

-- ============================================================
-- 9. Update interval distribution (distinct users, last 7 days)
-- Requires clients sending settings.fetchIntervalMin (>= telemetry v2).
-- ============================================================
select
  coalesce(settings_json ->> 'fetchIntervalMin', 'unknown') as interval_min,
  count(distinct account_token_hash) as users
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by interval_min
order by users desc;

-- ============================================================
-- 10. Night-sleep window adoption (distinct users, last 7 days)
-- Presence of sleepStartHour implies the feature is enabled; the
-- boolean itself is intentionally not stored.
-- ============================================================
select
  case when settings_json ? 'sleepStartHour' then 'enabled' else 'disabled' end as night_sleep,
  count(distinct account_token_hash) as users
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by night_sleep
order by users desc;

-- ============================================================
-- 11. Location mode and GPS cache usage (last 7 days)
-- ============================================================
select
  coalesce(location_mode, 'unknown') as location_mode,
  count(*) as fetches,
  count(*) filter (where used_gps_cache) as via_gps_cache
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by location_mode
order by fetches desc;

-- ============================================================
-- 12. Retry pressure: attempts per fetch (last 7 days)
-- attempt = 1 means first try; higher buckets indicate flaky fetches.
-- ============================================================
select
  coalesce(attempt::text, 'unknown') as attempt,
  count(*) as fetches
from telemetry_weather_fetch
where received_at >= now() - interval '7 days'
group by attempt
order by attempt;
