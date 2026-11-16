#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software; you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=decode_partial_tag_scan
if [[ ${1:-} == --label && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo 'test_decode_partial_tag_scan: unexpected arguments' >&2
    exit 2
fi

work_dir=$(mktemp -d "$repo_root/test_output/${label}_work.XXXXXX")
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf 'partially damaged marker scan payload' > "$work_dir/payload.bin"

(
    cd "$work_dir"
    "$makocode_bin" encode --input=payload.bin --page-width=80 --page-height=80 \
        "--output-dir=$work_dir/encoded" --prefix=source >/dev/null
)

layout=$("$repo_root/test_output/bin/canonical_tile_harness" \
    "$work_dir/encoded/source.ppm" 80 80 1 --metadata-layout)
python3 - "$work_dir/encoded/source.ppm" "$work_dir/scan.ppm" "$layout" <<'PY'
from ppm_test_utils import read_ppm, write_ppm
import math
import sys

source_path, output_path = sys.argv[1:3]
square_x, square_y, square_side, prefix_bits, tag_y, symbol_count = map(int, sys.argv[3].split())
src_w, src_h, source = read_ppm(source_path)
assert (src_w, src_h) == (80, 80)
tile_x, tile_y = (src_w - 48) // 2, (src_h - 48) // 2
marker_bits = 0xD1BF3F45
logo_cells = []
for bit in range(32):
    x = tile_x + 20 + (bit & 7)
    y = tile_y + tag_y + 1 + bit // 8
    offset = (y * src_w + x) * 3
    luminance = 0.2126 * source[offset] + 0.7152 * source[offset + 1] + 0.0722 * source[offset + 2]
    if luminance < 128:
        logo_cells.append((bit, offset))
assert len(logo_cells) == 20

# Erase 16 known dark marker modules. Exactly half of the 32 expected bits
# remain detectable; all other metadata rows and cells stay intact.
for bit, offset in logo_cells[:16]:
    source[offset:offset + 3] = [255, 255, 255]
matches = 0
for bit in range(32):
    x = tile_x + 20 + (bit & 7)
    y = tile_y + tag_y + 1 + bit // 8
    offset = (y * src_w + x) * 3
    observed = source[offset] < 128
    expected = bool((marker_bits >> (31 - bit)) & 1)
    matches += observed == expected
assert matches == 16, matches

scale = 4.0
coverage = 4
out_w = out_h = 336
center_x = center_y = (out_w - 1) * 0.5
src_cx = src_cy = (src_w - 1) * 0.5
pixels = bytearray([255]) * (out_w * out_h * 3)
for y in range(out_h):
    for x in range(out_w):
        total = 0
        for sy in range(coverage):
            py = y + (sy + 0.5) / coverage - 0.5 - center_y
            for sx in range(coverage):
                px = x + (sx + 0.5) / coverage - 0.5 - center_x
                module_x = math.floor(src_cx + px / scale + 0.5)
                module_y = math.floor(src_cy + py / scale + 0.5)
                if 0 <= module_x < src_w and 0 <= module_y < src_h:
                    total += source[(module_y * src_w + module_x) * 3]
                else:
                    total += 255
        value = round(total / (coverage * coverage))
        offset = (y * out_w + x) * 3
        pixels[offset:offset + 3] = bytes((value, value, value))
write_ppm(output_path, out_w, out_h, pixels)
PY

if ! "$makocode_bin" decode --debug "--output-dir=$work_dir/decoded" \
    "$work_dir/scan.ppm" >"$work_dir/decode.log" 2>&1; then
    cat "$work_dir/decode.log" >&2
    echo 'test_decode_partial_tag_scan: partially damaged scan did not decode' >&2
    exit 1
fi
if ! grep -Fq 'partial MAKO/CODE marker recovered by metadata ECC' "$work_dir/decode.log"; then
    cat "$work_dir/decode.log" >&2
    echo 'test_decode_partial_tag_scan: relaxed marker path was not used' >&2
    exit 1
fi
cmp --silent "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"
printf 'partially damaged MAKO/CODE scan round-trip passed (label=%s)\n' "$label"
