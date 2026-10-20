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
label="decode_parse_retry"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            if [[ $# -lt 2 || -z $2 ]]; then
                echo 'test_decode_parse_retry: --label requires a value' >&2
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
            printf 'test_decode_parse_retry: unknown flag %s\n' "$1" >&2
            exit 1
            ;;
    esac
done
if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_decode_parse_retry: invalid --label' >&2
    exit 1
fi

work_dir="$test_dir/${label}_work"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_decode_parse_retry: makocode binary not found at $makocode_bin" >&2
    exit 1
fi
ppm_transform_bin="$repo_root/test/ppm_transform"
if [[ ! -x $ppm_transform_bin ]]; then
    echo "test_decode_parse_retry: ppm_transform helper not found at $ppm_transform_bin" >&2
    exit 1
fi

rm -rf "$work_dir"
mkdir -p "$work_dir/encoded" "$work_dir/output"
trap 'status=$?; if [[ $status -eq 0 ]]; then rm -rf "$work_dir"; fi' EXIT

payload="$work_dir/payload.bin"
encoded_ppm="$work_dir/encoded/${label}.ppm"
damaged_ppm="$work_dir/invalid_stream.ppm"
decode_log="$work_dir/decode.log"
"$ppm_transform_bin" bytes --output "$payload" --size 8192 --seed 1337
"$makocode_bin" encode \
    "--input=$payload" \
    --ecc=0 \
    --page-width=500 \
    --page-height=500 \
    --no-filename \
    --no-page-count \
    "--prefix=$label" \
    "--output-dir=$work_dir/encoded" >/dev/null
"$ppm_transform_bin" corrupt-footer-valid-data-too-corrupt \
    --input "$encoded_ppm" \
    --output "$damaged_ppm" \
    --border-keep 80 \
    --seed 1337

decode_status=0
if "$makocode_bin" decode "$damaged_ppm" "--output-dir=$work_dir/output" >"$decode_log" 2>&1; then
    decode_status=0
else
    decode_status=$?
fi
if [[ $decode_status -eq 0 ]]; then
    echo 'test_decode_parse_retry: decode unexpectedly succeeded' >&2
    cat "$decode_log" >&2
    exit 1
fi
if [[ $(grep -Fxc 'decode: parse failure; retrying without fiducial subgrid' "$decode_log") -ne 1 ]]; then
    echo 'test_decode_parse_retry: generic parse retry was not reached' >&2
    cat "$decode_log" >&2
    exit 1
fi
if [[ $(grep -Fxc 'decode: parse failure' "$decode_log") -ne 1 ]]; then
    echo 'test_decode_parse_retry: terminal parse failure was not reached' >&2
    cat "$decode_log" >&2
    exit 1
fi

printf 'SUCCESS generic parse failure retries once, then fails terminally\n'
