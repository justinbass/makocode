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
    --size 34000 \
     \
    --width 360 \
    --height 360 \
    --palette "$palette" \

shopt -s nullglob
work_dirs=("$repo_root"/test_output/"${label}_work."*)
shopt -u nullglob
if [[ ${#work_dirs[@]} -eq 0 ]]; then
    echo "base-5 palette work directory was not created" >&2
    exit 1
fi
work_dir=$(ls -td "${work_dirs[@]}" | head -n 1)
shopt -s nullglob
pages=("$work_dir"/"${label}_random_payload_encoded"*.ppm)
shopt -u nullglob
if [[ ${#pages[@]} -ne 1 ]]; then
    echo "base-5 palette expected one page, got ${#pages[@]}" >&2
    exit 1
fi

decoded_metadata_dir="${pages[0]%/*}/metadata"
mkdir -p "$decoded_metadata_dir"
"$repo_root/makocode" decode --decoded-file --palette "$palette" \
    --output-dir="$decoded_metadata_dir" "${pages[0]}" >/dev/null
tile_layout=$(${CANONICAL_TILE_HARNESS_BIN:-"$repo_root/test/canonical_tile_harness"} \
    "${pages[0]}" 360 360 1 --metadata-layout)
python3 - "${pages[0]}" "$decoded_metadata_dir" "$tile_layout" <<'PY'
from pathlib import Path
from ppm_test_utils import read_ppm
import sys

ppm_path = Path(sys.argv[1])
metadata_dir = Path(sys.argv[2])
width, height, pixels = read_ppm(ppm_path)
maxval = 255
assert (width, height, maxval) == (360, 360, 255)
square_x0, square_y0, square_side, _, tag_y0, _ = map(int, sys.argv[3].split())
values = iter(pixels)
tile_x0 = width // 2 - 24 + square_x0
tile_x1 = tile_x0 + square_side
tile_y0 = height // 2 - 24 + tag_y0
tile_y1 = tile_y0 + square_side + 6
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
