#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=decode_scale_relative_registration
if [[ ${1:-} == --label ]]; then label=$2; shift 2; fi
[[ $# == 0 ]]
work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
mkdir -p "$work_dir/encoded" "$work_dir/control" "$work_dir/decoded"
printf makocode > "$work_dir/payload.bin"
(cd "$work_dir"; "$makocode_bin" encode --input=payload.bin --ecc=1.17 \
    --page-width=80 --page-height=80 \
    "--output-dir=$work_dir/encoded" --prefix=source >/dev/null)
python3 - "$work_dir/encoded/source.ppm" "$work_dir/scan.ppm" <<'PYTHON'
from pathlib import Path
from ppm_test_utils import read_ppm, write_gray_ppm
import math
import sys
source_width, source_height, pixels = read_ppm(sys.argv[1])
assert (source_width, source_height) == (80, 80)
assert len(pixels) == 80 * 80 * 3
# Rasterize a fresh encode at 5.8 scan pixels/module, using 4x4 coverage.
# No scanned image, measured scan pixels, or external fixture data are used.
width, height = 464, 464
pitch, ox, oy = 5.8, 0.0, 0.0
canvas = []
for y in range(height):
    for x in range(width):
        total = 0
        for sy in (.125, .375, .625, .875):
            for sx in (.125, .375, .625, .875):
                col = math.floor((x + sx - ox) / pitch)
                row = math.floor((y + sy - oy) / pitch)
                total += (120 + round(114 * pixels[(row * 80 + col) * 3] / 255)
                          if 0 <= col < 80 and 0 <= row < 80 else 234)
        canvas.append(round(total / 16))
write_gray_ppm(sys.argv[2], width, height, canvas)
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
