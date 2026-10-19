#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software; you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
#
set -euo pipefail

script_dir=$(cd -- "$(dirname "$0")" && pwd -P)

usage() {
    cat <<'USAGE'
Usage: test_overlay_ecc_target.sh --label CASE_ID

  --label CASE_ID    Prefix for artifacts under test_output/.
  --help             Show this message.
USAGE
}

label=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --label)
            if [[ $# -lt 2 || -z $2 ]]; then
                echo "test_overlay_ecc_target: --label requires a value" >&2
                exit 1
            fi
            label=$2
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "test_overlay_ecc_target: unknown flag '$1'" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z $label ]]; then
    echo "test_overlay_ecc_target: --label is required" >&2
    usage >&2
    exit 1
fi

export MAKO_OVERLAY_ECC_TARGET=0.25
export MAKO_OVERLAY_ENCODE_OPTS='--ecc-fill --no-filename --no-page-count'
exec "$script_dir/test_overlay_e2e.sh" "$label"
