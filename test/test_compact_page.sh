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
. "$script_dir/lib/visual_artifacts.sh"

usage() {
    cat <<'USAGE'
Usage: test_compact_page.sh [--label NAME]

  --label NAME    Prefix for artifacts under test_output/ (default: compact_page).
  --help          Show this message.
USAGE
}

label="compact_page"
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
            echo "test_compact_page: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_compact_page: --label requires a value" >&2
    exit 1
fi
if [[ $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo "test_compact_page: label contains unsupported characters" >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_compact_page: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_compact_work"
source_dir="$work_dir/source"
page_dir="$work_dir/pages"
decoded_dir="$work_dir/decoded"
page_width=500
page_height=800

cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -eq 0 ]]; then
        rm -rf "$work_dir"
    fi
}
on_exit() {
    local exit_code=$?
    cleanup "$exit_code"
}
trap 'on_exit' EXIT

rm -rf "$work_dir"
mkdir -p "$source_dir" "$page_dir" "$decoded_dir"
mako_test_seed_init "$repo_root"
payload_seed=$(mako_test_case_seed "compact_page/$label")
"$repo_root/test/ppm_transform" bytes \
    --output "$source_dir/payload.bin" \
    --size 8192 \
    --seed "$payload_seed"

encode_cmd=(
    "$makocode_bin" encode
    --input=payload.bin
    --ecc=0
    "--page-width=$page_width"
    "--page-height=$page_height"
    --no-filename
    --no-page-count
    --compact-page
    "--output-dir=$page_dir"
    "--prefix=${label}_compact"
)
(
    cd "$source_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppms=("$page_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -ne 1 ]]; then
    echo "test_compact_page: expected one encoded PPM, found ${#ppms[@]}" >&2
    exit 1
fi

ppm_magic=$(sed -n '1p' "${ppms[0]}")
dimensions=$(sed -n '2p' "${ppms[0]}")
read -r output_width output_height <<< "$dimensions"
if [[ $ppm_magic != P3 || $output_width != "$page_width" || ! $output_height =~ ^[0-9]+$ ]] ||
   ((output_height <= 0 || output_height >= page_height)); then
    echo "test_compact_page: expected a compact ${page_width}x<${page_height} P3 page, got '$ppm_magic' '$dimensions'" >&2
    exit 1
fi

"$makocode_bin" decode "${ppms[0]}" "--output-dir=$decoded_dir" >/dev/null
decoded_payload="$decoded_dir/payload.bin"
if [[ ! -f $decoded_payload ]]; then
    echo "test_compact_page: decode did not restore payload.bin" >&2
    exit 1
fi
cmp --silent "$source_dir/payload.bin" "$decoded_payload"
visual_ppm="$test_dir/${label}_compact_page.ppm"
cp "${ppms[0]}" "$visual_ppm"
mako_register_visual_artifacts "$repo_root" "$visual_ppm"

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS compact-page encode/decode (%sx%s)\n' "$label_fmt" "$output_width" "$output_height"
