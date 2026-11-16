#!/usr/bin/env bash
# Copyright (C) 2026 Justin Alan Bass.
# SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
# No warranty; see LICENSE.

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
