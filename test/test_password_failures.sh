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
Usage: test_password_failures.sh [--label NAME]

  --label NAME    Artifact prefix under test_output/ (default: password_failures).
  --help          Show this help message.
USAGE
}

label="password_failures"
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
            echo "test_password_failures: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_password_failures: --label requires a value" >&2
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

run_expect_failure() {
    local phase=$1
    shift
    print_labelled "$phase" "$@"
    set +e
    "$@" >/dev/null 2>&1
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        echo "test_password_failures: command unexpectedly succeeded for $phase" >&2
        exit 1
    fi
}

repo_root=$(cd -- "$script_dir/.." && pwd -P)
mako_test_seed_init "$repo_root"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"
if [[ ! -x $makocode_bin ]]; then
    echo "test_password_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_password_failures: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

payload_seed=$(mako_test_case_seed "payload/$label")
MAKOCODE_TEST_SEED=$(mako_test_case_seed "crypto/$label")
export MAKOCODE_TEST_SEED

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_password_work"
payload="$test_dir/${label}_payload.bin"
ppm_target="$test_dir/${label}_encoded.ppm"

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
mkdir -p "$work_dir"
rm -f "$payload" "$ppm_target"

"$ppm_transform_bin" bytes --output "$payload" --size 32768 --seed "$payload_seed"
password="suite-password"

encode_cmd=(
    "$makocode_bin" encode
    "--input=$payload"
    --ecc=0.5
    --page-width=720
    --page-height=720
    "--password=$password"
    "--output-dir=$work_dir"
    "--prefix=${label}_page"
    --debug
)
print_labelled "encode" "${encode_cmd[@]}"
"${encode_cmd[@]}" >/dev/null
encode_csv="$work_dir/${label}_page_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo "test_password_failures: encode did not write fiducial CSV" >&2
    exit 1
fi
cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

shopt -s nullglob
ppms=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_password_failures: encode emitted no PPMs" >&2
    exit 1
fi
ppm_path="${ppms[0]}"
mv -f "$ppm_path" "$ppm_target"

decoded_no_pw="$work_dir/decoded_no_pw"
decoded_wrong_pw="$work_dir/decoded_wrong_pw"
mkdir -p "$decoded_no_pw" "$decoded_wrong_pw"

run_expect_failure "decode-missing-password" \
    "$makocode_bin" decode "$ppm_target" --output-dir "$decoded_no_pw" --debug
missing_password_csv="$decoded_no_pw/decode_fiducials.csv"
if [[ ! -f $missing_password_csv ]]; then
    echo "test_password_failures: missing-password decode did not write fiducial CSV" >&2
    exit 1
fi
cp "$missing_password_csv" "$test_dir/${label}_missing_password_decode_fiducials.csv"
mako_check_fiducials \
    "$test_dir/${label}_encode_fiducials.csv" \
    "$test_dir/${label}_missing_password_decode_fiducials.csv" \
    "$label missing password"

run_expect_failure "decode-wrong-password" \
    "$makocode_bin" decode "$ppm_target" --output-dir "$decoded_wrong_pw" --password=bad-password --debug
wrong_password_csv="$decoded_wrong_pw/decode_fiducials.csv"
if [[ ! -f $wrong_password_csv ]]; then
    echo "test_password_failures: wrong-password decode did not write fiducial CSV" >&2
    exit 1
fi
cp "$wrong_password_csv" "$test_dir/${label}_wrong_password_decode_fiducials.csv"
mako_check_fiducials \
    "$test_dir/${label}_encode_fiducials.csv" \
    "$test_dir/${label}_wrong_password_decode_fiducials.csv" \
    "$label wrong password"

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS password failures enforced\n' "$label_fmt"
