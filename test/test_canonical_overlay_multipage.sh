#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"
canonical_tile_harness_bin=${CANONICAL_TILE_HARNESS_BIN:-"$repo_root/test/canonical_tile_harness"}
work=$(mktemp -d "$repo_root/test_output/canonical_overlay_work.XXXXXX")

if [[ ! -x "$bin" || ! -x "$ppm_transform_bin" ]]; then
    echo "canonical overlay: required binaries are missing" >&2
    exit 1
fi

if [[ ! -x $canonical_tile_harness_bin ]]; then
    canonical_tile_harness_bin="$work/canonical_tile_harness"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -pedantic -O2 \
        "$script_dir/test_canonical_tile_harness.cpp" -o "$canonical_tile_harness_bin"
fi
python3 - "$work/payload.bin" <<'PY'
import random, sys
source = random.Random(103)
with open(sys.argv[1], 'wb') as stream:
    stream.write(bytes(source.randrange(256) for _ in range(262144)))
PY

mkdir -p "$work/pages"
(cd "$work" && "$bin" encode --input=payload.bin --ecc=0.05 --overlay-fraction=0.01 \
    --page-width=1000 --page-height=1000 \
    --prefix=page --output-dir="$work/pages" >/dev/null)
shopt -s nullglob
pages=("$work"/pages/*.ppm)
shopt -u nullglob
if [[ ${#pages[@]} -lt 2 ]]; then
    echo "canonical overlay: expected a multipage encoding, found ${#pages[@]} page(s)" >&2
    exit 1
fi

# Every page must agree on the protected selector settings, even when one
# page declares no overlay reserve and therefore has no selector map.
python3 - "$work/mixed_payload.bin" <<'PY'
import random, sys
rng = random.Random(104)
with open(sys.argv[1], 'wb') as stream:
    stream.write(bytes(rng.randrange(256) for _ in range(4096)))
PY
mkdir -p "$work/mixed_reserved" "$work/mixed_plain"
(cd "$work" && "$bin" encode --input=mixed_payload.bin --ecc=0.03 --overlay-fraction=0.01 \
    --page-width=1000 --page-height=1000 \
    --prefix=reserved --output-dir="$work/mixed_reserved" >/dev/null)
(cd "$work" && "$bin" encode --input=mixed_payload.bin --ecc=0.02 \
    --page-width=1000 --page-height=1000 \
    --prefix=plain --output-dir="$work/mixed_plain" >/dev/null)
reserved_page=$(find "$work/mixed_reserved" -maxdepth 1 -type f -name '*.ppm' -print -quit)
plain_page=$(find "$work/mixed_plain" -maxdepth 1 -type f -name '*.ppm' -print -quit)
if "$bin" decode --output-dir="$work/mixed_decode" "$reserved_page" "$plain_page" \
    >"$work/mixed_decode.log" 2>&1; then
    echo "canonical overlay: decode accepted conflicting per-page selector settings" >&2
    exit 1
fi
if ! rg -q "conflicting overlay selector settings between pages" "$work/mixed_decode.log"; then
    echo "canonical overlay: mixed selector pages failed for the wrong reason" >&2
    exit 1
fi

page_indices=()
page_paths=()
page_summaries=()
for page in "${pages[@]}"; do
    summary=$("$canonical_tile_harness_bin" "$page" 1000 1000 1 --report-page)
    IFS=$'\t' read -r page_index page_count block_count parity unknown_budget overlay_budget selector_present selector_total selector_seed selector_counts <<< "$summary"
    if [[ -z ${page_index:-} || $page_count -ne ${#pages[@]} ||
          $selector_present -ne 1 || $block_count -eq 0 || $parity -eq 0 ||
          $overlay_budget -eq 0 || $selector_total -ne $((block_count * overlay_budget)) ]]; then
        echo "canonical overlay: invalid base tile selector or ECC metadata" >&2
        exit 1
    fi
    page_indices+=("$page_index")
    page_paths+=("$page")
    page_summaries+=("$summary")
done
ordered_pages=()
ordered_summaries=()
for ((expected_index = 1; expected_index <= ${#pages[@]}; ++expected_index)); do
    match=-1
    for candidate in "${!page_indices[@]}"; do
        if [[ ${page_indices[$candidate]} -eq $expected_index ]]; then
            match=$candidate
            break
        fi
    done
    if [[ $match -lt 0 ]]; then
        echo "canonical overlay: missing serialized page index $expected_index" >&2
        exit 1
    fi
    ordered_pages+=("${page_paths[$match]}")
    ordered_summaries+=("${page_summaries[$match]}")
done
pages=("${ordered_pages[@]}")
page_summaries=("${ordered_summaries[@]}")

# The right edge remains payload. Corrupting the centered protected tile must
# fail closed because its selector and ECC budgets are authoritative.
python3 - "${pages[0]}" "$work/bad_tile.ppm" <<'PY'
import sys
from ppm_test_utils import read_ppm, write_ppm

width, height, pixels = read_ppm(sys.argv[1])
assert (width, height) == (1000, 1000)
stripe_x0 = width - (width + 7) // 8
nonwhite = 0
tile_x = tile_y = (width - 48) // 2
for y in range(height):
    for x in range(width):
        index = (y * width + x) * 3
        rgb = tuple(pixels[index:index + 3])
        if x >= stripe_x0 and rgb != (255, 255, 255):
            nonwhite += 1
        if tile_x <= x < tile_x + 48 and tile_y <= y < tile_y + 48:
            pixels[index:index + 3] = (127, 127, 127)
write_ppm(sys.argv[2], width, height, pixels)
assert nonwhite > (width - stripe_x0) * height // 5, 'right edge is still an unused white stripe'
PY
if "$canonical_tile_harness_bin" "$work/bad_tile.ppm" 1000 1000 1 --report-page-direct \
    >"$work/bad_tile.log" 2>&1; then
    echo "canonical overlay: extraction accepted a corrupted centered selector tile" >&2
    exit 1
fi

"$ppm_transform_bin" overlay-mask --output "$work/overlay.ppm" \
    --circle-color "0 0 0" --background-color "255 255 255" \
    --width 1000 --height 1000
mkdir -p "$work/merged" "$work/decoded"
merged_pages=()
for index in "${!pages[@]}"; do
    merged="$work/merged/page_${index}.ppm"
    "$bin" overlay --debug "${pages[$index]}" "$work/overlay.ppm" 0.10 > "$merged" 2>"$work/overlay_${index}.log"
    selector_report=$(awk '/overlay selector symbols used:/ {print $5}' "$work/overlay_${index}.log")
    IFS=/ read -r selector_used selector_capacity <<< "$selector_report"
    if [[ -z $selector_used || -z $selector_capacity || $selector_used -eq 0 ||
          $selector_used -gt $selector_capacity ]]; then
        echo "canonical overlay: overlay did not consume a protected selector" >&2
        exit 1
    fi
    change_report=$("$ppm_transform_bin" overlay-check --base "${pages[$index]}" \
        --merged "$merged" --skip-grayscale 1)
    changed=$(printf '%s\n' "$change_report" | awk '/overlay pixels modified:/ {print $4}')
    if [[ -z $changed || $changed -eq 0 ]]; then
        echo "canonical overlay: page $((index + 1)) had no visible overlay changes" >&2
        exit 1
    fi
    merged_summary=$("$canonical_tile_harness_bin" "$merged" 1000 1000 1 --report-page)
    IFS=$'\t' read -r merged_index merged_count merged_blocks merged_parity merged_unknown merged_overlay merged_selector_present merged_total merged_seed merged_counts <<< "$merged_summary"
    IFS=$'\t' read -r base_index base_count base_blocks base_parity base_unknown base_overlay base_selector_present base_total base_seed base_counts <<< "${page_summaries[$index]}"
    if [[ $merged_index -ne $((index + 1)) || $merged_count -ne ${#pages[@]} ||
          $merged_index -ne $base_index || $merged_count -ne $base_count ||
          $merged_blocks -ne $base_blocks || $merged_parity -ne $base_parity ||
          $merged_unknown -ne $base_unknown || $merged_overlay -ne $base_overlay ||
          $merged_selector_present -ne 1 || $merged_seed -ne $base_seed ||
          $merged_total -ne $base_total || $selector_capacity -ne $((merged_blocks * merged_overlay)) ]]; then
        echo "canonical overlay: merged page selector or ECC metadata is inconsistent" >&2
        exit 1
    fi
    IFS=, read -r -a merged_count_values <<< "$merged_counts"
    merged_sum=0
    for selector_count in "${merged_count_values[@]}"; do
        if [[ $selector_count -ne $merged_overlay ]]; then
            echo "canonical overlay: decoder did not reconstruct the full selector set" >&2
            exit 1
        fi
        merged_sum=$((merged_sum + selector_count))
    done
    if [[ $merged_sum -ne $merged_total ]]; then
        echo "canonical overlay: full selector total is inconsistent" >&2
        exit 1
    fi
    merged_pages+=("$merged")
done
"$bin" decode --output-dir="$work/decoded" "${merged_pages[@]}" >/dev/null
cmp "$work/payload.bin" "$work/decoded/payload.bin"
