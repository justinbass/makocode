#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_decode_failures.sh [--label NAME]

  --label NAME    Identifier for log messages (default: decode_failures).
  --help          Show this help message.
USAGE
}

label="decode_failures"
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
            echo "test_decode_failures: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_decode_failures: --label requires a value" >&2
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

run_expect_failure() {
    local phase=$1
    shift
    print_makocode_cmd "$phase" "$@"
    set +e
    "$@" >/dev/null 2>&1
    local status=$?
    set -e
    if [[ $status -eq 0 ]]; then
        echo "test_decode_failures: decoder unexpectedly succeeded for $phase" >&2
        exit 1
    fi
}

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_failures: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

mkdir -p "$repo_root/test"
tmp_dir=$(mktemp -d "$repo_root/test/decode_fail_tmp.XXXXXX")
cleanup() {
    if [[ -d $tmp_dir ]]; then
        rm -rf "$tmp_dir"
    fi
}
trap cleanup EXIT

wrong_depth="$tmp_dir/wrong_depth.ppm"
cat > "$wrong_depth" <<'PPM'
P3
2 2
42
0 0 0  0 0 0
0 0 0  0 0 0
PPM

invalid_magic="$tmp_dir/invalid_magic.ppm"
cat > "$invalid_magic" <<'PPM'
P6
2 2
255
PPM

run_expect_failure "decode-wrong-depth" "$makocode_bin" decode "$wrong_depth"
run_expect_failure "decode-invalid-magic" "$makocode_bin" decode "$invalid_magic"

label_fmt=$(mako_format_label "$label")
printf '%s %bSUCCESS%b decode failures rejected as expected\n' "$label_fmt" "$MAKO_PASS_COLOR" "$MAKO_RESET_COLOR"
