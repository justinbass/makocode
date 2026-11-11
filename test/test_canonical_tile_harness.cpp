// Copyright (C) 2026 Justin Alan Bass.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include <stdio.h>
#define main makocode_cli_main
#include "../makocode.cpp"
#undef main

static bool parse_u32_token(PpmParserState& state, u32& value) {
    const char* token = 0;
    usize length = 0u;
    u64 parsed = 0u;
    if (!ppm_next_token(state, &token, &length) ||
        !ascii_to_u64(token, length, &parsed) ||
        parsed > 0xFFFFFFFFull) {
        return false;
    }
    value = (u32)parsed;
    return true;
}

int main(int argc, char** argv) {
    if (argc == 2 && ascii_equals_token(argv[1], ascii_length(argv[1]), "--test-variable-fields")) {
        const u64 expected[] = {0u, 1u, 65535u, 65536u, 0x123456789ABull, U64_MAX_VALUE};
        for (usize i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) {
            MetadataTile::BitWriter writer;
            if (!MetadataTile::write_length_count(writer, expected[i])) return 1;
            MetadataTile::BitReader reader;
            reader.reset(writer.data(), writer.bit_size());
            u64 actual = 0u;
            if (!MetadataTile::read_length_count(reader, actual) || actual != expected[i] ||
                reader.failed || reader.cursor != writer.bit_size()) return 1;
        }
        return 0;
    }
    if (argc == 2 && ascii_equals_token(argv[1], ascii_length(argv[1]), "--test-metadata-rs")) {
        makocode::ByteBuffer data;
        if (!data.ensure(1000u)) return 1;
        data.size = 1000u;
        for (usize i = 0u; i < data.size; ++i) data.data[i] = (u8)(i * 73u + 19u);
        makocode::ByteBuffer codeword;
        if (!MetadataTile::rs_encode_with_parity(data, 232u, codeword) ||
            codeword.size != 11208u) return 1;
        makocode::ByteBuffer damaged;
        if (!damaged.ensure(codeword.size)) return 1;
        damaged.size = codeword.size;
        memcpy(damaged.data, codeword.data, codeword.size);
        usize input_offset = 0u;
        for (u32 block = 0u; block < 44u; ++block) {
            usize count = block == 43u ? 11u : 23u;
            damaged.data[input_offset + count / 2u] ^= (u8)(block + 1u);
            input_offset += count + 232u;
        }
        u32 corrections = 0u;
        if (!MetadataTile::rs_decode_with_parity(damaged, 1000u, 232u, corrections) ||
            corrections != 44u || damaged.size != data.size ||
            memcmp(damaged.data, data.data, data.size) != 0) return 1;
        if (!damaged.ensure(codeword.size)) return 1;
        damaged.size = codeword.size;
        memcpy(damaged.data, codeword.data, codeword.size);
        for (u32 i = 0u; i < 117u; ++i) damaged.data[i] ^= (u8)(i + 1u);
        corrections = 0u;
        if (MetadataTile::rs_decode_with_parity(damaged, 1000u, 232u, corrections)) return 1;
        return 0;
    }
    if (argc == 2 && ascii_equals_token(argv[1], ascii_length(argv[1]), "--test-marker-layout")) {
        for (u32 palette_case = 0u; palette_case < 4u; ++palette_case) {
            MetadataTile::Values values;
            values.page_bits = 1u;
            values.page_count = 1u;
            values.page_index = 1u;
            values.page_width_pixels = MetadataTile::TILE_SIDE;
            values.page_height_pixels = MetadataTile::TILE_SIDE;
            values.fiducial_marker_size_pixels = 4u;
            values.fiducial_columns = 1u;
            values.fiducial_rows = 1u;
            values.overwrite_layout = true;
            values.palette_background_first = true;
            values.palette_count = palette_case == 2u ? 4u :
                                   palette_case == 3u ? 3u : 2u;
            if (palette_case == 0u) {
                values.palette[0] = PaletteColor{255u, 255u, 255u};
                values.palette[1] = PaletteColor{0u, 255u, 255u};
            } else if (palette_case == 1u) {
                values.palette[0] = PaletteColor{0u, 255u, 255u};
                values.palette[1] = PaletteColor{255u, 255u, 255u};
            } else if (palette_case == 2u) {
                values.palette[0] = PaletteColor{255u, 255u, 255u};
                values.palette[1] = PaletteColor{0u, 255u, 255u};
                values.palette[2] = PaletteColor{255u, 0u, 255u};
                values.palette[3] = PaletteColor{255u, 255u, 0u};
            } else {
                values.palette[0] = PaletteColor{255u, 255u, 255u};
                values.palette[1] = PaletteColor{0u, 255u, 255u};
                values.palette[2] = PaletteColor{255u, 0u, 255u};
            }
            makocode::ByteBuffer bits;
            makocode::ByteBuffer active;
            if (!MetadataTile::build_tile_bits(values, bits, &active) ||
                values.metadata_square_side == 0u ||
                values.metadata_square_side > MetadataTile::TILE_MAX_METADATA_SQUARE_SIDE) {
                fprintf(stderr, "marker build failed case=%u\n", palette_case); return 1;
            }
            usize metadata_cells = (usize)values.metadata_length_prefix_bits +
                                   values.metadata_symbol_count;
            usize square_cells = (usize)values.metadata_square_side * values.metadata_square_side;
            u32 expected_side = 1u;
            while ((usize)expected_side * expected_side < metadata_cells) ++expected_side;
            if (metadata_cells > square_cells || values.metadata_square_side != expected_side ||
                values.metadata_square_y0 != values.metadata_tag_y0 + MetadataTile::TILE_HEADER_ROWS ||
                values.metadata_tile_height != values.metadata_square_side + MetadataTile::TILE_HEADER_ROWS) {
                fprintf(stderr, "marker square failed case=%u\n", palette_case); return 1;
            }
            u32 palette_tag_x0 = MetadataTile::palette_tag_x0(values.palette_count);
            for (u32 x = 0u; x <= values.palette_count; ++x) {
                u8 expected = x < values.palette_count ? (u8)x : 0u;
                if (bits.data[(usize)values.metadata_tag_y0 * MetadataTile::TILE_SIDE +
                              palette_tag_x0 + x] != expected) {
                    fprintf(stderr, "palette row failed case=%u\n", palette_case); return 1;
                }
            }
            u32 marker_x0 = MetadataTile::tag_marker_x0();
            for (u32 bit = 0u; bit < 32u; ++bit) {
                usize index = (usize)(values.metadata_tag_y0 + 1u + bit / 8u) *
                                  MetadataTile::TILE_SIDE + marker_x0 + (bit & 7u);
                u8 expected = MetadataTile::makocode_tag_bit(bit)
                                  ? (u8)(values.palette_count - 1u) : 0u;
                if (bits.data[index] != expected) { fprintf(stderr, "marker bits failed case=%u\n", palette_case); return 1; }
            }
            u8 tile_rgb[MetadataTile::TILE_SIDE * MetadataTile::TILE_SIDE * 3u];
            double luminances[MetadataTile::TILE_SIDE * MetadataTile::TILE_SIDE];
            for (usize cell = 0u; cell < bits.size; ++cell) {
                u8 symbol = bits.data[cell];
                if (symbol >= values.palette_count) { fprintf(stderr, "symbol range failed case=%u\n", palette_case); return 1; }
                const PaletteColor& color = values.palette[symbol];
                tile_rgb[cell * 3u] = color.r;
                tile_rgb[cell * 3u + 1u] = color.g;
                tile_rgb[cell * 3u + 2u] = color.b;
                luminances[cell] = MetadataTile::rgb_luminance_u8(color.r, color.g, color.b);
            }
            MetadataTile::Values decoded;
            makocode::ByteBuffer palette_text;
            PaletteColor found_palette[MAX_CUSTOM_PALETTE_COLORS];
            u32 found_palette_count = 0u;
            double found_threshold = 0.0;
            bool found_row = MetadataTile::decode_palette_tag_row(
                tile_rgb + (usize)values.metadata_tag_y0 * MetadataTile::TILE_SIDE * 3u,
                found_palette, found_palette_count, false, &found_threshold);
            if (!found_row || found_palette_count != values.palette_count) {
                fprintf(stderr, "palette row decode failed case=%u count=%u\n",
                        palette_case, found_palette_count); return 1;
            }
            u32 marker_errors = 0u;
            for (u32 bit = 0u; bit < 32u; ++bit) {
                usize index = (usize)(values.metadata_tag_y0 + 1u + bit / 8u) *
                                  MetadataTile::TILE_SIDE + marker_x0 + (bit & 7u);
                bool observed = luminances[index] < found_threshold;
                if (observed != MetadataTile::makocode_tag_bit(bit)) ++marker_errors;
            }
            if (marker_errors) {
                if (palette_case != 1u || marker_errors != 32u) {
                    fprintf(stderr, "marker threshold errors case=%u count=%u threshold=%.2f\n",
                            palette_case, marker_errors, found_threshold); return 1;
                }
            }
            if (!MetadataTile::decode_tile_samples(tile_rgb, luminances, decoded, palette_text) ||
                decoded.palette_count != values.palette_count ||
                decoded.metadata_square_side != values.metadata_square_side) {
                fprintf(stderr, "marker decode failed case=%u side=%u tag=%u bitcount=%u symbols=%u parity=%u\n",
                        palette_case, values.metadata_square_side, values.metadata_tag_y0,
                        values.metadata_bit_count, values.metadata_symbol_count,
                        values.metadata_ecc_bytes); return 1;
            }
        }
        return 0;
    }
    if (argc == 4 && ascii_equals_token(argv[1], ascii_length(argv[1]), "--capacity")) {
        u64 width_value = 0u;
        u64 height_value = 0u;
        if (!ascii_to_u64(argv[2], ascii_length(argv[2]), &width_value) ||
            !ascii_to_u64(argv[3], ascii_length(argv[3]), &height_value) ||
            width_value == 0u ||
            height_value == 0u ||
            width_value > 0xFFFFFFFFull || height_value > 0xFFFFFFFFull) {
            fprintf(stderr, "canonical tile: invalid capacity dimensions\n");
            return 2;
        }
        u32 width = (u32)width_value;
        u32 height = (u32)height_value;
        makocode::ByteBuffer mask;
        u64 reserved_pixels = 0u;
        if (!compute_fiducial_reservation(width,
                                          height,
                                          height,
                                          reserved_pixels,
                                          &mask,
                                          false,
                                          0u,
                                          0u,
                                          0u,
                                          0u)) {
            fprintf(stderr, "canonical tile: canonical capacity mask failed\n");
            return 1;
        }
        u64 overwrite_slots = 0u;
        for (usize i = 0u; i < mask.size; ++i) {
            if (mask.data[i] == OVERWRITE_MASK_PIXEL) {
                ++overwrite_slots;
            } else if (mask.data[i] != 0u) {
                fprintf(stderr, "canonical tile: unexpected reserved region in payload capacity\n");
                return 1;
            }
        }
        MetadataTile::Placement center = MetadataTile::compute_center_tile_placement(width, height);
        u32 expected_x0 = width >= MetadataTile::TILE_SIDE
                              ? (width - MetadataTile::TILE_SIDE) / 2u : 0u;
        u32 expected_y0 = height >= MetadataTile::TILE_SIDE
                              ? (height - MetadataTile::TILE_SIDE) / 2u : 0u;
        u32 expected_offset_x = width < MetadataTile::TILE_SIDE
                                    ? (MetadataTile::TILE_SIDE - width) / 2u : 0u;
        u32 expected_offset_y = height < MetadataTile::TILE_SIDE
                                    ? (MetadataTile::TILE_SIDE - height) / 2u : 0u;
        if (!center.valid || center.x0 != expected_x0 || center.y0 != expected_y0 ||
            center.offset_x != expected_offset_x || center.offset_y != expected_offset_y ||
            overwrite_slots != reserved_pixels || overwrite_slots > (u64)width * height) {
            fprintf(stderr, "canonical tile: invalid centered layout capacity\n");
            return 1;
        }
        return 0;
    }
    bool check_ecc_budget = argc == 7 &&
                            ascii_equals_token(argv[5], ascii_length(argv[5]), "--check-ecc-budget");
    bool check_reserve_reject = argc == 6 &&
                                ascii_equals_token(argv[5], ascii_length(argv[5]), "--check-reserve-reject");
    bool check_structural = argc == 6 &&
                            ascii_equals_token(argv[5], ascii_length(argv[5]), "--check-structural");
    bool report_page = argc == 6 &&
                       ascii_equals_token(argv[5], ascii_length(argv[5]), "--report-page");
    bool report_page_direct = argc == 6 &&
                              ascii_equals_token(argv[5], ascii_length(argv[5]), "--report-page-direct");
    bool report_metadata_layout = argc == 6 &&
                                  ascii_equals_token(argv[5], ascii_length(argv[5]), "--metadata-layout");
    if (argc != 5 && !check_ecc_budget && !check_reserve_reject && !check_structural &&
        !report_page && !report_page_direct && !report_metadata_layout) {
        fprintf(stderr, "canonical tile: usage: harness PPM WIDTH HEIGHT EXPECT_CURRENT [--check-ecc-budget PERCENT | --check-reserve-reject | --check-structural | --report-page | --report-page-direct] | --capacity WIDTH HEIGHT\n");
        return 2;
    }
    u32 expected_width = 0u;
    u32 expected_height = 0u;
    u64 expected_width_value = 0u;
    u64 expected_height_value = 0u;
    u64 expect_canonical_value = 0u;
    if (!ascii_to_u64(argv[2], ascii_length(argv[2]), &expected_width_value) ||
        !ascii_to_u64(argv[3], ascii_length(argv[3]), &expected_height_value) ||
        !ascii_to_u64(argv[4], ascii_length(argv[4]), &expect_canonical_value) ||
        expected_width_value > 0xFFFFFFFFull ||
        expected_height_value > 0xFFFFFFFFull || expect_canonical_value != 1u) {
        fprintf(stderr, "canonical tile: invalid expected dimensions/current tag\n");
        return 2;
    }
    expected_width = (u32)expected_width_value;
    expected_height = (u32)expected_height_value;

    makocode::ByteBuffer input;
    if (!read_entire_file(argv[1], input) || !input.data || input.size == 0u) {
        fprintf(stderr, "canonical tile: failed to read PPM\n");
        return 1;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    const char* token = 0;
    usize token_length = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 max_value = 0u;
    if (!ppm_next_token(state, &token, &token_length) ||
        !ascii_equals_token(token, token_length, "P6") ||
        !parse_u32_token(state, width) ||
        !parse_u32_token(state, height) ||
        !parse_u32_token(state, max_value) ||
        width != expected_width || height != expected_height || max_value != 255u ||
        !ppm_consume_binary_separator(state)) {
        fprintf(stderr, "canonical tile: invalid PPM header\n");
        return 1;
    }
    u64 pixel_count = (u64)width * height;
    makocode::ByteBuffer pixels;
    if (!ppm_read_rgb_pixels(state, pixel_count, pixels)) {
        fprintf(stderr, "canonical tile: failed to parse PPM pixels\n");
        return 1;
    }

    if (report_page_direct || report_metadata_layout) {
        MetadataTile::Values direct_values = {};
        makocode::ByteBuffer direct_palette_text;
        bool direct_tile_decoded = false;
        for (u32 footer_rows = 0u; footer_rows <= 32u && footer_rows < height;
             ++footer_rows) {
            u32 data_height = height - footer_rows;
            MetadataTile::Placement direct_placement =
                MetadataTile::compute_center_tile_placement(width, data_height);
            if (direct_placement.valid &&
                MetadataTile::decode_tile(pixels.data,
                                          width,
                                          height,
                                          data_height,
                                          direct_placement,
                                          direct_values,
                                          direct_palette_text)) {
                direct_tile_decoded = true;
                break;
            }
        }
        usize metadata_cells = 0u;
        u32 expected_metadata_side = 0u;
        if (!direct_tile_decoded ||
            direct_values.schema_version != MetadataTile::TILE_SCHEMA_VERSION ||
            !direct_values.overwrite_layout || !direct_values.ecc_enabled) {
            fprintf(stderr, "canonical tile: protected tile failed at expected placement\n");
            return 1;
        }
        if (report_metadata_layout) {
            printf("%u %u %u %u %u %u\n",
                   direct_values.metadata_square_x0,
                   direct_values.metadata_square_y0,
                   direct_values.metadata_square_side,
                   direct_values.metadata_length_prefix_bits,
                   direct_values.metadata_tag_y0,
                   direct_values.metadata_symbol_count);
            return 0;
        }
        metadata_cells = (usize)direct_values.metadata_length_prefix_bits +
                         (usize)direct_values.metadata_symbol_count;
        expected_metadata_side = 1u;
        while ((usize)expected_metadata_side * expected_metadata_side < metadata_cells) {
            ++expected_metadata_side;
        }
        if (direct_values.schema_version != 0u ||
            direct_values.metadata_square_side != expected_metadata_side ||
            direct_values.metadata_square_y0 != direct_values.metadata_tag_y0 + MetadataTile::TILE_HEADER_ROWS ||
            direct_values.metadata_tile_height != direct_values.metadata_square_side + MetadataTile::TILE_HEADER_ROWS ||
            direct_values.metadata_tile_width >= MetadataTile::TILE_SIDE ||
            direct_values.metadata_tile_height >= MetadataTile::TILE_SIDE) {
            fprintf(stderr, "canonical tile: metadata is not a compact schema-0 square\n");
            return 1;
        }
        return 0;
    }

    ImageMappingConfig mapping;
    makocode::ByteBuffer frame;
    makocode::ByteBuffer frame_erasures;
    makocode::ByteBuffer selector_counts;
    u64 frame_bits = 0u;
    u64 selector_seed = 0u;
    u16 unknown_budget = 0u;
    u16 overlay_budget = 0u;
    PpmParserState extracted_metadata;
    if (!ppm_extract_frame_bits(input,
                                mapping,
                                frame,
                                frame_bits,
                                extracted_metadata,
                                false,
                                (bool*)0,
                                (makocode::ByteBuffer*)0,
                                1u,
                                &frame_erasures,
                                &selector_seed,
                                &selector_counts,
                                &unknown_budget,
                                &overlay_budget)) {
        fprintf(stderr, "canonical tile: failed to extract current-format page metadata\n");
        return 1;
    }
    u32 data_height = height;
    if (extracted_metadata.has_footer_rows &&
        extracted_metadata.footer_rows_value < (u64)height) {
        data_height = height - (u32)extracted_metadata.footer_rows_value;
    }
    MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(width, data_height);
    MetadataTile::Values values = {};
    makocode::ByteBuffer palette_text;
    if (!placement.valid ||
        !MetadataTile::decode_tile(pixels.data,
                                   width,
                                   data_height,
                                   height,
                                   placement,
                                   values,
                                   palette_text)) {
        fprintf(stderr, "canonical tile: protected tile failed at expected placement\n");
        return 1;
    }
    if (placement.x0 != (width - MetadataTile::TILE_SIDE) / 2u ||
        placement.y0 != (data_height - MetadataTile::TILE_SIDE) / 2u ||
        values.schema_version != MetadataTile::TILE_SCHEMA_VERSION || !values.overwrite_layout ||
        !values.ecc_enabled || values.ecc_parity == 0u ||
        (check_structural && values.metadata_ecc_bytes != 0u) ||
        values.page_index == 0u || values.page_index > values.page_count ||
        (check_structural && (values.page_count != 1u || values.page_index != 1u ||
                              values.overlay_selector_present))) {
        fprintf(stderr, "canonical tile: expected centered current-format tile with structural ECC\n");
        return 1;
    }
    if (check_structural) {
        bool fiducial_crosses_tile = false;
        double min_x = (double)values.fiducial_margin_pixels;
        double max_x = (double)(width - 1u - values.fiducial_margin_pixels);
        double min_y = (double)values.fiducial_margin_pixels;
        double max_y = (double)(data_height - 1u - values.fiducial_margin_pixels);
        for (u32 row = 0u; row < values.fiducial_rows && !fiducial_crosses_tile; ++row) {
            double ty = values.fiducial_rows == 1u
                            ? 0.5
                            : (double)row / (double)(values.fiducial_rows - 1u);
            double center_y = min_y + (max_y - min_y) * ty;
            int start_y = (int)(center_y - ((double)values.fiducial_marker_size_pixels - 1.0) * 0.5);
            for (u32 column = 0u; column < values.fiducial_columns && !fiducial_crosses_tile; ++column) {
                double tx = values.fiducial_columns == 1u
                                ? 0.5
                                : (double)column / (double)(values.fiducial_columns - 1u);
                double center_x = min_x + (max_x - min_x) * tx;
                int start_x = (int)(center_x - ((double)values.fiducial_marker_size_pixels - 1.0) * 0.5);
                fiducial_crosses_tile =
                    start_x < (int)(placement.x0 + MetadataTile::TILE_SIDE) &&
                    start_x + (int)values.fiducial_marker_size_pixels > (int)placement.x0 &&
                    start_y < (int)(placement.y0 + MetadataTile::TILE_SIDE) &&
                    start_y + (int)values.fiducial_marker_size_pixels > (int)placement.y0;
            }
        }
        if (!fiducial_crosses_tile) {
            fprintf(stderr, "canonical tile: structural case has no fiducial-tile collision\n");
            return 1;
        }
    }
    if (check_ecc_budget || check_reserve_reject || check_structural || report_page) {
        u64 requested_percent = 0u;
        if ((check_ecc_budget &&
             (!ascii_to_u64(argv[6], ascii_length(argv[6]), &requested_percent) ||
              requested_percent == 0u || requested_percent > 10000u)) ||
            !values.ecc_enabled ||
            ((check_ecc_budget || check_reserve_reject || check_structural) && values.page_count != 1u)) {
            fprintf(stderr, "canonical tile: invalid ECC budget check arguments or tile metadata\n");
            return 2;
        }
        u16 baseline_data = 0u;
        u16 baseline_parity = 0u;
        u64 baseline_blocks = 0u;
        if (check_ecc_budget &&
            !makocode::compute_ecc_layout((usize)values.ecc_original_bytes,
                                          (double)requested_percent / 100.0,
                                          baseline_data,
                                          baseline_parity,
                                          baseline_blocks)) {
            fprintf(stderr, "canonical tile: failed to derive requested ECC baseline\n");
            return 1;
        }
        (void)baseline_data;
        (void)baseline_blocks;
        u16 expected_unknown_budget = check_ecc_budget
                                          ? (u16)(2u * ((u32)baseline_parity / 2u))
                                          : 0u;
        u16 expected_overlay_budget = check_ecc_budget
                                          ? (u16)((u32)baseline_parity / 2u)
                                          : 0u;
        if ((check_ecc_budget && (unknown_budget != expected_unknown_budget ||
                                  overlay_budget != expected_overlay_budget)) ||
            (values.overlay_selector_present
                 ? selector_counts.size != values.ecc_block_count
                 : selector_counts.size != 0u)) {
            fprintf(stderr, "canonical tile: tile selector and ECC budgets do not preserve requested baseline\n");
            return 1;
        }
        for (usize block = 0u; !report_page && values.overlay_selector_present &&
                               block < selector_counts.size; ++block) {
            if (selector_counts.data[block] != overlay_budget) {
                fprintf(stderr, "canonical tile: decoder did not reconstruct the full deterministic selector set\n");
                return 1;
            }
        }
        u64 payload_bits = 0u;
        if (!report_page) {
            makocode::ByteBuffer payload;
            makocode::ByteBuffer erased_symbols;
            if (!frame_bits_to_payload(frame.data,
                                       frame_bits,
                                       extracted_metadata,
                                       payload,
                                       payload_bits,
                                       (u64*)0,
                                       frame_erasures.data,
                                       frame_bits,
                                       &erased_symbols)) {
                fprintf(stderr, "canonical tile: failed to recover erased payload symbols\n");
                return 1;
            }
            usize block_total = (usize)values.ecc_block_data + (usize)values.ecc_parity;
            makocode::ByteBuffer parse_erasures;
            if (check_reserve_reject &&
                !parse_erasures.append_bytes(erased_symbols.data, erased_symbols.size)) {
                fprintf(stderr, "canonical tile: could not copy parser erasures\n");
                return 1;
            }
            if (block_total == 0u || values.ecc_block_count == 0u ||
                values.ecc_block_count > (u64)(USIZE_MAX_VALUE / block_total) ||
                (usize)values.ecc_block_count * block_total != payload.size ||
                erased_symbols.size != payload.size ||
                !makocode::fisher_yates_unshuffle(erased_symbols.data, erased_symbols.size)) {
                fprintf(stderr, "canonical tile: invalid extracted ECC symbol layout\n");
                return 1;
            }
            u64 reserve_reject_budget = 0u;
            for (u64 block = 0u; block < values.ecc_block_count; ++block) {
                usize block_start = (usize)block * block_total;
                u32 structural_erasures = 0u;
                for (usize symbol = 0u; symbol < block_total; ++symbol) {
                    if (erased_symbols.data[block_start + symbol]) ++structural_erasures;
                }
                u32 overlay_erasures = values.overlay_selector_present
                                           ? (u32)selector_counts.data[(usize)block]
                                           : 0u;
                if (structural_erasures + overlay_erasures +
                        (u32)unknown_budget > values.ecc_parity) {
                    fprintf(stderr, "canonical tile: structural erasures consumed unknown-error ECC reserve\n");
                    return 1;
                }
                if (check_reserve_reject) {
                    u64 candidate = (u64)values.ecc_parity -
                                    structural_erasures - unknown_budget + 1u;
                    if (candidate <= values.ecc_parity && candidate > overlay_budget) {
                        reserve_reject_budget = candidate;
                    }
                }
            }
            if (check_reserve_reject) {
                if (!values.overlay_selector_present ||
                    selector_counts.size != values.ecc_block_count ||
                    reserve_reject_budget == 0u) {
                    fprintf(stderr, "canonical tile: test page has no valid selector reserve boundary\n");
                    return 1;
                }
                u8 header_bytes[makocode::ECC_HEADER_TOTAL_BYTES];
                makocode::ByteBuffer parse_stream;
                makocode::ByteBuffer synthetic_parse_erasures;
                if ((payload_bits & 7u) != 0u ||
                    payload_bits > U64_MAX_VALUE - (u64)makocode::ECC_HEADER_TOTAL_BITS ||
                    payload.size > USIZE_MAX_VALUE - makocode::ECC_HEADER_TOTAL_BYTES ||
                    !makocode::build_ecc_header_bytes(header_bytes,
                                                      values.ecc_block_data,
                                                      values.ecc_parity,
                                                      values.ecc_block_count,
                                                      values.ecc_original_bytes,
                                                      values.ecc_header_magic,
                                                      values.ecc_header_version,
                                                      values.ecc_header_flags,
                                                      values.ecc_header_reserved) ||
                    !parse_stream.ensure(makocode::ECC_HEADER_TOTAL_BYTES + payload.size) ||
                    !synthetic_parse_erasures.ensure(makocode::ECC_HEADER_TOTAL_BYTES + parse_erasures.size)) {
                    fprintf(stderr, "canonical tile: could not build synthetic ECC parser input\n");
                    return 1;
                }
                parse_stream.size = makocode::ECC_HEADER_TOTAL_BYTES + payload.size;
                memcpy(parse_stream.data, header_bytes, makocode::ECC_HEADER_TOTAL_BYTES);
                memcpy(parse_stream.data + makocode::ECC_HEADER_TOTAL_BYTES,
                       payload.data,
                       payload.size);
                synthetic_parse_erasures.size = makocode::ECC_HEADER_TOTAL_BYTES + parse_erasures.size;
                memset(synthetic_parse_erasures.data, 0, makocode::ECC_HEADER_TOTAL_BYTES);
                memcpy(synthetic_parse_erasures.data + makocode::ECC_HEADER_TOTAL_BYTES,
                       parse_erasures.data,
                       parse_erasures.size);
                u64 parse_bit_count = payload_bits + (u64)makocode::ECC_HEADER_TOTAL_BITS;
                makocode::ByteBuffer valid_input;
                makocode::ByteBuffer invalid_input;
                if (!valid_input.append_bytes(parse_stream.data, parse_stream.size) ||
                    !invalid_input.append_bytes(parse_stream.data, parse_stream.size)) {
                    fprintf(stderr, "canonical tile: could not copy parser regression inputs\n");
                    return 1;
                }
                makocode::DecoderContext valid_decoder;
                if (!valid_decoder.parse(valid_input.data,
                                         parse_bit_count,
                                         0,
                                         0u,
                                         synthetic_parse_erasures.data,
                                         synthetic_parse_erasures.size,
                                         selector_seed,
                                         selector_counts.data,
                                         selector_counts.size,
                                         unknown_budget,
                                         overlay_budget,
                                         true,
                                         true,
                                         values.lzma_uncompressed_bytes,
                                         values.schema_version != 0u &&
                                             values.schema_version < 4u)) {
                    fprintf(stderr, "canonical tile: valid selector reserve did not decode before reserve mutation\n");
                    return 1;
                }
                if (overlay_budget == 0u || selector_counts.size == 0u) {
                    fprintf(stderr, "canonical tile: partial selector test has no reserved symbols\n");
                    return 1;
                }
                makocode::ByteBuffer partial_input;
                makocode::ByteBuffer partial_selector_counts;
                if (!partial_input.append_bytes(parse_stream.data, parse_stream.size) ||
                    !partial_selector_counts.append_bytes(selector_counts.data,
                                                          selector_counts.size)) {
                    fprintf(stderr, "canonical tile: could not copy partial selector regression inputs\n");
                    return 1;
                }
                --partial_selector_counts.data[0u];
                makocode::DecoderContext partial_decoder;
                if (partial_decoder.parse(partial_input.data,
                                          parse_bit_count,
                                          0,
                                          0u,
                                          synthetic_parse_erasures.data,
                                          synthetic_parse_erasures.size,
                                          selector_seed,
                                          partial_selector_counts.data,
                                          partial_selector_counts.size,
                                          unknown_budget,
                                          overlay_budget,
                                          true,
                                          true,
                                          values.lzma_uncompressed_bytes) ||
                    !partial_decoder.ecc_correction_failed()) {
                    fprintf(stderr, "canonical tile: parser accepted a partial fixed selector map\n");
                    return 1;
                }
                makocode::ByteBuffer missing_selector_input;
                if (!missing_selector_input.append_bytes(parse_stream.data, parse_stream.size)) {
                    fprintf(stderr, "canonical tile: could not copy missing selector regression input\n");
                    return 1;
                }
                makocode::DecoderContext missing_selector_decoder;
                if (missing_selector_decoder.parse(missing_selector_input.data,
                                                   parse_bit_count,
                                                   0,
                                                   0u,
                                                   synthetic_parse_erasures.data,
                                                   synthetic_parse_erasures.size,
                                                   selector_seed,
                                                   0,
                                                   0u,
                                                   unknown_budget,
                                                   overlay_budget,
                                                   true,
                                                   true,
                                                   values.lzma_uncompressed_bytes) ||
                    !missing_selector_decoder.ecc_correction_failed()) {
                    fprintf(stderr, "canonical tile: parser accepted a missing fixed selector map\n");
                    return 1;
                }
                makocode::DecoderContext invalid_decoder;
                if (invalid_decoder.parse(invalid_input.data,
                                          parse_bit_count,
                                          0,
                                          0u,
                                          synthetic_parse_erasures.data,
                                          synthetic_parse_erasures.size,
                                          selector_seed,
                                          selector_counts.data,
                                          selector_counts.size,
                                          unknown_budget,
                                          (u16)reserve_reject_budget,
                                          true,
                                          true,
                                          values.lzma_uncompressed_bytes) ||
                    !invalid_decoder.ecc_correction_failed()) {
                    fprintf(stderr, "canonical tile: parser accepted impossible declared overlay reserve\n");
                    return 1;
                }
            }
        }
        if (report_page) {
            u64 selector_total = 0u;
            for (usize block = 0u; block < selector_counts.size; ++block) {
                selector_total += selector_counts.data[block];
            }
            printf("%llu\t%llu\t%llu\t%u\t%u\t%u\t%u\t%llu\t%llu\t",
                   (unsigned long long)values.page_index,
                   (unsigned long long)values.page_count,
                   (unsigned long long)values.ecc_block_count,
                   (unsigned)values.ecc_parity,
                   (unsigned)unknown_budget,
                   (unsigned)overlay_budget,
                   values.overlay_selector_present ? 1u : 0u,
                   (unsigned long long)selector_total,
                   (unsigned long long)selector_seed);
            for (usize block = 0u; block < selector_counts.size; ++block) {
                if (block) putchar(',');
                printf("%u", (unsigned)selector_counts.data[block]);
            }
            putchar('\n');
        }
        (void)payload_bits;
        (void)selector_seed;
    }
    return 0;
}
