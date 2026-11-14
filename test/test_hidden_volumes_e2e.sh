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
. "$script_dir/lib/test_seed.sh"

label="hidden_volumes"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            label=${2:-}
            shift 2
            ;;
        --help)
            echo "Usage: test_hidden_volumes_e2e.sh [--label NAME]"
            exit 0
            ;;
        *)
            echo "test_hidden_volumes_e2e: unknown flag '$1'" >&2
            exit 1
            ;;
    esac
done
if [[ -z $label ]]; then
    echo "test_hidden_volumes_e2e: label may not be empty" >&2
    exit 1
fi

mako_test_seed_init "$repo_root"
makocode_bin=${MAKOCODE_BIN:-"$repo_root/makocode"}
ppm_transform_bin="$repo_root/test/ppm_transform"
test_dir="$repo_root/test_output"
work_dir=$(mktemp -d "$test_dir/${label}_hidden_work.XXXXXX")
source_dir="$work_dir/source"
mkdir -p "$source_dir/hidden_tree/nested/empty" "$source_dir/pages"
if [[ ! -x $makocode_bin || ! -x $ppm_transform_bin ]]; then
    echo "test_hidden_volumes_e2e: required binaries are missing" >&2
    exit 1
fi

visible_seed=$(mako_test_case_seed "payload/$label/visible")
hidden_seed=$(mako_test_case_seed "payload/$label/hidden")
"$ppm_transform_bin" bytes --output "$source_dir/visible.bin" --size 16384 --seed "$visible_seed"
"$ppm_transform_bin" bytes --output "$source_dir/hidden_tree/nested/secret.bin" --size 1024 --seed "$hidden_seed"
printf 'hidden archive root file\n' > "$source_dir/hidden_tree/root.txt"
: > "$source_dir/hidden_tree/empty.bin"
"$ppm_transform_bin" overlay-mask --output "$work_dir/overlay.ppm" \
    --circle-color "0 0 0" --background-color "255 255 255" --width 1000 --height 1000
"$ppm_transform_bin" overlay-mask --output "$work_dir/overlay_wrong_size.ppm" \
    --circle-color "0 0 0" --background-color "255 255 255" --width 800 --height 1000

fresh_random_data() {
    local key=$1 chunk random_data=""
    for part in 0 1 2 3 4 5 6; do
        chunk=$(mako_test_case_seed "crypto/$label/$key/$part")
        printf -v chunk '%08x' "$chunk"
        random_data+=$chunk
    done
    printf '%s' "$random_data"
}

run_e2e_case() {
    local name=$1 expected_pages=$2 visible_password=$3 hidden_password=$4
    local hidden_container=$5 random_data_needed=$6
    shift 6
    local pages="$work_dir/$name-pages"
    local visible_out="$work_dir/$name-visible"
    local hidden_out="$work_dir/$name-hidden"
    local random_data
    random_data=$(fresh_random_data "$name")
    mkdir -p "$pages" "$visible_out"
    local -a encode_cmd=("$makocode_bin" encode --input=visible.bin
        --page-width=1000 --page-height=1000 "--output-dir=$pages" "--prefix=$name")
    if [[ -n $visible_password ]]; then
        encode_cmd+=("--password=$visible_password" "--random-data=$random_data")
    elif [[ $random_data_needed == 1 ]]; then
        encode_cmd+=("--random-data=$random_data")
    fi
    if [[ -n $hidden_password ]]; then
        encode_cmd+=(--hidden-input=hidden_tree "--hidden-password=$hidden_password")
    fi
    encode_cmd+=("$@")
    local crypto_seed
    crypto_seed=$(mako_test_case_seed "encoder/$label/$name")
    if ! (cd "$source_dir" && MAKOCODE_TEST_SEED="$crypto_seed" "${encode_cmd[@]}") \
        >"$work_dir/$name-encode.log" 2>&1; then
        echo "test_hidden_volumes_e2e: encode failed for $name" >&2
        cat "$work_dir/$name-encode.log" >&2
        return 1
    fi
    shopt -s nullglob
    local -a ppms=("$pages/$name"*.ppm)
    shopt -u nullglob
    if [[ ${#ppms[@]} -ne $expected_pages ]]; then
        echo "test_hidden_volumes_e2e: $name expected $expected_pages page(s), found ${#ppms[@]}" >&2
        return 1
    fi
    local -a decode_cmd=("$makocode_bin" decode "--output-dir=$visible_out")
    if [[ -n $visible_password ]]; then decode_cmd+=("--password=$visible_password"); fi
    decode_cmd+=("${ppms[@]}")
    if ! "${decode_cmd[@]}" >"$work_dir/$name-visible-decode.log" 2>&1; then
        echo "test_hidden_volumes_e2e: visible decode failed for $name" >&2
        cat "$work_dir/$name-visible-decode.log" >&2
        return 1
    fi
    if [[ ! -f $visible_out/visible.bin ]] || ! cmp --silent "$source_dir/visible.bin" "$visible_out/visible.bin"; then
        echo "test_hidden_volumes_e2e: visible archive mismatch for $name" >&2
        return 1
    fi
    if [[ -n $hidden_password ]]; then
        mkdir -p "$hidden_out"
        local -a hidden_decode=("$makocode_bin" decode "--output-dir=$hidden_out"
            "--password=$hidden_password" "${ppms[@]}")
        if ! "${hidden_decode[@]}" >"$work_dir/$name-hidden-decode.log" 2>&1; then
            echo "test_hidden_volumes_e2e: hidden decode failed for $name" >&2
            cat "$work_dir/$name-hidden-decode.log" >&2
            return 1
        fi
        for relative_path in root.txt empty.bin nested/secret.bin; do
            if [[ ! -f $hidden_out/hidden_tree/$relative_path ]] ||
               ! cmp --silent "$source_dir/hidden_tree/$relative_path" "$hidden_out/hidden_tree/$relative_path"; then
                echo "test_hidden_volumes_e2e: hidden archive mismatch for $name at $relative_path" >&2
                return 1
            fi
        done
        [[ -d $hidden_out/hidden_tree/nested/empty ]]
        [[ ! -e $visible_out/hidden_tree ]]
        [[ ! -e $hidden_out/visible.bin ]]
    elif [[ $hidden_container == 1 ]]; then
        mkdir -p "$hidden_out"
        if "$makocode_bin" decode --output-dir="$hidden_out" "${ppms[@]}" \
            >"$work_dir/$name-no-password-decode.log" 2>&1; then
            echo "test_hidden_volumes_e2e: hidden-capable container decoded without a password for $name" >&2
            return 1
        fi
        if "$makocode_bin" decode "--output-dir=$hidden_out" --password=not-the-hidden-password \
            "${ppms[@]}" >"$work_dir/$name-filler-decode.log" 2>&1; then
            echo "test_hidden_volumes_e2e: filler-only container opened as a hidden archive for $name" >&2
            return 1
        fi
        [[ ! -e $hidden_out/visible.bin && ! -e $hidden_out/hidden_tree ]]
    fi
    printf 'hidden-e2e %s PASS (%d page(s))\n' "$name" "$expected_pages"
}

expect_encode_reject() {
    local name=$1 expected=$2
    shift 2
    local output_dir="$work_dir/$name-rejected-output"
    mkdir -p "$output_dir"
    local random_data
    random_data=$(fresh_random_data "$name")
    local has_password=0 has_random_data=0 arg
    for arg in "$@"; do
        [[ $arg == --password=* ]] && has_password=1
        [[ $arg == --random-data=* ]] && has_random_data=1
    done
    local -a reject_cmd=("$makocode_bin" encode --input=visible.bin
        --page-width=1000 --page-height=1000 "--output-dir=$output_dir" "--prefix=$name")
    if [[ $has_password == 1 && $has_random_data == 0 ]]; then
        reject_cmd+=("--random-data=$random_data")
    fi
    reject_cmd+=("$@")
    local status=0
    if (cd "$source_dir" && "${reject_cmd[@]}") >"$work_dir/$name-reject.log" 2>&1; then
        echo "test_hidden_volumes_e2e: invalid combination unexpectedly succeeded: $name" >&2
        return 1
    else
        status=$?
    fi
    if [[ $status -eq 0 ]] || ! grep -Fq -- "$expected" "$work_dir/$name-reject.log"; then
        echo "test_hidden_volumes_e2e: $name was rejected for an unexpected reason" >&2
        cat "$work_dir/$name-reject.log" >&2
        return 1
    fi
    shopt -s nullglob
    local -a rejected_pages=("$output_dir"/*.ppm)
    shopt -u nullglob
    if [[ ${#rejected_pages[@]} -ne 0 ]]; then
        echo "test_hidden_volumes_e2e: rejected encode emitted pages for $name" >&2
        return 1
    fi
    printf 'hidden-e2e %s REJECT PASS\n' "$name"
}

visible_password="visible test password"
hidden_password="hidden test password"

# Ordinary volume options, including the explicit 0 value and both page-fill modes.
run_e2e_case visible_default 1 "" "" 0 0
run_e2e_case visible_hidden_zero 1 "" "" 0 0 --hidden=0 --ecc=0.5
run_e2e_case visible_password_hidden_zero 1 "$visible_password" "" 0 0 --hidden=0
run_e2e_case ecc_fill_one_page 1 "" "" 0 1 --ecc=fill --fill-pages=1
run_e2e_case ecc_fill_two_pages 2 "" "" 0 1 --ecc=fill --fill-pages=2
run_e2e_case ecc_fill_current_page 1 "" "" 0 0 --ecc=fill

# Ratio modes cover both a real hidden tree and a filler-only region.
run_e2e_case hidden_ratio_auto 1 "$visible_password" "$hidden_password" 1 0 --hidden=0.5
run_e2e_case hidden_ratio_ecc 1 "$visible_password" "$hidden_password" 1 0 --hidden=2 --ecc=0.5
run_e2e_case hidden_ratio_filler_only 1 "$visible_password" "" 1 0 --hidden=0.5 --ecc=0.5
run_e2e_case hidden_ratio_auto_overlay_filler 1 "$visible_password" "" 1 0 \
    --hidden=0.5 --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"
run_e2e_case hidden_ratio_ecc_fill_one_page 1 "$visible_password" "$hidden_password" 1 0 \
    --hidden=2 --ecc=fill
run_e2e_case hidden_ratio_ecc_fill 2 "$visible_password" "$hidden_password" 1 0 \
    --hidden=2 --ecc=fill --fill-pages=2

# Fill modes include no hidden archive, a hidden tree, multiple pages, and overlay recovery.
run_e2e_case hidden_fill_filler_only 1 "$visible_password" "" 1 0 --hidden=fill
run_e2e_case hidden_fill_archive_auto 1 "$visible_password" "$hidden_password" 1 0 \
    --hidden fill
run_e2e_case hidden_fill_archive 1 "$visible_password" "$hidden_password" 1 0 \
    --hidden=fill --ecc=0.5 --fill-pages=1
run_e2e_case hidden_fill_two_pages 2 "$visible_password" "$hidden_password" 1 0 \
    --hidden=fill --ecc=0.25 --fill-pages=2
run_e2e_case hidden_fill_auto_two_pages 2 "$visible_password" "$hidden_password" 1 0 \
    --hidden=fill --fill-pages=2
run_e2e_case hidden_fill_overlay 1 "$visible_password" "$hidden_password" 1 0 \
    --hidden=fill --ecc=0.5 --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"
run_e2e_case hidden_fill_overlay_two_pages 2 "$visible_password" "$hidden_password" 1 0 \
    --hidden=fill --ecc=0.5 --fill-pages=2 --overlay-fraction=0.1 \
    "--overlay-ppm=$work_dir/overlay.ppm"
run_e2e_case hidden_ratio_overlay 1 "$visible_password" "$hidden_password" 1 0 \
    --hidden=0.5 --ecc=0.5 --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"
run_e2e_case visible_ratio_overlay 1 "" "" 0 0 --ecc=0.5 \
    --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"
run_e2e_case hidden_ratio_ecc_fill_overlay 2 "$visible_password" "$hidden_password" 1 0 \
    --hidden=2 --ecc=fill --fill-pages=2 --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"
run_e2e_case ecc_fill_overlay 1 "" "" 0 1 --ecc=fill --fill-pages=1 \
    --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"

# Password failures and every unsupported option pairing stay failures.
if "$makocode_bin" decode --output-dir="$work_dir/wrong-password" \
    --password=wrong-password "$work_dir/hidden_ratio_auto-pages/hidden_ratio_auto.ppm" \
    >"$work_dir/wrong-hidden-password.log" 2>&1; then
    echo "test_hidden_volumes_e2e: wrong hidden password unexpectedly opened the archive" >&2
    exit 1
fi
expect_encode_reject hidden_fill_ecc_fill "--hidden fill and --ecc fill compete" \
    --hidden=fill --ecc=fill "--password=$visible_password"
expect_encode_reject ecc_disabled_visible "ECC cannot cover structural erasures" --ecc=0
expect_encode_reject ecc_disabled_hidden_ratio "ECC cannot cover structural erasures" \
    --hidden=64 --ecc=0 "--password=$visible_password"
expect_encode_reject ecc_disabled_hidden_fill "ECC cannot cover structural erasures" \
    --hidden=fill --ecc=0 --fill-pages=1 "--password=$visible_password"
expect_encode_reject fill_pages_without_fill "--fill-pages requires --hidden fill or --ecc fill" \
    --hidden=2 --ecc=0.5 --fill-pages=1 "--password=$visible_password"
expect_encode_reject hidden_inputs_without_mode "hidden inputs and password require --hidden" \
    --hidden-input=hidden_tree "--hidden-password=$hidden_password"
expect_encode_reject hidden_input_without_password "--hidden-input and --hidden-password must be supplied together" \
    --hidden=0.5 --hidden-input=hidden_tree "--password=$visible_password"
expect_encode_reject hidden_password_without_input "--hidden-input and --hidden-password must be supplied together" \
    --hidden=0.5 "--password=$visible_password" "--hidden-password=$hidden_password"
expect_encode_reject hidden_options_with_zero "hidden inputs and password require --hidden" \
    --hidden=0 --hidden-input=hidden_tree "--hidden-password=$hidden_password" "--password=$visible_password"
expect_encode_reject hidden_without_visible_password "--hidden requires --password and --random-data" \
    --hidden=0.5
expect_encode_reject identical_passwords "visible and hidden passwords must differ" \
    --hidden=0.5 --hidden-input=hidden_tree "--hidden-password=$visible_password" \
    "--password=$visible_password"
expect_encode_reject hidden_capacity_too_small "hidden payload exceeds the --hidden capacity" \
    --hidden=0.001 --hidden-input=hidden_tree "--hidden-password=$hidden_password" \
    "--password=$visible_password"
expect_encode_reject hidden_ratio_negative "--hidden ratio must be between 0 and 64" --hidden=-1
expect_encode_reject hidden_ratio_too_large "--hidden ratio must be between 0 and 64" --hidden=65
expect_encode_reject duplicate_hidden_option "--hidden may be specified only once" --hidden=0.5 --hidden=2
expect_encode_reject fill_pages_zero "--fill-pages must be between 1 and 1000000" --ecc=fill --fill-pages=0
expect_encode_reject duplicate_fill_pages "--fill-pages may be specified only once" \
    --ecc=fill --fill-pages=1 --fill-pages=2
expect_encode_reject fill_pages_with_numeric_modes "--fill-pages requires --hidden fill or --ecc fill" \
    --hidden=0.5 --ecc=0.5 --fill-pages=1 "--password=$visible_password"
expect_encode_reject overlay_fraction_without_ppm "--overlay-fraction and --overlay-ppm must be used together" \
    --ecc=0.5 --overlay-fraction=0.1
expect_encode_reject overlay_ppm_without_fraction "--overlay-fraction and --overlay-ppm must be used together" \
    --ecc=0.5 "--overlay-ppm=$work_dir/overlay.ppm"
expect_encode_reject overlay_fraction_too_large "--overlay-fraction must be between 0.0 and 1.0" \
    --ecc=0.5 --overlay-fraction=1.1 "--overlay-ppm=$work_dir/overlay.ppm"
expect_encode_reject overlay_without_ecc "overlay requires ECC" --ecc=0 \
    --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay.ppm"
expect_encode_reject multipage_overlay_ecc_too_low "multi-page overlays require --ecc of at least 0.5" \
    --hidden=fill --hidden-input=hidden_tree "--hidden-password=$hidden_password" \
    --password="$visible_password" --ecc=0.25 --fill-pages=2 --overlay-fraction=0.1 \
    "--overlay-ppm=$work_dir/overlay.ppm"
expect_encode_reject overlay_wrong_dimensions "--overlay-ppm dimensions must match" \
    --ecc=0.5 --overlay-fraction=0.1 "--overlay-ppm=$work_dir/overlay_wrong_size.ppm"
expect_encode_reject random_data_without_password_or_fill "--random-data requires --password or --fill-pages" \
    --random-data=ffeeddccbbaa99887766554433221100123456789abcdef012345678
expect_encode_reject hidden_missing_value "--hidden requires a ratio or 'fill'" --hidden
expect_encode_reject hidden_ratio_not_numeric "--hidden ratio must be between 0 and 64" --hidden=secret
expect_encode_reject fill_pages_missing_value "--fill-pages requires a positive page count" --ecc=fill --fill-pages
expect_encode_reject fill_pages_over_limit "--fill-pages must be between 1 and 1000000" \
    --ecc=fill --fill-pages=1000001

if (cd "$source_dir" && "$makocode_bin" encode --input=visible.bin \
    --page-width=1000 --page-height=1000 --password="$visible_password" \
    --output-dir="$work_dir/missing_random_output") >"$work_dir/missing_random.log" 2>&1; then
    echo "test_hidden_volumes_e2e: password without random data unexpectedly succeeded" >&2
    exit 1
fi
grep -Fq -- "--password requires --random-data" "$work_dir/missing_random.log"

printf 'hidden volumes e2e ok (%s)\n' "$label"
