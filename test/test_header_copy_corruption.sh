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
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"

usage() {
    cat <<'USAGE'
Usage: test_header_copy_corruption.sh [--label NAME]

  --label NAME    Identifier for log messages and generated artifacts.
  --help          Show this help message.
USAGE
}

label="header_copy_corruption"
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
            echo "test_header_copy_corruption: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_header_copy_corruption: --label requires a value" >&2
    exit 1
fi
mako_test_seed_init "$repo_root"
payload_seed=$(mako_test_case_seed "payload/$label")
MAKOCODE_TEST_SEED=$(mako_test_case_seed "crypto/$label")
export MAKOCODE_TEST_SEED

if [[ ! -x $makocode_bin ]]; then
    echo "test_header_copy_corruption: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_header_copy_corruption: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir=$(mktemp -d "$test_dir/${label}_work.XXXXXX")
payload_final="$work_dir/random.bin"
encoded_ppm="$work_dir/${label}_encoded.ppm"

mkdir -p "$work_dir"

"$ppm_transform_bin" bytes --output "$payload_final" --size 4096 --seed "$payload_seed"

encode_prefix="${label}_page"
encode_cmd=("$makocode_bin" encode "--input=random.bin" "--ecc=0.03" "--page-width=600" "--page-height=600" "--output-dir=$work_dir" "--prefix=$encode_prefix" --debug)
(cd "$work_dir" && "${encode_cmd[@]}") >/dev/null
encode_csv="$work_dir/${encode_prefix}_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo "test_header_copy_corruption: encode did not write fiducial CSV" >&2
    exit 1
fi

shopt -s nullglob
ppm_paths=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppm_paths[@]} -eq 0 ]]; then
    echo "test_header_copy_corruption: encode produced no PPM pages" >&2
    exit 1
fi
if [[ ${#ppm_paths[@]} -gt 1 ]]; then
    printf -v selected "%s" "${ppm_paths[0]}"
else
    selected="${ppm_paths[0]}"
fi
mv "$selected" "$encoded_ppm"

run_decode_expect() {
    local scenario=$1
    local expect_success=$2
    local corrupt_count=$3
    local output_dir="$work_dir/${scenario}_decoded"
    local damaged_ppm="$work_dir/${label}_${scenario}_corrupted.ppm"
    local decode_log="$work_dir/${scenario}_decode.log"
    mkdir -p "$output_dir"

    "$ppm_transform_bin" transform \
        --input "$encoded_ppm" \
        --output "$damaged_ppm" \
        --corrupt-former-header-slots "$corrupt_count" >/dev/null

    local decode_cmd=("$makocode_bin" decode "--output-dir=$output_dir" --debug)
    decode_cmd+=("$damaged_ppm")

    set +e
    "${decode_cmd[@]}" >/dev/null 2>"$decode_log"
    local status=$?
    set -e
    if [[ $expect_success -eq 1 && $status -ne 0 ]]; then
        echo "test_header_copy_corruption: decode ${scenario} failed unexpectedly" >&2
        exit 1
    fi
    if [[ $expect_success -eq 0 && $status -eq 0 ]]; then
        echo "test_header_copy_corruption: decode ${scenario} unexpectedly succeeded" >&2
        exit 1
    fi
    local fiducial_csv="$output_dir/decode_fiducials.csv"
    if [[ ! -f $fiducial_csv ]]; then
        echo "test_header_copy_corruption: decode ${scenario} did not write fiducial CSV" >&2
        exit 1
    fi
    mako_check_fiducials \
        "$encode_csv" \
        "$fiducial_csv" \
        "$label $scenario"
    if [[ $expect_success -eq 1 ]]; then
        if [[ ! -f "$output_dir/random.bin" ]]; then
            echo "test_header_copy_corruption: decode ${scenario} missing random.bin" >&2
            exit 1
        fi
        cmp --silent "$payload_final" "$output_dir/random.bin"
    fi
}

run_decode_expect "one_group" 1 1
run_decode_expect "two_groups" 1 2
# These former in-band header slots now contain ordinary RS-protected payload.
# Damage one, two, and three six-symbol groups and require exact recovery.
run_decode_expect "three_groups" 1 3

label_fmt="$label"
printf '%s SUCCESS ECC payload recovery at former header slots\n' "$label_fmt"
