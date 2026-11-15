#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform="$repo_root/test/ppm_transform"
work=$(mktemp -d "$repo_root/test_output/0118_decode_metadata_free_large_work.XXXXXX")
seed=${MAKO_TEST_SEED:-1944}
case_seed=$((seed + 130363))
payload_size=$((1800 + (case_seed % 501)))
label="large_page"

"$ppm_transform" bytes --output - --size "$payload_size" --seed "$case_seed" |
    "$bin" encode --output-dir="$work" --prefix="$label" \
        --page-width=500 --page-height=500 \
        --palette "White Black" --ecc=3.5 >/dev/null

shopt -s nullglob
source_pages=("$work"/*.ppm)
shopt -u nullglob
if [[ ${#source_pages[@]} -ne 1 ]]; then
    echo "metadata-free large page: expected one encoded page, found ${#source_pages[@]}" >&2
    exit 1
fi
source_ppm=${source_pages[0]}
damaged_ppm="$work/$label.damaged.ppm"

# A prefix without "_encoded" keeps the output free of embedded fiducial marks.
python3 - "$source_ppm" "$damaged_ppm" <<'PY'
import sys
from ppm_test_utils import read_ppm, write_ppm

source, destination = sys.argv[1:]
width, height, pixels = read_ppm(source)
assert (width, height) == (500, 500), (width, height)
center_x = width // 2
center_y = height // 2
radius = 37
tile_x0 = tile_y0 = (width - 48) // 2
for y in range(tile_y0, tile_y0 + 48):
    for x in range(tile_x0, tile_x0 + 48):
        assert (x - center_x) ** 2 + (y - center_y) ** 2 <= radius ** 2
changed = 0
for y in range(height):
    for x in range(width):
        if (x - center_x) ** 2 + (y - center_y) ** 2 <= radius ** 2:
            offset = (y * width + x) * 3
            pixels[offset:offset + 3] = b'\x00\x00\x00'
            changed += 1
assert changed > 4000
write_ppm(destination, width, height, pixels)
PY

"$bin" decode --decoded-stdout "$damaged_ppm" \
    2>"$work/$label.decode.log" |
    python3 "$script_dir/check_decoded_stdout.py" --generator "$ppm_transform" \
        --size "$payload_size" --seed "$case_seed" --label "metadata-free large page"

printf 'metadata-free large page recovered without fiducials or damage hints\n'
