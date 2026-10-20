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
label="${1:-ppm_transform_cli}"
work_dir="$repo_root/test_output/${label}_work"
ppm_transform_bin="$repo_root/test/ppm_transform"

if [[ ! -x "$ppm_transform_bin" ]]; then
    echo "test_ppm_transform_cli: helper not found at $ppm_transform_bin" >&2
    exit 1
fi

rm -rf "$work_dir"
mkdir -p "$work_dir"
trap 'status=$?; if [[ $status -eq 0 ]]; then rm -rf "$work_dir"; fi' EXIT

if "$ppm_transform_bin" --help >"$work_dir/help.out" 2>"$work_dir/help.err"; then
    help_status=0
else
    help_status=$?
fi
if [[ $help_status -ne 0 ]] || ! grep -Fq 'Usage:' "$work_dir/help.err"; then
    echo 'test_ppm_transform_cli: --help did not print usage successfully' >&2
    exit 1
fi

if "$ppm_transform_bin" >"$work_dir/no_command.out" 2>"$work_dir/no_command.err"; then
    no_command_status=0
else
    no_command_status=$?
fi
if [[ $no_command_status -ne 1 ]] || ! grep -Fq 'Usage:' "$work_dir/no_command.err"; then
    echo 'test_ppm_transform_cli: no-command invocation did not print usage and fail' >&2
    exit 1
fi

if "$ppm_transform_bin" bytes-len >"$work_dir/die.out" 2>"$work_dir/die.err"; then
    die_status=0
else
    die_status=$?
fi
if [[ $die_status -ne 1 ]] || ! grep -Fq 'bytes-len requires TEXT' "$work_dir/die.err"; then
    echo 'test_ppm_transform_cli: missing bytes-len argument did not report the expected error' >&2
    exit 1
fi

if "$ppm_transform_bin" unknown-command >"$work_dir/die2.out" 2>"$work_dir/die2.err"; then
    die2_status=0
else
    die2_status=$?
fi
if [[ $die2_status -ne 1 ]] || ! grep -Fq 'unknown command unknown-command' "$work_dir/die2.err"; then
    echo 'test_ppm_transform_cli: unknown command did not report the expected error' >&2
    exit 1
fi

printf 'SUCCESS ppm_transform help and fatal error paths\n'
