#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See
# the LICENSE file for the full license text.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
. "$script_dir/lib/colors.sh"
. "$script_dir/lib/fiducials.sh"

usage() {
    cat <<'USAGE'
Usage: test_archive_tree_roundtrip.sh [--label NAME]

  --label NAME    Prefix for artifacts under test_output/ (default: archive_tree_roundtrip).
  --help          Show this message.
USAGE
}

label="archive_tree_roundtrip"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_archive_tree_roundtrip: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_archive_tree_roundtrip: --label requires a value" >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_archive_tree_roundtrip: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_archive_work"
source_dir="$work_dir/source"
page_dir="$work_dir/pages"
decoded_dir="$work_dir/decoded"
if [[ $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo "test_archive_tree_roundtrip: label contains unsupported characters" >&2
    exit 1
fi

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
trap 'on_exit' EXIT

mkdir -p "$test_dir"
rm -rf "$work_dir"
rm -f "$test_dir/${label}_archive_tree_encoded_"*.ppm
mkdir -p \
    "$source_dir/tree/empty" \
    "$source_dir/tree/nested/deep_empty" \
    "$source_dir/empty_only/nested/leaf_empty" \
    "$page_dir"
printf 'x' > "$source_dir/tree/root.txt"
printf 'y' > "$source_dir/tree/nested/leaf.txt"
: > "$source_dir/tree/zero.bin"

encode_cmd=(
    "$makocode_bin" encode
    --input=tree
    --input=empty_only
    --ecc=0
    --page-width=400
    --page-height=240
    --no-filename
    --no-page-count
    --prefix=archive_tree
    --debug
    "--output-dir=$page_dir"
)
(
    cd "$source_dir"
    "${encode_cmd[@]}"
) >/dev/null
encode_csv="$page_dir/archive_tree_encode_fiducials.csv"
if [[ ! -f $encode_csv ]]; then
    echo "test_archive_tree_roundtrip: encode did not write fiducial CSV" >&2
    exit 1
fi
cp "$encode_csv" "$test_dir/${label}_encode_fiducials.csv"

shopt -s nullglob
ppms=("$page_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_archive_tree_roundtrip: encode did not emit a PPM" >&2
    exit 1
fi

mkdir -p "$decoded_dir"
decode_cmd=("$makocode_bin" decode "--output-dir=$decoded_dir" --debug "${ppms[@]}")
"${decode_cmd[@]}" >/dev/null
decode_csv="$decoded_dir/decode_fiducials.csv"
if [[ ! -f $decode_csv ]]; then
    echo "test_archive_tree_roundtrip: decode did not write fiducial CSV" >&2
    exit 1
fi
cp "$decode_csv" "$test_dir/${label}_decode_fiducials.csv"
mako_check_fiducials \
    "$test_dir/${label}_encode_fiducials.csv" \
    "$test_dir/${label}_decode_fiducials.csv" \
    "$label"

for tree_name in tree empty_only; do
    source_manifest="$work_dir/${tree_name}_source.manifest"
    decoded_manifest="$work_dir/${tree_name}_decoded.manifest"
    if [[ ! -d "$decoded_dir/$tree_name" ]]; then
        echo "test_archive_tree_roundtrip: decode missing directory $tree_name" >&2
        exit 1
    fi
    (cd "$source_dir/$tree_name" && find . -print | LC_ALL=C sort) > "$source_manifest"
    (cd "$decoded_dir/$tree_name" && find . -print | LC_ALL=C sort) > "$decoded_manifest"
    if ! cmp --silent "$source_manifest" "$decoded_manifest"; then
        echo "test_archive_tree_roundtrip: directory tree differs for $tree_name" >&2
        diff -u "$source_manifest" "$decoded_manifest" >&2 || true
        exit 1
    fi
done

for relative_path in root.txt nested/leaf.txt zero.bin; do
    if [[ ! -f "$decoded_dir/tree/$relative_path" ]]; then
        echo "test_archive_tree_roundtrip: decode missing file tree/$relative_path" >&2
        exit 1
    fi
    cmp --silent "$source_dir/tree/$relative_path" "$decoded_dir/tree/$relative_path"
done

page_index=0
for ppm in "${ppms[@]}"; do
    page_index=$((page_index + 1))
    printf -v page_suffix '%02d' "$page_index"
    mv -f "$ppm" "$test_dir/${label}_archive_tree_encoded_${page_suffix}.ppm"
done

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS archive directory tree roundtrip (%d page(s))\n' "$label_fmt" "$page_index"
