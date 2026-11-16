#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
# No warranty; see LICENSE.

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)
repo_root=$(cd -- "$script_dir/.." && pwd -P)
. "$script_dir/lib/colors.sh"
. "$script_dir/lib/test_seed.sh"

usage() {
    cat <<'USAGE'
Usage: run_test_suite.sh --prepare

  --prepare    Prepare output and initialize the master seed.
USAGE
}

prepare_suite() {
    unset MAKO_TEST_SEED_REPORTED
    mkdir -p "$repo_root/test_output"
    mako_test_seed_init "$repo_root"
    mkdir -p "$repo_root/test_output/test_logs"
}

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

case $1 in
    --prepare)
        prepare_suite
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
