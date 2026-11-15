#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform="$repo_root/test/ppm_transform"
work=$(mktemp -d "$repo_root/test_output/0111_decode_missing_pages_work.XXXXXX")
seed=2110

"$ppm_transform" bytes --output - --size 32768 --seed "$seed" |
    "$bin" encode --output-dir="$work" --prefix=page --page-width=400 --page-height=400 \
        --ecc=0.8 >/dev/null

shopt -s nullglob
pages=("$work"/page*.ppm)
shopt -u nullglob
if [[ ${#pages[@]} -ne 4 ]]; then
    echo "decode missing pages: expected four encoded pages, found ${#pages[@]}" >&2
    exit 1
fi

check_pages() {
    local label=$1
    shift
    "$bin" decode --decoded-stdout "$@" 2>"$work/$label.log" |
        python3 "$script_dir/check_decoded_stdout.py" --generator "$ppm_transform" \
            --size 32768 --seed "$seed" --label "$label"
}

check_pages missing_middle "${pages[0]}" "${pages[2]}" "${pages[3]}"
check_pages missing_middle_shuffled "${pages[3]}" "${pages[0]}" "${pages[2]}"
check_pages missing_first "${pages[1]}" "${pages[2]}" "${pages[3]}"
check_pages missing_last "${pages[0]}" "${pages[1]}" "${pages[2]}"

mkdir -p "$work/stdin_pages"
stdin_seed=2111
"$ppm_transform" bytes --output - --size 8192 --seed "$stdin_seed" |
    "$bin" encode --output-dir="$work/stdin_pages" --prefix=stdin \
        --page-width=400 --page-height=400 --ecc=2.0 >/dev/null
stdin_page=$(find "$work/stdin_pages" -maxdepth 1 -type f -name '*0001.ppm' -print -quit)
if [[ -z $stdin_page ]]; then
    echo "decode missing pages: two-page stdin fixture was not encoded" >&2
    exit 1
fi
cat "$stdin_page" | "$bin" decode --decoded-stdout 2>"$work/missing_stdin_last.log" |
    python3 "$script_dir/check_decoded_stdout.py" --generator "$ppm_transform" \
        --size 8192 --seed "$stdin_seed" --label missing_stdin_last

printf 'decode missing pages: metadata-indexed leading, middle, and trailing pages recovered\n'
