#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=${1:-decode_current_metadata}
work_dir="$repo_root/test_output/${label}_work"
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf 'x' > "$work_dir/payload.bin"
(cd "$work_dir" && "$repo_root/makocode" encode --input=payload.bin --ecc=0 \
    --page-width=80 --page-height=80 --no-filename --no-page-count \
    --output-dir="$work_dir/encoded" --prefix=page >/dev/null)
python3 - "$work_dir/encoded/page.ppm" "$work_dir/damaged.ppm" <<'PY'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text().split()
assert source[:4] == ['P3', '80', '80', '255']
pixels = list(map(int, source[4:]))
assert len(pixels) == 80 * 80 * 3
tile_x = tile_y = (80 - 48) // 2
# The first payload module is inside the protected tile body, after the border
# and five repeated header rows. Flip that encoded module to exercise tile RS.
x = tile_x + 1
y = tile_y + 1 + 5
index = (y * 80 + x) * 3
original = pixels[index:index + 3]
assert original in ([0, 0, 0], [255, 255, 255])
pixels[index:index + 3] = [255 - value for value in original]
assert pixels[index:index + 3] != original
with open(sys.argv[2], 'w') as out:
    out.write('P3\n80 80\n255\n')
    for offset in range(0, len(pixels), 3):
        out.write(f'{pixels[offset]} {pixels[offset + 1]} {pixels[offset + 2]}\n')
PY
"$repo_root/makocode" decode --output-dir="$work_dir/decoded" \
    "$work_dir/damaged.ppm" >"$work_dir/decode.log" 2>&1 || {
        cat "$work_dir/decode.log" >&2
        exit 1
    }
cmp "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"

# Without the protected center tile, canonical stream alignment and length
# cannot be established. Corrupt the full tile and require a clean decode error.
python3 - "$work_dir/encoded/page.ppm" "$work_dir/no_bootstrap.ppm" <<'PY'
from pathlib import Path
import sys

tokens = Path(sys.argv[1]).read_text().split()
width, height = int(tokens[1]), int(tokens[2])
pixels = list(map(int, tokens[4:]))
tile_x = tile_y = (width - 48) // 2
for y in range(tile_y, tile_y + 48):
    for x in range(tile_x, tile_x + 48):
        index = (y * width + x) * 3
        pixels[index:index + 3] = [127, 127, 127]
with open(sys.argv[2], 'w') as out:
    out.write(f'P3\n{width} {height}\n255\n')
    for offset in range(0, len(pixels), 3):
        out.write(f'{pixels[offset]} {pixels[offset + 1]} {pixels[offset + 2]}\n')
PY
if "$repo_root/makocode" decode --output-dir="$work_dir/no_bootstrap_out" \
    "$work_dir/no_bootstrap.ppm" >"$work_dir/no_bootstrap.log" 2>&1; then
    echo 'test_decode_current_metadata: decode accepted a page without canonical bootstrap metadata' >&2
    exit 1
fi
if ! grep -Fq 'metadata tile missing; aborting' "$work_dir/no_bootstrap.log" &&
   ! grep -Fq 'invalid canonical metadata tile' "$work_dir/no_bootstrap.log"; then
    cat "$work_dir/no_bootstrap.log" >&2
    echo 'test_decode_current_metadata: missing-tile failure was not explicit' >&2
    exit 1
fi
