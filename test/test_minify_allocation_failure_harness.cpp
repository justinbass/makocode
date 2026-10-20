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

extern "C" void* minify_test_malloc(unsigned long size) {
    allocation_count += 1u;
    if (allocation_count == 3u) {
        return 0;
    }
    return malloc(size);
}

int main() {
    if (command_minify(0, 0) != 1) {
        fprintf(stderr, "minify allocation failure harness: expected exit status 1\n");
        return 1;
    }
    if (allocation_count != 3u) {
        fprintf(stderr, "minify allocation failure harness: failed unexpected allocation %lu\n",
                allocation_count);
        return 1;
    }
    return 0;
}
