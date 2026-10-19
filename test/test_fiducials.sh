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
. "$script_dir/lib/colors.sh"
. "$script_dir/lib/visual_artifacts.sh"

usage() {
    cat <<'USAGE'
Usage: test_fiducials.sh [--label CASE_ID]

  --label CASE_ID  Prefix for artifacts under test_output/ (default: fiducials).
  --help           Show this message.
USAGE
}

label="fiducials"
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
            printf 'test_fiducials: unknown flag %s\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_fiducials: invalid --label' >&2
    exit 1
fi

makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    printf 'test_fiducials: makocode binary not found at %s\n' "$makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_fiducials_work"
pages_dir="$work_dir/pages"
decoded_dir="$work_dir/decoded"
payload="$work_dir/payload.bin"
encoded_ppm="$pages_dir/${label}_fiducials.ppm"
encode_csv="$pages_dir/${label}_fiducials_encode_fiducials.csv"
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
trap 'on_exit' EXIT

rm -rf "$work_dir"
mkdir -p "$pages_dir" "$decoded_dir"
printf '\000fiducials custom grid payload\377\n' > "$payload"

encode_cmd=(
    "$makocode_bin" encode
    --input=payload.bin
    --ecc=0
    --page-width=240
    --page-height=240
    --no-filename
    --no-page-count
    --fiducials=6,30,15
    "--prefix=${label}_fiducials"
    "--output-dir=$pages_dir"
    --debug
)
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

if [[ ! -f $encoded_ppm ]]; then
    printf 'test_fiducials: encode did not produce %s\n' "$encoded_ppm" >&2
    exit 1
fi
if [[ ! -f $encode_csv ]]; then
    echo 'test_fiducials: encode did not write fiducial CSV' >&2
    exit 1
fi

# The configured 6-pixel marker at margin 15 is centered at 14.5 pixels;
# 30-pixel spacing puts the next marker center at 44.5.
if ! awk -F, '
    NR == 2 { exit !($1 == 1 && $2 == 0 && $3 == 0 && $4 == "14.500" && $5 == "14.500") }
' "$encode_csv"; then
    echo 'test_fiducials: first marker does not match configured size and margin' >&2
    exit 1
fi
if ! awk -F, '
    NR == 3 { exit !($1 == 1 && $2 == 0 && $3 == 1 && $4 == "44.500" && $5 == "14.500") }
' "$encode_csv"; then
    echo 'test_fiducials: second marker does not match configured spacing' >&2
    exit 1
fi

"$makocode_bin" decode "$encoded_ppm" "--output-dir=$decoded_dir" >/dev/null
if [[ ! -f $decoded_payload ]] || ! cmp --silent "$payload" "$decoded_payload"; then
    echo 'test_fiducials: decoded payload differs from encoded payload' >&2
    exit 1
fi
visual_ppm="$test_dir/${label}_fiducials_page.ppm"
cp "$encoded_ppm" "$visual_ppm"
mako_register_visual_artifacts "$repo_root" "$visual_ppm"

printf '%s SUCCESS custom fiducial grid and payload round-trip\n' "$(mako_format_label "$label")"
