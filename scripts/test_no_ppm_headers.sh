#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"
repo_root=$(cd -- "$script_dir/.." && pwd -P)

usage() {
    cat <<'USAGE'
Usage: test_no_ppm_headers.sh [--label NAME]

Scans every .ppm under test/ and fails if any file contains a PPM comment/header
line (a line starting with '#', optionally preceded by whitespace).

USAGE
}

label="no_ppm_headers"
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
            echo "test_no_ppm_headers: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_no_ppm_headers: --label requires a value" >&2
    exit 1
fi

test_dir="$repo_root/test"
if [[ ! -d $test_dir ]]; then
    echo "test_no_ppm_headers: missing test dir: $test_dir" >&2
    exit 1
fi

label_fmt=$(mako_format_label "$label")

ppm_files=()
while IFS= read -r path; do
    ppm_files+=("$path")
done < <(find "$test_dir" -type f -name '*.ppm' -print | LC_ALL=C sort)
if [[ ${#ppm_files[@]} -eq 0 ]]; then
    echo "test_no_ppm_headers: no .ppm files found under $test_dir" >&2
    exit 1
fi

failed=0
for path in "${ppm_files[@]}"; do
    # We only care about header-style comments; these should appear very early in the file.
    # Read a small prefix to keep the scan fast even for huge PPMs.
    if hit=$(LC_ALL=C sed -n '1,256p' "$path" | grep -n -m 1 -E '^[[:space:]]*#' || true); then
        if [[ -n $hit ]]; then
            printf '%s %bFAIL%b %s contains PPM header comment: %s\n' \
                "$label_fmt" "$MAKO_FAIL_COLOR" "$MAKO_RESET_COLOR" "$path" "$hit" >&2
            failed=1
            break
        fi
    fi
done

if [[ $failed -ne 0 ]]; then
    exit 1
fi

printf '%s %bPASS%b (%d files)\n' "$label_fmt" "$MAKO_PASS_COLOR" "$MAKO_RESET_COLOR" "${#ppm_files[@]}"
