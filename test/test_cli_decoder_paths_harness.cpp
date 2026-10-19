/*
Copyright (C) 2026 Justin Alan Bass.
This file is part of MakoCode. MakoCode is free software: you can
redistribute it and/or modify it under the terms of the GNU Affero
General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
MakoCode is distributed without any warranty; without even the implied
warranty of merchantability or fitness for a particular purpose. See
the LICENSE file for the full license text.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define main makocode_embedded_cli_main
#include "../makocode.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "decoder paths harness: expected P5, P6, and archive output paths\n");
        return 1;
    }

    makocode::image::ImageBuffer loaded = {};
    if (makocode::image::load_ppm_grayscale(argv[1], loaded, 0) != makocode::image::LoadSuccess ||
        loaded.width != 3u || loaded.height != 1u ||
        loaded.pixels[0] != 0u || loaded.pixels[1] != 127u || loaded.pixels[2] != 255u) {
        fprintf(stderr, "decoder paths harness: P5 grayscale load mismatch\n");
        makocode::image::release(loaded);
        return 1;
    }
    if (makocode::image::load_ppm_grayscale(argv[2], loaded, 0) != makocode::image::LoadSuccess ||
        loaded.width != 2u || loaded.height != 1u ||
        loaded.pixels[0] != 2u || loaded.pixels[1] != 118u) {
        fprintf(stderr, "decoder paths harness: P6 grayscale conversion mismatch\n");
        makocode::image::release(loaded);
        return 1;
    }
    makocode::image::release(loaded);

    ArchiveBuildContext traversal_archive;
    const char* traversal_path = "../traversal_escape";
    usize traversal_path_length = ascii_length(traversal_path);
    if (!archive_init(traversal_archive) ||
        !archive_append_u8(traversal_archive.buffer, 1u) ||
        !archive_append_u32(traversal_archive.buffer, (u32)traversal_path_length) ||
        !archive_append_bytes(traversal_archive.buffer,
                              (const u8*)traversal_path,
                              traversal_path_length)) {
        fprintf(stderr, "decoder paths harness: traversal archive setup failed\n");
        return 1;
    }
    traversal_archive.entry_count = 1u;
    if (!archive_finalize(traversal_archive)) {
        fprintf(stderr, "decoder paths harness: traversal archive finalization failed\n");
        return 1;
    }
    struct stat outside_entry;
    char outside_path[4096];
    int outside_path_length = snprintf(outside_path,
                                       sizeof(outside_path),
                                       "%s/../traversal_escape",
                                       argv[3]);
    if (outside_path_length < 0 || (usize)outside_path_length >= sizeof(outside_path) ||
        unpack_archive_to_directory(traversal_archive.buffer, argv[3]) ||
        stat(outside_path, &outside_entry) == 0) {
        fprintf(stderr, "decoder paths harness: archive directory traversal was not rejected\n");
        return 1;
    }

    makocode::image::ImageBuffer border_image = {};
    border_image.width = 7u;
    border_image.height = 7u;
    border_image.pixels = (unsigned char*)malloc(49u);
    if (!border_image.pixels) {
        fprintf(stderr, "decoder paths harness: border fixture allocation failed\n");
        return 1;
    }
    memset(border_image.pixels, 255, 49u);
    border_image.pixels[3u * 7u] = 0u;
    border_image.pixels[3u * 7u + 3u] = 0u;
    makocode::ByteBuffer dirt_mask;
    bool border_ok = makocode::remove_border_dirt(border_image, 200u, &dirt_mask, 1u);
    bool border_assertions = border_ok && dirt_mask.size == 49u &&
                             border_image.pixels[3u * 7u] == 255u &&
                             dirt_mask.data[3u * 7u] == 255u &&
                             border_image.pixels[3u * 7u + 3u] == 0u &&
                             dirt_mask.data[3u * 7u + 3u] == 0u;
    dirt_mask.release();
    makocode::image::release(border_image);
    if (!border_assertions) {
        fprintf(stderr, "decoder paths harness: border dirt cleanup/protected margin mismatch\n");
        return 1;
    }

    unsigned char source[4096];
    for (usize i = 0u; i < (usize)sizeof(source); ++i) {
        source[i] = (unsigned char)(((i % 97u) < 60u) ? ('A' + (i % 4u)) : (i & 0xFFu));
    }
    unsigned char compressed[8192];
    unsigned char properties[LZMA_PROPS_SIZE];
    size_t compressed_size = sizeof(compressed);
    size_t properties_size = sizeof(properties);
    if (makocode::LzmaCompress(compressed,
                               &compressed_size,
                               source,
                               sizeof(source),
                               properties,
                               &properties_size,
                               5,
                               1u << 16u,
                               3,
                               0,
                               2,
                               64,
                               1) != SZ_OK) {
        fprintf(stderr, "decoder paths harness: LZMA fixture compression failed\n");
        return 1;
    }

    makocode::CLzmaDec decoder;
    LzmaDec_Construct(&decoder);
    if (makocode::LzmaDec_Allocate(&decoder,
                                   properties,
                                   (unsigned)properties_size,
                                   &makocode::g_Alloc) != SZ_OK) {
        fprintf(stderr, "decoder paths harness: LZMA decoder allocation failed\n");
        return 1;
    }
    makocode::LzmaDec_Init(&decoder);
    unsigned char decoded[sizeof(source)];
    makocode::SizeT decoded_size = sizeof(decoded);
    makocode::SizeT input_size = (makocode::SizeT)compressed_size;
    makocode::ELzmaStatus decode_status = makocode::LZMA_STATUS_NOT_SPECIFIED;
    makocode::SRes decode_result = makocode::LzmaDec_DecodeToBuf(&decoder,
                                                       decoded,
                                                       &decoded_size,
                                                       compressed,
                                                       &input_size,
                                                       makocode::LZMA_FINISH_ANY,
                                                       &decode_status);
    makocode::LzmaDec_Free(&decoder, &makocode::g_Alloc);
    if (decode_result != SZ_OK || decoded_size != sizeof(source) ||
        memcmp(source, decoded, sizeof(source)) != 0) {
        fprintf(stderr, "decoder paths harness: LzmaDec_DecodeToBuf output mismatch\n");
        return 1;
    }

    printf("direct P5/P6 grayscale loader, archive traversal rejection, border cleanup, and LzmaDec_DecodeToBuf checks passed\n");
    return 0;
}
