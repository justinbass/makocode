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

mode="step"
if [[ $# -gt 0 && $1 == "--note" ]]; then
    mode="note"
    shift
fi

if [[ $# -lt 2 ]]; then
    cat >&2 <<'USAGE'
Usage: make_step.sh [--note] TAG DESCRIPTION [COMMAND [ARGS...]]
USAGE
    exit 1
fi

tag=$1
shift
desc=$1
shift || true

if [[ -n ${NO_COLOR:-} || ! -t 1 ]]; then
    tag_fmt="[${tag}]"
    green=""
    red=""
    reset=""
else
    tag_fmt=$'\033[1;34m['"${tag}"$']\033[0m'
    green=$'\033[32m'
    red=$'\033[31m'
    reset=$'\033[0m'
fi

if [[ $mode == "note" ]]; then
    printf '%b %s%b\n' "$tag_fmt" "$desc" "$reset"
    exit 0
fi

if [[ $# -eq 0 ]]; then
    printf '%b missing command%b\n' "$tag_fmt" "$reset" >&2
    exit 2
fi

if "$@"; then
    printf '%b %-28s ... %bPASS%b\n' "$tag_fmt" "$desc" "$green" "$reset"
else
    status=$?
    printf '%b %-28s ... %bFAIL (exit %d)%b\n' "$tag_fmt" "$desc" "$red" "$status" "$reset"
    exit "$status"
fi
