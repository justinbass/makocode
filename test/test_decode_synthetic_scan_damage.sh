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
label="decode_synthetic_scan_damage"
if [[ ${1:-} == --label && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
margin_noise=0
off_center_noise=0
if [[ ${1:-} == --margin-noise ]]; then
    margin_noise=1
    shift
fi
if [[ ${1:-} == --off-center-noise ]]; then
    off_center_noise=1
    shift
fi
if [[ $# -ne 0 ]]; then
    echo 'test_decode_synthetic_scan_damage: unexpected arguments' >&2
    exit 2
fi

work_dir="$repo_root/test_output/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded_control" \
    "$work_dir/decoded_soft" "$work_dir/decoded_damage" \
    "$work_dir/tmp/makocode_probe_enc"
payload_path=tmp/makocode_probe_enc/makocode.bin
if [[ $margin_noise == 1 ]]; then
    payload_path=payload.bin
    printf 'scan-test' > "$work_dir/$payload_path"
else
    printf makocode > "$work_dir/$payload_path"
fi

(
    cd "$work_dir"
    "$makocode_bin" encode \
        "--input=$payload_path" \
        --ecc=0 --page-width=55 --page-height=55 \
        --no-filename --no-page-count \
        "--output-dir=$work_dir/encoded" --prefix=synthetic > /dev/null
)

# Render a 2.24-pixel/module page with four-by-four subpixel coverage. Six
# border marks prevent a dark-bounds override; three off-center ink pixels
# affect the tile-derived affine sampler while leaving true centers readable.
# --margin-noise instead adds an isolated mark outside the page, allowing the
# dark-bounds override to mistake that mark for part of the page geometry.
python3 - "$work_dir/encoded/synthetic.ppm" "$work_dir/damaged.ppm" \
    "$work_dir/recovered.ppm" "$work_dir/soft.ppm" "$margin_noise" "$off_center_noise" <<'PY'
from pathlib import Path
import math
import random
import sys

source = Path(sys.argv[1]).read_text().split()
assert source[:4] == ['P3', '55', '55', '255']
pixels = list(map(int, source[4:]))
assert len(pixels) == 55 * 55 * 3
modules = [int(sum(pixels[index:index + 3]) < 384)
           for index in range(0, len(pixels), 3)]
width, height = 137, 132
margin_noise = sys.argv[5] == '1'
pitch, offset_x, offset_y = 2.24, 2.6 if margin_noise else 2.2, 3.5
base = []
for y in range(height):
    row = []
    for x in range(width):
        dark = 0
        for sub_y in (0.125, 0.375, 0.625, 0.875):
            for sub_x in (0.125, 0.375, 0.625, 0.875):
                logical_x = math.floor((x + sub_x - offset_x) / pitch)
                logical_y = math.floor((y + sub_y - offset_y) / pitch)
                if 0 <= logical_x < 55 and 0 <= logical_y < 55:
                    dark += modules[logical_y * 55 + logical_x]
        row.append(round(230 - 200 * dark / 16))
    base.append(row)

if not margin_noise:
    for x, y in ((0, 1), (0, 2), (0, 3), (1, 0), (2, 0), (3, 0)):
        base[y][x] = 80
damaged = [row.copy() for row in base]
if margin_noise:
    assert offset_x + 55 * pitch < 133  # Mark lies outside the page.
    for y in range(125, 128):
        for x in range(133, 136):
            damaged[y][x] = 80
else:
    for x, y in ((6, 78), (6, 79), (7, 79)):
        damaged[y][x] = 0

# Reproducible scan specks outside every known cell center. The assertion
# below independently checks that the encoded module values survive.
if sys.argv[6] == '1':
    damaged = [row.copy() for row in base]
    centers = {(round((x + 0.5) * pitch + offset_x - 0.5),
                round((y + 0.5) * pitch + offset_y - 0.5))
               for y in range(55) for x in range(55)}
    rng = random.Random(0)
    for y in range(height):
        for x in range(width):
            if (x, y) not in centers and rng.random() < 0.03:
                damaged[y][x] = 0

with Path(sys.argv[2]).open('w', encoding='ascii') as output:
    output.write(f'P3\n{width} {height}\n255\n')
    for row in damaged:
        for value in row:
            output.write(f'{value} {value} {value}\n')

with Path(sys.argv[4]).open('w', encoding='ascii') as output:
    output.write(f'P3\n{width} {height}\n255\n')
    for row in base:
        for value in row:
            output.write(f'{value} {value} {value}\n')

with Path(sys.argv[3]).open('w', encoding='ascii') as output:
    output.write('P3\n55 55\n255\n')
    for y in range(55):
        for x in range(55):
            sample_x = round((x + 0.5) * pitch + offset_x - 0.5)
            sample_y = round((y + 0.5) * pitch + offset_y - 0.5)
            recovered = int(damaged[sample_y][sample_x] < 128)
            original = modules[y * 55 + x]
            assert recovered == original, (x, y)
            value = 0 if recovered else 255
            output.write(f'{value} {value} {value}\n')
print('all 3025 generated modules remain recoverable at known centers')
PY

"$makocode_bin" decode "--output-dir=$work_dir/decoded_control" \
    "$work_dir/recovered.ppm" >"$work_dir/control.log" 2>&1
cmp --silent "$work_dir/$payload_path" "$work_dir/decoded_control/$payload_path"
"$makocode_bin" decode "--output-dir=$work_dir/decoded_soft" \
    "$work_dir/soft.ppm" >"$work_dir/soft.log" 2>&1
cmp --silent "$work_dir/$payload_path" "$work_dir/decoded_soft/$payload_path"

status=0
debug_option=--debug
if [[ $off_center_noise == 1 ]]; then
    debug_option=
fi
"$makocode_bin" decode ${debug_option:+"$debug_option"} "--output-dir=$work_dir/decoded_damage" \
    "$work_dir/damaged.ppm" >"$work_dir/damage.log" 2>&1 || status=$?
if [[ $status -eq 0 ]]; then
    cmp --silent "$work_dir/$payload_path" "$work_dir/decoded_damage/$payload_path"
    exit 0
fi

if [[ $(grep -Fxc 'decode: parse failure; retrying without fiducial subgrid' \
             "$work_dir/damage.log") -ne 1 ||
      $(grep -Fxc 'decode: parse failure' "$work_dir/damage.log") -ne 1 ]]; then
    cat "$work_dir/damage.log" >&2
    exit 1
fi
if [[ $off_center_noise == 1 ]]; then
    printf '%s\n' 'decode: parse failure; retrying without fiducial subgrid' \
        'decode: parse failure' > "$work_dir/expected.log"
    cmp "$work_dir/expected.log" "$work_dir/damage.log"
    cat "$work_dir/damage.log" >&2
    "${CXX:-g++}" -std=c++17 -O2 \
        "$script_dir/test_scan_sampling_failure_probe.cpp" -o "$work_dir/sampling_probe"
    "$work_dir/sampling_probe" "$work_dir/encoded/synthetic.ppm" "$work_dir/damaged.ppm"
    echo "recoverable synthetic scan failed (exit $status); artifacts: $work_dir" >&2
    exit 1
fi
"${CXX:-g++}" -std=c++17 -O2 \
    "$script_dir/test_synthetic_scan_frame_probe.cpp" -o "$work_dir/frame_probe"
"$work_dir/frame_probe" "$work_dir/encoded/synthetic.ppm" \
    "$work_dir/damaged.ppm"
echo 'generated, recoverable scan failed with the expected parse failure' >&2
exit 1
