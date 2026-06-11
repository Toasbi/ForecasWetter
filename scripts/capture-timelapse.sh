#!/usr/bin/env bash

set -euo pipefail

# Capture the radar-view time-lapse on emery + aplite (one rebuild per frame,
# because watch.now is compile-time), plus a single calendar still on basalt.
# Frames land under screenshot/<version>/timelapse/frames/<platform>/ ready for
# scripts/assemble-gif.sh. RUN ON THE MAC (needs the Pebble SDK + emulator).
#
# Usage:   scripts/capture-timelapse.sh <version>
# Example: scripts/capture-timelapse.sh v1.0.0

if [[ $# -lt 1 ]]; then
  printf 'Usage: %s <version>\n' "$0" >&2
  exit 1
fi

version="$1"
timelapse_platforms=(emery aplite)
frames_root="screenshot/$version/timelapse/frames"
calendar_dir="screenshot/$version/calendar"

# `timeout` is GNU coreutils, absent on stock macOS; fall back to gtimeout.
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd=(timeout 30)
elif command -v gtimeout >/dev/null 2>&1; then
  timeout_cmd=(gtimeout 30)
else
  timeout_cmd=()
fi

# install_with_retries <platform>
install_with_retries() {
  local platform="$1"
  local attempt
  for attempt in 1 2 3; do
    if pebble install build/warnweather-dev.pbw --emulator "$platform"; then
      return 0
    fi
    if [[ $attempt -eq 3 ]]; then
      printf 'ERROR: could not install on %s after 3 attempts\n' "$platform" >&2
      pebble kill 2>/dev/null || true
      exit 1
    fi
    printf 'Install attempt %d on %s failed, retrying...\n' "$attempt" "$platform" >&2
    sleep 4
  done
}

# boot_wait <platform> — emery boots slower than the others.
boot_wait() {
  if [[ "$1" == "emery" ]]; then sleep 12; else sleep 5; fi
}

node scripts/gen-timelapse-fixtures.js

shopt -s nullglob
frame_fixtures=(fixtures/timelapse-*.json)
if [[ ${#frame_fixtures[@]} -eq 0 ]]; then
  printf 'No timelapse fixtures were generated\n' >&2
  exit 1
fi

for platform in "${timelapse_platforms[@]}"; do
  mkdir -p "$frames_root/$platform"
done

pebble kill 2>/dev/null || true
sleep 2

for fixture in "${frame_fixtures[@]}"; do
  base="$(basename "$fixture" .json)"   # timelapse-00
  nn="${base#timelapse-}"               # 00
  printf '\n==> frame %s\n' "$nn"
  FIXTURE="$base" mise run build -- dev

  for platform in "${timelapse_platforms[@]}"; do
    install_with_retries "$platform"
    boot_wait "$platform"
    # Boot view is the calendar; tap to toggle to the radar view.
    pebble emu-tap --emulator "$platform"
    sleep 1
    output="$frames_root/$platform/frame_$nn.png"
    "${timeout_cmd[@]+"${timeout_cmd[@]}"}" pebble screenshot "$output" --emulator "$platform"
    printf 'Saved %s\n' "$output"
    pebble kill 2>/dev/null || true
    sleep 4
  done
done

# Single calendar still on basalt using the readme fixture (no radar tap).
printf '\n==> calendar still (basalt)\n'
mkdir -p "$calendar_dir"
FIXTURE=readme mise run build -- dev
install_with_retries basalt
boot_wait basalt
"${timeout_cmd[@]+"${timeout_cmd[@]}"}" pebble screenshot "$calendar_dir/basalt.png" --emulator basalt
printf 'Saved %s\n' "$calendar_dir/basalt.png"
pebble kill 2>/dev/null || true

printf '\nDone. Next:\n'
printf '  scripts/assemble-gif.sh %s emery\n' "$version"
printf '  scripts/assemble-gif.sh %s aplite\n' "$version"
