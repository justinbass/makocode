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

work_dir="$repo_root/test_output/${label}_work"
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded_5x" \
    "$work_dir/decoded_4_5x_hard" "$work_dir/decoded_4_5x_soft"
printf 'makocode' > "$work_dir/makocode.bin"

(
    cd "$work_dir"
    "$repo_root/makocode" encode \
        --input=makocode.bin --ecc=0 --page-width=55 --page-height=55 \
        --no-filename --no-page-count \
        "--output-dir=$work_dir/encoded" --prefix=small_scan > /dev/null
)

# The 4.5x scan is 137x132 with a 2.239 pixels/cell source transform. The
# successful 5x control is 150x146 and reports a 2.477 pixels/cell pitch.
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
import sys

tokens = Path(sys.argv[1]).read_text().split()
assert tokens[0] == 'P3' and tokens[3] == '255'
width, height = int(tokens[1]), int(tokens[2])
pixels = list(map(int, tokens[4:]))
assert len(pixels) == width * height * 3
if sys.argv[3] == '4_5x':
    assert (width, height) == (123, 123)
    canvas_width, canvas_height, offset_x, offset_y = 137, 132, 2, 4
else:
    assert (width, height) == (136, 136)
    canvas_width, canvas_height, offset_x, offset_y = 150, 146, 4, 5

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
original = Path(sys.argv[4]).read_text().split()
assert original[:4] == ['P3', '55', '55', '255']
source_pixels = list(map(int, original[4:]))
pitch = float(sys.argv[5])
verification_canvas = soft_canvas if sys.argv[3] == '4_5x' else canvas
for y in range(55):
    for x in range(55):
        sample_x = round(offset_x + (x + 0.5) * pitch - 0.5)
        sample_y = round(offset_y + (y + 0.5) * pitch - 0.5)
        original_index = (y * 55 + x) * 3
        canvas_index = (sample_y * canvas_width + sample_x) * 3
        assert (sum(source_pixels[original_index:original_index + 3]) < 384) == \
               (sum(verification_canvas[canvas_index:canvas_index + 3]) < 384)

if sys.argv[3] == '4_5x':
    Path(sys.argv[6]).write_text(
        f'P3\n{canvas_width} {canvas_height}\n255\n' +
        '\n'.join(f'{hard_canvas[i]} {hard_canvas[i + 1]} {hard_canvas[i + 2]}'
                  for i in range(0, len(hard_canvas), 3)) + '\n'
    )
    Path(sys.argv[7]).write_text(
        f'P3\n{canvas_width} {canvas_height}\n255\n' +
        '\n'.join(f'{soft_canvas[i]} {soft_canvas[i + 1]} {soft_canvas[i + 2]}'
                  for i in range(0, len(soft_canvas), 3)) + '\n'
    )
else:
    Path(sys.argv[2]).write_text(
        f'P3\n{canvas_width} {canvas_height}\n255\n' +
        '\n'.join(f'{canvas[i]} {canvas[i + 1]} {canvas[i + 2]}'
                  for i in range(0, len(canvas), 3)) + '\n'
    )
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
assert (row['scale'], row['tx'], row['ty']) == ('2.236', '1.500', '3.500')
PY
printf 'small scan affine-bias decode passed (label=%s)\n' "$label"
