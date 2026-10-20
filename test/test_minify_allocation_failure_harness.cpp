/*
Copyright (C) 2026 Justin Alan Bass.
This file is part of MakoCode. MakoCode is free software: you can
redistribute it and/or modify it under the terms of the GNU Affero
General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
*/

#include <stdio.h>
#include <stdlib.h>

#define main makocode_embedded_main
#define malloc minify_test_malloc
#include "../makocode.cpp"
#undef malloc
#undef main

static unsigned long allocation_count = 0u;
static unsigned long allocation_failure_at = 3u;

extern "C" void* minify_test_malloc(unsigned long size) {
    allocation_count += 1u;
    if (allocation_count == allocation_failure_at) {
        return 0;
    }
    return malloc(size);
}

int main(int argc, char** argv) {
    if (argc == 2) {
        allocation_failure_at = strtoul(argv[1], 0, 10);
    } else if (argc != 1) {
        fprintf(stderr, "minify allocation failure harness: expected at most one argument\n");
        return 1;
    }
    if (command_minify(0, 0) != 1) {
        fprintf(stderr, "minify allocation failure harness: expected exit status 1\n");
        return 1;
    }
    if (allocation_count != allocation_failure_at) {
        fprintf(stderr, "minify allocation failure harness: failed unexpected allocation %lu\n",
                allocation_count);
        return 1;
    }
    return 0;
}
