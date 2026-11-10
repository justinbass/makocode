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
. "$script_dir/lib/colors.sh"
. "$script_dir/lib/fiducials.sh"
. "$script_dir/lib/visual_artifacts.sh"

label="0059_prefix_ends_encoded"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --help)
            printf 'Usage: %s [--label NAME]\n' "$(basename "$0")"
            exit 0
            ;;
        *)
            printf 'test_prefix_ends_encoded: unknown flag %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_prefix_ends_encoded: invalid --label' >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    printf 'test_prefix_ends_encoded: makocode binary not found at %s\n' "$makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_prefix_work"
pages_dir="$work_dir/pages"
decoded_dir="$work_dir/decoded"
payload="$work_dir/payload.bin"
prefix="${label}_payload_encoded"
encoded_ppm="$pages_dir/$prefix.ppm"
decoded_payload="$decoded_dir/payload.bin"

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
trap on_exit EXIT

rm -rf "$work_dir"
mkdir -p "$pages_dir" "$decoded_dir"
printf '\000prefix-encoded roundtrip payload\377\n' > "$payload"

encode_cmd=(
    "$makocode_bin" encode
    --input=payload.bin
    --ecc=0
    --page-width=160
    --page-height=160
    --palette='Cyan Black'
    --no-filename
    --no-page-count
    "--prefix=$prefix"
    "--output-dir=$pages_dir"
    --debug
)
printf '%s encode: %q ' "$(mako_format_label "$label")" "${encode_cmd[0]}"
printf '%q ' "${encode_cmd[@]:1}"
printf '\n'
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null
encode_csv="$pages_dir/${prefix}_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo 'test_prefix_ends_encoded: encode did not write fiducial CSV' >&2
    exit 1
fi
cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

shopt -s nullglob
ppms=("$pages_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -ne 1 || ${ppms[0]} != "$encoded_ppm" ]]; then
    printf 'test_prefix_ends_encoded: expected exactly %s; found %d PPM file(s)\n' \
        "$encoded_ppm" "${#ppms[@]}" >&2
    printf '  %s\n' "${ppms[@]}" >&2
    exit 1
fi

# The 4x4 default marker at the upper-left fiducial location follows the
# brightest palette color, cyan. This checks the marker color directly.
if ! python3 - "$script_dir" "$encoded_ppm" <<'PYTHON'
import sys
sys.path.insert(0, sys.argv[1])
from ppm_test_utils import read_ppm
width, height, pixels = read_ppm(sys.argv[2])
background = 0
for y in range(10, 14):
    for x in range(10, 14):
        index = (y * width + x) * 3
        background += pixels[index:index + 3] == [0, 255, 255]
raise SystemExit(0 if width >= 14 and height >= 14 and background == 16 else 1)
PYTHON
then
    echo 'test_prefix_ends_encoded: expected the upper-left 4x4 fiducial marker to match the cyan background' >&2
    exit 1
fi

"$makocode_bin" decode "$encoded_ppm" "--output-dir=$decoded_dir" --debug >/dev/null
decode_csv="$decoded_dir/decode_fiducials.csv"
if [[ ! -f $decode_csv ]]; then
    echo 'test_prefix_ends_encoded: decode did not write fiducial CSV' >&2
    exit 1
fi
cp "$decode_csv" "$test_dir/${label}_decode_fiducials.csv"
mako_check_fiducials \
    "$test_dir/${label}_encode_fiducials.csv" \
    "$test_dir/${label}_decode_fiducials.csv" \
    "$label"
if [[ ! -f $decoded_payload ]] || ! cmp --silent "$payload" "$decoded_payload"; then
    echo 'test_prefix_ends_encoded: decoded payload differs from input' >&2
    exit 1
fi
visual_ppm="$test_dir/${label}_prefix_encoded.ppm"
cp "$encoded_ppm" "$visual_ppm"
mako_register_visual_artifacts "$repo_root" "$visual_ppm"

printf '%s SUCCESS prefix filename, fiducials, and payload round-trip\n' "$(mako_format_label "$label")"
