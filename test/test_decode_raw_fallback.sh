#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
test_dir="$repo_root/test_output"
label="decode_raw_fallback"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            if [[ $# -lt 2 || -z $2 ]]; then
                echo 'test_decode_raw_fallback: --label requires a value' >&2
                exit 1
            fi
            label=$2
            shift 2
            ;;
        --help)
            printf 'Usage: %s [--label NAME]\n' "$(basename "$0")"
            exit 0
            ;;
        *)
            printf 'test_decode_raw_fallback: unknown flag %s\n' "$1" >&2
            exit 1
            ;;
    esac
done
if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_decode_raw_fallback: invalid --label' >&2
    exit 1
fi
work_dir="$test_dir/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}

if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_raw_fallback: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

rm -rf "$work_dir"
mkdir -p "$work_dir/disabled" "$work_dir/enabled"
trap 'status=$?; if [[ $status -eq 0 ]]; then rm -rf "$work_dir"; fi' EXIT

harness_bin="$work_dir/raw_fallback_harness"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -pedantic -O2 \
    "$script_dir/test_decode_raw_fallback_harness.cpp" -o "$harness_bin"

payload="$work_dir/raw_payload.bin"
ppm="$work_dir/raw_payload.ppm"
printf 'raw decoded bytes\000\377\nnot a MakoCode archive' > "$payload"
"$harness_bin" "$payload" "$ppm"

if env -u MAKOCODE_DECODE_RAW_FALLBACK "$makocode_bin" decode "$ppm" \
    "--output-dir=$work_dir/disabled" >/dev/null 2>&1; then
    echo 'test_decode_raw_fallback: decode unexpectedly succeeded with fallback disabled' >&2
    exit 1
fi
if [[ -e "$work_dir/disabled/payload.bin" ]]; then
    echo 'test_decode_raw_fallback: fallback wrote payload.bin while disabled' >&2
    exit 1
fi

MAKOCODE_DECODE_RAW_FALLBACK=1 "$makocode_bin" decode "$ppm" \
    "--output-dir=$work_dir/enabled" >/dev/null
if [[ ! -f "$work_dir/enabled/payload.bin" ]] ||
    ! cmp --silent "$payload" "$work_dir/enabled/payload.bin"; then
    echo 'test_decode_raw_fallback: enabled fallback payload.bin mismatch' >&2
    exit 1
fi

printf 'SUCCESS raw decode fallback is gated and writes the recovered payload\n'
