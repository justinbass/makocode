#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See
# the LICENSE file for the full license text.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/test_seed.sh"
. "$script_dir/lib/fiducials.sh"
makocode_bin="$repo_root/makocode"
ppm_transform_bin="$repo_root/test/ppm_transform"

if [[ ! -x "$makocode_bin" ]]; then
    echo "test_overlay_e2e: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if [[ ! -x "$ppm_transform_bin" ]]; then
    echo "test_overlay_e2e: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

label="${1:-3004}"
if [[ -z "$label" ]]; then
    echo "test_overlay_e2e: label may not be empty" >&2
    exit 1
fi

mako_test_seed_init "$repo_root"
payload_seed=$(mako_test_case_seed "payload/$label")
MAKOCODE_TEST_SEED=$(mako_test_case_seed "crypto/$label")
export MAKOCODE_TEST_SEED

test_dir="$repo_root/test_output"
work_dir=$(mktemp -d "$test_dir/${label}_overlay_work.XXXXXX")
canonical_tile_harness_bin=${CANONICAL_TILE_HARNESS_BIN:-"$repo_root/test/canonical_tile_harness"}
payload_path="$work_dir/payload.bin"
overlay_path="$work_dir/${label}_overlay_mask.ppm"
encoded_dir="$work_dir/encoded_pages"

ecc="${MAKO_OVERLAY_ECC:-}"
fraction="${MAKO_OVERLAY_FRACTION:-0.1}"
ignore_colors="${MAKO_OVERLAY_IGNORE_COLORS:-}"
payload_size="${MAKO_OVERLAY_SIZE:-32768}"
page_width="${MAKO_OVERLAY_PAGE_WIDTH:-1000}"
page_height="${MAKO_OVERLAY_PAGE_HEIGHT:-1000}"
footer="${MAKO_OVERLAY_FOOTER:-}"
font_size="${MAKO_OVERLAY_FONT_SIZE:-}"

mkdir -p "$test_dir" "$work_dir" "$encoded_dir"
if [[ ! -x $canonical_tile_harness_bin ]]; then
    canonical_tile_harness_bin="$work_dir/canonical_tile_harness"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -pedantic -O2 \
        "$script_dir/test_canonical_tile_harness.cpp" -o "$canonical_tile_harness_bin"
fi

"$ppm_transform_bin" bytes --output "$payload_path" --size "$payload_size" --seed "$payload_seed"
"$ppm_transform_bin" overlay-mask --output "$overlay_path" \
    --circle-color "${MAKO_OVERLAY_CIRCLE_COLOR:-0 0 0}" \
    --background-color "${MAKO_OVERLAY_BACKGROUND_COLOR:-255 255 255}" \
    --width "$page_width" --height "$page_height"

encode_cmd=(
    "$makocode_bin" encode
    "--input=payload.bin"
    "--overlay-fraction=$fraction"
    "--overlay-ppm=$overlay_path"
    "--page-width=$page_width"
    "--page-height=$page_height"
    "--output-dir=$encoded_dir"
    "--prefix=${label}_overlay"
    --debug
)
if [[ -n "$ecc" ]]; then
    encode_cmd+=("--ecc=$ecc")
fi
if [[ -n "$footer" ]]; then
    encode_cmd+=(--footer "$footer")
fi
if [[ -n "$font_size" ]]; then
    encode_cmd+=(--font-size "$font_size")
fi
if [[ -n "${MAKO_OVERLAY_PALETTE:-}" ]]; then
    encode_cmd+=(--palette "${MAKO_OVERLAY_PALETTE}")
fi
if [[ -n "$ignore_colors" ]]; then
    encode_cmd+=(--overlay-ignore-colors "$ignore_colors")
fi
if [[ -n "${MAKO_OVERLAY_ENCODE_OPTS:-}" ]]; then
    read -r -a encode_extra <<< "$MAKO_OVERLAY_ENCODE_OPTS"
    if [[ ${#encode_extra[@]} -gt 0 ]]; then
        encode_cmd+=("${encode_extra[@]}")
    fi
fi
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >"$work_dir/encode.log" 2>&1
if ! rg -q 'debug: overlay bits used=[1-9][0-9]*/' "$work_dir/encode.log"; then
    echo "test_overlay_e2e: encoder did not apply any protected overlay bits" >&2
    cat "$work_dir/encode.log" >&2
    exit 1
fi
encode_csv="$encoded_dir/${label}_overlay_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo "test_overlay_e2e: encode did not write fiducial CSV" >&2
    exit 1
fi

shopt -s nullglob
ppms=("$encoded_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_overlay_e2e: encode did not produce a ppm page" >&2
    exit 1
fi
if [[ -n "${MAKO_OVERLAY_EXPECT_PAGES:-}" &&
      ${#ppms[@]} -ne $MAKO_OVERLAY_EXPECT_PAGES ]]; then
    echo "test_overlay_e2e: expected $MAKO_OVERLAY_EXPECT_PAGES page(s), found ${#ppms[@]}" >&2
    exit 1
fi

# Filenames are not the authority for document order. Read each protected tile,
# validate its page metadata, and order pages by the serialized one-based index.
page_indices=()
page_summaries=()
page_paths=()
for page in "${ppms[@]}"; do
    summary=$("$canonical_tile_harness_bin" "$page" "$page_width" "$page_height" 1 --report-page)
    IFS=$'\t' read -r page_index page_count block_count parity unknown_budget overlay_budget selector_present selector_total selector_seed selector_counts <<< "$summary"
    if [[ -z ${page_index:-} || $page_count -ne ${#ppms[@]} ]]; then
        echo "test_overlay_e2e: tile page count does not match emitted PPM pages" >&2
        exit 1
    fi
    page_indices+=("$page_index")
    page_summaries+=("$summary")
    page_paths+=("$page")
done
ordered_ppms=()
ordered_summaries=()
for ((expected_index = 1; expected_index <= ${#ppms[@]}; ++expected_index)); do
    match=-1
    for candidate in "${!page_indices[@]}"; do
        if [[ ${page_indices[$candidate]} -eq $expected_index ]]; then
            match=$candidate
            break
        fi
    done
    if [[ $match -lt 0 ]]; then
        echo "test_overlay_e2e: missing tile page index $expected_index" >&2
        exit 1
    fi
    ordered_ppms+=("${page_paths[$match]}")
    ordered_summaries+=("${page_summaries[$match]}")
    IFS=$'\t' read -r page_index page_count block_count parity unknown_budget overlay_budget selector_present selector_total selector_seed selector_counts <<< "${page_summaries[$match]}"
    if [[ $selector_present -ne 1 || $block_count -eq 0 || $parity -eq 0 ||
          $overlay_budget -eq 0 || $selector_total -ne $((block_count * overlay_budget)) ]]; then
        echo "test_overlay_e2e: page lacks its centered selector budget or ECC metadata" >&2
        exit 1
    fi
done
ppms=("${ordered_ppms[@]}")
page_summaries=("${ordered_summaries[@]}")

merged_pages=("${ppms[@]}")

decoded_dir="$work_dir/decoded"
mkdir -p "$decoded_dir"
"$makocode_bin" decode "${merged_pages[@]}" --output-dir "$decoded_dir" --debug
decode_csv="$decoded_dir/decode_fiducials.csv"
if [[ ! -f $decode_csv ]]; then
    echo "test_overlay_e2e: decode did not write fiducial CSV" >&2
    exit 1
fi
mako_check_fiducials \
    "$encode_csv" \
    "$decode_csv" \
    "$label overlay"
decoded_source="$decoded_dir/payload.bin"
if [[ ! -f $decoded_source ]]; then
    echo "test_overlay_e2e: decode missing payload.bin" >&2
    exit 1
fi
cmp --silent "$payload_path" "$decoded_source"

echo "overlay e2e ok (label=$label, artifacts prefix ${label}_overlay_*)"
