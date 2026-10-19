/*
Copyright (C) 2026 Justin Alan Bass.
This file is part of MakoCode. MakoCode is free software: you can
redistribute it and/or modify it under the terms of the GNU Affero
General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
*/

#include <stdio.h>

#define main makocode_embedded_cli_main
#include "../makocode.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "raw fallback harness: expected payload input and PPM output paths\n");
        return 1;
    }

    makocode::ByteBuffer payload;
    if (!read_entire_file(argv[1], payload)) {
        fprintf(stderr, "raw fallback harness: failed to read payload\n");
        return 1;
    }
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = 0.2;
    if (!encoder.set_payload(payload.data, payload.size) || !encoder.build()) {
        fprintf(stderr, "raw fallback harness: failed to encode payload\n");
        return 1;
    }

    ImageMappingConfig mapping;
    const u32 width = 500u;
    const u32 height = 500u;
    PageFooterConfig footer_config;
    footer_config.display_filename = false;
    footer_config.display_page_info = false;
    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    FooterLayout footer_layout;
    u32 data_height = height;
    u64 bits_per_page = 0u;
    u64 page_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count) ||
        !compute_page_layout(mapping,
                             footer_config,
                             frame_bit_count,
                             width,
                             height,
                             footer_layout,
                             data_height,
                             bits_per_page,
                             page_count) ||
        page_count != 1u) {
        fprintf(stderr, "raw fallback harness: failed to lay out encoded frame\n");
        return 1;
    }

    makocode::ByteBuffer page;
    if (!encode_page_to_ppm(mapping,
                            frame_bits,
                            frame_bit_count,
                            0u,
                            width,
                            height,
                            1u,
                            page_count,
                            bits_per_page,
                            &encoder.ecc_info(),
                            0,
                            0u,
                            footer_layout,
                            page)) {
        fprintf(stderr, "raw fallback harness: failed to render PPM frame\n");
        return 1;
    }
    makocode::ByteBuffer with_fiducials;
    if (!apply_default_fiducial_grid(page, with_fiducials, data_height, 0, 1u)) {
        fprintf(stderr, "raw fallback harness: failed to add fiducials\n");
        return 1;
    }
    if (!write_buffer_to_file(argv[2], with_fiducials)) {
        fprintf(stderr, "raw fallback harness: failed to write PPM frame\n");
        return 1;
    }
    return 0;
}
