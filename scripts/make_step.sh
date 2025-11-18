#!/usr/bin/env bash
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

printf '%b %-28s ... ' "$tag_fmt" "$desc"
if "$@"; then
    printf '%bPASS%b\n' "$green" "$reset"
else
    status=$?
    printf '%bFAIL (exit %d)%b\n' "$red" "$status" "$reset"
    exit "$status"
fi
