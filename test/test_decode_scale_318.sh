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

work_dir=$(mktemp -d "$repo_root/test_output/${label}_scale_318_work.XXXXXX")
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf '0' > "$work_dir/payload.bin"

(
    cd "$work_dir"
    "$repo_root/makocode" encode \
        --input=payload.bin \
        --ecc=0.25 \
        --page-width=160 \
        --page-height=160 \
        "--output-dir=$work_dir/encoded" \
        "--prefix=$label" >/dev/null
)

"$repo_root/test/ppm_transform" transform \
    --input "$work_dir/encoded/$label"*.ppm \
    --output "$work_dir/scaled.ppm" \
    --scale-x 3.18 \
    --scale-y 3.18

# Keep the binary render at its exact scaled dimensions.
cp "$work_dir/scaled.ppm" "$work_dir/fixture.ppm"

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
