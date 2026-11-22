/*
 * CLI command dispatch.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_common.hpp"
int command_encode(int arg_count, char** args);
int command_decode(int arg_count, char** args);
int command_minify(int arg_count, char** args);
int command_checkpoint(int arg_count, char** args);

int main(int argc, char** argv) {
    if (argc < 2) {
        write_usage();
        return 0;
    }
    const char* command = argv[1];
    usize length = ascii_length(command);
    if (ascii_equals_token(command, length, "--help") ||
        ascii_equals_token(command, length, "-h")) {
        write_usage();
        return 0;
    }
    int command_argc = argc - 2;
    char** command_argv = argv + 2;
    if (ascii_compare(command, "encode") == 0) {
        return command_encode(command_argc, command_argv);
    }
    if (ascii_compare(command, "decode") == 0) {
        return command_decode(command_argc, command_argv);
    }
    if (ascii_compare(command, "checkpoint") == 0) {
        return command_checkpoint(command_argc, command_argv);
    }
    if (ascii_compare(command, "minify") == 0) {
        return command_minify(command_argc, command_argv);
    }
    write_usage();
    return 0;
}
