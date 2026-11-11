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
merged_dir="$work_dir/merged_pages"

ecc="${MAKO_OVERLAY_ECC:-1.0}"
ignore_colors="${MAKO_OVERLAY_IGNORE_COLORS:-}"
ecc_target="${MAKO_OVERLAY_ECC_TARGET:-}"
page_width="${MAKO_OVERLAY_PAGE_WIDTH:-1000}"
page_height="${MAKO_OVERLAY_PAGE_HEIGHT:-1000}"

mkdir -p "$test_dir" "$work_dir" "$encoded_dir" "$merged_dir"
if [[ ! -x $canonical_tile_harness_bin ]]; then
    canonical_tile_harness_bin="$work_dir/canonical_tile_harness"
    "${CXX:-c++}" -std=c++17 -Wall -Wextra -pedantic -O2 \
        "$script_dir/test_canonical_tile_harness.cpp" -o "$canonical_tile_harness_bin"
fi

"$ppm_transform_bin" bytes --output "$payload_path" --size 32768 --seed "$payload_seed"

encode_cmd=(
    "$makocode_bin" encode
    "--input=payload.bin"
    "--ecc=$ecc"
    "--page-width=$page_width"
    "--page-height=$page_height"
    "--output-dir=$encoded_dir"
    "--prefix=${label}_overlay"
    --debug
)
if [[ -n "${MAKO_OVERLAY_PALETTE:-}" ]]; then
    encode_cmd+=(--palette "${MAKO_OVERLAY_PALETTE}")
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
) >/dev/null
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

circle_color="${MAKO_OVERLAY_CIRCLE_COLOR:-0 0 0}"
background_color="${MAKO_OVERLAY_BACKGROUND_COLOR:-255 255 255}"
"$ppm_transform_bin" overlay-mask \
    --output "$overlay_path" \
    --circle-color "$circle_color" \
    --background-color "$background_color" \
    --width "$page_width" \
    --height "$page_height"

fraction="${MAKO_OVERLAY_FRACTION:-0.1}"
overlay_cmd=("$makocode_bin" overlay)
overlay_cmd+=(--debug)
if [[ -n "$ignore_colors" ]]; then
    overlay_cmd+=(--ignore-colors "$ignore_colors")
fi
if [[ -n "$ecc_target" ]]; then
    overlay_cmd+=(--overlay-ecc-target "$ecc_target")
fi
merged_pages=()
for index in "${!ppms[@]}"; do
    merged_page="$merged_dir/$(basename "${ppms[$index]}")"
    page_overlay_cmd=("${overlay_cmd[@]}" "${ppms[$index]}" "$overlay_path" "$fraction")
    selector_log="$work_dir/overlay_selector_${index}.log"
    "${page_overlay_cmd[@]}" > "$merged_page" 2>"$selector_log"
    selector_report=$(awk '/overlay selector symbols used:/ {print $5}' "$selector_log")
    IFS=/ read -r selector_used selector_capacity <<< "$selector_report"
    merged_summary=$("$canonical_tile_harness_bin" "$merged_page" "$page_width" "$page_height" 1 --report-page)
    IFS=$'\t' read -r merged_index merged_count merged_blocks merged_parity merged_unknown merged_overlay merged_selector_present merged_total merged_seed merged_counts <<< "$merged_summary"
    IFS=$'\t' read -r base_index base_count base_blocks base_parity base_unknown base_overlay base_selector_present base_total base_seed base_counts <<< "${page_summaries[$index]}"
    if [[ $merged_index -ne $base_index || $merged_count -ne $base_count ||
          $merged_blocks -ne $base_blocks || $merged_parity -ne $base_parity ||
          $merged_unknown -ne $base_unknown || $merged_overlay -ne $base_overlay ||
          $base_selector_present -ne 1 || $merged_selector_present -ne 1 ||
          $merged_seed -ne $base_seed ||
          $merged_total -ne $((merged_blocks * merged_overlay)) ||
          $selector_capacity -ne $((merged_blocks * merged_overlay)) ]]; then
        echo "test_overlay_e2e: merged page selector or ECC metadata changed unexpectedly" >&2
        exit 1
    fi
    IFS=, read -r -a merged_count_values <<< "$merged_counts"
    merged_sum=0
    for selector_count in "${merged_count_values[@]}"; do
        if [[ $selector_count -ne $merged_overlay ]]; then
            echo "test_overlay_e2e: decoder did not reconstruct the full selector set" >&2
            exit 1
        fi
        merged_sum=$((merged_sum + selector_count))
    done
    if [[ $merged_sum -ne $merged_total || -z $selector_used || -z $selector_capacity ||
          $selector_used -gt $selector_capacity ]]; then
        echo "test_overlay_e2e: selector reserve report is inconsistent" >&2
        exit 1
    fi
    if [[ -n $ecc_target ]]; then
        available_budget=$((merged_blocks * merged_overlay))
        expected_budget=$(awk -v available="$available_budget" -v target="$ecc_target" \
            'BEGIN { printf "%d", available * target + 0.5 }')
        if [[ $selector_used -ne $expected_budget ]]; then
            echo "test_overlay_e2e: selector use $selector_used does not match requested ECC target $ecc_target (expected $expected_budget)" >&2
            exit 1
        fi
    elif [[ $selector_used -eq 0 ]]; then
        echo "test_overlay_e2e: overlay did not consume any protected selector budget" >&2
        exit 1
    fi
    merged_pages+=("$merged_page")
done

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

skip_grayscale="${MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK:-0}"
for index in "${!ppms[@]}"; do
    "$ppm_transform_bin" overlay-check --base "${ppms[$index]}" \
        --merged "${merged_pages[$index]}" --skip-grayscale "$skip_grayscale"
done

echo "overlay e2e ok (label=$label, artifacts prefix ${label}_overlay_*)"
