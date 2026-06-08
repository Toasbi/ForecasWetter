#!/usr/bin/env bash

set -euo pipefail

profile="dev"

if [[ "${1:-}" == "release" || "${1:-}" == "dev" ]]; then
  profile="$1"
  shift
fi

if [[ "${1:-}" == "--" ]]; then
  shift
fi

scripts/ensure-pebble-sdk.sh
mise run prepare-package -- "$profile"
node scripts/prepare-fixture.js
pebble build "$@"

# pebble build names the pbw after the project-directory basename, so in a
# git worktree it lands as build/<worktree-dir>.pbw rather than the canonical
# build/forecaswatch2.pbw the install scripts expect. Normalize the name.
pbw_built=$(ls -1t build/*.pbw 2>/dev/null | head -n1)
if [[ -z "$pbw_built" ]]; then
  echo "build.sh: no .pbw produced by pebble build" >&2
  exit 1
fi
if [[ "$pbw_built" != "build/forecaswatch2.pbw" ]]; then
  cp "$pbw_built" build/forecaswatch2.pbw
fi

if [[ "$profile" == "dev" ]]; then
  cp build/forecaswatch2.pbw build/forecaswatch2-dev.pbw
fi
