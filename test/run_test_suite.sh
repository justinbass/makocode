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
. "$script_dir/lib/test_seed.sh"

usage() {
    cat <<'USAGE'
Usage: run_test_suite.sh --prepare | --finalize

  --prepare    Initialize the master seed and clear visual symlinks.
  --finalize   Collect links to final visual artifacts.
USAGE
}

prepare_suite() {
    unset MAKO_TEST_SEED_REPORTED
    mako_test_seed_init "$repo_root"
    rm -rf "$repo_root/test_output/visual"
}

finalize_suite() {
    local visual_dir="$repo_root/test_output/visual"
    mkdir -p "$visual_dir"
}

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 2
fi

case $1 in
    --prepare)
        prepare_suite
        ;;
    --finalize)
        finalize_suite
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac
