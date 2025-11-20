#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_palette_metadata.sh [--label NAME] [--mode MODE]

  --label NAME    Artifact prefix under test/ (default: palette_metadata).
  --mode MODE     One of:
                    auto   (decode without supplying --palette, expect success)
                    wrong  (decode with an incorrect palette, expect failure)
  --help          Show this help message.
USAGE
}

label="palette_metadata"
mode="auto"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --mode)
            mode=${2:-}
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_palette_metadata: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_palette_metadata: --label requires a value" >&2
    exit 1
fi
if [[ $mode != "auto" && $mode != "wrong" ]]; then
    echo "test_palette_metadata: --mode must be 'auto' or 'wrong'" >&2
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

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_palette_metadata: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
work_dir="$test_dir/${label}_palette_work"
payload="$test_dir/${label}_payload.bin"
payload_work="$work_dir/payload.bin"
ppm_target="$test_dir/${label}_encoded.ppm"
decoded_target="$test_dir/${label}_decoded.bin"

cleanup() {
    if [[ -d $work_dir ]]; then
        rm -rf "$work_dir"
    fi
}
trap cleanup EXIT

rm -rf "$work_dir"
mkdir -p "$test_dir" "$work_dir"
rm -f "$payload" "$ppm_target" "$decoded_target"

head -c 16384 /dev/urandom > "$payload"
cp "$payload" "$payload_work"
palette="White Cyan Magenta Yellow"

encode_cmd=(
    "$makocode_bin" encode
    "--input=payload.bin"
    --ecc=0.25
    --page-width=640
    --page-height=640
    --palette "$palette"
    "--output-dir=$work_dir"
)
print_labelled "encode" "${encode_cmd[@]}"
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppms=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_palette_metadata: encode emitted no PPMs" >&2
    exit 1
fi
ppm_path="${ppms[0]}"
mv -f "$ppm_path" "$ppm_target"

decoded_dir="$work_dir/decoded"
mkdir -p "$decoded_dir"
decoded_payload="$decoded_dir/payload.bin"

case "$mode" in
    auto)
        decode_cmd=("$makocode_bin" decode "$ppm_target" --output-dir "$decoded_dir")
        print_labelled "decode-auto" "${decode_cmd[@]}"
        "${decode_cmd[@]}" >/dev/null
        if [[ ! -f $decoded_payload ]]; then
            echo "test_palette_metadata: decode missing payload.bin in auto mode" >&2
            exit 1
        fi
        cmp --silent "$payload" "$decoded_payload"
        ;;
    wrong)
        decode_cmd=(
            "$makocode_bin" decode
            "$ppm_target"
            --output-dir "$decoded_dir"
            --palette "White Black"
        )
        print_labelled "decode-wrong" "${decode_cmd[@]}"
        set +e
        "${decode_cmd[@]}" >/dev/null 2>&1
        status=$?
        set -e
        if [[ $status -eq 0 ]]; then
            echo "test_palette_metadata: decode unexpectedly succeeded with wrong palette" >&2
            exit 1
        fi
        ;;
esac

if [[ -f $decoded_payload ]]; then
    mv "$decoded_payload" "$decoded_target"
fi

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS mode=%s\n' "$label_fmt" "$mode"
