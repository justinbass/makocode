# shellcheck shell=bash

if [[ -z ${MAKO_COLORS_INITIALIZED:-} ]]; then
    if [[ -n ${NO_COLOR:-} || ! -t 1 ]]; then
        MAKO_LABEL_PREFIX=""
        MAKO_LABEL_SUFFIX=""
        MAKO_PASS_COLOR=""
        MAKO_FAIL_COLOR=""
        MAKO_RESET_COLOR=""
    else
        MAKO_LABEL_PREFIX=$'\033[1;34m'
        MAKO_LABEL_SUFFIX=$'\033[0m'
        MAKO_PASS_COLOR=$'\033[32m'
        MAKO_FAIL_COLOR=$'\033[31m'
        MAKO_RESET_COLOR=$'\033[0m'
    fi
    MAKO_COLORS_INITIALIZED=1
fi

mako_format_label() {
    printf '%b[%s]%b' "$MAKO_LABEL_PREFIX" "$1" "$MAKO_LABEL_SUFFIX"
}
