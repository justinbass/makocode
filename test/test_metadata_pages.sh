#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform="$repo_root/test_output/bin/ppm_transform"
work=$(mktemp -d "$repo_root/test_output/0119_metadata_pages_work.XXXXXX")
seed=${MAKO_TEST_SEED:-721}
payload_size=3000

run_case() {
    local label=$1
    local selector=$2
    local expected_pages=$3
    local report="$work/$label.report"

    "$ppm_transform" bytes --output - --size "$payload_size" --seed "$seed" |
        "$bin" encode --output-dir="$work" --prefix="$label" \
            --page-width=220 --page-height=220 --ecc=3.5 \
            "--metadata-pages=$selector" >/dev/null

    for page in 1 2 3; do
        test -f "$work/${label}_page_000${page}.ppm"
    done

    "$bin" decode --decoded-stdout \
        "$work/${label}_page_0001.ppm" \
        "$work/${label}_page_0002.ppm" \
        "$work/${label}_page_0003.ppm" \
        >"$report" 2>"$work/$label.decode.log"

    python3 - "$report" "$expected_pages" <<'PY'
import sys

report_path, expected_text = sys.argv[1:]
expected = [int(page) for page in expected_text.split(",")]
actual = []
current_page = None
with open(report_path, "r", encoding="ascii") as report:
    for line in report:
        line = line.rstrip("\n")
        if line.startswith("page "):
            current_page = int(line.split()[1])
        elif line.startswith("metadata tile_schema "):
            actual.append(current_page)
if actual != expected:
    raise SystemExit(f"metadata pages mismatch: expected {expected}, got {actual}")
PY

    python3 "$script_dir/check_decoded_stdout.py" \
        --generator "$ppm_transform" --size "$payload_size" --seed "$seed" \
        --label "$label" <"$report"
}

run_case metadata_middle middle 2
run_case metadata_one 1 1
run_case metadata_range 2-3 2,3
run_case metadata_list 1,3 1,3
run_case metadata_last last 3

printf 'metadata page selection: selected-page tiles and all three payload pages round-tripped\n'
