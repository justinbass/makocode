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
. "$script_dir/lib/fiducials.sh"
. "$script_dir/lib/visual_artifacts.sh"

usage() {
    cat <<'USAGE'
Usage: test_decode_stdin.sh [--label NAME]

  --label NAME    Artifact prefix under test_output/ (default: decode_stdin).
  --help          Show this help message.
USAGE
}

label="decode_stdin"
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
            echo "test_decode_stdin: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_decode_stdin: --label requires a value" >&2
    exit 1
fi

format_command() {
    local formatted="" quoted=""
    for arg in "$@"; do
        printf -v quoted '%q' "$arg"
        if [[ -z $formatted ]]; then
            formatted=$quoted
        else
            formatted+=" $quoted"
        fi
    done
    printf '%s' "$formatted"
}

print_labelled() {
    local phase=$1
    shift
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    printf '%s %s: %s\n' "$label_fmt" "$phase" "$(format_command "$@")"
}

makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_stdin: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_stdin_work"
payload="$work_dir/payload.bin"
decoded_dir="$work_dir/decoded"

cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -eq 0 ]]; then
        rm -rf "$work_dir"
    fi
}
on_exit() {
    local exit_code=$?
    cleanup "$exit_code"
    if [[ $exit_code -ne 0 ]]; then
        local label_fmt
        label_fmt=$(mako_format_label "$label")
        printf '%s %bFAIL (exit %d)%b\n\n' "$label_fmt" "$MAKO_FAIL_COLOR" "$exit_code" "$MAKO_RESET_COLOR" >&2
    fi
}
trap on_exit EXIT

rm -rf "$work_dir"
mkdir -p "$work_dir"
printf '\000stdin coverage payload\377\n' > "$payload"

encode_prefix="${label}_stdin"
encode_cmd=("$makocode_bin" encode --input=payload.bin --ecc=0.5 --page-width=500 --page-height=500 "--output-dir=$work_dir" --no-filename --no-page-count "--prefix=$encode_prefix" --debug)
print_labelled "encode" "${encode_cmd[@]}"
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null
encode_csv="$work_dir/${encode_prefix}_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo "test_decode_stdin: encode did not write fiducial CSV" >&2
    exit 1
fi
cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

shopt -s nullglob
ppm_paths=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppm_paths[@]} -ne 1 ]]; then
    echo "test_decode_stdin: expected exactly one encoded PPM, found ${#ppm_paths[@]}" >&2
    exit 1
fi

mkdir -p "$decoded_dir"
decode_cmd=("$makocode_bin" decode "--output-dir=$decoded_dir" --debug)
print_labelled "decode-stdin" cat "${ppm_paths[0]}" '|' "${decode_cmd[@]}"
cat "${ppm_paths[0]}" | "${decode_cmd[@]}" >/dev/null
decode_csv="$decoded_dir/decode_fiducials.csv"
if [[ ! -f $decode_csv ]]; then
    echo "test_decode_stdin: stdin decode did not write fiducial CSV" >&2
    exit 1
fi
cp "$decode_csv" "$test_dir/${label}_decode_fiducials.csv"
mako_check_fiducials \
    "$test_dir/${label}_encode_fiducials.csv" \
    "$test_dir/${label}_decode_fiducials.csv" \
    "$label stdin"

decoded_payload="$decoded_dir/payload.bin"
if [[ ! -f $decoded_payload ]]; then
    echo "test_decode_stdin: stdin decode did not emit payload.bin" >&2
    exit 1
fi
if ! cmp --silent "$payload" "$decoded_payload"; then
    echo "test_decode_stdin: stdin decode payload differs from encoded payload" >&2
    exit 1
fi
visual_ppm="$test_dir/${label}_stdin_encoded.ppm"
cp "${ppm_paths[0]}" "$visual_ppm"
mako_register_visual_artifacts "$repo_root" "$visual_ppm"

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS stdin payload matched byte-for-byte\n' "$label_fmt"
