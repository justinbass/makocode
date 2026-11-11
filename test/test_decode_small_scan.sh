#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label="decode_small_scan"
if [[ ${1:-} == "--label" && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo "test_decode_small_scan: unexpected arguments" >&2
    exit 2
fi

work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
mkdir -p "$work_dir/encoded" "$work_dir/decoded_5x" \
    "$work_dir/decoded_4_5x_hard" "$work_dir/decoded_4_5x_soft"
printf 'makocode' > "$work_dir/makocode.bin"

(
    cd "$work_dir"
    "$repo_root/makocode" encode \
        --input=makocode.bin --ecc=0.25 --page-width=80 --page-height=80 \
        "--output-dir=$work_dir/encoded" --prefix=small_scan > /dev/null
)

# The scan fixtures retain their original 2.239 and 2.477 pixels/cell pitches.
for sample in 4_5x 5x; do
    if [[ $sample == 4_5x ]]; then
        pitch=2.239
    else
        pitch=2.477
    fi
    "$repo_root/test/ppm_transform" transform \
        --input "$work_dir/encoded/small_scan.ppm" \
        --output "$work_dir/${sample}_scaled.ppm" \
        --scale-x "$pitch" --scale-y "$pitch"

    python3 - "$work_dir/${sample}_scaled.ppm" "$work_dir/${sample}_scan.ppm" \
        "$sample" "$work_dir/encoded/small_scan.ppm" "$pitch" \
        "$work_dir/4_5x_hard_scan.ppm" "$work_dir/4_5x_soft_scan.ppm" <<'PY'
from pathlib import Path
from ppm_test_utils import read_ppm, write_ppm
import sys

width, height, pixels = read_ppm(sys.argv[1])
assert len(pixels) == width * height * 3
if sys.argv[3] == '4_5x':
    assert (width, height) == (179, 179)
    canvas_width, canvas_height, offset_x, offset_y = 193, 188, 2, 4
else:
    assert (width, height) == (198, 198)
    canvas_width, canvas_height, offset_x, offset_y = 212, 208, 4, 5

canvas = [255] * (canvas_width * canvas_height * 3)
hard_canvas = [230] * (canvas_width * canvas_height * 3)
soft_canvas = [230] * (canvas_width * canvas_height * 3)
for y in range(height):
    for x in range(width):
        source = (y * width + x) * 3
        target = ((y + offset_y) * canvas_width + x + offset_x) * 3
        canvas[target:target + 3] = pixels[source:source + 3]
        if sys.argv[3] == '4_5x':
            hard = 40 if sum(pixels[source:source + 3]) < 384 else 230
            hard_canvas[target:target + 3] = [hard, hard, hard]
            soft_canvas[target:target + 3] = [
                30 + round(0.8 * pixels[source + channel])
                for channel in range(3)
            ]

# Verify every logical cell still has its source value at the known geometric
# center. The soft raster retains the bilinear gray edges produced by scaling.
original_width, original_height, source_pixels = read_ppm(sys.argv[4])
assert (original_width, original_height) == (80, 80)
pitch = float(sys.argv[5])
verification_canvas = soft_canvas if sys.argv[3] == '4_5x' else canvas
for y in range(80):
    for x in range(80):
        sample_x = round(offset_x + (x + 0.5) * pitch - 0.5)
        sample_y = round(offset_y + (y + 0.5) * pitch - 0.5)
        original_index = (y * 80 + x) * 3
        canvas_index = (sample_y * canvas_width + sample_x) * 3
        assert (sum(source_pixels[original_index:original_index + 3]) < 384) == \
               (sum(verification_canvas[canvas_index:canvas_index + 3]) < 384)

if sys.argv[3] == '4_5x':
    write_ppm(sys.argv[6], canvas_width, canvas_height, hard_canvas)
    write_ppm(sys.argv[7], canvas_width, canvas_height, soft_canvas)
else:
    write_ppm(sys.argv[2], canvas_width, canvas_height, canvas)
PY
done

"$repo_root/makocode" decode \
    "--output-dir=$work_dir/decoded_5x" \
    "$work_dir/5x_scan.ppm" > "$work_dir/decode_5x.log" 2>&1
cmp --silent "$work_dir/makocode.bin" "$work_dir/decoded_5x/makocode.bin"

"$repo_root/makocode" decode --debug \
    "--output-dir=$work_dir/decoded_4_5x_hard" \
    "$work_dir/4_5x_hard_scan.ppm" > "$work_dir/decode_4_5x_hard.log" 2>&1
cmp --silent "$work_dir/makocode.bin" \
    "$work_dir/decoded_4_5x_hard/makocode.bin"

"$repo_root/makocode" decode --debug \
    "--output-dir=$work_dir/decoded_4_5x_soft" \
    "$work_dir/4_5x_soft_scan.ppm" > "$work_dir/decode_4_5x_soft.log" 2>&1
cmp --silent "$work_dir/makocode.bin" \
    "$work_dir/decoded_4_5x_soft/makocode.bin"
python3 - "$work_dir/decoded_4_5x_soft/decode_affine.csv" <<'PY'
import csv
import sys

with open(sys.argv[1], newline='') as affine_file:
    row = next(csv.DictReader(affine_file))
assert row['has_affine'] == '1'
assert abs(float(row['scale']) - 2.239) <= 0.01
# The scan is placed at (2, 4); affine translation uses pixel-center coordinates.
assert abs(float(row['tx']) - 1.5) <= 0.75
assert abs(float(row['ty']) - 3.5) <= 0.75
assert abs(float(row['rotation_degrees'])) <= 0.25
PY
printf 'small scan affine-bias decode passed (label=%s)\n' "$label"
