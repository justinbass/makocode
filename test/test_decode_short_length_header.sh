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
label="decode_short_length_header"
if [[ ${1:-} == "--label" && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo 'test_decode_short_length_header: unexpected arguments' >&2
    exit 2
fi

work_dir="$repo_root/test_output/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/clean" "$work_dir/damaged" \
    "$work_dir/alias" "$work_dir/decoded_clean" "$work_dir/decoded_damaged" \
    "$work_dir/decoded_alias"
printf 'makocode' > "$work_dir/makocode.bin"

(
    cd "$work_dir"
    "$makocode_bin" encode \
        --input=makocode.bin \
        --ecc=0 --page-width=55 --page-height=55 \
        --no-filename --no-page-count \
        "--output-dir=$work_dir/encoded" --prefix=length_header >/dev/null
)

python3 - "$work_dir/encoded/length_header.ppm" \
    "$work_dir/damaged/source.ppm" <<'PY'
from pathlib import Path
import sys

tokens = Path(sys.argv[1]).read_text().split()
assert tokens[:4] == ['P3', '55', '55', '255']
pixels = list(map(int, tokens[4:]))
# Logical cell (4,0) carries bit 4 of the 64-bit frame-length header.
index = (4 * 3)
assert pixels[index:index + 3] == [0, 0, 0]
pixels[index:index + 3] = [255 - value for value in pixels[index:index + 3]]
Path(sys.argv[2]).write_text(
    'P3\n55 55\n255\n' +
    '\n'.join(f'{pixels[i]} {pixels[i + 1]} {pixels[i + 2]}'
              for i in range(0, len(pixels), 3)) + '\n'
)
PY

for sample in clean damaged; do
    source="$work_dir/encoded/length_header.ppm"
    if [[ $sample == damaged ]]; then
        source="$work_dir/damaged/source.ppm"
    fi
    "$ppm_transform_bin" transform \
        --input "$source" \
        --output "$work_dir/$sample/scaled.ppm" \
        --scale-x 2.018181818 --scale-y 2.018181818
done

python3 - "$work_dir/clean/scaled.ppm" "$work_dir/clean/scan.ppm" \
    "$work_dir/damaged/scaled.ppm" "$work_dir/damaged/scan.ppm" <<'PY'
from pathlib import Path
import sys

for source_name, output_name in ((sys.argv[1], sys.argv[2]),
                                (sys.argv[3], sys.argv[4])):
    tokens = Path(source_name).read_text().split()
    assert tokens[0] == 'P3' and tokens[3] == '255'
    width, height = int(tokens[1]), int(tokens[2])
    assert (width, height) == (111, 111)
    source = list(map(int, tokens[4:]))
    assert len(source) == width * height * 3
    canvas = [230] * (115 * 115 * 3)
    for y in range(height):
        for x in range(width):
            src = (y * width + x) * 3
            dst = ((y + 2) * 115 + x + 2) * 3
            canvas[dst:dst + 3] = [30 + round(0.8 * value)
                                   for value in source[src:src + 3]]
    Path(output_name).write_text(
        'P3\n115 115\n255\n' +
        '\n'.join(f'{canvas[i]} {canvas[i + 1]} {canvas[i + 2]}'
                  for i in range(0, len(canvas), 3)) + '\n'
    )
PY

python3 - "$work_dir/clean/scan.ppm" "$work_dir/alias/scan.ppm" <<'PY'
from pathlib import Path
import sys

tokens = []
for line in Path(sys.argv[1]).read_text().splitlines():
    tokens.extend(line.split('#', 1)[0].split())
assert tokens[:4] == ['P3', '115', '115', '255']
pixels = list(map(int, tokens[4:]))
scale = 2.018181818
offset = 1.5

# These are payload cells outside the frame-length header. Keep each nearest
# center pixel intact, but make the surrounding 2x2 interpolation neighborhood
# look like the opposite color. Bilinear sampling then mixes adjacent cells;
# nearest-center sampling still sees the encoded cell value.
for cell_x, cell_y in ((0, 3), (9, 3), (2, 5), (10, 6), (0, 8), (3, 10)):
    sample_x = scale * (cell_x + 0.5) + offset
    sample_y = scale * (cell_y + 0.5) + offset
    x0, y0 = int(sample_x), int(sample_y)
    nearest_x, nearest_y = int(sample_x + 0.5), int(sample_y + 0.5)
    center_index = (nearest_y * 115 + nearest_x) * 3
    center_luma = sum(pixels[center_index:center_index + 3]) / 3
    opposite = [234, 234, 234] if center_luma < 128 else [30, 30, 30]
    for y in (y0, y0 + 1):
        for x in (x0, x0 + 1):
            if (x, y) == (nearest_x, nearest_y):
                continue
            index = (y * 115 + x) * 3
            pixels[index:index + 3] = opposite

Path(sys.argv[2]).write_text(
    'P3\n115 115\n255\n' +
    '\n'.join(f'{pixels[i]} {pixels[i + 1]} {pixels[i + 2]}'
              for i in range(0, len(pixels), 3)) + '\n'
)
PY

"$makocode_bin" decode \
    "--output-dir=$work_dir/decoded_clean" "$work_dir/clean/scan.ppm" \
    >"$work_dir/decode_clean.log" 2>&1
cmp --silent "$work_dir/makocode.bin" "$work_dir/decoded_clean/makocode.bin"

if ! "$makocode_bin" decode \
    "--output-dir=$work_dir/decoded_damaged" "$work_dir/damaged/scan.ppm" \
    >"$work_dir/decode_damaged.log" 2>&1; then
    cat "$work_dir/decode_damaged.log" >&2
    echo 'test_decode_short_length_header: damaged round-trip did not decode' >&2
    exit 1
fi
if ! grep -Fqx 'decode: recovered frame length from a one-bit header correction' \
    "$work_dir/decode_damaged.log"; then
    cat "$work_dir/decode_damaged.log" >&2
    echo 'test_decode_short_length_header: decoder did not use length recovery' >&2
    exit 1
fi
cmp --silent "$work_dir/makocode.bin" "$work_dir/decoded_damaged/makocode.bin"

if ! "$makocode_bin" decode \
    "--output-dir=$work_dir/decoded_alias" "$work_dir/alias/scan.ppm" \
    >"$work_dir/decode_alias.log" 2>&1; then
    cat "$work_dir/decode_alias.log" >&2
    echo 'test_decode_short_length_header: low-resolution alias scan did not decode' >&2
    exit 1
fi
cmp --silent "$work_dir/makocode.bin" "$work_dir/decoded_alias/makocode.bin"
printf 'low-resolution affine scan round-trips passed (label=%s)\n' "$label"
