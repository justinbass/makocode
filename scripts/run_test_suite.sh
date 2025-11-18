#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname "$0")/.." && pwd -P)
suite_bin="$repo_root/scripts/run_roundtrip.sh"
cli_test="$repo_root/scripts/test_cli_output_dir.sh"
decode_test="$repo_root/scripts/test_decode_failures.sh"

if [[ ! -x $suite_bin ]]; then
    echo "run_test_suite: missing $suite_bin" >&2
    exit 1
fi

case_counter=1
first_case=1

print_header() {
    local label=$1
    local description=$2
    if [[ $first_case -eq 0 ]]; then
        printf '\n'
    else
        first_case=0
    fi
    printf '[%s] %s\n' "$label" "$description"
}

execute_case() {
    local label=$1
    local description=$2
    shift 2
    print_header "$label" "$description"
    set +e
    "$@"
    local status=$?
    set -e
    if [[ $status -ne 0 ]]; then
        printf '[%s] FAIL (exit %d)\n' "$label" "$status"
        exit $status
    fi
    printf '[%s] PASS\n' "$label"
}

run_roundtrip_case() {
    local slug=$1
    local description=$2
    shift 2
    local new_label
    printf -v new_label "%04d_%s" "$case_counter" "$slug"
    case_counter=$((case_counter + 1))
    execute_case "$new_label" "$description" "$suite_bin" --label "$new_label" "$@"
}

run_cli_case() {
    local slug=$1
    local description=$2
    local new_label
    printf -v new_label "%04d_%s" "$case_counter" "$slug"
    case_counter=$((case_counter + 1))
    execute_case "$new_label" "$description" "$cli_test" --label "$new_label"
}

run_decode_case() {
    local slug=$1
    local description=$2
    local new_label
    printf -v new_label "%04d_%s" "$case_counter" "$slug"
    case_counter=$((case_counter + 1))
    execute_case "$new_label" "$description" "$decode_test" --label "$new_label"
}

run_roundtrip_case "baseline_8k_ecc_050" "8 KiB payload, ECC 0.5 baseline" \
    --size 8192 --ecc 0.5 --width 500 --height 500

run_roundtrip_case "baseline_8k_ecc_000" "8 KiB payload, ECC disabled baseline" \
    --size 8192 --ecc 0 --width 500 --height 500

run_roundtrip_case "baseline_multi_page_ecc_100" "128 KiB payload, ECC 1.0 multi-page" \
    --size 131072 --ecc 1.0 --width 700 --height 700 --multi-page

run_roundtrip_case "palette_white_black" "Two-color palette stress" \
    --size 8192 --ecc 0.25 --width 360 --height 360 --palette "White Black"

run_roundtrip_case "palette_cmy" "Three-color palette stress" \
    --size 8192 --ecc 0.25 --width 420 --height 420 --palette "White Cyan Magenta"

run_roundtrip_case "palette_cmyk" "Four-color palette stress" \
    --size 8192 --ecc 0.25 --width 480 --height 480 --palette "White Cyan Magenta Yellow"

run_roundtrip_case "palette_cmykw" "Five-color palette stress" \
    --size 8192 --ecc 0.25 --width 480 --height 480 --palette "White Cyan Magenta Yellow Black"

run_roundtrip_case "palette_hex_octet" "Expanded hex palette stress" \
    --size 8192 --ecc 0.25 --width 480 --height 480 \
    --palette "FFFFFF FF0000 00FF00 0000FF FFFF00 FF00FF 00FFFF 000000"

run_roundtrip_case "large_canvas_cmykw" "Large canvas CMYKW palette" \
    --size 32768 --ecc 0.25 --width 1000 --height 1000 --palette "White Cyan Magenta Yellow Black"

run_roundtrip_case "low_ecc_fiducial" "Low ECC fiducial reservation, tiny pages" \
    --size 256 --ecc 0 --width 64 --height 64 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count"

run_roundtrip_case "footer_metadata" "Footer metadata rendering stress" \
    --size 65536 --ecc 0.25 --width 1300 --height 800 \
    --title "MAKOCODE FOOTER TITLE WITH DUST IGNORED 2025-11-09 >>>" \
    --font-size 2

run_roundtrip_case "gray_100k_single" "100 KiB grayscale single page baseline" \
    --size 102400 --ecc 0 --width 1100 --height 1100

run_roundtrip_case "gray_100k_scaled" "100 KiB grayscale scaled 2.5x" \
    --size 102400 --ecc 0 --width 1100 --height 1100 --scale 2.5

run_roundtrip_case "gray_100k_stretch_h26_v24" "Grayscale stretch horizontal 2.6 / vertical 2.4" \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale-x 2.6 --scale-y 2.4

run_roundtrip_case "gray_100k_stretch_h24_v26" "Grayscale stretch horizontal 2.4 / vertical 2.6" \
    --size 102400 --ecc 0 --width 1100 --height 1100 \
    --scale-x 2.4 --scale-y 2.6

run_roundtrip_case "password_ecc" "Password-protected payload with ECC" \
    --size 32768 --ecc 0.5 --width 720 --height 720 --password suite-password

run_roundtrip_case "ecc_multi_page_massive" "262 KiB ECC multi-page stress" \
    --size 262144 --ecc 0.25 --width 520 --height 520 --multi-page

run_roundtrip_case "palette_base5_custom" "Custom palette/base-5 mode" \
    --size 16384 --ecc 0.25 --width 640 --height 640 \
    --palette "White Cyan Magenta Yellow Black"

run_roundtrip_case "border_dirt" "Border dirt / distortion stress" \
    --size 32768 --ecc 0.25 --width 640 --height 640 \
    --border-thickness 12 --border-density 0.15

run_cli_case "cli_output_dir" "CLI respects explicit output directory"

run_decode_case "decode_failures" "Decoder rejects malformed PPM inputs"
