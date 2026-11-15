#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform="$repo_root/test/ppm_transform"
work=$(mktemp -d "$repo_root/test_output/decode_metadata_free.XXXXXX")
seed=${MAKO_TEST_SEED:-1944}
palettes=(
    'White Black'
    'White Cyan Magenta Yellow'
    'White Cyan Magenta Yellow Black'
    'White #FF0000 #00FF00 #0000FF Yellow Magenta Cyan Black'
)

case_pids=()
for palette_index in "${!palettes[@]}"; do
(
    palette=${palettes[$palette_index]}
    case_seed=$((seed + 7919 * palette_index + 17))
    payload_size=$((700 + (case_seed % 151)))
    label="palette_${palette_index}"
    "$ppm_transform" bytes --output - --size "$payload_size" --seed "$case_seed" |
        "$bin" encode --output-dir="$work" --prefix="$label" \
            --page-width=220 --page-height=220 \
            --palette "$palette" --ecc=3.5 >/dev/null
    source_ppm="$work/$label.ppm"
    damaged_ppm="$work/$label.damaged.ppm"
    python3 - "$source_ppm" "$damaged_ppm" <<'PY'
import math
import sys
from ppm_test_utils import read_ppm, write_ppm

source, destination = sys.argv[1:]
width, height, pixels = read_ppm(source)
assert (width, height) == (220, 220), (width, height)
center_x = center_y = width // 2
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
            --size "$payload_size" --seed "$case_seed" --label "metadata-free $palette"
) &
    case_pids+=("$!")
    if [[ ${#case_pids[@]} -ge 2 ]]; then
        wait "${case_pids[0]}"
        wait "${case_pids[1]}"
        case_pids=()
    fi
done
if [[ ${#case_pids[@]} -gt 0 ]]; then
    for case_pid in "${case_pids[@]}"; do
        wait "$case_pid"
    done
fi

printf 'metadata-free recovery: all standard palettes round-tripped without damage hints\n'
