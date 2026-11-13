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
. "$script_dir/lib/affine.sh"

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

usage() {
    cat <<'USAGE'
Usage: run_roundtrip.sh --label NAME --size BYTES [--ecc VALUE|fill] [--width PX] [--height PX] [options]

Required:
  --label NAME          Artifact prefix under test_output/ (e.g., 3001_demo).
  --size BYTES          Payload size fed to the encoder.
  --ecc VALUE|fill      Optional parity/data ratio (0-254); fill maximizes page use.
                        Omit to select the minimum ECC for metadata, fiducials, and configured overlay erasures.
  --width PX            Optional fixed page width; omitted dimensions fit the payload.
  --height PX           Optional fixed page height; omitted dimensions fit the payload.

Payload:
  --payload-literal TEXT  Use TEXT bytes as the payload instead of random data.
  Payload bytes use stdin and decoded-stdout by default.

Layout:
  --palette "..."       Custom palette passed to encode/decode.
  --footer TEXT         Footer template (forwarded to encode).
  --font-size PX        Footer font size.
  --multi-page          Require multiple encoded pages and decode all PPMs in order.
  --encode-opt OPT      Extra flag forwarded verbatim to `makocode encode` (repeatable).
  --decode-opt OPT      Extra flag forwarded verbatim to `makocode decode` (repeatable).

Security:
  --password TEXT       Enable encryption and pass TEXT to encode/decode.

Distortions (applied after baseline roundtrip when requested):
  --scale FACTOR        Scale both axes by FACTOR.
  --scale-x FACTOR      Scale horizontally by FACTOR.
  --scale-y FACTOR      Scale vertically by FACTOR.
  --rotate DEG          Rotate by DEG degrees.
  --skew-x PIXELS       Shear horizontally across the page height.
  --skew-y PIXELS       Shear vertically across the page width.
  --border-thickness PX Add noisy dirt along all borders with thickness PX.
  --border-density R    Density (0-1) for border dirt when applied (default 0.35).
  --transform-seed N    Seed for deterministic distortions (default 0).
  --ink-blot-radius PX  Draw a circular blot of radius PX at the page center (requires color).
  --ink-blot-color C    Blot color name (White/Black) or RRGGBB hex.
  --paper-color C       Paper tint color (White/Black) or RRGGBB hex.
  --paper-alpha A       Uniform paper blend (0-1).
  --paper-splotch-alpha A  Extra paper blend from a splotchy field (0-1).
  --paper-splotch-shade A  Splotchy shading/dimming (0-1).
  --paper-splotch-px PX    Splotch patch size in pixels.
  --white-checker          Overwrite alternating pixels with white after transforms.

General:
  --help                Show this help message.

Environment:
  MAKOCODE_BIN          Override path to the makocode binary (default: ./makocode).
USAGE
}

label=""
size=""
ecc=""
width=""
height=""
payload_literal=""
multi_page=0
palette=""
password=""
footer=""
font_size=""
declare -a encode_opts=()
declare -a decode_opts=()
scale_x="1"
scale_y="1"
rotate_deg="0"
skew_x="0"
skew_y="0"
border_thickness="0"
border_density="0.35"
transform_seed="0"
ink_blot_radius="0"
ink_blot_color=""
paper_color=""
paper_alpha="0"
paper_splotch_alpha="0"
paper_splotch_shade="0"
paper_splotch_px="0"
white_checker=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --size)
            size=${2:-}
            shift 2
            ;;
        --ecc)
            ecc=${2:-}
            shift 2
            ;;
        --width)
            width=${2:-}
            shift 2
            ;;
        --height)
            height=${2:-}
            shift 2
            ;;
        --payload-literal)
            payload_literal=${2:-}
            shift 2
            ;;
        --palette)
            palette=${2:-}
            shift 2
            ;;
        --password)
            password=${2:-}
            shift 2
            ;;
        --footer)
            footer=${2:-}
            shift 2
            ;;
        --font-size)
            font_size=${2:-}
            shift 2
            ;;
        --multi-page)
            multi_page=1
            shift
            ;;
        --encode-opt)
            if [[ $# -lt 2 || -z ${2:-} ]]; then
                echo "run_roundtrip: --encode-opt requires a value" >&2
                usage >&2
                exit 1
            fi
            encode_opts+=("${2}")
            shift 2
            ;;
        --decode-opt)
            if [[ $# -lt 2 || -z ${2:-} ]]; then
                echo "run_roundtrip: --decode-opt requires a value" >&2
                usage >&2
                exit 1
            fi
            decode_opts+=("${2}")
            shift 2
            ;;
        --scale)
            scale_x=${2:-}
            scale_y=${2:-}
            shift 2
            ;;
        --scale-x)
            scale_x=${2:-}
            shift 2
            ;;
        --scale-y)
            scale_y=${2:-}
            shift 2
            ;;
        --rotate)
            rotate_deg=${2:-}
            shift 2
            ;;
        --skew-x)
            skew_x=${2:-}
            shift 2
            ;;
        --skew-y)
            skew_y=${2:-}
            shift 2
            ;;
        --border-thickness)
            border_thickness=${2:-}
            shift 2
            ;;
        --border-density)
            border_density=${2:-}
            shift 2
            ;;
        --transform-seed)
            transform_seed=${2:-}
            shift 2
            ;;
        --ink-blot-radius)
            ink_blot_radius=${2:-}
            shift 2
            ;;
        --ink-blot-color)
            ink_blot_color=${2:-}
            shift 2
            ;;
        --paper-color)
            paper_color=${2:-}
            shift 2
            ;;
        --paper-alpha)
            paper_alpha=${2:-}
            shift 2
            ;;
        --paper-splotch-alpha)
            paper_splotch_alpha=${2:-}
            shift 2
            ;;
        --paper-splotch-shade)
            paper_splotch_shade=${2:-}
            shift 2
            ;;
        --paper-splotch-px)
            paper_splotch_px=${2:-}
            shift 2
            ;;
        --white-checker)
            white_checker=1
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "run_roundtrip: unknown option '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || -z $size ]]; then
    echo "run_roundtrip: --label and --size are required" >&2
    usage >&2
    exit 1
fi

if (( ink_blot_radius > 0 )) && [[ -z $ink_blot_color ]]; then
    echo "run_roundtrip: --ink-blot-radius requires --ink-blot-color" >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "run_roundtrip: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
mako_test_seed_init "$repo_root"
payload_seed=$(mako_test_case_seed "payload/$label")
MAKOCODE_TEST_SEED=$(mako_test_case_seed "crypto/$label")
export MAKOCODE_TEST_SEED
mkdir -p "$test_dir"
work_dir=$(mktemp -d "$test_dir/${label}_work.XXXXXX")
encode_prefix="${label}_random_payload_encoded"
encoded_prefix="$work_dir/$encode_prefix"

if [[ -n $payload_literal ]]; then
    payload_len=$("$repo_root/test/ppm_transform" bytes-len "$payload_literal")
    if [[ $payload_len -ne $size ]]; then
        echo "run_roundtrip: --size ($size) must match --payload-literal byte length ($payload_len)" >&2
        exit 1
    fi
    expected_args=(--literal "$payload_literal")
else
    expected_args=(--size "$size" --seed "$payload_seed")
fi

encode_cmd=("$makocode_bin" encode "--output-dir=$work_dir" "--prefix=$encode_prefix")
if [[ -n $ecc ]]; then
    encode_cmd+=("--ecc=$ecc")
fi
if [[ -n $width ]]; then
    encode_cmd+=("--page-width=$width")
fi
if [[ -n $height ]]; then
    encode_cmd+=("--page-height=$height")
fi
if [[ -n $palette ]]; then
    encode_cmd+=("--palette" "$palette")
fi
if [[ -n $password ]]; then
    encode_cmd+=("--password=$password" "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb")
fi
if [[ -n $footer ]]; then
    encode_cmd+=("--footer" "$footer")
fi
if [[ -n $font_size ]]; then
    encode_cmd+=("--font-size" "$font_size")
fi
if ((${#encode_opts[@]})); then
    for opt in "${encode_opts[@]}"; do
        encode_cmd+=("$opt")
    done
fi
print_makocode_cmd "encode" "${encode_cmd[@]}"
if [[ -n $payload_literal ]]; then
    printf '%s' "$payload_literal" | "${encode_cmd[@]}" >/dev/null
else
    "$repo_root/test/ppm_transform" bytes --output - --size "$size" --seed "$payload_seed" |
        "${encode_cmd[@]}" >/dev/null
fi

shopt -s nullglob
ppm_paths=("${encoded_prefix}"*.ppm)
shopt -u nullglob
if [[ ${#ppm_paths[@]} -eq 0 ]]; then
    echo "run_roundtrip: encode produced no PPM pages" >&2
    exit 1
fi
IFS=$'\n' ppm_paths=($(printf '%s\n' "${ppm_paths[@]}" | LC_ALL=C sort))
unset IFS

if [[ $label == "0000_tiny_0" ]]; then
    {
        read -r tiny_magic
        read -r tiny_width tiny_height
        read -r tiny_max_value
    } < "${ppm_paths[0]}"
    if [[ $tiny_magic != "P6" || $tiny_max_value != "255" ||
          $tiny_width -ge 48 || $tiny_height -ge 48 ]]; then
        echo "run_roundtrip: one-byte metadata tile was not clamped below 48x48" >&2
        exit 1
    fi
fi

if [[ $multi_page -eq 1 && ${#ppm_paths[@]} -lt 2 ]]; then
    echo "run_roundtrip: --multi-page requested but encode produced only ${#ppm_paths[@]} PPM page(s)" >&2
    exit 1
fi

baseline_decode_cmd=("$makocode_bin" decode --decoded-stdout)
if ((${#decode_opts[@]})); then
    for opt in "${decode_opts[@]}"; do
        baseline_decode_cmd+=("$opt")
    done
fi
if [[ -n $palette ]]; then
    baseline_decode_cmd+=("--palette" "$palette")
fi
if [[ -n $password ]]; then
    baseline_decode_cmd+=("--password=$password")
fi
print_makocode_cmd "decode-stdout" "${baseline_decode_cmd[@]}"
if [[ ${#ppm_paths[@]} -eq 1 ]]; then
    cat "${ppm_paths[0]}" |
        "${baseline_decode_cmd[@]}" 2>/dev/null |
        python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
            "${expected_args[@]}" --label "$label baseline"
else
    "${baseline_decode_cmd[@]}" "${ppm_paths[@]}" 2>/dev/null |
        python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
            "${expected_args[@]}" --label "$label baseline"
fi

transform_needed=0
if [[ $scale_x != 1 || $scale_y != 1 || $rotate_deg != 0 || $skew_x != 0 || $skew_y != 0 || $border_thickness != 0 ]]; then
    transform_needed=1
fi
if (( ink_blot_radius > 0 )); then
    transform_needed=1
fi
if [[ $paper_alpha != 0 || $paper_splotch_alpha != 0 || $paper_splotch_shade != 0 || $paper_splotch_px != 0 ]]; then
    transform_needed=1
fi
if [[ $white_checker -eq 1 ]]; then
    transform_needed=1
fi

affine_check_enabled=0
affine_tolerance_percent=1
if awk -v scale_x="$scale_x" -v scale_y="$scale_y" -v rotation="$rotate_deg" \
       -v skew_x="$skew_x" -v skew_y="$skew_y" \
       'BEGIN { exit !((scale_x + 0) == (scale_y + 0) && (rotation + 0) != 0 &&
                       (skew_x + 0) == 0 && (skew_y + 0) == 0) }'; then
    affine_check_enabled=1
    expected_affine_scale=$scale_x
    expected_affine_rotation=$rotate_deg
fi

if [[ $transform_needed -eq 1 ]]; then
    transform_options=(
        --scale-x "$scale_x"
        --scale-y "$scale_y"
        --rotate "$rotate_deg"
        --skew-x "$skew_x"
        --skew-y "$skew_y"
        --border-thickness "$border_thickness"
        --border-density "$border_density"
        --seed "$transform_seed"
        --ink-blot-radius "$ink_blot_radius"
        --ink-blot-color "$ink_blot_color"
        --paper-color "$paper_color"
        --paper-alpha "$paper_alpha"
        --paper-splotch-alpha "$paper_splotch_alpha"
        --paper-splotch-shade "$paper_splotch_shade"
        --paper-splotch-px "$paper_splotch_px"
    )
    if [[ $white_checker -eq 1 ]]; then
        transform_options+=(--white-checker --preserve-canonical-control)
    fi
    if [[ ${#ppm_paths[@]} -eq 1 ]]; then
        "$repo_root/test/ppm_transform" transform --input "${ppm_paths[0]}" --output - "${transform_options[@]}" |
            "${baseline_decode_cmd[@]}" 2>/dev/null |
            python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
                "${expected_args[@]}" --tolerance-percent 5 --label "$label transformed"
        if [[ $affine_check_enabled -eq 1 ]]; then
            affine_page="$work_dir/affine_scan.ppm"
            "$repo_root/test/ppm_transform" transform --input "${ppm_paths[0]}" --output "$affine_page" "${transform_options[@]}"
            affine_dir="$work_dir/affine_decode"
            mkdir -p "$affine_dir"
            if ! "$makocode_bin" decode --debug "--output-dir=$affine_dir" "$affine_page" >/dev/null 2>&1; then
                echo "run_roundtrip: diagnostic affine decode failed" >&2
                exit 1
            fi
            mako_check_affine "$affine_dir/decode_affine.csv" \
                "$expected_affine_scale" "$expected_affine_rotation" "$affine_tolerance_percent" 1 \
                "$label transformed"
        fi
    else
        transformed_paths=()
        page_index=0
        for ppm in "${ppm_paths[@]}"; do
            page_index=$((page_index + 1))
            transformed_path="$work_dir/transformed_${page_index}.ppm"
            "$repo_root/test/ppm_transform" transform --input "$ppm" --output "$transformed_path" "${transform_options[@]}"
            transformed_paths+=("$transformed_path")
        done
        "${baseline_decode_cmd[@]}" "${transformed_paths[@]}" 2>/dev/null |
            python3 "$script_dir/check_decoded_stdout.py" --generator "$repo_root/test/ppm_transform" \
                "${expected_args[@]}" --tolerance-percent 5 --label "$label transformed"
    fi
fi

label_fmt=$(mako_format_label "$label")
suffix=""
if [[ $transform_needed -eq 1 ]]; then
    suffix=" (transformed)"
fi
printf '%s SUCCESS ecc=%s size=%s pages=%d%s\n\n' \
    "$label_fmt" "$ecc" "$size" "${#ppm_paths[@]}" "$suffix"
