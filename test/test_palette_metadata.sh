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
. "$script_dir/lib/test_seed.sh"
. "$script_dir/lib/fiducials.sh"

usage() {
    cat <<'USAGE'
Usage: test_palette_metadata.sh [--label NAME] [--mode MODE]

  --label NAME    Artifact prefix under test_output/ (default: palette_metadata).
  --mode MODE     One of:
                    auto   (decode without supplying --palette, expect success)
                    wrong  (decode with an incorrect palette, expect failure)
  --help          Show this help message.
USAGE
}

label="palette_metadata"
mode="auto"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --mode)
            mode=${2:-}
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_palette_metadata: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_palette_metadata: --label requires a value" >&2
    exit 1
fi
if [[ $mode != "auto" && $mode != "wrong" ]]; then
    echo "test_palette_metadata: --mode must be 'auto' or 'wrong'" >&2
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

repo_root=$(cd -- "$script_dir/.." && pwd -P)
mako_test_seed_init "$repo_root"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"
if [[ ! -x $makocode_bin ]]; then
    echo "test_palette_metadata: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_palette_metadata: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

payload_seed=$(mako_test_case_seed "payload/$label/$mode")
MAKOCODE_TEST_SEED=$(mako_test_case_seed "crypto/$label/$mode")
export MAKOCODE_TEST_SEED

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_palette_work"
payload="$test_dir/${label}_payload.bin"
payload_work="$work_dir/payload.bin"
ppm_target="$test_dir/${label}_encoded.ppm"
decoded_target="$test_dir/${label}_decoded.bin"

cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -ne 0 ]]; then
        return
    fi
    if [[ -d $work_dir ]]; then
        rm -rf "$work_dir"
    fi
}
on_exit() {
    local exit_code=$?
    cleanup "$exit_code"
}
trap 'on_exit' EXIT

rm -rf "$work_dir"
mkdir -p "$test_dir" "$work_dir"
rm -f "$payload" "$ppm_target" "$decoded_target"

"$ppm_transform_bin" bytes --output "$payload" --size 16384 --seed "$payload_seed"
cp "$payload" "$payload_work"
palette="White Cyan Magenta Yellow"

encode_cmd=(
    "$makocode_bin" encode
    "--input=payload.bin"
    --ecc=0.25
    --page-width=640
    --page-height=640
    --palette "$palette"
    "--output-dir=$work_dir"
    "--prefix=${label}_page"
    --debug
)
print_labelled "encode" "${encode_cmd[@]}"
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null
encode_csv="$work_dir/${label}_page_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo "test_palette_metadata: encode did not write fiducial CSV" >&2
    exit 1
fi
cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

shopt -s nullglob
ppms=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_palette_metadata: encode emitted no PPMs" >&2
    exit 1
fi
ppm_path="${ppms[0]}"
mv -f "$ppm_path" "$ppm_target"

decoded_dir="$work_dir/decoded"
mkdir -p "$decoded_dir"
decoded_payload="$decoded_dir/payload.bin"

case "$mode" in
    auto)
        decode_cmd=("$makocode_bin" decode "$ppm_target" --output-dir "$decoded_dir" --debug)
        print_labelled "decode-auto" "${decode_cmd[@]}"
        "${decode_cmd[@]}" >/dev/null
        decode_csv="$decoded_dir/decode_fiducials.csv"
        if [[ ! -f $decode_csv ]]; then
            echo "test_palette_metadata: auto decode did not write fiducial CSV" >&2
            exit 1
        fi
        cp "$decode_csv" "$test_dir/${label}_decode_fiducials.csv"
        mako_check_fiducials \
            "$test_dir/${label}_encode_fiducials.csv" \
            "$test_dir/${label}_decode_fiducials.csv" \
            "$label auto"
        if [[ ! -f $decoded_payload ]]; then
            echo "test_palette_metadata: decode missing payload.bin in auto mode" >&2
            exit 1
        fi
        cmp --silent "$payload" "$decoded_payload"
        ;;
    wrong)
        decode_cmd=(
            "$makocode_bin" decode
            "$ppm_target"
            --output-dir "$decoded_dir"
            --palette "White Black"
            --debug
        )
        print_labelled "decode-wrong" "${decode_cmd[@]}"
        set +e
        "${decode_cmd[@]}" >/dev/null 2>&1
        status=$?
        set -e
        if [[ $status -eq 0 ]]; then
            echo "test_palette_metadata: decode unexpectedly succeeded with wrong palette" >&2
            exit 1
        fi
        wrong_decode_csv="$decoded_dir/decode_fiducials.csv"
        if [[ ! -f $wrong_decode_csv ]]; then
            echo "test_palette_metadata: wrong-palette decode did not write fiducial CSV" >&2
            exit 1
        fi
        cp "$wrong_decode_csv" "$test_dir/${label}_wrong_decode_fiducials.csv"
        ;;
esac

if [[ -f $decoded_payload ]]; then
    mv "$decoded_payload" "$decoded_target"
fi

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS mode=%s\n' "$label_fmt" "$mode"
