#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
# No warranty; see LICENSE.

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_fiducials.sh [--label CASE_ID]

  --label CASE_ID  Prefix for artifacts under test_output/ (default: fiducials).
  --help           Show this message.
USAGE
}

label="fiducials"
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
            printf 'test_fiducials: unknown flag %s\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_fiducials: invalid --label' >&2
    exit 1
fi

makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    printf 'test_fiducials: makocode binary not found at %s\n' "$makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir=$(mktemp -d "$test_dir/${label}_fiducials_work.XXXXXX")
pages_dir="$work_dir/pages"
decoded_dir="$work_dir/decoded"
payload="$work_dir/payload.bin"
encoded_ppm="$pages_dir/${label}_fiducials.ppm"
encode_csv="$pages_dir/${label}_fiducials_encode_fiducials.csv"
decoded_payload="$decoded_dir/payload.bin"

mkdir -p "$pages_dir" "$decoded_dir"
printf '\000fiducials custom grid payload\377\n' > "$payload"

encode_cmd=(
    "$makocode_bin" encode
    --input=payload.bin

    --page-width=240
    --page-height=240
    --fiducial-spacing=30
    "--prefix=${label}_fiducials"
    "--output-dir=$pages_dir"
    --debug
)
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

if [[ ! -f $encoded_ppm ]]; then
    printf 'test_fiducials: encode did not produce %s\n' "$encoded_ppm" >&2
    exit 1
fi
if [[ ! -f $encode_csv ]]; then
    echo 'test_fiducials: encode did not write fiducial CSV' >&2
    exit 1
fi

# The hardcoded 2x2 marker grid is centered on the 240-pixel page;
# 30-pixel spacing puts the first two marker centers at 29.5 and 59.5.
if ! awk -F, '
    NR == 2 { exit !($1 == 1 && $2 == 0 && $3 == 0 && $4 == "29.500" && $5 == "29.500") }
' "$encode_csv"; then
    echo 'test_fiducials: first marker is not centered on the page' >&2
    exit 1
fi
if ! awk -F, '
    NR == 3 { exit !($1 == 1 && $2 == 0 && $3 == 1 && $4 == "59.500" && $5 == "29.500") }
' "$encode_csv"; then
    echo 'test_fiducials: second marker does not match configured spacing' >&2
    exit 1
fi

if ! python3 - "$script_dir" "$encoded_ppm" <<'PYTHON'
import sys
sys.path.insert(0, sys.argv[1])
from ppm_test_utils import read_ppm
width, height, pixels = read_ppm(sys.argv[2])
marker = 0
for y in range(29, 31):
    for x in range(29, 31):
        index = (y * width + x) * 3
        marker += pixels[index:index + 3] == [255, 255, 255]
raise SystemExit(0 if width >= 31 and height >= 31 and marker == 4 else 1)
PYTHON
then
    echo 'test_fiducials: expected the first grayscale-page marker to use white' >&2
    exit 1
fi

"$makocode_bin" decode "$encoded_ppm" "--output-dir=$decoded_dir" >/dev/null
if [[ ! -f $decoded_payload ]] || ! cmp --silent "$payload" "$decoded_payload"; then
    echo 'test_fiducials: decoded payload differs from encoded payload' >&2
    exit 1
fi
retained_ppm="$work_dir/${label}_fiducials_page.ppm"
cp "$encoded_ppm" "$retained_ppm"

printf '%s SUCCESS custom fiducial grid and payload round-trip\n' "$(mako_format_label "$label")"
