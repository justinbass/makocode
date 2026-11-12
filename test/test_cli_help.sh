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

usage() {
    cat <<'USAGE'
Usage: test_cli_help.sh [--label NAME]

  --label NAME    Prefix for output labels (default: cli_help).
  --help          Show this message.
USAGE
}

label="cli_help"
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
            echo "test_cli_help: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_cli_help: --label requires a value" >&2
    exit 1
fi

repo_root=$(cd -- "$script_dir/.." && pwd -P)
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
if [[ ! -x $makocode_bin ]]; then
    echo "test_cli_help: makocode binary not found at $makocode_bin" >&2
    exit 1
fi

tmp_dir=$(mktemp -d "$repo_root/test_output/${label}_help.XXXXXX")

run_expect_help() {
    local phase=$1
    local expected_first=$2
    local expected_second=$3
    shift 3
    local output="$tmp_dir/$phase.txt"
    local label_fmt
    label_fmt=$(mako_format_label "$label")
    printf '%s %s: ' "$label_fmt" "$phase"
    printf '%q ' "$@"
    printf '\n'

    local status=0
    if "$@" >"$output" 2>&1; then
        status=0
    else
        status=$?
    fi
    if [[ $status -ne 0 ]]; then
        printf 'test_cli_help: %s exited %d\n' "$phase" "$status" >&2
        cat "$output" >&2
        return 1
    fi
    if ! grep -Fq -- "$expected_first" "$output" ||
       ! grep -Fq -- "$expected_second" "$output"; then
        printf 'test_cli_help: %s output is missing expected help text\n' "$phase" >&2
        cat "$output" >&2
        return 1
    fi
}

run_expect_reject() {
    local phase=$1
    local expected=$2
    shift 2
    local output="$tmp_dir/$phase.txt"
    local status=0
    if "$@" >"$output" 2>&1; then
        echo "test_cli_help: $phase unexpectedly succeeded" >&2
        return 1
    else
        status=$?
    fi
    if [[ $status -eq 0 ]] || ! grep -Fq -- "$expected" "$output"; then
        echo "test_cli_help: $phase did not reject the value for the expected reason" >&2
        cat "$output" >&2
        return 1
    fi
}

run_expect_help top-level 'Usage:' 'makocode encode [options]' "$makocode_bin"
run_expect_help top-level-long-help 'Usage:' 'makocode encode [options]' \
    "$makocode_bin" --help
run_expect_help top-level-short-help 'Usage:' 'makocode encode [options]' \
    "$makocode_bin" -h
run_expect_help unknown-command 'Usage:' 'makocode encode [options]' "$makocode_bin" unknown-command
run_expect_help encode-help 'Usage: makocode encode' '--input PATH' "$makocode_bin" encode --help
run_expect_help encode-footer-help 'Usage: makocode encode' '--footer TEXT' "$makocode_bin" encode --help
run_expect_help encode-ecc-help 'default 0.10' 'sufficient base --ecc value' \
    "$makocode_bin" encode --help
run_expect_help decode-help 'Usage: makocode decode' 'stdin' "$makocode_bin" decode --help
run_expect_help overlay-help 'Usage: makocode overlay' '--overlay-ecc-target TARGET' "$makocode_bin" overlay --help
run_expect_help minify-help 'Usage: makocode minify' 'Strips comments/whitespace' "$makocode_bin" minify --help
run_expect_reject ecc-precision '--ecc must be 0.00 to 655.35 with at most 2 decimal places' \
    "$makocode_bin" encode --ecc=0.001
run_expect_reject ecc-range '--ecc must be 0.00 to 655.35 with at most 2 decimal places' \
    "$makocode_bin" encode --ecc=655.36
run_expect_reject overlay-fraction-precision '--overlay-fraction must be 0.00 to 0.99 with at most 2 decimal places' \
    "$makocode_bin" encode --overlay-fraction=0.001
run_expect_reject overlay-fraction-one '--overlay-fraction must be 0.00 to 0.99 with at most 2 decimal places; 1.00 is invalid' \
    "$makocode_bin" encode --overlay-fraction=1.00

printf 'x' > "$tmp_dir/payload.bin"
printf makocode > "$tmp_dir/small_page.bin"
small_page_error="$tmp_dir/small-page-ecc-minimum.txt"
if "$makocode_bin" encode "--input=$tmp_dir/small_page.bin" \
    --page-width=80 --page-height=80 "--output-dir=$tmp_dir/small_page_out" \
    >"$small_page_error" 2>&1; then
    echo "test_cli_help: small-page ECC default unexpectedly succeeded" >&2
    exit 1
fi
minimum_small_page_ecc=$(sed -n \
    's/.*use at least base --ecc \([0-9][0-9]*\.[0-9][0-9]\) for this payload\/page layout.*/\1/p' \
    "$small_page_error")
if [[ -z $minimum_small_page_ecc ]]; then
    echo "test_cli_help: small-page ECC rejection did not report a minimum target" >&2
    cat "$small_page_error" >&2
    exit 1
fi
mkdir -p "$tmp_dir/small_page_minimum"
"$makocode_bin" encode "--input=$tmp_dir/small_page.bin" \
    --page-width=80 --page-height=80 "--ecc=$minimum_small_page_ecc" \
    "--output-dir=$tmp_dir/small_page_minimum" --prefix=small_page_minimum >/dev/null
"$repo_root/test/ppm_transform" bytes --output "$tmp_dir/default.bin" --size 8192 --seed 55
mkdir -p "$tmp_dir/defaults" "$tmp_dir/ecc_max" "$tmp_dir/ecc_max_decoded" \
    "$tmp_dir/overlay_fraction" "$tmp_dir/overlay_fraction_decoded"
(cd "$tmp_dir" && "$makocode_bin" encode --input=default.bin \
    "--output-dir=$tmp_dir/defaults" --prefix=defaults >/dev/null)
default_page="$tmp_dir/defaults/defaults.ppm"
read -r _ < "$default_page"
read -r default_width default_height < <(sed -n '2p' "$default_page")
harness=${CANONICAL_TILE_HARNESS_BIN:-"$repo_root/test/canonical_tile_harness"}
default_settings=$("$harness" "$default_page" "$default_width" "$default_height" 1 \
    --report-ecc-settings)
if [[ $default_settings != $'10\t0' ]]; then
    echo "test_cli_help: encode defaults were ECC/overlay $default_settings, expected 10/0" >&2
    exit 1
fi
(cd "$tmp_dir" && "$makocode_bin" encode --input=payload.bin --ecc=655.35 \
    --overlay-fraction=0.00 "--output-dir=$tmp_dir/ecc_max" \
    --prefix=ecc_max >/dev/null)
"$makocode_bin" decode --output-dir="$tmp_dir/ecc_max_decoded" \
    "$tmp_dir/ecc_max/ecc_max.ppm" >/dev/null
cmp "$tmp_dir/payload.bin" "$tmp_dir/ecc_max_decoded/payload.bin"
(cd "$tmp_dir" && "$makocode_bin" encode --input=default.bin --ecc=0.50 \
    --overlay-fraction=0.20 "--output-dir=$tmp_dir/overlay_fraction" \
    --prefix=overlay_fraction >/dev/null)
overlay_page="$tmp_dir/overlay_fraction/overlay_fraction.ppm"
read -r _ < "$overlay_page"
read -r overlay_width overlay_height < <(sed -n '2p' "$overlay_page")
overlay_settings=$("$harness" "$overlay_page" "$overlay_width" "$overlay_height" 1 \
    --report-ecc-settings)
if [[ $overlay_settings != $'50\t13107' ]]; then
    echo "test_cli_help: overlay fraction encoded as $overlay_settings, expected Q0.16 50/13107" >&2
    exit 1
fi
"$makocode_bin" decode --output-dir="$tmp_dir/overlay_fraction_decoded" \
    "$overlay_page" >/dev/null
cmp "$tmp_dir/default.bin" "$tmp_dir/overlay_fraction_decoded/default.bin"

if grep -Eq '^  --' "$tmp_dir/top-level.txt"; then
    echo 'test_cli_help: top-level help lists command-specific options' >&2
    cat "$tmp_dir/top-level.txt" >&2
    exit 1
fi
if ! grep -Fq -- 'default 0.10' "$tmp_dir/encode-help.txt" ||
   ! grep -Fq -- 'default 0.00' "$tmp_dir/encode-help.txt" ||
   ! grep -Fq -- '0 disables payload ECC' "$tmp_dir/encode-help.txt" ||
   ! grep -Fq -- 'Q0.16 (raw / 65536)' "$tmp_dir/encode-help.txt" ||
   ! grep -Fq -- '255 total symbols' "$tmp_dir/encode-help.txt" ||
   ! grep -Fq -- 'omitted width fits the encoded payload' "$tmp_dir/encode-help.txt" ||
   ! grep -Fq -- 'omitted height fits the encoded payload' "$tmp_dir/encode-help.txt" ||
   grep -Fq -- '--compact-page' "$tmp_dir/encode-help.txt" ||
   grep -Fq -- '--footer' "$tmp_dir/decode-help.txt" ||
   grep -Fq -- '--ecc-fill' "$tmp_dir/decode-help.txt"; then
    echo 'test_cli_help: command-specific layout options are incorrect' >&2
    cat "$tmp_dir/encode-help.txt" "$tmp_dir/decode-help.txt" >&2
    exit 1
fi

label_fmt=$(mako_format_label "$label")
printf '%s SUCCESS CLI usage and command help\n' "$label_fmt"
