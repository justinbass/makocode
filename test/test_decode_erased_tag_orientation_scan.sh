#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software; you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=decode_erased_tag_orientation_scan
if [[ ${1:-} == --label && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo 'test_decode_erased_tag_orientation_scan: unexpected arguments' >&2
    exit 2
fi

work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
mkdir -p "$work_dir/encoded"
printf 'erased marker orientation scan payload' > "$work_dir/payload.bin"

(
    cd "$work_dir"
    "$makocode_bin" encode --input=payload.bin --page-width=80 --page-height=80 \
        "--output-dir=$work_dir/encoded" --prefix=source >/dev/null
)

layout=$("$repo_root/test/canonical_tile_harness" \
    "$work_dir/encoded/source.ppm" 80 80 1 --metadata-layout)

cases=(
    '90 0 0'
    '180 0 0'
    '270 0 0'
    '0 1 0'
    '0 0 1'
)

case_pids=()
for case_spec in "${cases[@]}"; do
(
    read -r angle flip_x flip_y <<<"$case_spec"
    scan="$work_dir/scan_${angle}_${flip_x}_${flip_y}.ppm"
    python3 - "$work_dir/encoded/source.ppm" "$scan" "$layout" "$angle" "$flip_x" "$flip_y" <<'PY'
from ppm_test_utils import read_ppm, write_ppm
import math
import sys

src_w, src_h, source = read_ppm(sys.argv[1])
out_path = sys.argv[2]
_, _, _, _, tag_y, _ = map(int, sys.argv[3].split())
angle = math.radians(float(sys.argv[4]))
flip_x, flip_y = int(sys.argv[5]), int(sys.argv[6])

# The marker occupies columns 20–27 of the centered tile and rows tag_y+1–4.
# Replace every marker cell with white before simulating the scan.
tile_x, tile_y = (src_w - 48) // 2, (src_h - 48) // 2
erased_cells = 0
for row in range(4):
    for col in range(8):
        offset = ((tile_y + tag_y + 1 + row) * src_w + tile_x + 20 + col) * 3
        if source[offset:offset + 3] != [255, 255, 255]:
            erased_cells += 1
        source[offset:offset + 3] = b'\xff\xff\xff'
assert erased_cells > 0
assert all(
    source[((tile_y + tag_y + 1 + row) * src_w + tile_x + 20 + col) * 3:
           ((tile_y + tag_y + 1 + row) * src_w + tile_x + 20 + col) * 3 + 3] == [255, 255, 255]
    for row in range(4) for col in range(8)
)

scale = 4.0
coverage = 4
cosine, sine = math.cos(angle), math.sin(angle)
extent_w = scale * (abs(cosine) * src_w + abs(sine) * src_h)
extent_h = scale * (abs(sine) * src_w + abs(cosine) * src_h)
out_w, out_h = math.ceil(extent_w) + 16, math.ceil(extent_h) + 16
center_x, center_y = (out_w - 1) * 0.5, (out_h - 1) * 0.5
src_cx, src_cy = (src_w - 1) * 0.5, (src_h - 1) * 0.5
pixels = bytearray([255]) * (out_w * out_h * 3)

for y in range(out_h):
    for x in range(out_w):
        total = 0
        for sy in range(coverage):
            py = y + (sy + 0.5) / coverage - 0.5 - center_y
            for sx in range(coverage):
                px = x + (sx + 0.5) / coverage - 0.5 - center_x
                rx = (cosine * px + sine * py) / scale
                ry = (-sine * px + cosine * py) / scale
                mx = src_cx + rx * (-1 if flip_x else 1)
                my = src_cy + ry * (-1 if flip_y else 1)
                col, row = math.floor(mx + 0.5), math.floor(my + 0.5)
                if 0 <= col < src_w and 0 <= row < src_h:
                    total += source[(row * src_w + col) * 3]
                else:
                    total += 255
        value = round(total / (coverage * coverage))
        offset = (y * out_w + x) * 3
        pixels[offset:offset + 3] = bytes((value, value, value))

write_ppm(out_path, out_w, out_h, pixels)
PY
    decoded_dir="$work_dir/decoded_${angle}_${flip_x}_${flip_y}"
    mkdir -p "$decoded_dir"
    log="$work_dir/decode_${angle}_${flip_x}_${flip_y}.log"
    if ! "$makocode_bin" decode --debug "--output-dir=$decoded_dir" "$scan" \
        >"$log" 2>&1; then
        cat "$log" >&2
        echo "test_decode_erased_tag_orientation_scan: scan failed for angle=$angle flip_x=$flip_x flip_y=$flip_y" >&2
        exit 1
    fi
    if ! grep -Fq 'metadata ECC recovered from header length row' "$log"; then
        cat "$log" >&2
        echo 'test_decode_erased_tag_orientation_scan: markerless header path was not used' >&2
        exit 1
    fi
    cmp --silent "$work_dir/payload.bin" "$decoded_dir/payload.bin"
    rm -f "$decoded_dir/payload.bin"
) &
    case_pids+=("$!")
    if [[ ${#case_pids[@]} -ge 4 ]]; then
        wait "${case_pids[0]}"
        case_pids=("${case_pids[@]:1}")
    fi
done
for case_pid in "${case_pids[@]}"; do
    wait "$case_pid"
done

printf 'fully erased MAKO/CODE orientation scans passed (label=%s)\n' "$label"
