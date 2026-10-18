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
Usage: test_decode_failures.sh [--label NAME] [--case NAME]

  --label NAME    Identifier for log messages (default: decode_failures).
  --case NAME     Which case to run:
                   wrong_depth
                   invalid_magic
                   all_black
                   all_white
                   random_noise
                   footer_data_destroyed
                   footer_valid_data_too_corrupt
                   all (default)
  --help          Show this help message.
USAGE
}

label="decode_failures"
case_name="all"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --case)
            case_name=${2:-}
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_decode_failures: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_decode_failures: --label requires a value" >&2
    exit 1
fi

if [[ -z ${case_name:-} ]]; then
    echo "test_decode_failures: --case requires a value" >&2
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

print_makocode_cmd() {
    local phase=$1
    shift
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    printf '%s makocode %s: %s\n' "$label_fmt" "$phase" "$(format_command "$@")"
}

run_expect_failure() {
    local phase=$1
    shift
    print_makocode_cmd "$phase" "$@"
    set +e
    "$@" >/dev/null 2>&1
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        echo "test_decode_failures: decoder unexpectedly succeeded for $phase" >&2
        exit 1
    fi
}

save_decode_fiducials() {
    local source_csv=$1
    local suffix=$2
    local target_csv="$test_dir/${label}_${suffix}_decode_fiducials.csv"
    if [[ ! -f $source_csv ]]; then
        echo "test_decode_failures: decode did not write fiducial CSV ($source_csv)" >&2
        exit 1
    fi
    cp "$source_csv" "$target_csv"
    if [[ -f $test_dir/${label}_encode_fiducials.csv ]]; then
        mako_check_fiducials "$test_dir/${label}_encode_fiducials.csv" "$target_csv" "$label $suffix"
    fi
}

repo_root=$(cd -- "$script_dir/.." && pwd -P)
mako_test_seed_init "$repo_root"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
ppm_transform_bin="$repo_root/test/ppm_transform"
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_decode_failures: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
MAKOCODE_TEST_SEED=$(mako_test_case_seed "crypto/$label/$case_name")
export MAKOCODE_TEST_SEED
mkdir -p "$test_dir"
wrong_depth="$test_dir/${label}_wrong_depth.ppm"
invalid_magic="$test_dir/${label}_invalid_magic.ppm"
all_black="$test_dir/${label}_all_black.ppm"
all_white="$test_dir/${label}_all_white.ppm"
random_noise="$test_dir/${label}_random_noise.ppm"
footer_data_destroyed="$test_dir/${label}_footer_present_data_destroyed.ppm"
footer_valid_data_too_corrupt="$test_dir/${label}_footer_valid_data_too_corrupt.ppm"
work_dir="$test_dir/${label}_work"
payload_path="$work_dir/payload.bin"
rm -f "$wrong_depth" "$invalid_magic" "$all_black" "$all_white" "$random_noise" "$footer_data_destroyed" "$footer_valid_data_too_corrupt"
rm -rf "$work_dir"

run_case_wrong_depth() {
    cat > "$wrong_depth" <<'PPM'
P3
2 2
42
0 0 0  0 0 0
0 0 0  0 0 0
PPM
    local decode_dir="$work_dir/wrong_depth_decode"
    run_expect_failure "decode-wrong-depth" "$makocode_bin" decode "$wrong_depth" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" wrong_depth
}

run_case_invalid_magic() {
    cat > "$invalid_magic" <<'PPM'
P6
2 2
255
PPM
    local decode_dir="$work_dir/invalid_magic_decode"
    run_expect_failure "decode-invalid-magic" "$makocode_bin" decode "$invalid_magic" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" invalid_magic
}

write_solid_ppm() {
    local path=$1
    local width=$2
    local height=$3
    local r=$4
    local g=$5
    local b=$6
    "$ppm_transform_bin" solid --output "$path" --width "$width" --height "$height" --r "$r" --g "$g" --b "$b"
}

# 1) Totally black, no barcode present.
run_case_all_black() {
    write_solid_ppm "$all_black" 256 256 0 0 0
    local decode_dir="$work_dir/all_black_decode"
    run_expect_failure "decode-all-black" "$makocode_bin" decode "$all_black" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" all_black
}

# 2) Totally white, no barcode present.
run_case_all_white() {
    write_solid_ppm "$all_white" 256 256 255 255 255
    local decode_dir="$work_dir/all_white_decode"
    run_expect_failure "decode-all-white" "$makocode_bin" decode "$all_white" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" all_white
}

write_noise_ppm() {
    local path=$1
    local width=$2
    local height=$3
    local seed=$4
    "$ppm_transform_bin" noise --output "$path" --width "$width" --height "$height" --seed "$seed"
}

# 3) Random noise image (deterministic seed).
run_case_random_noise() {
    write_noise_ppm "$random_noise" 256 256 1337
    local decode_dir="$work_dir/random_noise_decode"
    run_expect_failure "decode-random-noise" "$makocode_bin" decode "$random_noise" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" random_noise
}

# 4) Footer stripe present, but data region destroyed with noise.
run_case_footer_data_destroyed() {
    mkdir -p "$work_dir"
    local payload_seed
    payload_seed=$(mako_test_case_seed "payload/$label/footer_data_destroyed")
    "$ppm_transform_bin" bytes --output "$payload_path" --size 64 --seed "$payload_seed"
    print_makocode_cmd "encode-footer-only" \
        "$makocode_bin" encode \
        "--input=payload.bin" \
        "--page-width=260" \
        "--page-height=260" \
        "--no-filename" \
        "--no-page-count" \
        "--prefix=${label}_footer_data_destroyed" \
        --debug \
        "--output-dir=$work_dir"
    (
        cd "$work_dir"
        "$makocode_bin" encode \
            --input=payload.bin \
            --page-width=260 \
            --page-height=260 \
            --no-filename \
            --no-page-count \
            --prefix="${label}_footer_data_destroyed" \
            --debug \
            --output-dir="$work_dir" \
            >/dev/null
    )
    encode_csv="$work_dir/${label}_footer_data_destroyed_encode_fiducials.csv"
    if [[ ! -f $encode_csv ]]; then
        echo "test_decode_failures: footer corruption encode did not write fiducial CSV" >&2
        exit 1
    fi
    cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

    shopt -s nullglob
    ppm_paths=("$work_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppm_paths[@]} -eq 0 ]]; then
        echo "test_decode_failures: encode produced no PPM pages for footer corruption case" >&2
        exit 1
    fi
    if [[ ${#ppm_paths[@]} -ne 1 ]]; then
        echo "test_decode_failures: footer corruption case expected 1 page, got ${#ppm_paths[@]}" >&2
        exit 1
    fi
    "$ppm_transform_bin" corrupt-footer-data-destroyed \
        --input "${ppm_paths[0]}" \
        --output "$footer_data_destroyed"

    local decode_dir="$work_dir/footer_data_destroyed_decode"
    run_expect_failure "decode-footer-present-data-destroyed" "$makocode_bin" decode "$footer_data_destroyed" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" footer_data_destroyed
    rm -rf "$work_dir"
}

run_case_footer_valid_data_too_corrupt() {
    mkdir -p "$work_dir"
    local payload_seed
    payload_seed=$(mako_test_case_seed "payload/$label/footer_valid_data_too_corrupt")
    "$ppm_transform_bin" bytes --output "$payload_path" --size 32768 --seed "$payload_seed"
    print_makocode_cmd "encode-ecc-overwhelmed" \
        "$makocode_bin" encode \
        "--input=payload.bin" \
        "--ecc=0.25" \
        "--page-width=600" \
        "--page-height=600" \
        "--no-filename" \
        "--no-page-count" \
        "--prefix=${label}_footer_valid_data_too_corrupt" \
        --debug \
        "--output-dir=$work_dir"
    (
        cd "$work_dir"
        "$makocode_bin" encode \
            --input=payload.bin \
            --ecc=0.25 \
            --page-width=600 \
            --page-height=600 \
            --no-filename \
            --no-page-count \
            --prefix="${label}_footer_valid_data_too_corrupt" \
            --debug \
            --output-dir="$work_dir" \
            >/dev/null
    )
    encode_csv="$work_dir/${label}_footer_valid_data_too_corrupt_encode_fiducials.csv"
    if [[ ! -f $encode_csv ]]; then
        echo "test_decode_failures: ECC corruption encode did not write fiducial CSV" >&2
        exit 1
    fi
    cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

    shopt -s nullglob
    ppm_paths=("$work_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppm_paths[@]} -eq 0 ]]; then
        echo "test_decode_failures: encode produced no PPM pages for ECC overwhelm case" >&2
        exit 1
    fi
    if [[ ${#ppm_paths[@]} -ne 1 ]]; then
        echo "test_decode_failures: ECC overwhelm case expected 1 page, got ${#ppm_paths[@]}" >&2
        exit 1
    fi
    "$ppm_transform_bin" corrupt-footer-valid-data-too-corrupt \
        --input "${ppm_paths[0]}" \
        --output "$footer_valid_data_too_corrupt"

    local decode_dir="$work_dir/footer_valid_data_too_corrupt_decode"
    run_expect_failure "decode-footer-valid-data-too-corrupt" "$makocode_bin" decode "$footer_valid_data_too_corrupt" --debug "--output-dir=$decode_dir"
    save_decode_fiducials "$decode_dir/decode_fiducials.csv" footer_valid_data_too_corrupt
    rm -rf "$work_dir"
}

case "${case_name}" in
    wrong_depth)
        run_case_wrong_depth
        ;;
    invalid_magic)
        run_case_invalid_magic
        ;;
    all_black)
        run_case_all_black
        ;;
    all_white)
        run_case_all_white
        ;;
    random_noise)
        run_case_random_noise
        ;;
    footer_data_destroyed)
        run_case_footer_data_destroyed
        ;;
    footer_valid_data_too_corrupt)
        run_case_footer_valid_data_too_corrupt
        ;;
    all)
        run_case_wrong_depth
        run_case_invalid_magic
        run_case_all_black
        run_case_all_white
        run_case_random_noise
        run_case_footer_data_destroyed
        run_case_footer_valid_data_too_corrupt
        ;;
    *)
        echo "test_decode_failures: unknown --case '${case_name}'" >&2
        usage >&2
        exit 1
        ;;
esac

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS decode failures rejected as expected\n' "$label_fmt"
