#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=decode_current_metadata
if [[ ${1:-} == --label ]]; then label=$2; shift 2; fi
[[ $# == 0 ]]
work_dir="$repo_root/test_output/${label}_work"
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf 'x' > "$work_dir/payload.bin"
(cd "$work_dir" && "$repo_root/makocode" encode --input=payload.bin --ecc=0.25 \
    --page-width=80 --page-height=80 \
    --output-dir="$work_dir/encoded" --prefix=page >/dev/null)
layout=$(${CANONICAL_TILE_HARNESS_BIN:-"$repo_root/test/canonical_tile_harness"} \
    "$work_dir/encoded/page.ppm" 80 80 1 --metadata-layout)
read -r square_x square_y square_side prefix_bits tag_y symbol_count <<<"$layout"
python3 - "$work_dir/encoded/page.ppm" "$work_dir/damaged.ppm" \
    "$square_x" "$square_y" "$square_side" "$prefix_bits" <<'PY'
from pathlib import Path
from ppm_test_utils import read_ppm, write_ppm
import sys
width, height, pixels = read_ppm(sys.argv[1])
assert (width, height) == (80, 80)
assert len(pixels) == 80 * 80 * 3
tile_x = tile_y = (80 - 48) // 2
# Flip a protected data module after the outer schema and parity fields.
square_x, square_y, square_side, prefix_bits = map(int, sys.argv[3:7])
cell = prefix_bits + 16
x = tile_x + square_x + cell % square_side
y = tile_y + square_y + cell // square_side
index = (y * 80 + x) * 3
original = pixels[index:index + 3]
assert original in ([0, 0, 0], [255, 255, 255])
pixels[index:index + 3] = [255 - value for value in original]
assert pixels[index:index + 3] != original
write_ppm(sys.argv[2], 80, 80, pixels)
PY
"$repo_root/makocode" decode --output-dir="$work_dir/decoded" \
    "$work_dir/damaged.ppm" >"$work_dir/decode.log" 2>&1 || {
        cat "$work_dir/decode.log" >&2
        exit 1
    }
cmp "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"

# Without the protected center tile, canonical stream alignment and length
# cannot be established. Corrupt the full tile and require a clean decode error.
python3 - "$work_dir/encoded/page.ppm" "$work_dir/no_bootstrap.ppm" <<'PY'
from pathlib import Path
from ppm_test_utils import read_ppm, write_ppm
import sys

width, height, pixels = read_ppm(sys.argv[1])
tile_x = tile_y = (width - 48) // 2
for y in range(tile_y, tile_y + 48):
    for x in range(tile_x, tile_x + 48):
        index = (y * width + x) * 3
        pixels[index:index + 3] = [127, 127, 127]
write_ppm(sys.argv[2], width, height, pixels)
PY
if "$repo_root/makocode" decode --output-dir="$work_dir/no_bootstrap_out" \
    "$work_dir/no_bootstrap.ppm" >"$work_dir/no_bootstrap.log" 2>&1; then
    echo 'test_decode_current_metadata: decode accepted a page without canonical bootstrap metadata' >&2
    exit 1
fi
if ! grep -Fq 'metadata tile missing; aborting' "$work_dir/no_bootstrap.log" &&
   ! grep -Fq 'invalid canonical metadata tile' "$work_dir/no_bootstrap.log"; then
    cat "$work_dir/no_bootstrap.log" >&2
    echo 'test_decode_current_metadata: missing-tile failure was not explicit' >&2
    exit 1
fi
