#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for the full
# license text.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label="decode_scale_318"
if [[ ${1:-} == "--label" && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo "test_decode_scale_318: unexpected arguments" >&2
    exit 2
fi

work_dir="$repo_root/test_output/${label}_scale_318_work"
cleanup() {
    local status=$?
    if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then
        rm -rf "$work_dir"
    fi
}
trap cleanup EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf '0' > "$work_dir/payload.bin"

(
    cd "$work_dir"
    "$repo_root/makocode" encode \
        --input=payload.bin \
        --ecc=0 \
        --page-width=55 \
        --page-height=55 \
        --no-filename \
        --no-page-count \
        "--output-dir=$work_dir/encoded" \
        "--prefix=$label" >/dev/null
)

"$repo_root/test/ppm_transform" transform \
    --input "$work_dir/encoded/$label"*.ppm \
    --output "$work_dir/scaled.ppm" \
    --scale-x 3.18 \
    --scale-y 3.18

# Keep the scaled 55x55 page on the 190x190 white canvas used by the
# regression fixture. Centering a 175px render leaves a 7px border per side.
awk '
    NR == 1 { if ($0 != "P3") exit 2; next }
    /^[[:space:]]*#/ { next }
    {
        for (i = 1; i <= NF; ++i) {
            if (!have_width) { source_width = $i; have_width = 1; continue }
            if (!have_height) { source_height = $i; have_height = 1; continue }
            if (!have_maxval) { if ($i != 255) exit 2; have_maxval = 1; continue }
            pixels[++count] = $i
        }
    }
    END {
        if (!have_width || !have_height || !have_maxval || count != source_width * source_height * 3) exit 2
        canvas_width = 190
        canvas_height = 190
        offset_x = int((canvas_width - source_width) / 2)
        offset_y = int((canvas_height - source_height) / 2)
        if (source_width > canvas_width || source_height > canvas_height) exit 2
        print "P3"
        print canvas_width, canvas_height
        print 255
        for (y = 0; y < canvas_height; ++y) {
            for (x = 0; x < canvas_width; ++x) {
                if (x >= offset_x && x < offset_x + source_width &&
                    y >= offset_y && y < offset_y + source_height) {
                    source_index = ((y - offset_y) * source_width + (x - offset_x)) * 3
                    printf "%d %d %d\n", pixels[source_index + 1], pixels[source_index + 2], pixels[source_index + 3]
                } else {
                    print "255 255 255"
                }
            }
        }
    }
' "$work_dir/scaled.ppm" > "$work_dir/fixture.ppm"

if ! "$repo_root/makocode" decode \
    "--output-dir=$work_dir/decoded" \
    "$work_dir/fixture.ppm" >"$work_dir/decode.log" 2>&1; then
    cat "$work_dir/decode.log" >&2
    exit 1
fi
if [[ ! -f "$work_dir/decoded/payload.bin" ]] ||
   ! cmp --silent "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"; then
    echo "test_decode_scale_318: decoded payload did not match input" >&2
    exit 1
fi
printf 'decode scale 3.18 ok (label=%s)\n' "$label"
