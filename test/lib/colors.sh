# Copyright (C) 2026 Justin Alan Bass.
# This file is part of MakoCode. MakoCode is free software: you can
# redistribute it and/or modify it under the terms of the GNU Affero
# General Public License as published by the Free Software Foundation,
# either version 3 of the License, or (at your option) any later version.
# MakoCode is distributed without any warranty; without even the implied
# warranty of merchantability or fitness for a particular purpose. See
# the LICENSE file for the full license text.
#
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
