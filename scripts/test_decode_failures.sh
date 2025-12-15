#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

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

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
ppm_transform_bin="$repo_root/scripts/ppm_transform"
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_decode_failures: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
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
    run_expect_failure "decode-wrong-depth" "$makocode_bin" decode "$wrong_depth"
}

run_case_invalid_magic() {
    cat > "$invalid_magic" <<'PPM'
P6
2 2
255
PPM
    run_expect_failure "decode-invalid-magic" "$makocode_bin" decode "$invalid_magic"
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
    run_expect_failure "decode-all-black" "$makocode_bin" decode "$all_black"
}

# 2) Totally white, no barcode present.
run_case_all_white() {
    write_solid_ppm "$all_white" 256 256 255 255 255
    run_expect_failure "decode-all-white" "$makocode_bin" decode "$all_white"
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
    run_expect_failure "decode-random-noise" "$makocode_bin" decode "$random_noise"
}

# 4) Footer stripe present, but data region destroyed with noise.
run_case_footer_data_destroyed() {
    mkdir -p "$work_dir"
    head -c 64 /dev/urandom > "$payload_path"
    print_makocode_cmd "encode-footer-only" \
        "$makocode_bin" encode \
        "--input=payload.bin" \
        "--page-width=260" \
        "--page-height=260" \
        "--no-filename" \
        "--no-page-count" \
        "--output-dir=$work_dir"
    (
        cd "$work_dir"
        "$makocode_bin" encode \
            --input=payload.bin \
            --page-width=260 \
            --page-height=260 \
            --no-filename \
            --no-page-count \
            --output-dir="$work_dir" \
            >/dev/null
    )

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

    run_expect_failure "decode-footer-present-data-destroyed" "$makocode_bin" decode "$footer_data_destroyed"
    rm -rf "$work_dir"
}

run_case_footer_valid_data_too_corrupt() {
    mkdir -p "$work_dir"
    head -c 32768 /dev/urandom > "$payload_path"
    print_makocode_cmd "encode-ecc-overwhelmed" \
        "$makocode_bin" encode \
        "--input=payload.bin" \
        "--ecc=0.25" \
        "--page-width=600" \
        "--page-height=600" \
        "--no-filename" \
        "--no-page-count" \
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
            --output-dir="$work_dir" \
            >/dev/null
    )

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

    run_expect_failure "decode-footer-valid-data-too-corrupt" "$makocode_bin" decode "$footer_valid_data_too_corrupt"
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
