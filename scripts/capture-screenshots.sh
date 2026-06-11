#!/usr/bin/env bash

set -euo pipefail

# Builds the dev .pbw, installs it on each supported emulator platform,
# waits for the watchface to render, and saves screenshots to
# screenshot/<version>/raw/<platform>.png ready for mise composite <version>.
#
# Usage:   scripts/capture-screenshots.sh <version> [fixture]
# Example: scripts/capture-screenshots.sh v1.0.0
#          scripts/capture-screenshots.sh v1.0.0 berlin

if [[ $# -lt 1 ]]; then
  printf 'Usage: %s <version> [fixture]\n' "$0" >&2
  printf 'Example: %s v1.0.0 berlin\n' "$0" >&2
  exit 1
fi

version="$1"
export FIXTURE="${2:-berlin}"
raw_dir="screenshot/$version/raw"
platforms=(aplite basalt diorite emery flint)

# The watch always boots on the calendar view; fixtures that showcase the
# rain radar need an accel tap to toggle the top view before the screenshot.
radar_fixtures=(berlin rainy)
wants_radar=0
for radar_fixture in "${radar_fixtures[@]}"; do
  if [[ "$FIXTURE" == "$radar_fixture" ]]; then
    wants_radar=1
  fi
done

# `timeout` is GNU coreutils and absent on stock macOS; fall back to gtimeout
# (brew coreutils) or run without a timeout if neither is available.
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd=(timeout 30)
elif command -v gtimeout >/dev/null 2>&1; then
  timeout_cmd=(gtimeout 30)
else
  timeout_cmd=()
fi

mkdir -p "$raw_dir"

printf 'Building dev .pbw...\n'
mise run build -- dev

pebble kill 2>/dev/null || true
sleep 2

for platform in "${platforms[@]}"; do
  printf '\n==> %s\n' "$platform"

  for attempt in 1 2 3; do
    if pebble install build/warnweather-dev.pbw --emulator "$platform"; then
      break
    fi
    if [[ $attempt -eq 3 ]]; then
      printf 'ERROR: could not install on %s after 3 attempts\n' "$platform" >&2
      pebble kill 2>/dev/null || true
      exit 1
    fi
    printf 'Install attempt %d failed, retrying...\n' "$attempt" >&2
    sleep 4
  done

  # emery boots slower; give it extra time before screenshotting
  if [[ "$platform" == "emery" ]]; then
    sleep 12
  else
    sleep 5
  fi

  if [[ $wants_radar -eq 1 ]]; then
    pebble emu-tap --emulator "$platform"
    sleep 1
  fi

  output="$raw_dir/$platform.png"
  "${timeout_cmd[@]+"${timeout_cmd[@]}"}" pebble screenshot "$output" --emulator "$platform"
  printf 'Saved %s\n' "$output"

  pebble kill 2>/dev/null || true
  sleep 4
done

printf '\nAll screenshots captured in %s\n' "$raw_dir"
printf 'Next: mise composite %s\n' "$version"
