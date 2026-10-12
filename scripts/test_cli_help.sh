#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_cli_help.sh [--label NAME]

  --label NAME    Prefix for output labels (default: cli_help).
  --help          Show this message.
USAGE
}

label="cli_help"
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
            echo "test_cli_help: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_cli_help: --label requires a value" >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_cli_help: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/${label}_help.XXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT

run_expect_help() {
    local phase=$1
    local expected_first=$2
    local expected_second=$3
    shift 3
    local output="$tmp_dir/$phase.txt"
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    printf '%s %s: ' "$label_fmt" "$phase"
    printf '%q ' "$@"
    printf '\n'

    local status=0
    if "$@" >"$output" 2>&1; then
        status=0
    else
        status=$?
    fi
    if [[ $status -ne 0 ]]; then
        printf 'test_cli_help: %s exited %d\n' "$phase" "$status" >&2
        cat "$output" >&2
        return 1
    fi
    if ! grep -Fq -- "$expected_first" "$output" ||
       ! grep -Fq -- "$expected_second" "$output"; then
        printf 'test_cli_help: %s output is missing expected help text\n' "$phase" >&2
        cat "$output" >&2
        return 1
    fi
}

run_expect_help top-level 'Usage:' 'makocode encode [options]' "$makocode_bin"
run_expect_help encode-help 'Usage: makocode encode' '--input PATH' "$makocode_bin" encode --help
run_expect_help decode-help 'Usage: makocode decode' 'stdin' "$makocode_bin" decode --help
run_expect_help overlay-help 'Usage: makocode overlay' '--overlay-ecc-target TARGET' "$makocode_bin" overlay --help
run_expect_help minify-help 'Usage: makocode minify' 'Strips comments/whitespace' "$makocode_bin" minify --help

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS CLI usage and command help\n' "$label_fmt"
