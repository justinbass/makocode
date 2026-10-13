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
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/colors.sh"

usage() {
    cat <<'USAGE'
Usage: test_cli_decoder_paths.sh [--label NAME]

  --label NAME    Prefix for artifacts under test/ (default: cli_decoder_paths).
  --help          Show this message.
USAGE
}

label="cli_decoder_paths"
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
            echo "test_cli_decoder_paths: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_cli_decoder_paths: --label requires a value" >&2
    exit 1
fi

makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_cli_decoder_paths: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

test_dir="$repo_root/test"
work_dir="$test_dir/${label}_decoder_work"
harness_src="$script_dir/test_cli_decoder_paths_harness.cpp"
if [[ ! -f $harness_src ]]; then
    echo "test_cli_decoder_paths: decoder paths harness not found at $harness_src" >&2
    exit 1
fi
trap 'status=$?; if [[ $status -eq 0 && ${MAKOCODE_COVERAGE_PROBES:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT

rm -rf "$work_dir"
mkdir -p "$work_dir"
harness_bin="$work_dir/decoder_paths_harness"
harness_inputs="$work_dir/harness_inputs"
mkdir -p "$harness_inputs"
p5_fixture="$harness_inputs/grayscale.pgm"
p6_fixture="$harness_inputs/color.ppm"
printf 'P5\n3 1\n255\n\000\177\377' > "$p5_fixture"
printf 'P6\n2 1\n255\n\001\002\003\376\144\000' > "$p6_fixture"
if [[ ${MAKOCODE_COVERAGE_PROBES:-0} == 1 ]]; then
    cxx_flags=(-std=c++17 -Wall -Wextra -pedantic -O0 -g --coverage)
else
    cxx_flags=(-std=c++17 -Wall -Wextra -pedantic -O2)
fi
printf '%s compile test-only decoder paths harness\n' "$(mako_format_label "$label")"
"${CXX:-g++}" "${cxx_flags[@]}" "$harness_src" -o "$harness_bin"
"$harness_bin" "$p5_fixture" "$p6_fixture"
for ecc in 0 0.5; do
    mode_dir="$work_dir/ecc_$ecc"
    input_dir="$mode_dir/input"
    ppm_dir="$mode_dir/ppm"
    decoded_dir="$mode_dir/decoded"
    mkdir -p "$input_dir" "$ppm_dir" "$decoded_dir"
    payload="$input_dir/payload.bin"
    # Repetition yields a compact LZMA stream while preserving an exact byte comparison.
    for ((i = 0; i < 256; ++i)); do
        printf 'LZMA CLI decoder path regression payload; repetition=%03d\n' "$i" >> "$payload"
    done

    encode_cmd=("$makocode_bin" encode --input=payload.bin "--ecc=$ecc" --page-width=500 --page-height=500 --no-filename --no-page-count "--output-dir=$ppm_dir")
    printf '%s encode (ECC %s):' "$(mako_format_label "$label")" "$ecc"
    printf ' %q' "${encode_cmd[@]}"
    printf '\n'
    (cd "$input_dir" && "${encode_cmd[@]}") >/dev/null

    shopt -s nullglob
    ppm_paths=("$ppm_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppm_paths[@]} -eq 0 ]]; then
        echo "test_cli_decoder_paths: encode emitted no PPM pages for ECC $ecc" >&2
        exit 1
    fi
    decode_cmd=("$makocode_bin" decode "--output-dir=$decoded_dir" "${ppm_paths[@]}")
    printf '%s decode (ECC %s):' "$(mako_format_label "$label")" "$ecc"
    printf ' %q' "${decode_cmd[@]}"
    printf '\n'
    "${decode_cmd[@]}" >/dev/null
    decoded_payload="$decoded_dir/payload.bin"
    if [[ ! -f $decoded_payload ]] || ! cmp --silent "$payload" "$decoded_payload"; then
        echo "test_cli_decoder_paths: decoded payload mismatch for ECC $ecc" >&2
        exit 1
    fi
done

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS grayscale loading, border cleanup, direct LZMA buffer decoding, and CLI LZMA roundtrips\n' "$label_fmt"
