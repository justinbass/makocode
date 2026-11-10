#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"
. "$script_dir/lib/test_seed.sh"

usage() {
    cat <<'USAGE'
Usage: test_page_fit.sh [--label NAME]

  --label NAME    Prefix for artifacts under test_output/ (default: page_fit).
  --help          Show this message.
USAGE
}

label="page_fit"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_page_fit: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo "test_page_fit: invalid --label" >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_page_fit: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -eq 0 ]]; then
        rm -rf \
            "$test_dir/${label}_fit_both_pages" \
            "$test_dir/${label}_fit_height_pages" \
            "$test_dir/${label}_fit_width_pages" \
            "$test_dir/${label}_margin_pages"
    fi
}
on_exit() {
    cleanup "$?"
}
trap on_exit EXIT

mako_test_seed_init "$repo_root"
payload_seed=$(mako_test_case_seed "page_fit/$label")

for mode in both height width; do
    local_page_dir="$test_dir/${label}_fit_${mode}_pages"
    rm -rf "$local_page_dir"
    mkdir -p "$local_page_dir"
    encode_cmd=("$makocode_bin" encode --ecc=0 \
        "--output-dir=$local_page_dir" "--prefix=${label}_${mode}")
    if [[ $mode == height ]]; then
        encode_cmd+=(--page-width=500)
    elif [[ $mode == width ]]; then
        encode_cmd+=(--page-height=500)
    fi
    "$repo_root/test/ppm_transform" bytes --output - --size 8192 --seed "$payload_seed" |
        "${encode_cmd[@]}" >/dev/null
    shopt -s nullglob
    ppms=("$local_page_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppms[@]} -ne 1 ]]; then
        echo "test_page_fit: $mode fit expected one page, found ${#ppms[@]}" >&2
        exit 1
    fi
    read -r output_width output_height < <(sed -n '2p' "${ppms[0]}")
    case "$mode" in
        both)
            if ((output_width >= 2480 || output_height >= 3508)); then
                echo "test_page_fit: automatic dimensions did not fit the payload: ${output_width}x${output_height}" >&2
                exit 1
            fi
            retained_ppm="$test_dir/${label}_fit.ppm"
            cp "${ppms[0]}" "$retained_ppm"
            ;;
        height)
            if ((output_width != 500 || output_height >= 3508)); then
                echo "test_page_fit: height-only fit produced ${output_width}x${output_height}" >&2
                exit 1
            fi
            ;;
        width)
            if ((output_height != 500 || output_width >= 2480)); then
                echo "test_page_fit: width-only fit produced ${output_width}x${output_height}" >&2
                exit 1
            fi
            ;;
    esac
    cat "${ppms[0]}" |
        "$makocode_bin" decode --decoded-stdout 2>/dev/null |
        python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
            --size 8192 --seed "$payload_seed" --label "$label $mode fit"
done

margin_pages="$test_dir/${label}_margin_pages"
mkdir -p "$margin_pages"
printf 'makocode' |
    "$makocode_bin" encode --ecc=0 \
        --page-width=160 --page-height=160 \
        "--output-dir=$margin_pages" "--prefix=${label}_margin" >/dev/null
shopt -s nullglob
margin_ppms=("$margin_pages"/*.ppm)
shopt -u nullglob
if [[ ${#margin_ppms[@]} -ne 1 ]]; then
    echo "test_page_fit: fixed small page did not produce one margin test page" >&2
    exit 1
fi
white_fraction=$(python3 - "${margin_ppms[0]}" <<'PY'
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
header, pixels = data.split(b"\n255\n", 1)
width, height = map(int, header.splitlines()[-1].split())
white = sum(pixels[i] == 255 and pixels[i + 1] == 255 and pixels[i + 2] == 255
            for i in range(0, len(pixels), 3))
print(white / (width * height))
PY
)
if ! awk -v white_fraction="$white_fraction" 'BEGIN { exit !(white_fraction > 0.60) }'; then
    echo "test_page_fit: small fixed page lacks a large white margin (white fraction $white_fraction)" >&2
    exit 1
fi
cat "${margin_ppms[0]}" |
    "$makocode_bin" decode --decoded-stdout 2>/dev/null |
    python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
        --literal makocode --label "$label small-page margin"
retained_ppm="$test_dir/${label}_small_page_white_margin.ppm"
cp "${margin_ppms[0]}" "$retained_ppm"
rm -rf "$margin_pages"

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS automatic fitting and small-page white margin\n' "$label_fmt"
