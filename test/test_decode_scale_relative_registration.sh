#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
legacy=1
label=decode_scale_relative_registration
if [[ ${1:-} == --label ]]; then label=$2; shift 2; fi
if [[ ${1:-} == --current ]]; then legacy=0; shift; fi
[[ $# == 0 ]]
work_dir="$repo_root/test_output/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/control" "$work_dir/decoded"
printf makocode > "$work_dir/payload.bin"
(cd "$work_dir"; "$makocode_bin" encode --input=payload.bin --ecc=0 \
    --page-width=55 --page-height=55 --no-filename --no-page-count \
    "--output-dir=$work_dir/encoded" --prefix=source >/dev/null)
if [[ $legacy == 1 ]]; then
    "${CXX:-g++}" -O2 -std=c++17 "$script_dir/test_legacy_metadata_fixture.cpp" -o "$work_dir/legacy_fixture"
    "$work_dir/legacy_fixture" "$work_dir/encoded/source.ppm" "$work_dir/legacy.ppm"
    cp "$work_dir/legacy.ppm" "$work_dir/encoded/source.ppm"
fi
python3 - "$work_dir/encoded/source.ppm" "$work_dir/scan.ppm" <<'PYTHON'
from pathlib import Path
import math
import sys
source = Path(sys.argv[1]).read_text().split()
assert source[:4] == ['P3', '55', '55', '255']
pixels = list(map(int, source[4:]))
assert len(pixels) == 55 * 55 * 3
# Rasterize a fresh encode at 6 scan pixels/module, using 4x4 coverage.
# No scanned image, measured scan pixels, or external fixture data are used.
width, height = 340, 342
pitch, ox, oy = 6.0, 4.25, 5.25
canvas = []
for y in range(height):
    for x in range(width):
        total = 0
        for sy in (.125, .375, .625, .875):
            for sx in (.125, .375, .625, .875):
                col = math.floor((x + sx - ox) / pitch)
                row = math.floor((y + sy - oy) / pitch)
                total += (120 + round(114 * pixels[(row * 55 + col) * 3] / 255)
                          if 0 <= col < 55 and 0 <= row < 55 else 234)
        canvas.append(round(total / 16))
# Mild optical spread and two external ink streaks. The latter expand the
# supported dark bounds without changing any encoded module's center pixel.
blurred = canvas.copy()
for y in range(1, height - 1):
    for x in range(1, width - 1):
        i = y * width + x
        blurred[i] = round(.75 * canvas[i] + .0625 * sum(
            canvas[j] for j in (i-1, i+1, i-width, i+width)))
centers = {(round(ox + (x + .5) * pitch - .5),
            round(oy + (y + .5) * pitch - .5))
           for y in range(55) for x in range(55)}
for x, y in [(336, y) for y in range(20, 23)] + [(x, 337) for x in range(20, 23)]:
    assert (x, y) not in centers
    blurred[y * width + x] = 60
Path(sys.argv[2]).write_text(f'P3\n{width} {height}\n255\n' + '\n'.join(
    f'{value} {value} {value}' for value in blurred))
PYTHON
"$makocode_bin" decode "--output-dir=$work_dir/control" "$work_dir/encoded/source.ppm" >"$work_dir/control.log" 2>&1
cmp "$work_dir/payload.bin" "$work_dir/control/payload.bin"
if ! "$makocode_bin" decode "--output-dir=$work_dir/decoded" "$work_dir/scan.ppm" >"$work_dir/decode.log" 2>&1; then
    cat "$work_dir/decode.log" >&2
    exit 1
fi
cmp "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"
# Match the complete user-facing decoded stream, including metadata and archive.
"$makocode_bin" decode --decoded-stdout "$work_dir/encoded/source.ppm" >"$work_dir/expected.txt" 2>"$work_dir/stdout_control.log"
"$makocode_bin" decode --decoded-stdout "$work_dir/scan.ppm" >"$work_dir/actual.txt" 2>"$work_dir/stdout_scan.log"
cmp "$work_dir/expected.txt" "$work_dir/actual.txt"
