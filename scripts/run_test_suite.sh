#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"
repo_root=$(cd -- "$script_dir/.." && pwd -P)
suite_bin="$repo_root/scripts/run_roundtrip.sh"
cli_test="$repo_root/scripts/test_cli_output_dir.sh"
decode_test="$repo_root/scripts/test_decode_failures.sh"
overlay_test="$repo_root/scripts/test_overlay_e2e.sh"
palette_test="$repo_root/scripts/test_palette_metadata.sh"
password_fail_test="$repo_root/scripts/test_password_failures.sh"

# Full footer font charset, in `FOOTER_GLYPHS` order (makocode.cpp).
footer_charset_title=$' !"#$%&\\\'()*+,-./0123456789:;<=>?@[\\]^_`{|}~ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy'
footer_charset_font_size=2
footer_charset_filename="random.bin"
footer_charset_page_stub="Page 1/1"
footer_charset_title_len=${#footer_charset_title}
footer_charset_filename_len=${#footer_charset_filename}
footer_charset_page_len=${#footer_charset_page_stub}
footer_charset_text_len=$((footer_charset_title_len + 3 + footer_charset_filename_len + 3 + footer_charset_page_len))
footer_charset_glyph_width=$((5 * footer_charset_font_size))
footer_charset_char_spacing=$((footer_charset_font_size))
footer_charset_text_px=$((footer_charset_text_len * footer_charset_glyph_width + (footer_charset_text_len - 1) * footer_charset_char_spacing))
footer_charset_width=$((footer_charset_text_px + 64))

if [[ ! -x $suite_bin ]]; then
    echo "run_test_suite: missing $suite_bin" >&2
    exit 1
fi

case_counter=2
first_case=1

if [[ ${MAKE_STEP_LINE_OPEN:-0} -eq 1 ]]; then
    printf '\n'
    MAKE_STEP_LINE_OPEN=0
fi

print_header() {
    local label=$1
    local description=$2
    if [[ $first_case -eq 0 ]]; then
        printf '\n'
    else
        first_case=0
    fi
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    printf '%s %s\n' "$label_fmt" "$description"
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
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    if [[ $status -ne 0 ]]; then
        printf '%s %bFAIL (exit %d)%b\n' "$label_fmt" "$MAKO_FAIL_COLOR" "$status" "$MAKO_RESET_COLOR"
        exit $status
    fi
    printf '%s %bPASS%b\n' "$label_fmt" "$MAKO_PASS_COLOR" "$MAKO_RESET_COLOR"
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

run_script_case() {
    local script=$1
    local slug=$2
    local description=$3
    shift 3
    local new_label
    printf -v new_label "%04d_%s" "$case_counter" "$slug"
    case_counter=$((case_counter + 1))
    execute_case "$new_label" "$description" "$script" --label "$new_label" "$@"
}

run_overlay_case() {
    local slug=$1
    local description=$2
    shift 2
    local env_args=()
    local overlay_encode_opts=()
    while [[ $# -gt 0 ]]; do
        case $1 in
            --overlay-palette)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-palette requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_PALETTE="$2")
                shift 2
                ;;
            --overlay-circle-color)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-circle-color requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_CIRCLE_COLOR="$2")
                shift 2
                ;;
            --overlay-circle-colors)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-circle-colors requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_CIRCLE_COLORS="$2")
                shift 2
                ;;
            --overlay-background-color)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-background-color requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_BACKGROUND_COLOR="$2")
                shift 2
                ;;
            --overlay-mask-palette-base)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-mask-palette-base requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_MASK_PALETTE_BASE="$2")
                shift 2
                ;;
            --overlay-mask-palette-text)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-mask-palette-text requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_MASK_PALETTE_TEXT="$2")
                shift 2
                ;;
            --overlay-allowed-colors)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-allowed-colors requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_ALLOWED_COLORS="$2")
                shift 2
                ;;
            --overlay-fraction)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-fraction requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_FRACTION="$2")
                shift 2
                ;;
            --overlay-skip-grayscale-check)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-skip-grayscale-check requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_SKIP_GRAYSCALE_CHECK="$2")
                shift 2
                ;;
            --overlay-ignore-colors)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-ignore-colors requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_IGNORE_COLORS="$2")
                shift 2
                ;;
            --overlay-ecc-target)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-ecc-target requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_ECC_TARGET="$2")
                shift 2
                ;;
            --overlay-ecc-target=*)
                env_args+=(MAKO_OVERLAY_ECC_TARGET="${1#*=}")
                shift
                ;;
            --overlay-encode-opt)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --overlay-encode-opt requires a value" >&2
                    exit 1
                fi
                overlay_encode_opts+=("$2")
                shift 2
                ;;
            --ecc)
                if [[ $# -lt 2 ]]; then
                    echo "run_overlay_case: --ecc requires a value" >&2
                    exit 1
                fi
                env_args+=(MAKO_OVERLAY_ECC="$2")
                shift 2
                ;;
            *=*)
                env_args+=("$1")
                shift
                ;;
            *)
                echo "run_overlay_case: unknown option $1" >&2
                exit 1
                ;;
        esac
    done
    if [[ ${#overlay_encode_opts[@]} -gt 0 ]]; then
        env_args+=(MAKO_OVERLAY_ENCODE_OPTS="${overlay_encode_opts[*]}")
    fi
    local new_label
    printf -v new_label "%04d_%s" "$case_counter" "$slug"
    case_counter=$((case_counter + 1))
    if [[ ${#env_args[@]} -gt 0 ]]; then
        execute_case "$new_label" "$description" env "${env_args[@]}" "$overlay_test" "$new_label"
    else
        execute_case "$new_label" "$description" "$overlay_test" "$new_label"
    fi
}

run_roundtrip_label_case() {
    local label=$1
    local description=$2
    shift 2
    execute_case "$label" "$description" "$suite_bin" --label "$label" "$@"
}

run_roundtrip_label_case "0000_tiny_0" "1 byte payload literal '0' baseline" \
    --size 1 --ecc 0 --width 200 --height 64 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --encode-opt "--compact-page" \
    --payload-literal "0"

run_roundtrip_label_case "0001_tiny_makocode" "8 byte payload literal 'makocode' baseline" \
    --size 8 --ecc 0 --width 200 --height 64 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count" \
    --encode-opt "--compact-page" \
    --payload-literal "makocode"

run_roundtrip_case "baseline_8k_ecc_050" "8 KiB payload, ECC 0.5 baseline" \
    --size 8192 --ecc 0.5 --width 500 --height 500

run_roundtrip_case "baseline_8k_ecc_000" "8 KiB payload, ECC disabled baseline" \
    --size 8192 --ecc 0 --width 500 --height 500

run_roundtrip_case "baseline_multi_page_ecc_100" "128 KiB payload, ECC 1.0 multi-page" \
    --size 131072 --ecc 1.0 --width 700 --height 700 --multi-page

run_roundtrip_case "ecc_fill_moderate" "ECC fill saturates moderate payload" \
    --size 12000 --ecc 0.5 --width 420 --height 420 --encode-opt "--ecc-fill"

run_roundtrip_case "ecc_fill_sparse_large_page" "ECC fill small payload on large page" \
    --size 1024 --ecc 0.5 --width 1400 --height 1800 --encode-opt "--ecc-fill"

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
    --size 8192 --ecc 0.25 --width 1000 --height 1000 --palette "White Cyan Magenta Yellow Black"

run_roundtrip_case "low_ecc_fiducial" "Low ECC fiducial reservation, tiny pages" \
    --size 256 --ecc 0 --width 200 --height 64 \
    --encode-opt "--no-filename" \
    --encode-opt "--no-page-count"

run_roundtrip_case "footer_metadata" "Footer metadata rendering stress" \
    --size 65536 --ecc 0.25 --width "$footer_charset_width" --height 800 \
    --title "$footer_charset_title" \
    --font-size "$footer_charset_font_size"

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

run_roundtrip_case "gray_rotate_skew_seeded" "Grayscale rotate/skew with deterministic seed" \
    --size 65536 --ecc 0.25 --width 900 --height 900

run_roundtrip_case "password_ecc" "Password-protected payload with ECC" \
    --size 32768 --ecc 0.5 --width 720 --height 720 --password suite-password

run_roundtrip_case "ecc_multi_page_massive" "262 KiB ECC multi-page stress" \
    --size 262144 --ecc 0.25 --width 520 --height 520 --multi-page

run_roundtrip_case "multi_page_distorted" "Multi-page payload with scale/rotate distortions" \
    --size 24576 --ecc 0.5 --width 620 --height 620 --multi-page

run_roundtrip_case "palette_base5_custom" "Custom palette/base-5 mode" \
    --size 16384 --ecc 0.25 --width 640 --height 640 \
    --palette "White Cyan Magenta Yellow Black"

run_roundtrip_case "border_dirt" "Border dirt / distortion stress" \
    --size 32768 --ecc 0.25 --width 640 --height 640

run_cli_case "cli_output_dir" "CLI respects explicit output directory"

run_decode_case "decode_failures" "Decoder rejects malformed PPM inputs"

#run_script_case "$palette_test" "palette_auto_discovery" "Decoder auto-discovers palette metadata" \
#    --mode auto

run_script_case "$palette_test" "palette_wrong_rejected" "Decoder rejects forced wrong palette" \
    --mode wrong

run_script_case "$password_fail_test" "password_failures" "Decoder enforces password requirement"

run_script_case "$repo_root/scripts/test_header_copy_corruption.sh" \
    "header_copy_corruption" "Header copy RS repairs before decode"

run_roundtrip_case "palette_white_black_blot_black" "High-ECC black blot recovery on White/Black page" \
    --size 4096 --ecc 8.0 --width 600 --height 600 --palette "White Black" \
    --ink-blot-radius 180 --ink-blot-color black

run_roundtrip_case "palette_white_black_blot_white" "High-ECC white blot recovery on White/Black page" \
    --size 4096 --ecc 8.0 --width 600 --height 600 --palette "White Black" \
    --ink-blot-radius 180 --ink-blot-color white

run_overlay_case "overlay_e2e" "Overlay CLI merges masks and decodes" \
    --overlay-fraction 0.35 \
    --overlay-encode-opt "--ecc-fill"

run_overlay_case "overlay_palette_cmyy" "Overlay CLI respects CMY+Yellow palette with yellow mask" \
    --overlay-fraction 0.10 \
    --overlay-encode-opt "--ecc-fill" \
    --overlay-palette "White Cyan Magenta Yellow" \
    --overlay-circle-color "255 255 0" \
    --overlay-skip-grayscale-check 1

run_overlay_case "overlay_bw_base_color_cmwy" "Black/white base with CMYW circle overlay" \
    --overlay-fraction 0.05 \
    --overlay-encode-opt "--ecc-fill" \
    --overlay-palette "White Black" \
    --overlay-mask-palette-base 4 \
    --overlay-mask-palette-text "White Cyan Magenta Yellow" \
    --overlay-background-color "0 0 0" \
    --overlay-circle-colors "0 255 255;255 0 255;255 255 0;255 255 255" \
    --overlay-skip-grayscale-check 1 \
    --overlay-allowed-colors "0 0 0;255 255 255;0 255 255;255 0 255;255 255 0"

run_overlay_case "overlay_cmy_base_bw" "CMYW base with black/white circle overlay" \
    --overlay-fraction 0.1 \
    --overlay-encode-opt "--ecc-fill" \
    --overlay-palette "White Cyan Magenta Yellow" \
    --overlay-skip-grayscale-check 1 \
    --overlay-allowed-colors "0 0 0;255 255 255;0 255 255;255 0 255;255 255 0"

run_overlay_case "overlay_e2e_ignore_colors" "Overlay CLI skips white background when merging" \
    --overlay-fraction 0.35 \
    --overlay-encode-opt "--ecc-fill" \
    --overlay-ignore-colors "White"

run_overlay_case "overlay_bw_ecc_target_025" "Black/white circle overlay obeys ECC target 0.25" \
    --overlay-fraction 0.1 \
    --overlay-encode-opt "--ecc-fill" \
    --overlay-palette "White Black" \
    --overlay-circle-color "0 0 0" \
    --overlay-background-color "255 255 255" \
    --overlay-allowed-colors "0 0 0;255 255 255" \
    --overlay-skip-grayscale-check 1

run_overlay_case "overlay_bw_ecc_target_050" "Black/white circle overlay obeys ECC target 0.5" \
    --overlay-fraction 0.1 \
    --overlay-encode-opt "--ecc-fill" \
    --overlay-palette "White Black" \
    --overlay-circle-color "0 0 0" \
    --overlay-background-color "255 255 255" \
    --overlay-allowed-colors "0 0 0;255 255 255" \
    --overlay-skip-grayscale-check 1
