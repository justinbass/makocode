#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=decode_recover_palette
if [[ ${1:-} == --label ]]; then label=$2; shift 2; fi
[[ $# == 0 ]]
work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
mkdir -p "$work_dir"
payload="$work_dir/payload.bin"
"$repo_root/test_output/bin/ppm_transform" bytes --output "$payload" --size 8192 --seed 48621

damage_palette_row() {
    local source=$1 destination=$2 palette_count=$3
    local layout
    layout=$("$repo_root/test_output/bin/canonical_tile_harness" "$source" 700 700 1 --metadata-layout)
    python3 - "$source" "$destination" "$palette_count" "$layout" <<'PY'
from ppm_test_utils import read_ppm, write_ppm
import sys

source, destination = sys.argv[1:3]
palette_count = int(sys.argv[3])
square_x, square_y, square_side, prefix_bits, tag_y, symbol_count = map(int, sys.argv[4].split())
width, height, pixels = read_ppm(source)
original = pixels[:]
tile_x = (width - 48) // 2
tile_y = (height - 48) // 2
palette_x = (48 - (palette_count + 1)) // 2
row_y = tile_y + tag_y
for x in range(tile_x + palette_x, tile_x + palette_x + palette_count + 1):
    offset = (row_y * width + x) * 3
    pixels[offset:offset + 3] = [255, 255, 255]
changed = []
for index in range(0, len(pixels), 3):
    if pixels[index:index + 3] != original[index:index + 3]:
        changed.append(((index // 3) % width, (index // 3) // width))
assert changed
assert all(tile_x + palette_x <= x < tile_x + palette_x + palette_count + 1 and y == row_y
           for x, y in changed), changed[:10]
write_ppm(destination, width, height, pixels)
PY
}

run_common_case() {
    local case_name=$1 palette_count=$2 palette=$3
    local encoded="$work_dir/${case_name}_encoded"
    local decoded="$work_dir/${case_name}_decoded"
    mkdir -p "$encoded" "$decoded"
    (cd "$work_dir" && "$repo_root/makocode" encode --input payload.bin \
        --output-dir "$encoded" --prefix page --page-width 700 --page-height 700 \
        --ecc 2.0 --palette "$palette") >/dev/null
    shopt -s nullglob
    local pages=("$encoded"/page*.ppm)
    shopt -u nullglob
    [[ ${#pages[@]} == 1 ]]
    local damaged="$work_dir/${case_name}_damaged.ppm"
    damage_palette_row "${pages[0]}" "$damaged" "$palette_count"
    "$repo_root/makocode" decode --recover-palette --recover-limit 4 \
        --output-dir "$decoded" "$damaged" >"$work_dir/${case_name}.log" 2>&1
    cmp --silent "$payload" "$decoded/payload.bin"
    if [[ $case_name == two_color ]]; then
        local huge_limit_decoded="$work_dir/huge_limit_decoded"
        mkdir -p "$huge_limit_decoded"
        "$repo_root/makocode" decode --recover-palette \
            --recover-limit 100000000000000000000000 \
            --output-dir "$huge_limit_decoded" "$damaged" \
            >"$work_dir/huge_limit.log" 2>&1
        cmp --silent "$payload" "$huge_limit_decoded/payload.bin"
    fi
}

run_common_case two_color 2 'White Black'
run_common_case four_color 4 'White Cyan Magenta Yellow'
run_common_case five_color 5 'White Cyan Magenta Yellow Black'
run_common_case eight_color 8 'White Black #FF0000 #00FF00 #0000FF Cyan Magenta Yellow'

odd_palette='White #17D94B #E24A91 #2977C3 #D6B32A Black'
odd_encoded="$work_dir/odd_encoded"
odd_decoded="$work_dir/odd_decoded"
mkdir -p "$odd_encoded" "$odd_decoded"
(cd "$work_dir" && "$repo_root/makocode" encode --input payload.bin \
    --output-dir "$odd_encoded" --prefix page --page-width 700 --page-height 700 \
    --ecc 2.0 --palette "$odd_palette") >/dev/null
shopt -s nullglob
odd_pages=("$odd_encoded"/page*.ppm)
shopt -u nullglob
[[ ${#odd_pages[@]} == 1 ]]
odd_page=${odd_pages[0]}
odd_damaged="$work_dir/odd_damaged.ppm"
damage_palette_row "$odd_page" "$odd_damaged" 6
if "$repo_root/makocode" decode --recover-palette --recover-limit 4 \
    --output-dir "$odd_decoded" "$odd_damaged" >"$work_dir/odd.log" 2>&1; then
    echo 'decode recover palette: unexpected match for six-color custom palette within four candidates' >&2
    exit 1
fi
if ! grep -Fq 'palette recovery tried 4 candidate(s)' "$work_dir/odd.log"; then
    cat "$work_dir/odd.log" >&2
    echo 'decode recover palette: expected bounded-search diagnostic' >&2
    exit 1
fi

"$repo_root/makocode" decode --recover-palette --recover-limit 1 --palette "$odd_palette" \
    --output-dir "$odd_decoded" "$odd_damaged" >"$work_dir/odd_override.log" 2>&1
cmp --silent "$payload" "$odd_decoded/payload.bin"
printf 'decode recover palette: common palettes recovered; custom palette stopped at limit\n'
