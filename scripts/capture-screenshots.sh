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
platforms=(basalt diorite emery flint)

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

  sleep 5

  output="$raw_dir/$platform.png"
  pebble screenshot "$output" --emulator "$platform"
  printf 'Saved %s\n' "$output"

  pebble kill 2>/dev/null || true
  sleep 4
done

printf '\nAll screenshots captured in %s\n' "$raw_dir"
printf 'Next: mise composite %s\n' "$version"
