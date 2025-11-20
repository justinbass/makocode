#!/usr/bin/env bash
set -euo pipefail

: "${MAKE_STEP_LINE_OPEN:=0}"
export MAKE_STEP_LINE_OPEN

ensure_clean_line() {
    if [[ ${MAKE_STEP_LINE_OPEN:-0} -eq 1 ]]; then
        printf '\n'
        MAKE_STEP_LINE_OPEN=0
        export MAKE_STEP_LINE_OPEN
    fi
}

mode="step"
if [[ $# -gt 0 && $1 == "--note" ]]; then
    mode="note"
    shift
fi

if [[ $# -lt 2 ]]; then
    ensure_clean_line
    cat >&2 <<'USAGE'
Usage: make_step.sh [--note] TAG DESCRIPTION [COMMAND [ARGS...]]
USAGE
    exit 1
fi

tag=$1
shift
desc=$1
shift || true

ensure_clean_line

if [[ -n ${NO_COLOR:-} || ! -t 1 ]]; then
    tag_fmt="[${tag}]"
    green=""
    red=""
    reset=""
    clear_line=$'\n'
else
    tag_fmt=$'\033[1;34m['"${tag}"$']\033[0m'
    green=$'\033[32m'
    red=$'\033[31m'
    reset=$'\033[0m'
    clear_line=$'\r\033[K'
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
MAKE_STEP_LINE_OPEN=1
export MAKE_STEP_LINE_OPEN
if "$@"; then
    printf '%b%b %-28s ... %bPASS%b\n' "$clear_line" "$tag_fmt" "$desc" "$green" "$reset"
    MAKE_STEP_LINE_OPEN=0
    export MAKE_STEP_LINE_OPEN
else
    status=$?
    printf '%b%b %-28s ... %bFAIL (exit %d)%b\n' "$clear_line" "$tag_fmt" "$desc" "$red" "$status" "$reset"
    MAKE_STEP_LINE_OPEN=0
    export MAKE_STEP_LINE_OPEN
    exit "$status"
fi
