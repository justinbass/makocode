// Copyright (C) 2026 Justin Alan Bass.
// SPDX-License-Identifier: AGPL-3.0-or-later
#define main makocode_cli_main
#include "../makocode.cpp"
#undef main

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    // The original scan and generated fixture select this same affine fit,
    // extract a frame, and corrupt the LZMA length in both retry modes.
    // Only generated images are passed here by the E2E test.
    u64 reference_size = 0u;
    u64 reference_bits = 0u;
    u64 reference_frame_bits = 0u;
    makocode::ByteBuffer first_sampled;
    for (int pass = 0; pass < 3; ++pass) {
        makocode::ByteBuffer image, frame, payload;
        PpmParserState metadata;
        ImageMappingConfig mapping;
        u64 frame_bits = 0u, payload_bits = 0u;
        bool retry_allowed = false;
        if (!read_entire_file(argv[pass == 0 ? 1 : 2], image) ||
            !ppm_extract_frame_bits(image, mapping, frame, frame_bits,
                                    metadata, pass == 2, &retry_allowed) ||
            !frame_bits_to_payload(frame.data, frame_bits, metadata,
                                   payload, payload_bits) ||
            !makocode::unshuffle_encoded_stream(payload.data, payload.size) ||
            payload.size < makocode::LZMA_HEADER_BYTES) return 1;
        u64 output_size = read_le_u64(payload.data + LZMA_PROPS_SIZE);
        printf("pass=%d frame_bits=%llu payload_bits=%llu LZMA_size=%llu "
               "affine=(%.9f,%.9f,%.9f)\n", pass,
               (unsigned long long)frame_bits, (unsigned long long)payload_bits,
               (unsigned long long)output_size, metadata.affine_transform.a00,
               metadata.affine_transform.tx, metadata.affine_transform.ty);
        if (pass == 0) {
            reference_size = output_size;
            reference_bits = payload_bits;
            reference_frame_bits = frame_bits;
            makocode::ByteBuffer decoded;
            if (!makocode::lzma_decompress(payload.data, payload_bits, decoded)) return 1;
        } else {
            if (frame_bits != reference_frame_bits || payload_bits > reference_bits ||
                reference_size >= 1024u ||
                output_size < (1ull << 50u) ||
                !metadata.has_affine_transform ||
                fabs(metadata.affine_transform.a00 - 2.269230769) > 0.000001 ||
                fabs(metadata.affine_transform.a11 - 2.269230769) > 0.000001 ||
                fabs(metadata.affine_transform.tx - 1.153846154) > 0.000001 ||
                fabs(metadata.affine_transform.ty - 2.653846154) > 0.000001) {
                fprintf(stderr, "scan failure differs from original sampling failure\n");
                return 1;
            }
            if (pass == 1) {
                if (!first_sampled.ensure(payload.size)) return 1;
                first_sampled.size = payload.size;
                memcpy(first_sampled.data, payload.data, payload.size);
            } else if (first_sampled.size != payload.size ||
                       memcmp(first_sampled.data, payload.data, payload.size)) {
                return 1;
            }
            printf("sampling failure: subgrid_disabled=%d frame_bits=%llu "
                   "payload_bits=%llu LZMA_size=%llu->%llu "
                   "affine=(%.9f,%.9f,%.9f)\n", pass == 2,
                   (unsigned long long)frame_bits, (unsigned long long)payload_bits,
                   (unsigned long long)reference_size, (unsigned long long)output_size,
                   metadata.affine_transform.a00, metadata.affine_transform.tx,
                   metadata.affine_transform.ty);
        }
    }
    return 0;
}
