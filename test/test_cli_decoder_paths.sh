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
. "$script_dir/lib/fiducials.sh"
. "$script_dir/lib/visual_artifacts.sh"

usage() {
    cat <<'USAGE'
Usage: test_cli_decoder_paths.sh [--label NAME]

  --label NAME    Prefix for artifacts under test_output/ (default: cli_decoder_paths).
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

test_dir="$repo_root/test_output"
work_dir="$test_dir/${label}_decoder_work"
harness_src="$script_dir/test_cli_decoder_paths_harness.cpp"
if [[ ! -f $harness_src ]]; then
    echo "test_cli_decoder_paths: decoder paths harness not found at $harness_src" >&2
    exit 1
fi
trap 'status=$?; if [[ $status -eq 0 && ${COVERAGE:-0} != 1 ]]; then rm -rf "$work_dir"; fi' EXIT

rm -rf "$work_dir"
mkdir -p "$work_dir"
harness_bin="$work_dir/decoder_paths_harness"
harness_inputs="$work_dir/harness_inputs"
harness_output="$work_dir/archive_output"
mkdir -p "$harness_inputs" "$harness_output"
p5_fixture="$harness_inputs/grayscale.pgm"
p6_fixture="$harness_inputs/color.ppm"
bad_format_fixture="$harness_inputs/unsupported.ppm"
printf 'P5\n3 1\n255\n\000\177\377' > "$p5_fixture"
printf 'P6\n2 1\n255\n\001\002\003\376\144\000' > "$p6_fixture"
printf 'P3\n1 1\n255\n0 0 0\n' > "$bad_format_fixture"
if [[ ${COVERAGE:-0} == 1 ]]; then
    cxx_flags=(-std=c++17 -Wall -Wextra -pedantic -O0 -g --coverage)
else
    cxx_flags=(-std=c++17 -Wall -Wextra -pedantic -O2)
fi
printf '%s compile test-only decoder paths harness\n' "$(mako_format_label "$label")"
"${CXX:-g++}" "${cxx_flags[@]}" "$harness_src" -o "$harness_bin"
"$harness_bin" "$p5_fixture" "$p6_fixture" "$bad_format_fixture" "$harness_output"
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

    encode_prefix="${label}_ecc_${ecc}"
    encode_cmd=("$makocode_bin" encode --input=payload.bin "--ecc=$ecc" --page-width=500 --page-height=500 --no-filename --no-page-count "--output-dir=$ppm_dir" "--prefix=$encode_prefix" --debug)
    printf '%s encode (ECC %s):' "$(mako_format_label "$label")" "$ecc"
    printf ' %q' "${encode_cmd[@]}"
    printf '\n'
    (cd "$input_dir" && "${encode_cmd[@]}") >/dev/null
    encode_csv="$ppm_dir/${encode_prefix}_encode_fiducials.csv"
    if [[ ! -f $encode_csv ]]; then
        echo "test_cli_decoder_paths: encode did not write fiducial CSV for ECC $ecc" >&2
        exit 1
    fi
    cp "$encode_csv" "$test_dir/${label}_ecc_${ecc}_encode_fiducials.csv"

    shopt -s nullglob
    ppm_paths=("$ppm_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#ppm_paths[@]} -eq 0 ]]; then
        echo "test_cli_decoder_paths: encode emitted no PPM pages for ECC $ecc" >&2
        exit 1
    fi
    decode_cmd=("$makocode_bin" decode "--output-dir=$decoded_dir" --debug "${ppm_paths[@]}")
    printf '%s decode (ECC %s):' "$(mako_format_label "$label")" "$ecc"
    printf ' %q' "${decode_cmd[@]}"
    printf '\n'
    "${decode_cmd[@]}" >/dev/null
    if [[ $ecc == 0 ]]; then
        password_decode_dir="$mode_dir/decoded_with_ignored_password"
        mkdir -p "$password_decode_dir"
        password_decode_output="$mode_dir/decode_with_ignored_password_output.txt"
        if ! "$makocode_bin" decode "--output-dir=$password_decode_dir" \
            --password=unused-password "${ppm_paths[@]}" >"$password_decode_output" 2>&1; then
            echo 'test_cli_decoder_paths: decode with an ignored password failed' >&2
            cat "$password_decode_output" >&2
            exit 1
        fi
        if ! grep -Fq 'decode: warning: payload was not encrypted; password ignored' \
            "$password_decode_output" ||
           ! cmp --silent "$payload" "$password_decode_dir/payload.bin"; then
            echo 'test_cli_decoder_paths: unencrypted payload password warning or output mismatch' >&2
            cat "$password_decode_output" >&2
            exit 1
        fi
    fi
    decode_csv="$decoded_dir/decode_fiducials.csv"
    if [[ ! -f $decode_csv ]]; then
        echo "test_cli_decoder_paths: decode did not write fiducial CSV for ECC $ecc" >&2
        exit 1
    fi
    cp "$decode_csv" "$test_dir/${label}_ecc_${ecc}_decode_fiducials.csv"
    mako_check_fiducials \
        "$test_dir/${label}_ecc_${ecc}_encode_fiducials.csv" \
        "$test_dir/${label}_ecc_${ecc}_decode_fiducials.csv" \
        "$label ECC $ecc"
    decoded_payload="$decoded_dir/payload.bin"
    if [[ ! -f $decoded_payload ]] || ! cmp --silent "$payload" "$decoded_payload"; then
        echo "test_cli_decoder_paths: decoded payload mismatch for ECC $ecc" >&2
        exit 1
    fi
    visual_targets=()
    page_index=0
    for ppm in "${ppm_paths[@]}"; do
        page_index=$((page_index + 1))
        visual_ppm="$test_dir/${label}_ecc_${ecc}_page_${page_index}.ppm"
        cp "$ppm" "$visual_ppm"
        visual_targets+=("$visual_ppm")
    done
    mako_register_visual_artifacts "$repo_root" "${visual_targets[@]}"
done

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS grayscale loading, border cleanup, direct LZMA buffer decoding, and CLI LZMA roundtrips\n' "$label_fmt"
