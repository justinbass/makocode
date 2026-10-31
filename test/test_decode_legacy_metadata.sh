#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail
script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
label=${2:-decode_legacy_metadata}
work_dir="$repo_root/test_output/${label}_work"
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT
rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/decoded"
printf 'legacy metadata roundtrip' > "$work_dir/payload.bin"
(cd "$work_dir" && "$repo_root/makocode" encode --input=payload.bin --ecc=0 --page-width=55 --page-height=55 --no-filename --no-page-count --output-dir="$work_dir/encoded" --prefix=page >/dev/null)
"${CXX:-c++}" -std=c++17 -O2 "$script_dir/test_legacy_metadata_fixture.cpp" -o "$work_dir/fixture"
"$work_dir/fixture" "$work_dir/encoded/page.ppm" "$work_dir/legacy.ppm"
"$repo_root/makocode" decode --output-dir="$work_dir/decoded" "$work_dir/legacy.ppm" >"$work_dir/decode.log" 2>&1 || { cat "$work_dir/decode.log"; exit 1; }
cmp "$work_dir/payload.bin" "$work_dir/decoded/payload.bin"
