#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname "$0")/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$root/makocode"}
label=${2:-decode_scan_registration}
work=$(mktemp -d "$root/test_output/${label}_work.XXXXXX")
mkdir -p "$work/pages"
printf 'registration regression' > "$work/input.bin"
(cd "$work" && "$bin" encode --input=input.bin --ecc=0.00 --page-width=80 --page-height=80 --output-dir=pages --prefix=page >/dev/null)
python3 - "$work/pages/page.ppm" "$work/scan.ppm" <<'PY'
import math
import random
import sys
from pathlib import Path
from ppm_test_utils import read_ppm, write_gray_ppm

source_width, source_height, pixels = read_ppm(sys.argv[1])
assert (source_width, source_height) == (80, 80)
pitch, ox, oy = 2.75, 2.5, 2.0
width, height = 225, 226
centers = {(round((x + .5) * pitch + ox - .5),
            round((y + .5) * pitch + oy - .5))
           for y in range(80) for x in range(80)}
rng = random.Random(0)
scan = []
for y in range(height):
    for x in range(width):
        dark = 0
        for sy in (.125, .375, .625, .875):
            for sx in (.125, .375, .625, .875):
                col = math.floor((x + sx - ox) / pitch)
                row = math.floor((y + sy - oy) / pitch)
                if 0 <= col < 80 and 0 <= row < 80:
                    dark += pixels[3 * (row * 80 + col)] < 128
        value = round(230 - 200 * dark / 16)
        if (x, y) not in centers and rng.random() < .012:
            value = 0
        scan.append(value)
for y in range(80):
    for x in range(80):
        cx = round((x + .5) * pitch + ox - .5)
        cy = round((y + .5) * pitch + oy - .5)
        assert (scan[cy * width + cx] < 128) == (pixels[3 * (y * 80 + x)] < 128)
write_gray_ppm(sys.argv[2], width, height, scan)
PY
"$bin" decode --decoded-stdout "$work/pages/page.ppm" >"$work/expected.txt" 2>"$work/control.log"
"$bin" decode --decoded-stdout "$work/scan.ppm" >"$work/actual.txt" 2>"$work/scan.log" || { cat "$work/scan.log" >&2; exit 1; }
cmp "$work/expected.txt" "$work/actual.txt"
