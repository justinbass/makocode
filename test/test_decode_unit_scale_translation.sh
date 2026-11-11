#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
edge_bleed=0
optical_blur=0
label=decode_unit_scale_translation
if [[ ${1:-} == --label ]]; then label=$2; shift 2; fi
if [[ ${1:-} == --edge-bleed ]]; then edge_bleed=1; shift; fi
if [[ ${1:-} == --optical-blur ]]; then optical_blur=1; shift; fi
[[ $# == 0 ]]
work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
mkdir -p "$work_dir/encoded" "$work_dir/control" "$work_dir/decoded"
printf makocode > "$work_dir/payload.bin"
(cd "$work_dir"; "$makocode_bin" encode --input=payload.bin --ecc=0 \
    --page-width=80 --page-height=80 \
    "--output-dir=$work_dir/encoded" --prefix=source >/dev/null)
python3 - "$work_dir/encoded/source.ppm" "$work_dir/scan.ppm" "$edge_bleed" "$optical_blur" <<'PYTHON'
from pathlib import Path
from ppm_test_utils import read_ppm, write_ppm
import sys
source_width, source_height, pixels = read_ppm(sys.argv[1])
assert (source_width, source_height) == (80, 80)
assert len(pixels) == 80 * 80 * 3
# Unit-scale scan: off-center page, padding and reduced ink/paper contrast.
# The even-sided metadata tile has a half-pixel center on this canvas.
offset_x, offset_y = (2, 3) if sys.argv[3] == '1' else (3, 4)
canvas = [234] * (85 * 85 * 3)
for y in range(80):
    for x in range(80):
        value = 30 + round(0.8 * pixels[(y * 80 + x) * 3])
        target = ((y + offset_y) * 85 + x + offset_x) * 3
        canvas[target:target + 3] = [value] * 3
        assert (value < 128) == (pixels[(y * 80 + x) * 3] < 128)
if sys.argv[3] == '1':
    # Ink bleed outside the page expands both dark bounds by one pixel.
    # Every source module remains untouched and readable.
    for x, y in [(82, y) for y in range(20, 23)] + [(x, 83) for x in range(20, 23)]:
        assert x >= offset_x + 80 or y >= offset_y + 80
        canvas[(y * 85 + x) * 3:(y * 85 + x) * 3 + 3] = [30] * 3
if sys.argv[4] == '1':
    # Simulate ink/paper contrast loss and a symmetric optical point spread.
    ink = [80 + round(value * 154 / 255) for value in canvas]
    for y in range(1, 84):
        for x in range(1, 84):
            i = (y * 85 + x) * 3
            value = round(.65 * ink[i] + .0875 * sum(
                ink[j] for j in (i - 3, i + 3, i - 180, i + 180)))
            canvas[i:i+3] = [value] * 3
write_ppm(sys.argv[2], 85, 85, canvas)
PYTHON
"$makocode_bin" decode "--output-dir=$work_dir/control" "$work_dir/encoded/source.ppm" >"$work_dir/control.log" 2>&1
cmp "$work_dir/payload.bin" "$work_dir/control/payload.bin"
if ! "$makocode_bin" decode "--output-dir=$work_dir/decoded" "$work_dir/scan.ppm" >"$work_dir/decode.log" 2>&1; then
    cat "$work_dir/decode.log" >&2
    exit 1
fi
cmp "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"
# Match the complete user-facing decoded stream, including metadata and archive.
"$makocode_bin" decode --decoded-stdout "$work_dir/encoded/source.ppm" >"$work_dir/expected.txt" 2>"$work_dir/stdout_control.log"
"$makocode_bin" decode --decoded-stdout "$work_dir/scan.ppm" >"$work_dir/actual.txt" 2>"$work_dir/stdout_scan.log"
cmp "$work_dir/expected.txt" "$work_dir/actual.txt"
