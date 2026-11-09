#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License, either version 3 of the License, or (at your
# option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label="decode_affine_cell_sampling"
if [[ ${1:-} == "--label" && -n ${2:-} ]]; then
    label=$2
    shift 2
fi
if [[ $# -ne 0 ]]; then
    echo "test_decode_affine_cell_sampling: unexpected arguments" >&2
    exit 2
fi

work_dir="$repo_root/test_output/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_affine_cell_sampling: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_decode_affine_cell_sampling: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

cleanup() {
    local status=$?
    if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then
        rm -rf "$work_dir"
    fi
}
trap cleanup EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf 'makocode' > "$work_dir/makocode.bin"

(
    cd "$work_dir"
    "$makocode_bin" encode \
        --input=makocode.bin \
        --ecc=0 \
        --ecc-fill \
        --page-width=80 \
        --page-height=80 \
        --no-filename \
        --no-page-count \
        "--output-dir=$work_dir/encoded" \
        "--prefix=$label" >/dev/null
)

"$ppm_transform_bin" transform \
    --input "$work_dir/encoded/$label.ppm" \
    --output "$work_dir/scaled.ppm" \
    --scale-x 3.2363636363636363 \
    --scale-y 3.2363636363636363

# Put the 259x259 binary render at (7, 6) on the white canvas.
python3 "$script_dir/ppm_pad.py" "$work_dir/scaled.ppm" "$work_dir/fixture.ppm" 278 278 7 6

if ! "$makocode_bin" decode \
    "--output-dir=$work_dir/decoded" \
    "$work_dir/fixture.ppm" >"$work_dir/decode.log" 2>&1; then
    cat "$work_dir/decode.log" >&2
    exit 1
fi
if [[ ! -f "$work_dir/decoded/makocode.bin" ]] ||
   ! cmp --silent "$work_dir/makocode.bin" "$work_dir/decoded/makocode.bin"; then
    echo "test_decode_affine_cell_sampling: decoded payload did not match input" >&2
    exit 1
fi
printf 'decode affine cell sampling ok (label=%s)\n' "$label"
