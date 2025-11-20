#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_cli_output_dir.sh [--label NAME]

  --label NAME    Prefix for artifacts under test/ (default: cli_output_dir).
  --help          Show this message.
USAGE
}

label="cli_output_dir"
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
            echo "test_cli_output_dir: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_cli_output_dir: --label requires a value" >&2
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

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_cli_output_dir: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
work_dir="$test_dir/${label}_cli_work"
payload="$test_dir/${label}_cli_payload.bin"
payload_source_name="cli_payload.bin"
payload_work="$work_dir/$payload_source_name"
ppm_target="$test_dir/${label}_cli_payload_encoded.ppm"
decoded_payload="$test_dir/${label}_cli_payload_decoded.bin"

cleanup() {
    if [[ -d $work_dir ]]; then
        rm -rf "$work_dir"
    fi
}
trap cleanup EXIT

rm -rf "$work_dir"
mkdir -p "$test_dir" "$work_dir"
rm -f "$payload" "$ppm_target" "$decoded_payload"

printf '%s' "encode-decode-cli test payload" > "$payload"
cp "$payload" "$payload_work"
encode_cmd=(
    "$makocode_bin" encode
    "--input=$payload_source_name"
    --ecc=0.5
    --page-width=100
    --page-height=100
    --no-filename
    --no-page-count
    "--output-dir=$work_dir"
)
print_makocode_cmd "encode" "${encode_cmd[@]}"
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppms=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_cli_output_dir: encode did not emit a PPM" >&2
    exit 1
fi
ppm_path="${ppms[0]}"
mv -f "$ppm_path" "$ppm_target"
decode_dir="$work_dir/decoded"
mkdir -p "$decode_dir"
decode_cmd=("$makocode_bin" decode "$ppm_target" --output-dir "$decode_dir")
print_makocode_cmd "decode" "${decode_cmd[@]}"
"${decode_cmd[@]}" >/dev/null

decoded_source="$decode_dir/$payload_source_name"
if [[ ! -f $decoded_source ]]; then
    echo "test_cli_output_dir: decode missing cli_payload.bin" >&2
    exit 1
fi
cmp --silent "$payload" "$decoded_source"
mv "$decoded_source" "$decoded_payload"

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS CLI output-dir workflow\n' "$label_fmt"
