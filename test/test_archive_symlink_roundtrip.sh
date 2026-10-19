#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of merchantability or
# fitness for a particular purpose. See the LICENSE file for the full
# license text.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_archive_symlink_roundtrip.sh [--label NAME]

  --label NAME    Prefix for artifacts under test_output/ (default: archive_symlink_roundtrip).
  --help          Show this message.
USAGE
}

label="archive_symlink_roundtrip"
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
            echo "test_archive_symlink_roundtrip: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo "test_archive_symlink_roundtrip: invalid --label" >&2
    exit 1
fi

makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_archive_symlink_roundtrip: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_symlink_work"
source_dir="$work_dir/source"
encode_dir="$source_dir/input"
page_dir="$work_dir/pages"
decoded_dir="$work_dir/decoded"
cleanup() {
    local exit_code=$?
    if [[ $exit_code -eq 0 ]]; then
        rm -rf "$work_dir"
    fi
}
trap cleanup EXIT

rm -rf "$work_dir"
mkdir -p "$encode_dir" "$page_dir" "$decoded_dir"
printf 'target contents from above the encoded directory\n' > "$source_dir/program"
ln -s ../program "$encode_dir/program"

shopt -s dotglob nullglob
entries=("$encode_dir"/*)
shopt -u dotglob nullglob
if [[ ${#entries[@]} -ne 1 || ! -L ${entries[0]} ]]; then
    echo "test_archive_symlink_roundtrip: input directory must contain only the symlink" >&2
    exit 1
fi

encode_cmd=("$makocode_bin" encode --input=input --ecc=0 --page-width=400 --page-height=240 --no-filename --no-page-count "--output-dir=$page_dir")
(cd "$source_dir" && "${encode_cmd[@]}") >/dev/null

shopt -s nullglob
ppms=("$page_dir"/*.ppm)
shopt -u nullglob
if [[ ${#ppms[@]} -eq 0 ]]; then
    echo "test_archive_symlink_roundtrip: encode emitted no PPM pages" >&2
    exit 1
fi

"$makocode_bin" decode "--output-dir=$decoded_dir" "${ppms[@]}" >/dev/null
decoded_file="$decoded_dir/input/program"
if [[ ! -f $decoded_file || -L $decoded_file ]]; then
    echo "test_archive_symlink_roundtrip: decode did not create a regular file under the output directory" >&2
    exit 1
fi
if ! cmp --silent "$source_dir/program" "$decoded_file"; then
    echo "test_archive_symlink_roundtrip: decoded file contents differ from the symlink target" >&2
    exit 1
fi
if [[ ! -L "$encode_dir/program" || ! -f "$source_dir/program" ]]; then
    echo "test_archive_symlink_roundtrip: source symlink or target changed during round-trip" >&2
    exit 1
fi

printf '%s SUCCESS symlink target archived as a regular file under the decode output directory\n' \
    "$(mako_format_label "$label")"
