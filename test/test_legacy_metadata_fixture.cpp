// Copyright (C) 2026 Justin Alan Bass.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdio.h>
#define main makocode_cli_main
#include "../makocode.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    FILE* in = fopen(argv[1], "r");
    if (!in) return 1;
    char magic[8]; unsigned w, h, max;
    if (fscanf(in, "%7s%u%u%u", magic, &w, &h, &max) != 4 || strcmp(magic, "P3") || max != 255) return 1;
    makocode::ByteBuffer pixels;
    if (!pixels.ensure((usize)w * h * 3u)) return 1;
    pixels.size = (usize)w * h * 3u;
    for (usize i = 0; i < pixels.size; ++i) { unsigned v; if (fscanf(in, "%u", &v) != 1 || v > 255) return 1; pixels.data[i] = (u8)v; }
    fclose(in);
    MetadataTile::Placement pos = MetadataTile::compute_tile_placement(w, h);
    MetadataTile::Values values;
    makocode::ByteBuffer palette;
    if (!MetadataTile::decode_tile(pixels.data, w, h, h, pos, values, palette) || values.palette_count != 2) return 1;
    makocode::ByteBuffer metadata, codeword;
    if (!metadata.ensure(values.encoded_metadata_size + 1u)) return 1;
    memcpy(metadata.data, values.encoded_metadata, 4u);
    metadata.data[4] = 2u;
    memcpy(metadata.data + 5u, values.encoded_metadata + 4u, values.encoded_metadata_size - 5u);
    metadata.size = values.encoded_metadata_size + 1u;
    metadata.data[metadata.size - 1u] = MetadataTile::compute_crc8(metadata.data, metadata.size - 1u);
    if (!MetadataTile::rs_encode_with_parity(metadata, 12u, codeword)) return 1;
    u32 header = (0x4d4du << 16) | (2u << 12) | ((u32)metadata.size << 4) | 2u;
    for (u32 rep = 0; rep < 5; ++rep) for (u32 bit = 0; bit < 32; ++bit) {
        u8 value = ((header >> bit) & 1u) ? 0 : 255;
        usize index = ((usize)(pos.y0 + 1 + rep) * w + pos.x0 + 1 + bit) * 3u;
        pixels.data[index] = pixels.data[index + 1] = pixels.data[index + 2] = value;
    }
    usize cursor = 0;
    for (u32 y = 6; y < 47; ++y) for (u32 x = 1; x < 47; ++x) {
        if (x >= 16 && x < 31 && y >= 16 && y < 31) continue;
        if (cursor >= codeword.size * 24u) continue;
        usize bit = cursor++ / 3u;
        u8 value = ((codeword.data[bit / 8u] >> (bit % 8u)) & 1u) ? 0 : 255;
        usize index = ((usize)(pos.y0 + y) * w + pos.x0 + x) * 3u;
        pixels.data[index] = pixels.data[index + 1] = pixels.data[index + 2] = value;
    }
    if (cursor != codeword.size * 24u) return 1;
    // Damage one unused checkerboard border module.
    usize damaged = ((usize)pos.y0 * w + pos.x0) * 3u;
    pixels.data[damaged] = pixels.data[damaged + 1] = pixels.data[damaged + 2] = 0;
    FILE* out = fopen(argv[2], "w");
    if (!out) return 1;
    fprintf(out, "P3\n%u %u\n255\n", w, h);
    for (usize i = 0; i < pixels.size; i += 3) fprintf(out, "%u %u %u\n", pixels.data[i], pixels.data[i+1], pixels.data[i+2]);
    return fclose(out) != 0;
}
