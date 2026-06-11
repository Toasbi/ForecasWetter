#!/usr/bin/env bash

set -euo pipefail

# Assemble a platform's captured time-lapse PNG frames into an optimized,
# infinitely-looping GIF using ffmpeg's two-pass palette workflow.
#
# Usage:   scripts/assemble-gif.sh <version> <platform> [fps]
# Example: scripts/assemble-gif.sh v1.0.0 emery 6

if [[ $# -lt 2 ]]; then
  printf 'Usage: %s <version> <platform> [fps]\n' "$0" >&2
  exit 1
fi

version="$1"
platform="$2"
fps="${3:-6}"

frames_dir="screenshot/$version/timelapse/frames/$platform"
out="screenshot/$version/timelapse/$platform.gif"

if ! ls "$frames_dir"/frame_*.png >/dev/null 2>&1; then
  printf 'No frames found in %s\n' "$frames_dir" >&2
  exit 1
fi

mkdir -p "$(dirname "$out")"
tmpdir="$(mktemp -d -t ww-gif.XXXXXX)"
trap 'rm -rf "$tmpdir"' EXIT
palette="$tmpdir/palette.png"

ffmpeg -y -framerate "$fps" -start_number 0 -i "$frames_dir/frame_%02d.png" \
  -vf "palettegen=stats_mode=diff" -update 1 "$palette"

ffmpeg -y -framerate "$fps" -start_number 0 -i "$frames_dir/frame_%02d.png" -i "$palette" \
  -lavfi "paletteuse=dither=bayer:bayer_scale=3" -loop 0 "$out"

printf 'Wrote %s\n' "$out"
