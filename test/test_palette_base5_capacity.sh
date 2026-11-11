#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=""
if [[ $# -eq 2 && $1 == "--label" ]]; then
    label=$2
else
    echo "Usage: test_palette_base5_capacity.sh --label NAME" >&2
    exit 2
fi

palette="White Cyan Magenta Yellow Black"
bash "$script_dir/run_roundtrip.sh" \
    --label "$label" \
    --size 26000 \
    --ecc 0 \
    --width 320 \
    --height 320 \
    --palette "$palette" \

shopt -s nullglob
pages=("$repo_root"/test_output/"${label}_work."*/"${label}_random_payload_encoded"*.ppm)
shopt -u nullglob
if [[ ${#pages[@]} -ne 1 ]]; then
    echo "base-5 palette expected one page, got ${#pages[@]}" >&2
    exit 1
fi

decoded_metadata_dir="${pages[0]%/*}/metadata"
mkdir -p "$decoded_metadata_dir"
"$repo_root/makocode" decode --decoded-file --palette "$palette" \
    --output-dir="$decoded_metadata_dir" "${pages[0]}" >/dev/null
python3 - "${pages[0]}" "$decoded_metadata_dir" <<'PY'
from pathlib import Path
from ppm_test_utils import read_ppm
import sys

ppm_path = Path(sys.argv[1])
metadata_dir = Path(sys.argv[2])
width, height, pixels = read_ppm(ppm_path)
maxval = 255
assert (width, height, maxval) == (320, 320, 255)
values = iter(pixels)
tile_x0 = width // 2 - 24
tile_x1 = tile_x0 + 48
tile_y0 = height // 2 - 24
tile_y1 = tile_y0 + 48
black_outside_tile = 0
for y in range(height):
    for x in range(width):
        rgb = (next(values), next(values), next(values))
        if rgb == (0, 0, 0) and not (tile_x0 <= x < tile_x1 and tile_y0 <= y < tile_y1):
            black_outside_tile += 1
assert next(values, None) is None
assert black_outside_tile > 2500, black_outside_tile

decoded_files = list(metadata_dir.glob("*.txt"))
assert len(decoded_files) == 1, decoded_files
page_bits = [
    int(line.split()[-1])
    for line in decoded_files[0].read_text("ascii").splitlines()
    if line.startswith("metadata page_bits ")
]
assert len(page_bits) == 1, page_bits
assert page_bits[0] > 2 * width * height, page_bits[0]
PY
