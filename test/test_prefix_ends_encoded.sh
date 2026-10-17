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

label="0057_prefix_ends_encoded"
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
)
printf '%s encode: %q ' "$(mako_format_label "$label")" "${encode_cmd[0]}"
printf '%q ' "${encode_cmd[@]:1}"
printf '\n'
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppms=("$pages_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -ne 1 || ${ppms[0]} != "$encoded_ppm" ]]; then
    printf 'test_prefix_ends_encoded: expected exactly %s; found %d PPM file(s)\n' \
        "$encoded_ppm" "${#ppms[@]}" >&2
    printf '  %s\n' "${ppms[@]}" >&2
    exit 1
fi

# The 4x4 default marker at the upper-left fiducial location is white. Cyan and
# black are the only payload colors, so this checks that the PPM contains the
# fiducial grid rather than merely relying on successful decoding.
if ! awk '
    NR == 2 { width = $1; next }
    NR >= 4 {
        pixel = NR - 4
        x = pixel % width
        y = int(pixel / width)
        if (x >= 10 && x < 14 && y >= 10 && y < 14) {
            count++
            if ($1 == 255 && $2 == 255 && $3 == 255) white++
        }
    }
    END { exit !(count == 16 && white == 16) }
' "$encoded_ppm"; then
    echo 'test_prefix_ends_encoded: expected the upper-left 4x4 fiducial marker to be white' >&2
    exit 1
fi

"$makocode_bin" decode "$encoded_ppm" "--output-dir=$decoded_dir" >/dev/null
if [[ ! -f $decoded_payload ]] || ! cmp --silent "$payload" "$decoded_payload"; then
    echo 'test_prefix_ends_encoded: decoded payload differs from input' >&2
    exit 1
fi

printf '%s SUCCESS prefix filename, fiducials, and payload round-trip\n' "$(mako_format_label "$label")"
