#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}

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

if [[ ! -x $makocode_bin ]]; then
    echo "test_header_copy_corruption: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
work_dir="$test_dir/${label}_work"
payload_final="$test_dir/${label}_payload.bin"
encoded_ppm="$test_dir/${label}_encoded.ppm"

rm -rf "$work_dir"
mkdir -p "$work_dir"
rm -f "$payload_final" "$encoded_ppm"

head -c 4096 /dev/urandom > "$payload_final"
cp "$payload_final" "$work_dir/random.bin"

encode_cmd=("$makocode_bin" encode "--input=random.bin" "--ecc=0.5" "--page-width=600" "--page-height=600" "--output-dir=$work_dir")
(cd "$work_dir" && "${encode_cmd[@]}") >/dev/null

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
    local output_dir="$test_dir/${label}_${scenario}_decoded"
    rm -rf "$output_dir"
    mkdir -p "$output_dir"

    local decode_cmd=("$makocode_bin" decode "--output-dir=$output_dir")
    if [[ $corrupt_count -gt 0 ]]; then
        decode_cmd+=("--corrupt-header-copies=$corrupt_count")
    fi
    decode_cmd+=("$encoded_ppm")

    set +e
    "${decode_cmd[@]}" >/dev/null 2>&1
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
    if [[ $expect_success -eq 1 ]]; then
        if [[ ! -f "$output_dir/random.bin" ]]; then
            echo "test_header_copy_corruption: decode ${scenario} missing random.bin" >&2
            exit 1
        fi
        cmp --silent "$payload_final" "$output_dir/random.bin"
    fi
}

run_decode_expect "one_copy" 1 1
run_decode_expect "two_copies" 1 2
run_decode_expect "all_copies" 0 3

label_fmt="$label"
printf '%s SUCCESS header copy corruption expectations met\n' "$label_fmt"
