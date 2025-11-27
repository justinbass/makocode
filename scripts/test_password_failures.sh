#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_password_failures.sh [--label NAME]

  --label NAME    Artifact prefix under test/ (default: password_failures).
  --help          Show this help message.
USAGE
}

label="password_failures"
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
            echo "test_password_failures: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_password_failures: --label requires a value" >&2
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

run_expect_failure() {
    local phase=$1
    shift
    print_labelled "$phase" "$@"
    set +e
    "$@" >/dev/null 2>&1
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        echo "test_password_failures: command unexpectedly succeeded for $phase" >&2
        exit 1
    fi
}

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_password_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
work_dir="$test_dir/${label}_password_work"
payload="$test_dir/${label}_payload.bin"
ppm_target="$test_dir/${label}_encoded.ppm"

cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -ne 0 ]]; then
        return
    fi
    if [[ -d $work_dir ]]; then
        rm -rf "$work_dir"
    fi
}
on_exit() {
    local exit_code=$?
    cleanup "$exit_code"
}
trap 'on_exit' EXIT

rm -rf "$work_dir"
mkdir -p "$work_dir"
rm -f "$payload" "$ppm_target"

head -c 32768 /dev/urandom > "$payload"
password="suite-password"

encode_cmd=(
    "$makocode_bin" encode
    "--input=$payload"
    --ecc=0.5
    --page-width=720
    --page-height=720
    "--password=$password"
    "--output-dir=$work_dir"
)
print_labelled "encode" "${encode_cmd[@]}"
"${encode_cmd[@]}" >/dev/null

shopt -s nullglob
ppms=("$work_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_password_failures: encode emitted no PPMs" >&2
    exit 1
fi
ppm_path="${ppms[0]}"
mv -f "$ppm_path" "$ppm_target"

decoded_no_pw="$work_dir/decoded_no_pw"
decoded_wrong_pw="$work_dir/decoded_wrong_pw"
mkdir -p "$decoded_no_pw" "$decoded_wrong_pw"

run_expect_failure "decode-missing-password" \
    "$makocode_bin" decode "$ppm_target" --output-dir "$decoded_no_pw"

run_expect_failure "decode-wrong-password" \
    "$makocode_bin" decode "$ppm_target" --output-dir "$decoded_wrong_pw" --password=bad-password

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS password failures enforced\n' "$label_fmt"
