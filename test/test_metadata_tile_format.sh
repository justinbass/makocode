#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname "$0")/.." && pwd -P)
harness=${CANONICAL_TILE_HARNESS_BIN:-"$root/test/canonical_tile_harness"}
"$harness" --test-variable-fields
"$harness" --test-metadata-rs
"$harness" --test-marker-layout
