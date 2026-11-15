// Copyright (C) 2026 Justin Alan Bass.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdio.h>

#define main makocode_cli_main
#include "../makocode.cpp"
#undef main

struct BlobDecodeResult {
    bool parsed;
    bool partial;
    u64 unrecoverable_blocks;
    u64 erased_symbols;
};

static bool test_uniform_region_detector() {
    const u32 width = 100u;
    const u32 height = 100u;
    usize cell_count = (usize)width * height;
    makocode::ByteBuffer symbols;
    makocode::ByteBuffer ranks;
    if (!symbols.ensure(cell_count) ||
        cell_count > (usize)USIZE_MAX_VALUE / sizeof(u64) ||
        !ranks.ensure(cell_count * sizeof(u64))) return false;
    symbols.size = cell_count;
    ranks.size = cell_count * sizeof(u64);
    u8* values = symbols.data;
    u64* payload_ranks = (u64*)ranks.data;
    u32 random_state = 0xA341316Cu;
    for (usize i = 0u; i < cell_count; ++i) {
        random_state ^= random_state << 13u;
        random_state ^= random_state >> 17u;
        random_state ^= random_state << 5u;
        values[i] = (u8)(random_state & 1u);
        payload_ranks[i] = i;
    }
    MetadataTile::Placement placement = {};
    makocode::ByteBuffer random_mask;
    if (!detect_uniform_payload_regions(values,
                                        width,
                                        height,
                                        payload_ranks,
                                        0,
                                        0u,
                                        placement,
                                        1u,
                                        cell_count,
                                        random_mask)) return false;
    for (usize i = 0u; i < random_mask.size; ++i) {
        if (random_mask.data[i]) return false;
    }
    for (u32 y = 20u; y < 80u; ++y) {
        for (u32 x = 20u; x < 80u; ++x) values[(usize)y * width + x] = 1u;
    }
    makocode::ByteBuffer blot_mask;
    if (!detect_uniform_payload_regions(values,
                                        width,
                                        height,
                                        payload_ranks,
                                        0,
                                        0u,
                                        placement,
                                        1u,
                                        cell_count,
                                        blot_mask)) return false;
    u64 marked = 0u;
    for (usize i = 0u; i < blot_mask.size; ++i) marked += blot_mask.data[i] ? 1u : 0u;
    return marked >= 1500u && marked < 5000u;
}

static bool decode_blob_fixture(const char* path,
                                bool detect_uniform_blots,
                                BlobDecodeResult& result) {
    result = {};
    makocode::ByteBuffer ppm;
    if (!read_entire_file(path, ppm)) return false;
    ImageMappingConfig mapping;
    makocode::ByteBuffer frame_bits;
    makocode::ByteBuffer frame_erasures;
    u64 frame_bit_count = 0u;
    PpmParserState state;
    bool retry_without_subgrid = false;
    bool color_rotation = false;
    bool frame_extracted = ppm_extract_frame_bits(ppm,
                                                  mapping,
                                                  frame_bits,
                                                  frame_bit_count,
                                                  state,
                                                  false,
                                                  &retry_without_subgrid,
                                                  0,
                                                  1u,
                                                  &frame_erasures,
                                                  0,
                                                  0,
                                                  0,
                                                  0,
                                                  &color_rotation,
                                                  0,
                                                  false,
                                                  0,
                                                  0,
                                                  0,
                                                  0u,
                                                  0u,
                                                  0u,
                                                  detect_uniform_blots);
    if (!frame_extracted || !state.has_encoded_tile ||
        !metadata_schema_is_current(state.encoded_tile.schema_version)) return false;
    if (color_rotation) {
        for (usize i = 0u; i < frame_bits.size; ++i) {
            u8 rotate = (u8)((i % 3u) + 1u);
            frame_bits.data[i] = rotate_right_u8(frame_bits.data[i], rotate);
        }
        for (usize i = 0u; i < frame_erasures.size; ++i) {
            u8 rotate = (u8)((i % 3u) + 1u);
            frame_erasures.data[i] = rotate_right_u8(frame_erasures.data[i], rotate);
        }
    }

    makocode::ByteBuffer payload_stream;
    makocode::ByteBuffer payload_erasures;
    u64 payload_bits = 0u;
    if (!frame_bits_to_payload(frame_bits.data,
                               frame_bit_count,
                               state,
                               payload_stream,
                               payload_bits,
                               0,
                               frame_erasures.data,
                               frame_bit_count,
                               &payload_erasures)) return false;
    for (usize i = 0u; i < payload_erasures.size; ++i) {
        if (payload_erasures.data[i]) ++result.erased_symbols;
    }

    const MetadataTile::Values& tile = state.encoded_tile;
    if (!tile.ecc_enabled || !tile.ecc_block_data || !tile.ecc_parity ||
        !tile.ecc_block_count || tile.ecc_block_count > U64_MAX_VALUE /
            (u64)(tile.ecc_block_data + tile.ecc_parity)) return false;
    u64 protected_bytes = tile.ecc_block_count *
                          (u64)(tile.ecc_block_data + tile.ecc_parity);
    if (protected_bytes > payload_stream.size ||
        protected_bytes > (u64)USIZE_MAX_VALUE - makocode::ECC_HEADER_TOTAL_BYTES) return false;

    u8 header[makocode::ECC_HEADER_TOTAL_BYTES];
    if (!makocode::build_ecc_header_bytes(header,
                                          tile.ecc_block_data,
                                          tile.ecc_parity,
                                          tile.ecc_block_count,
                                          tile.ecc_original_bytes,
                                          tile.ecc_header_magic,
                                          tile.ecc_header_version,
                                          tile.ecc_header_flags,
                                          tile.ecc_header_reserved)) return false;
    makocode::ByteBuffer decoder_stream;
    if (!decoder_stream.ensure(makocode::ECC_HEADER_TOTAL_BYTES + (usize)protected_bytes)) {
        return false;
    }
    decoder_stream.size = makocode::ECC_HEADER_TOTAL_BYTES + (usize)protected_bytes;
    memcpy(decoder_stream.data, header, makocode::ECC_HEADER_TOTAL_BYTES);
    memcpy(decoder_stream.data + makocode::ECC_HEADER_TOTAL_BYTES,
           payload_stream.data,
           (usize)protected_bytes);

    makocode::ByteBuffer decoder_erasures;
    if (!decoder_erasures.ensure(decoder_stream.size)) return false;
    decoder_erasures.size = decoder_stream.size;
    memset(decoder_erasures.data, 0, makocode::ECC_HEADER_TOTAL_BYTES);
    for (usize i = 0u; i < (usize)protected_bytes; ++i) {
        decoder_erasures.data[makocode::ECC_HEADER_TOTAL_BYTES + i] =
            i < payload_erasures.size ? payload_erasures.data[i] : 0u;
    }

    makocode::DecoderContext decoder;
    result.parsed = decoder.parse(decoder_stream.data,
                                  (u64)decoder_stream.size * 8u,
                                  0,
                                  0u,
                                  decoder_erasures.data,
                                  decoder_erasures.size,
                                  0u,
                                  0,
                                  0u,
                                  0u,
                                  0u,
                                  true,
                                  true,
                                  makocode::LZMA_SIZE_FROM_HEADER,
                                  false);
    result.partial = decoder.partial_payload;
    result.unrecoverable_blocks = decoder.ecc_statistics().unrecoverable_blocks;
    return true;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "blob erasure harness: expected one mutated PPM path\n");
        return 2;
    }
    if (!test_uniform_region_detector()) {
        fprintf(stderr, "blob erasure harness: uniform-region thresholds failed\n");
        return 1;
    }
    BlobDecodeResult without_detection = {};
    BlobDecodeResult with_detection = {};
    if (!decode_blob_fixture(argv[1], false, without_detection) ||
        !decode_blob_fixture(argv[1], true, with_detection)) {
        fprintf(stderr, "blob erasure harness: failed to decode mutation fixture\n");
        return 1;
    }
    if ((without_detection.parsed && !without_detection.partial &&
         without_detection.unrecoverable_blocks == 0u) ||
        without_detection.unrecoverable_blocks == 0u) {
        fprintf(stderr,
                "blob erasure harness: mutation did not exceed unknown-error ECC capacity\n");
        return 1;
    }
    if (!with_detection.parsed || with_detection.partial ||
        with_detection.unrecoverable_blocks != 0u ||
        with_detection.erased_symbols <= without_detection.erased_symbols) {
        fprintf(stderr,
                "blob erasure harness: detected positions did not recover all ECC blocks "
                "(unknown parsed=%u partial=%u blocks=%llu erased=%llu; "
                "detected parsed=%u partial=%u blocks=%llu erased=%llu)\n",
                without_detection.parsed ? 1u : 0u,
                without_detection.partial ? 1u : 0u,
                (unsigned long long)without_detection.unrecoverable_blocks,
                (unsigned long long)without_detection.erased_symbols,
                with_detection.parsed ? 1u : 0u,
                with_detection.partial ? 1u : 0u,
                (unsigned long long)with_detection.unrecoverable_blocks,
                (unsigned long long)with_detection.erased_symbols);
        return 1;
    }
    puts("blob erasure harness: unknown-position failure and detected-position recovery passed");
    return 0;
}
