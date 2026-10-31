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
label="decode_lowres_rotated_scan"
if [[ ${1:-} == "--label" && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo 'test_decode_lowres_rotated_scan: unexpected arguments' >&2
    exit 2
fi

work_dir="$repo_root/test_output/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/scan" "$work_dir/decoded"
printf 'makocode' > "$work_dir/makocode.bin"

(
    cd "$work_dir"
    "$makocode_bin" encode \
        --input=makocode.bin \
        --ecc=0 --page-width=55 --page-height=55 \
        --no-filename --no-page-count \
        "--output-dir=$work_dir/encoded" --prefix=source >/dev/null
)

python3 - "$work_dir/encoded/source.ppm" "$work_dir/scan/scan.ppm" <<'PY'
from pathlib import Path
import math
import sys

tokens = Path(sys.argv[1]).read_text().split()
assert tokens[0] == 'P3' and tokens[3] == '255'
source_width, source_height = int(tokens[1]), int(tokens[2])
source = list(map(int, tokens[4:]))
assert (source_width, source_height) == (55, 55)
assert len(source) == source_width * source_height * 3

# Rasterize the encoded modules through one affine camera pass. Four-by-four
# coverage samples model antialiasing without resampling an already transformed
# image. Coordinates are pixel centers in the decoder's affine coordinate space.
scale = 1.75
angle = math.radians(-1.25)
cosine, sine = math.cos(angle), math.sin(angle)
a00 = scale * cosine
a01 = -scale * sine
a10 = scale * sine
a11 = scale * cosine
canvas_width, canvas_height = 101, 102
tx = 50.0 - 48.0 * cosine + 48.0 * sine
ty = 50.0 - 48.0 * sine - 48.0 * cosine
canvas = [230] * (canvas_width * canvas_height * 3)
coverage = 4

for y in range(canvas_height):
    for x in range(canvas_width):
        channels = [0, 0, 0]
        for sub_y in range(coverage):
            for sub_x in range(coverage):
                pixel_x = x + (sub_x + 0.5) / coverage - 0.5
                pixel_y = y + (sub_y + 0.5) / coverage - 0.5
                dx = pixel_x - tx
                dy = pixel_y - ty
                module_x = (a11 * dx - a01 * dy) / (scale * scale)
                module_y = (-a10 * dx + a00 * dy) / (scale * scale)
                col, row = math.floor(module_x), math.floor(module_y)
                if 0 <= col < source_width and 0 <= row < source_height:
                    value = source[(row * source_width + col) * 3]
                    value = 30 + round(0.8 * value)
                else:
                    value = 230
                for channel in range(3):
                    channels[channel] += value
        pixel = (y * canvas_width + x) * 3
        canvas[pixel:pixel + 3] = [round(value / (coverage * coverage))
                                   for value in channels]

# Add eight isolated scan defects at coarse-fit sample locations for payload
# modules. The true affine sample points avoid these pixels; a finer tile fit
# must recover the payload despite the coarse fit's one-pixel sampling shift.
damaged_samples = (
    (10, 1, 20, 5),
    (20, 1, 37, 4),
    (21, 1, 39, 4),
    (22, 1, 41, 4),
    (23, 1, 42, 4),
    (24, 1, 44, 4),
    (25, 1, 46, 4),
    (26, 1, 48, 4),
)
exact_samples = set()
for row in range(source_height):
    for col in range(source_width):
        if 3 <= col < 51 and 3 <= row < 51:
            continue
        sample_x = a00 * (col + 0.5) + a01 * (row + 0.5) + tx
        sample_y = a10 * (col + 0.5) + a11 * (row + 0.5) + ty
        exact_samples.add((math.floor(sample_x + 0.5), math.floor(sample_y + 0.5)))
for col, row, pixel_x, pixel_y in damaged_samples:
    assert (pixel_x, pixel_y) not in exact_samples
    expected = 30 + round(0.8 * source[(row * source_width + col) * 3])
    damaged_value = 234 if expected < 128 else 30
    pixel = (pixel_y * canvas_width + pixel_x) * 3
    canvas[pixel:pixel + 3] = [damaged_value] * 3

Path(sys.argv[2]).write_text(
    f'P3\n{canvas_width} {canvas_height}\n255\n' +
    '\n'.join(f'{canvas[i]} {canvas[i + 1]} {canvas[i + 2]}'
              for i in range(0, len(canvas), 3)) + '\n'
)
PY

if ! "$makocode_bin" decode \
    "--output-dir=$work_dir/decoded" "$work_dir/scan/scan.ppm" \
    >"$work_dir/decode.log" 2>&1; then
    cat "$work_dir/decode.log" >&2
    echo 'test_decode_lowres_rotated_scan: generated round-trip scan did not decode' >&2
    exit 1
fi
cmp --silent "$work_dir/makocode.bin" "$work_dir/decoded/makocode.bin"
printf 'generated low-resolution rotated scan round-trips passed (label=%s)\n' "$label"
