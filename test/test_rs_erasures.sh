#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
label=rs_erasures
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            if [[ $# -lt 2 || -z $2 ]]; then
                echo 'test_rs_erasures: --label requires a value' >&2
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
            printf 'test_rs_erasures: unknown flag %s\n' "$1" >&2
            exit 1
            ;;
    esac
done
if [[ -z $label || $label == *[!a-zA-Z0-9_.-]* ]]; then
    echo 'test_rs_erasures: invalid --label' >&2
    exit 1
fi

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/makocode-rs-erasures.${label}.XXXXXX")
trap 'status=$?; rm -rf "$work_dir"; exit "$status"' EXIT
harness_bin="$work_dir/rs_erasures_harness"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -pedantic -O2 \
    "$script_dir/test_rs_erasures_harness.cpp" -o "$harness_bin"
"$harness_bin"
