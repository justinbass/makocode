#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: run_roundtrip.sh --label NAME --size BYTES --ecc VALUE --width PX --height PX [--multi-page]

Environment:
  MAKOCODE_BIN   Path to the makocode binary (default: ./makocode)
USAGE
}

label=""
size=""
ecc=""
width=""
height=""
multi_page=0

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
        --multi-page)
            multi_page=1
            shift
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || -z $size || -z $ecc || -z $width || -z $height ]]; then
    echo "Missing required arguments" >&2
    usage >&2
    exit 1
fi

makocode_bin=${MAKOCODE_BIN:-./makocode}
if [[ ! -x $makocode_bin ]]; then
    echo "makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if command -v realpath >/dev/null 2>&1; then
    makocode_bin=$(realpath "$makocode_bin")
else
    makocode_dir=$(cd "$(dirname "$makocode_bin")" && pwd)
    makocode_bin="$makocode_dir/$(basename "$makocode_bin")"
fi

mkdir -p test
tmp_dir=$(mktemp -d "test/${label}_tmp.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

head -c "$size" /dev/urandom > "$tmp_dir/random.bin"

encode_args=("$makocode_bin" encode --input=random.bin --ecc="$ecc" --page-width="$width" --page-height="$height")
( cd "$tmp_dir" && "${encode_args[@]}" )

if [[ $multi_page -eq 1 ]]; then
    ( cd "$tmp_dir" && "$makocode_bin" decode ./*.ppm --output-dir decoded )
else
    ppm_file=$(cd "$tmp_dir" && ls -1 -- *.ppm | head -n1)
    if [[ -z $ppm_file ]]; then
        echo "No PPM output produced for $label" >&2
        exit 1
    fi
    ( cd "$tmp_dir" && "$makocode_bin" decode "$ppm_file" --output-dir decoded )
fi

origin_payload="test/${label}_random_payload.bin"
encoded_prefix="test/${label}_random_payload_encoded"
decoded_payload="test/${label}_random_payload_decoded.bin"

mv "$tmp_dir/random.bin" "$origin_payload"

if [[ $multi_page -eq 1 ]]; then
    page_idx=1
    for ppm in "$tmp_dir"/*.ppm; do
        dest=$(printf "%s_%02d.ppm" "$encoded_prefix" "$page_idx")
        mv "$ppm" "$dest"
        page_idx=$((page_idx + 1))
    done
else
    ppm_file=$(cd "$tmp_dir" && ls -1 -- *.ppm | head -n1)
    mv "$tmp_dir/$ppm_file" "${encoded_prefix}.ppm"
fi

mv "$tmp_dir/decoded/random.bin" "$decoded_payload"

cmp --silent "$origin_payload" "$decoded_payload"

echo "roundtrip $label ok (ecc=$ecc, size=$size)"
