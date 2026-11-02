// Copyright (C) 2026 Justin Alan Bass.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdio.h>
#define main makocode_cli_main
#include "../makocode.cpp"
#undef main

int main() {
    const u16 data_symbols = 32u;
    const u16 parity_symbols = 12u;
    const u16 codeword_length = (u16)(data_symbols + parity_symbols);
    u8 generator[makocode::RS_POLY_CAPACITY];
    u16 generator_length = 0u;
    if (!makocode::rs_build_generator(parity_symbols, generator, generator_length) ||
        generator_length != (u16)(parity_symbols + 1u)) {
        fprintf(stderr, "rs erasures: generator setup failed\n");
        return 1;
    }

    u8 original[makocode::RS_FIELD_SIZE] = {};
    for (u16 i = 0u; i < data_symbols; ++i) {
        original[i] = (u8)(((u32)i * 37u + 11u) & 0xFFu);
    }
    makocode::rs_compute_parity(generator,
                                parity_symbols,
                                original,
                                data_symbols,
                                original + data_symbols);
    u8 syndromes[makocode::RS_POLY_CAPACITY];
    bool all_zero = false;
    makocode::rs_compute_syndromes(original,
                                   codeword_length,
                                   parity_symbols,
                                   syndromes,
                                   all_zero);
    if (!all_zero) {
        fprintf(stderr, "rs erasures: generated codeword is invalid\n");
        return 1;
    }

    u8 damaged[makocode::RS_FIELD_SIZE];
    u16 corrected = 0u;
    u16 erasures_at_limit[12];
    memcpy(damaged, original, sizeof(original));
    for (u16 i = 0u; i < parity_symbols; ++i) {
        erasures_at_limit[i] = (u16)((i * 3u + 1u) % codeword_length);
        damaged[erasures_at_limit[i]] ^= (u8)(0x51u + i);
    }
    if (!makocode::rs_decode_block(damaged,
                                   data_symbols,
                                   parity_symbols,
                                   &corrected,
                                   erasures_at_limit,
                                   parity_symbols) ||
        memcmp(damaged, original, codeword_length) != 0 ||
        corrected != parity_symbols) {
        fprintf(stderr, "rs erasures: p erasures did not recover\n");
        return 1;
    }

    u16 too_many_erasures[13];
    memcpy(damaged, original, sizeof(original));
    for (u16 i = 0u; i < 13u; ++i) {
        too_many_erasures[i] = (u16)((i * 3u + 2u) % codeword_length);
        damaged[too_many_erasures[i]] ^= (u8)(0x71u + i);
    }
    if (makocode::rs_decode_block(damaged,
                                  data_symbols,
                                  parity_symbols,
                                  &corrected,
                                  too_many_erasures,
                                  13u)) {
        fprintf(stderr, "rs erasures: p+1 erasures unexpectedly decoded\n");
        return 1;
    }

    const u16 mixed_erasures[6] = {1u, 8u, 15u, 22u, 29u, 36u};
    const u16 mixed_errors[3] = {3u, 10u, 17u};
    memcpy(damaged, original, sizeof(original));
    for (u16 i = 0u; i < 6u; ++i) {
        damaged[mixed_erasures[i]] ^= (u8)(0x91u + i);
    }
    for (u16 i = 0u; i < 3u; ++i) {
        damaged[mixed_errors[i]] ^= (u8)(0x31u + i);
    }
    if (!makocode::rs_decode_block(damaged,
                                   data_symbols,
                                   parity_symbols,
                                   &corrected,
                                   mixed_erasures,
                                   6u) ||
        memcmp(damaged, original, codeword_length) != 0 ||
        corrected != 9u) {
        fprintf(stderr, "rs erasures: s+2e=p mixed case did not recover\n");
        return 1;
    }

    const u16 over_erasures[5] = {0u, 8u, 16u, 24u, 32u};
    const u16 over_errors[4] = {4u, 12u, 20u, 28u};
    memcpy(damaged, original, sizeof(original));
    for (u16 i = 0u; i < 5u; ++i) {
        damaged[over_erasures[i]] ^= (u8)(0xA1u + i);
    }
    for (u16 i = 0u; i < 4u; ++i) {
        damaged[over_errors[i]] ^= (u8)(0xC1u + i);
    }
    if (makocode::rs_decode_block(damaged,
                                  data_symbols,
                                  parity_symbols,
                                  &corrected,
                                  over_erasures,
                                  5u)) {
        fprintf(stderr, "rs erasures: s+2e=p+1 mixed case unexpectedly decoded\n");
        return 1;
    }

    u16 duplicate_erasures[2] = {4u, 4u};
    memcpy(damaged, original, sizeof(original));
    damaged[4u] ^= 0x5Au;
    if (makocode::rs_decode_block(damaged,
                                  data_symbols,
                                  parity_symbols,
                                  &corrected,
                                  duplicate_erasures,
                                  2u)) {
        fprintf(stderr, "rs erasures: duplicate position was accepted\n");
        return 1;
    }

    u16 out_of_range_erasure[1] = {codeword_length};
    if (makocode::rs_decode_block(original,
                                  data_symbols,
                                  parity_symbols,
                                  &corrected,
                                  out_of_range_erasure,
                                  1u)) {
        fprintf(stderr, "rs erasures: out-of-range position was accepted\n");
        return 1;
    }

    const u16 large_data_symbols = 26u;
    const u16 large_parity_symbols = 229u;
    const u16 large_codeword_length = 255u;
    const u16 large_erasure_count = 69u;
    const u16 large_unknown_count = 80u;
    u8 large_original[makocode::RS_FIELD_SIZE] = {};
    u8 large_damaged[makocode::RS_FIELD_SIZE] = {};
    u8 large_generator[makocode::RS_POLY_CAPACITY];
    u16 large_generator_length = 0u;
    u16 large_erasures[large_erasure_count];
    if (!makocode::rs_build_generator(large_parity_symbols,
                                      large_generator,
                                      large_generator_length) ||
        !large_generator_length) {
        fprintf(stderr, "rs erasures: high-parity generator setup failed\n");
        return 1;
    }
    for (u16 i = 0u; i < large_data_symbols; ++i) {
        large_original[i] = (u8)(((u32)i * 91u + 43u) & 0xFFu);
    }
    makocode::rs_compute_parity(large_generator,
                                large_parity_symbols,
                                large_original,
                                large_data_symbols,
                                large_original + large_data_symbols);
    memcpy(large_damaged, large_original, sizeof(large_original));
    for (u16 i = 0u; i < large_erasure_count; ++i) {
        large_erasures[i] = i;
        large_damaged[i] ^= (u8)(0x21u + i);
    }
    u8 pure_erasure_case[makocode::RS_FIELD_SIZE];
    memcpy(pure_erasure_case, large_damaged, sizeof(pure_erasure_case));
    if (!makocode::rs_decode_block(pure_erasure_case,
                                   large_data_symbols,
                                   large_parity_symbols,
                                   &corrected,
                                   large_erasures,
                                   large_erasure_count) ||
        memcmp(pure_erasure_case, large_original, large_codeword_length) != 0) {
        fprintf(stderr, "rs erasures: high-parity pure e=69 case did not recover\n");
        return 1;
    }
    for (u16 i = 0u; i < large_unknown_count; ++i) {
        u16 position = (u16)(large_erasure_count + i);
        large_damaged[position] ^= (u8)(i + 1u);
    }
    if (!makocode::rs_decode_block(large_damaged,
                                   large_data_symbols,
                                   large_parity_symbols,
                                   &corrected,
                                   large_erasures,
                                   large_erasure_count) ||
        memcmp(large_damaged, large_original, large_codeword_length) != 0 ||
        corrected != (u16)(large_erasure_count + large_unknown_count)) {
        fprintf(stderr, "rs erasures: high-parity e=69, t=80 boundary did not recover\n");
        return 1;
    }

    puts("rs erasures: boundary, mixed, duplicate, and range checks passed");
    return 0;
}
