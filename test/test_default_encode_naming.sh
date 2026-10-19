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

label="default_encode_naming"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --help)
            printf 'Usage: %s [--label NAME]\n' "$(basename "$0")"
            exit 0
            ;;
        *)
            printf 'test_default_encode_naming: unknown flag %s\n' "$1" >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_default_encode_naming: invalid --label' >&2
    exit 1
fi

makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    printf 'test_default_encode_naming: makocode binary not found at %s\n' "$makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_work"
pages_dir="$work_dir/pages"
payload="$work_dir/payload.bin"

cleanup() {
    local exit_code=${1:-0}
    if [[ $exit_code -eq 0 ]]; then
        rm -rf "$work_dir"
    fi
}
on_exit() {
    local exit_code=$?
    cleanup "$exit_code"
}
trap on_exit EXIT

rm -rf "$work_dir"
mkdir -p "$pages_dir"
printf '\000default timestamp naming payload\377\n' > "$payload"

encode_cmd=(
    "$makocode_bin" encode
    --input=payload.bin
    --ecc=0
    --page-width=160
    --page-height=160
    --palette='Cyan Black'
    --no-filename
    --no-page-count
    "--output-dir=$pages_dir"
    --debug
)
(
    cd "$work_dir"
    "${encode_cmd[@]}"
) >/dev/null

shopt -s nullglob
ppms=("$pages_dir"/*.ppm)
csvs=("$pages_dir"/*_encode_fiducials.csv)
shopt -u nullglob
if [[ ${#ppms[@]} -ne 1 ]]; then
    printf 'test_default_encode_naming: expected one timestamped PPM; found %d\n' "${#ppms[@]}" >&2
    printf '  %s\n' "${ppms[@]}" >&2
    exit 1
fi
if [[ ${#csvs[@]} -ne 1 ]]; then
    printf 'test_default_encode_naming: expected one timestamped fiducial CSV; found %d\n' "${#csvs[@]}" >&2
    printf '  %s\n' "${csvs[@]}" >&2
    exit 1
fi

ppm_name=${ppms[0]##*/}
csv_name=${csvs[0]##*/}
if [[ ! $ppm_name =~ ^[0-9]{8}T[0-9]{6}Z\.ppm$ ]]; then
    printf 'test_default_encode_naming: unexpected default PPM name: %s\n' "$ppm_name" >&2
    exit 1
fi
expected_csv="${ppm_name%.ppm}_encode_fiducials.csv"
if [[ $csv_name != "$expected_csv" ]]; then
    printf 'test_default_encode_naming: expected fiducial CSV %s; found %s\n' "$expected_csv" "$csv_name" >&2
    exit 1
fi

printf 'SUCCESS default UTC timestamp PPM and matching fiducial CSV names (%s)\n' "${ppm_name%.ppm}"
