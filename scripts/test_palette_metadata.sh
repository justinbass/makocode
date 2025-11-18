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

mkdir -p "$repo_root/test"
tmp_dir=$(mktemp -d "$repo_root/test/${label}_tmp.XXXXXX")
payload="$tmp_dir/payload.bin"
cleanup() {
    if [[ -d $tmp_dir ]]; then
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT

head -c 16384 /dev/urandom > "$payload"
palette="White Cyan Magenta Yellow"

encode_cmd=(
    "$makocode_bin" encode
    "--input=$(basename "$payload")"
    --ecc=0.25
    --page-width=640
    --page-height=640
    --palette "$palette"
)
print_labelled "encode" "${encode_cmd[@]}"
(
    cd "$tmp_dir"
    "${encode_cmd[@]}" >/dev/null
)

shopt -s nullglob
ppms=("$tmp_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_palette_metadata: encode emitted no PPMs" >&2
    exit 1
fi
ppm_path="${ppms[0]}"

decoded_dir="$tmp_dir/decoded"
mkdir -p "$decoded_dir"
decoded_payload="$decoded_dir/payload.bin"

case "$mode" in
    auto)
        decode_cmd=("$makocode_bin" decode "$ppm_path" --output-dir "$decoded_dir")
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
            "$ppm_path"
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

artifacts_prefix="$repo_root/test/${label}"
mv "$payload" "${artifacts_prefix}_payload.bin"
mv "$ppm_path" "${artifacts_prefix}_encoded.ppm"
if [[ -f $decoded_payload ]]; then
    mv "$decoded_payload" "${artifacts_prefix}_decoded.bin"
fi

label_fmt=$(mako_format_label "$label")
printf '%s %bSUCCESS%b mode=%s\n' "$label_fmt" "$MAKO_PASS_COLOR" "$MAKO_RESET_COLOR" "$mode"
