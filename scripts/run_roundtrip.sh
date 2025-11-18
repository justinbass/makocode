#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: run_roundtrip.sh --label NAME --size BYTES --ecc VALUE --width PX --height PX [options]

Required:
  --label NAME          Artifact prefix under test/ (e.g., 3001_demo).
  --size BYTES          Payload size fed to the encoder.
  --ecc VALUE           ECC redundancy ratio (floating point).
  --width PX            Page width in pixels.
  --height PX           Page height in pixels.

Layout:
  --palette "..."       Custom palette passed to encode/decode.
  --title TEXT          Footer title (forwarded to encode).
  --font-size PX        Footer font size.
  --multi-page          Treat output as a multi-page decode (all PPMs in order).
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
multi_page=0
palette=""
password=""
title=""
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
        --palette)
            palette=${2:-}
            shift 2
            ;;
        --password)
            password=${2:-}
            shift 2
            ;;
        --title)
            title=${2:-}
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

if [[ -z $label || -z $size || -z $ecc || -z $width || -z $height ]]; then
    echo "run_roundtrip: --label, --size, --ecc, --width, and --height are required" >&2
    usage >&2
    exit 1
fi

repo_root=$(cd -- "$(dirname "$0")/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "run_roundtrip: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

mkdir -p "$repo_root/test"
tmp_dir=$(mktemp -d "$repo_root/test/${label}_tmp.XXXXXX")
payload_path="$tmp_dir/random.bin"
cleanup() {
    if [[ -d $tmp_dir ]]; then
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT

head -c "$size" /dev/urandom > "$payload_path"

encode_cmd=("$makocode_bin" encode "--input=random.bin" "--ecc=$ecc" "--page-width=$width" "--page-height=$height")
if [[ -n $palette ]]; then
    encode_cmd+=("--palette" "$palette")
fi
if [[ -n $password ]]; then
    encode_cmd+=("--password=$password")
fi
if [[ -n $title ]]; then
    encode_cmd+=("--title" "$title")
fi
if [[ -n $font_size ]]; then
    encode_cmd+=("--font-size" "$font_size")
fi
if ((${#encode_opts[@]})); then
    for opt in "${encode_opts[@]}"; do
        encode_cmd+=("$opt")
    done
fi
(
    cd "$tmp_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppm_paths=("$tmp_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppm_paths[@]} -eq 0 ]]; then
    echo "run_roundtrip: encode produced no PPM pages" >&2
    exit 1
fi
IFS=$'\n' ppm_paths=($(printf '%s\n' "${ppm_paths[@]}" | LC_ALL=C sort))
unset IFS

baseline_decode_dir="$tmp_dir/decoded"
mkdir -p "$baseline_decode_dir"
decode_cmd=("$makocode_bin" decode)
if ((${#decode_opts[@]})); then
    for opt in "${decode_opts[@]}"; do
        decode_cmd+=("$opt")
    done
fi
if [[ -n $palette ]]; then
    decode_cmd+=("--palette" "$palette")
fi
if [[ -n $password ]]; then
    decode_cmd+=("--password=$password")
fi
decode_cmd+=("--output-dir=$baseline_decode_dir")
if [[ $multi_page -eq 1 ]]; then
    decode_cmd+=("${ppm_paths[@]}")
else
    decode_cmd+=("${ppm_paths[0]}")
fi
"${decode_cmd[@]}" >/dev/null

baseline_decoded_payload="$baseline_decode_dir/random.bin"
if [[ ! -f $baseline_decoded_payload ]]; then
    echo "run_roundtrip: decode did not emit random.bin" >&2
    exit 1
fi
cmp --silent "$payload_path" "$baseline_decoded_payload"

origin_payload="$repo_root/test/${label}_random_payload.bin"
encoded_prefix="$repo_root/test/${label}_random_payload_encoded"
decoded_payload_target="$repo_root/test/${label}_random_payload_decoded.bin"
baseline_targets=()

mv "$payload_path" "$origin_payload"
if [[ ${#ppm_paths[@]} -eq 1 ]]; then
    dest="${encoded_prefix}.ppm"
    mv "${ppm_paths[0]}" "$dest"
    baseline_targets+=("$dest")
else
    idx=1
    for ppm in "${ppm_paths[@]}"; do
        printf -v dest "%s_%02d.ppm" "$encoded_prefix" "$idx"
        mv "$ppm" "$dest"
        baseline_targets+=("$dest")
        idx=$((idx + 1))
    done
fi
mv "$baseline_decoded_payload" "$decoded_payload_target"

transform_needed=0
if [[ $scale_x != 1 || $scale_y != 1 || $rotate_deg != 0 || $skew_x != 0 || $skew_y != 0 || $border_thickness != 0 ]]; then
    transform_needed=1
fi

# Reuse the archived baseline artifacts when building transformed copies
ppm_targets=("${baseline_targets[@]}")
if [[ ${#ppm_targets[@]} -eq 0 ]]; then
    echo "run_roundtrip: failed to archive encoded artifacts" >&2
    exit 1
fi

if [[ $transform_needed -eq 1 ]]; then
    transform_outputs=()
    idx=1
    for ppm in "${ppm_targets[@]}"; do
        base_name=$(basename "$ppm")
        transformed_path="$tmp_dir/${base_name%.*}_transformed.ppm"
        python3 "$repo_root/scripts/ppm_transform.py" \
            --input "$ppm" \
            --output "$transformed_path" \
            --scale-x "$scale_x" \
            --scale-y "$scale_y" \
            --rotate "$rotate_deg" \
            --skew-x "$skew_x" \
            --skew-y "$skew_y" \
            --border-thickness "$border_thickness" \
            --border-density "$border_density" \
            --seed "$transform_seed"
        transform_outputs+=("$transformed_path")
        idx=$((idx + 1))
    done
    transformed_decode_dir="$tmp_dir/decoded_transformed"
    mkdir -p "$transformed_decode_dir"
    transform_decode_cmd=("$makocode_bin" decode)
    if ((${#decode_opts[@]})); then
        for opt in "${decode_opts[@]}"; do
            transform_decode_cmd+=("$opt")
        done
    fi
    if [[ -n $palette ]]; then
        transform_decode_cmd+=("--palette" "$palette")
    fi
    if [[ -n $password ]]; then
        transform_decode_cmd+=("--password=$password")
    fi
    transform_decode_cmd+=("--output-dir=$transformed_decode_dir")
    if [[ $multi_page -eq 1 ]]; then
        transform_decode_cmd+=("${transform_outputs[@]}")
    else
        transform_decode_cmd+=("${transform_outputs[0]}")
    fi
    "${transform_decode_cmd[@]}" >/dev/null
    transformed_decoded_payload="$transformed_decode_dir/random.bin"
    if [[ ! -f $transformed_decoded_payload ]]; then
        echo "run_roundtrip: transformed decode missing random.bin" >&2
        exit 1
    fi
    cmp --silent "$origin_payload" "$transformed_decoded_payload"
    transformed_prefix="$repo_root/test/${label}_random_payload_transformed_encoded"
    if [[ ${#transform_outputs[@]} -eq 1 ]]; then
        mv "${transform_outputs[0]}" "${transformed_prefix}.ppm"
    else
        idx=1
        for ppm in "${transform_outputs[@]}"; do
            printf -v dest "%s_%02d.ppm" "$transformed_prefix" "$idx"
            mv "$ppm" "$dest"
            idx=$((idx + 1))
        done
    fi
    mv "$transformed_decoded_payload" "$repo_root/test/${label}_random_payload_transformed_decoded.bin"
fi

echo "roundtrip $label ok (ecc=$ecc, size=$size, pages=${#ppm_targets[@]}${transform_needed:+, transformed})"
