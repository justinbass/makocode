#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
harness=${BLOB_ERASURE_HARNESS_BIN:-"$repo_root/test/blob_erasure_harness"}
ppm_transform="$repo_root/test/ppm_transform"
work=$(mktemp -d "$repo_root/test_output/0110_decode_blob_erasures_work.XXXXXX")
seed=1944

if [[ ! -x $bin || ! -x $harness || ! -x $ppm_transform ]]; then
    echo "decode blob erasures: required binaries are missing" >&2
    exit 1
fi

"$ppm_transform" bytes --output - --size 24576 --seed "$seed" |
    "$bin" encode --output-dir="$work" --prefix=blob --page-width=600 --page-height=600 \
        --palette "White Black" --ecc=0.75 >/dev/null

source_ppm=$(find "$work" -maxdepth 1 -type f -name 'blob*.ppm' -print -quit)
if [[ -z $source_ppm ]]; then
    echo "decode blob erasures: encode produced no page" >&2
    exit 1
fi

for blot_color in black white; do
    mutated_ppm="$work/blob_${blot_color}.ppm"
    python3 - "$source_ppm" "$mutated_ppm" "$blot_color" <<'PY'
import math
import sys
from ppm_test_utils import read_ppm, write_ppm

source, destination, color = sys.argv[1:]
width, height, pixels = read_ppm(source)
if (width, height) != (600, 600):
    raise SystemExit(f"unexpected page dimensions: {width}x{height}")
center_x, center_y, radius = 460, 400, 120
# Keep this mutation clear of the centered metadata square and its nearby rows.
square_x0 = square_y0 = (600 - 48) // 2
square_x1 = square_y1 = square_x0 + 48
nearest_x = min(max(center_x, square_x0), square_x1 - 1)
nearest_y = min(max(center_y, square_y0 - 6), square_y1 - 1)
assert (center_x - nearest_x) ** 2 + (center_y - nearest_y) ** 2 > radius ** 2
value = 0 if color == "black" else 255
changed = 0
for y in range(height):
    dy = y - center_y
    for x in range(width):
        dx = x - center_x
        if dx * dx + dy * dy <= radius * radius:
            index = (y * width + x) * 3
            pixels[index:index + 3] = [value, value, value]
            changed += 1
assert changed > 40000
write_ppm(destination, width, height, pixels)
PY
    "$bin" decode --decoded-stdout "$mutated_ppm" 2>"$work/decode_${blot_color}.log" |
        python3 "$script_dir/check_decoded_stdout.py" --generator "$ppm_transform" \
            --size 24576 --seed "$seed" --label "blob $blot_color detected"
    "$harness" "$mutated_ppm"
done

printf 'decode blob erasures: black and white mutations required known-position ECC recovery\n'
