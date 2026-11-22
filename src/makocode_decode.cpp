/*
 * Decoder command implementation.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_common.hpp"
int command_decode(int arg_count, char** args) {
    if (arguments_request_help(arg_count, args)) {
        write_decode_help();
        return 0;
    }
    g_fiducial_defaults.spacing_pixels = 24u;
    ImageMappingConfig mapping;
    static const usize MAX_INPUT_FILES = 1024u;
    const char* input_files[MAX_INPUT_FILES];
    usize file_count = 0u;
    static MetadataTile::Values encoded_pages[MAX_INPUT_FILES];
    bool encoded_page_present[MAX_INPUT_FILES] = {};
    u64 encoded_page_indices[MAX_INPUT_FILES] = {};
    usize encoded_page_count = 0u;
    bool decoded_file_requested = false;
    bool decoded_stdout_requested = false;
    u64 encoded_frame_bits = 0u;
    makocode::ByteBuffer password_buffer;
    bool have_password = false;
    bool retried_public_symbol_placement = false;
    makocode::ByteBuffer output_dir_buffer;
    const char* output_dir = ".";
    bool have_output_dir = false;
    bool debug_requested = false;
    bool recover_limit_specified = false;
    bool no_fiducials = false;
    bool no_metadata = false;
    for (int i = 0; i < arg_count; ++i) {
        bool handled = false;
        if (!process_image_mapping_option(arg_count, args, &i, mapping, "decode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        handled = false;
        if (!process_fiducial_spacing_option(arg_count, args, &i, g_fiducial_defaults, "decode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        const char* arg = args[i];
        if (ascii_equals_token(arg, ascii_length(arg), "--no-metadata")) {
            no_metadata = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--no-fiducials")) {
            no_fiducials = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--decoded-file")) {
            decoded_file_requested = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--decoded-stdout")) {
            decoded_stdout_requested = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--debug")) {
            debug_requested = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--recover-palette")) {
            mapping.recover_palette = true;
            continue;
        }
        const char recover_limit_prefix[] = "--recover-limit=";
        const char* recover_limit_text = 0;
        usize recover_limit_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--recover-limit")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "decode: --recover-limit requires a positive integer");
                return 1;
            }
            recover_limit_text = args[++i];
            recover_limit_length = ascii_length(recover_limit_text);
        } else if (ascii_starts_with(arg, recover_limit_prefix)) {
            recover_limit_text = arg + (sizeof(recover_limit_prefix) - 1u);
            recover_limit_length = ascii_length(recover_limit_text);
        }
        if (recover_limit_text) {
            bool has_nonzero_digit = false;
            bool valid_digits = recover_limit_length != 0u;
            for (usize digit = 0u; digit < recover_limit_length; ++digit) {
                if (recover_limit_text[digit] < '0' || recover_limit_text[digit] > '9') {
                    valid_digits = false;
                    break;
                }
                if (recover_limit_text[digit] != '0') has_nonzero_digit = true;
            }
            if (!valid_digits || !has_nonzero_digit) {
                console_line(2, "decode: --recover-limit must be a positive integer");
                return 1;
            }
            mapping.recover_limit_text = recover_limit_text;
            mapping.recover_limit_length = recover_limit_length;
            recover_limit_specified = true;
            continue;
        }
        
        const char output_prefix[] = "--output-dir=";
        const char* output_value = 0;
        usize output_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--output-dir")) {
            if (have_output_dir) {
                console_line(2, "decode: output directory specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "decode: --output-dir requires a non-empty path");
                return 1;
            }
            output_value = args[i + 1];
            output_length = ascii_length(output_value);
            i += 1;
        } else if (ascii_starts_with(arg, output_prefix)) {
            if (have_output_dir) {
                console_line(2, "decode: output directory specified multiple times");
                return 1;
            }
            output_value = arg + (sizeof(output_prefix) - 1u);
            output_length = ascii_length(output_value);
        }
        if (output_value) {
            if (output_length == 0u) {
                console_line(2, "decode: --output-dir requires a non-empty path");
                return 1;
            }
    if (!output_dir_buffer.ensure(output_length + 1u)) {
        console_line(2, "decode: failed to allocate output directory buffer");
        return 1;
    }
            for (usize j = 0u; j < output_length; ++j) {
                output_dir_buffer.data[j] = (u8)output_value[j];
            }
            output_dir_buffer.data[output_length] = 0u;
            output_dir_buffer.size = output_length;
            output_dir = (const char*)output_dir_buffer.data;
            have_output_dir = true;
            continue;
        }
        const char password_prefix[] = "--password=";
        const char* password_value = 0;
        usize password_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--password")) {
            if (have_password) {
                console_line(2, "decode: password specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "decode: --password requires a non-empty value");
                return 1;
            }
            password_value = args[i + 1];
            password_length = ascii_length(password_value);
            i += 1;
        } else if (ascii_starts_with(arg, password_prefix)) {
            if (have_password) {
                console_line(2, "decode: password specified multiple times");
                return 1;
            }
            password_value = arg + (sizeof(password_prefix) - 1u);
            password_length = ascii_length(password_value);
        }
        if (password_value) {
            if (password_length == 0u) {
                console_line(2, "decode: --password requires a non-empty value");
                return 1;
            }
            if (!password_buffer.ensure(password_length)) {
                console_line(2, "decode: failed to allocate password buffer");
                return 1;
            }
            for (usize j = 0u; j < password_length; ++j) {
                password_buffer.data[j] = (u8)password_value[j];
            }
            password_buffer.size = password_length;
            have_password = true;
            continue;
        }
        if (file_count >= MAX_INPUT_FILES) {
            console_line(2, "decode: too many input files");
            return 1;
        }
    input_files[file_count++] = arg;
}
    if (decoded_stdout_requested && (decoded_file_requested || debug_requested)) {
        console_line(2, "decode: --decoded-stdout cannot be combined with --decoded-file or --debug");
        return 1;
    }
    if (!mapping.recover_palette && recover_limit_specified) {
        console_line(2, "decode: --recover-limit requires --recover-palette");
        return 1;
    }
    if (no_fiducials) g_fiducial_defaults.spacing_pixels = 0u;
    if (no_metadata && file_count > 1u) {
        console_line(2, "decode: --no-metadata currently requires one page");
        return 1;
    }
    if (no_metadata && !no_fiducials) {
        console_line(2, "decode: --no-metadata requires --no-fiducials");
        return 1;
    }
    if (no_metadata && !mapping.palette_set) {
        console_line(2, "decode: --no-metadata requires the encoded --palette");
        return 1;
    }
    if (no_metadata &&
        (!mapping.page_width_set || !mapping.page_height_set)) {
        console_line(2, "decode: --no-metadata requires --page-width and --page-height");
        return 1;
    }
    const char* raw_fallback_setting =
        makocode_platform_getenv("MAKOCODE_DECODE_RAW_FALLBACK");
    bool allow_metadata_free_recovery = no_metadata ||
        (raw_fallback_setting && raw_fallback_setting[0] &&
         raw_fallback_setting[0] != '0');
    if (mapping.palette_set) {
        if (!image_mapping_build_custom_palette(mapping, "decode")) {
            return 1;
        }
    }
    makocode::PaletteSymbolPasswordContext palette_password_context;
    if (have_password &&
        !palette_password_context.derive((const char*)password_buffer.data,
                                         password_buffer.size)) {
        console_line(2, "decode: failed to derive password-keyed symbol placement");
        return 1;
    }
    if (have_password) {
        mapping.symbol_placement_password_context = &palette_password_context;
    }
    ImageMappingConfig mapping_overrides = mapping;
    const u64 requested_symbol_placement_seed = mapping.symbol_placement_seed;
    const bool requested_password_placement =
        mapping.symbol_placement_password_context != 0;
    bool metadata_free_layout_autodetect = no_metadata;
    u32 scatter_layout_attempt = 0u;
    makocode::ByteBuffer validated_metadata_free_payload;
    makocode::EccDecodeStats validated_metadata_free_ecc_stats;
    makocode::EncodedPayloadMetadata validated_metadata_free_payload_metadata;
    bool have_validated_metadata_free_payload = false;
    bool metadata_free_candidates_ambiguous = false;
    bool validated_metadata_free_password_attempted = false;
    bool validated_metadata_free_password_not_encrypted = false;
    auto advance_scatter_layout = [&]() -> bool {
        if (!metadata_free_layout_autodetect) return false;
        if (scatter_layout_attempt == 0u) {
            scatter_layout_attempt = 1u;
        } else if (scatter_layout_attempt == 1u) {
            if (have_validated_metadata_free_payload || !have_password ||
                !requested_password_placement) return false;
            scatter_layout_attempt = 2u;
            mapping_overrides.symbol_placement_seed =
                makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED;
            mapping_overrides.symbol_placement_password_context = 0;
            retried_public_symbol_placement = true;
        } else if (scatter_layout_attempt == 2u) {
            scatter_layout_attempt = 3u;
        } else {
            return false;
        }
        mapping_overrides.scatter_payload = (scatter_layout_attempt & 1u) != 0u;
        if (scatter_layout_attempt < 2u) {
            mapping_overrides.symbol_placement_seed = requested_symbol_placement_seed;
            mapping_overrides.symbol_placement_password_context = have_password
                ? &palette_password_context
                : 0;
        }
        return true;
    };
    makocode::ByteBuffer bitstream;
    makocode::ByteBuffer erased_payload_symbols;
    makocode::ByteBuffer overlay_selector_counts;
    u64 overlay_selector_seed = 0u;
    u16 overlay_unknown_parity_budget = 0u;
    u16 overlay_erasure_budget = 0u;
    u64 bit_count = 0u;
    PpmParserState aggregate_state;
    MetadataTile::Values single_retry_tile;
    bool single_retry_tile_available = false;
    bool have_metadata = false;
    bool force_disable_subgrid = false;
    bool retried_subgrid = false;
    bool force_tile_center_sampling = false;
    MetadataTile::Values center_probe_tile;
    bool center_probe_available = false;
    bool center_probe_page_present[MAX_INPUT_FILES] = {};
    u64 center_probe_page_capacity[MAX_INPUT_FILES] = {};
    bool center_probe_attempted = false;
    u32 small_scan_geometry_variant = 0u;
    makocode::ByteBuffer debug_fiducial_csv;
    makocode::ByteBuffer debug_fiducial_path;
    const usize debug_fiducial_header_size = (usize)sizeof("page,row,column,x,y,logical_x,logical_y\n") - 1u;
    makocode::ByteBuffer debug_affine_csv;
    makocode::ByteBuffer debug_affine_path;
    makocode::ByteBuffer stdin_ppm_stream;
    bool stdin_ppm_buffered = false;
    const usize debug_affine_header_size = (usize)sizeof("page,has_affine,scale,rotation_degrees,a00,a01,a10,a11,tx,ty\n") - 1u;
    if (debug_requested) {
        if (!debug_fiducial_csv.append_ascii("page,row,column,x,y,logical_x,logical_y\n") ||
            !join_output_path(output_dir, "decode_fiducials.csv", debug_fiducial_path) ||
            !ensure_parent_directories((const char*)debug_fiducial_path.data) ||
            !write_bytes_to_file((const char*)debug_fiducial_path.data,
                                 debug_fiducial_csv.data,
                                 debug_fiducial_csv.size) ||
            !debug_affine_csv.append_ascii("page,has_affine,scale,rotation_degrees,a00,a01,a10,a11,tx,ty\n") ||
            !join_output_path(output_dir, "decode_affine.csv", debug_affine_path) ||
            !ensure_parent_directories((const char*)debug_affine_path.data) ||
            !write_bytes_to_file((const char*)debug_affine_path.data,
                                 debug_affine_csv.data,
                                 debug_affine_csv.size)) {
            console_line(2, "decode: failed to prepare fiducial CSV");
            return 1;
        }
    }
retry_decode:
    if (!aggregate_state.has_encoded_tile ||
        aggregate_state.encoded_tile.page_width_pixels <= 96u ||
        aggregate_state.encoded_tile.page_height_pixels <= 96u) {
        mapping = mapping_overrides;
    }
    encoded_page_count = 0u;
    encoded_frame_bits = 0u;
    erased_payload_symbols.release();
    overlay_selector_counts.release();
    overlay_selector_seed = 0u;
    overlay_unknown_parity_budget = 0u;
    overlay_erasure_budget = 0u;
    if (debug_requested && debug_fiducial_csv.size > debug_fiducial_header_size) {
        debug_fiducial_csv.size = debug_fiducial_header_size;
        if (!write_bytes_to_file((const char*)debug_fiducial_path.data,
                                 debug_fiducial_csv.data,
                                 debug_fiducial_csv.size)) {
            console_line(2, "decode: failed to reset fiducial CSV");
            return 1;
        }
    }
    if (debug_requested && debug_affine_csv.size > debug_affine_header_size) {
        debug_affine_csv.size = debug_affine_header_size;
        if (!write_bytes_to_file((const char*)debug_affine_path.data,
                                 debug_affine_csv.data,
                                 debug_affine_csv.size)) {
            console_line(2, "decode: failed to reset affine CSV");
            return 1;
        }
    }
    bitstream.release();
    bit_count = 0u;
    aggregate_state = PpmParserState();
    have_metadata = false;
    if (file_count == 0u) {
        if (!stdin_ppm_buffered && !read_entire_stdin(stdin_ppm_stream)) {
            console_line(2, "decode: failed to read stdin");
            return 1;
        }
        stdin_ppm_buffered = true;
        const makocode::ByteBuffer& ppm_stream = stdin_ppm_stream;

        makocode::ByteBuffer frame_bits;
        makocode::ByteBuffer frame_erasure_bits;
        u64 frame_bit_count = 0u;
        u64 single_page_capacity_bits = 0u;
        PpmParserState single_state;
        bool retry_without_subgrid_allowed = false;
        bool single_color_rotation = false;
        bool frame_extracted = !no_metadata && ppm_extract_frame_bits(ppm_stream,
                                                      mapping,
                                                      frame_bits,
                                                      frame_bit_count,
                                                      single_state,
                                                      force_disable_subgrid,
                                                      &retry_without_subgrid_allowed,
                                                      debug_requested ? &debug_fiducial_csv : (makocode::ByteBuffer*)0,
                                                      1u,
                                                      &frame_erasure_bits,
                                                      &overlay_selector_seed,
                                                      &overlay_selector_counts,
                                                      &overlay_unknown_parity_budget,
                                                      &overlay_erasure_budget,
                                                      &single_color_rotation,
                                                      (const AffineTransform*)0,
                                                      force_tile_center_sampling,
                                                      &single_page_capacity_bits,
                                                      single_retry_tile_available
                                                          ? &single_retry_tile
                                                          : (const MetadataTile::Values*)0,
                                                      (const u8*)0,
                                                      0u,
                                                      0u,
                                                      small_scan_geometry_variant,
                                                      true,
                                                      true);

        if (!frame_extracted) {
            if (retry_without_subgrid_allowed && !force_disable_subgrid && !retried_subgrid) {

                console_line(2, "decode: retrying without fiducial subgrid (frame extraction failed)");
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            if (!allow_metadata_free_recovery) {
                console_line(2, "decode: metadata tile missing; aborting");
                return 1;
            }
            MetadataTile::Values recovered_values;
            bool recovered = recover_metadata_free_single_page(
                ppm_stream,
                mapping_overrides,
                no_metadata,
                frame_bits,
                frame_erasure_bits,
                frame_bit_count,
                single_page_capacity_bits,
                single_state,
                recovered_values);
            if (!recovered && have_password &&
                mapping_overrides.symbol_placement_password_context) {
                ImageMappingConfig public_placement_mapping = mapping_overrides;
                public_placement_mapping.symbol_placement_seed =
                    makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED;
                public_placement_mapping.symbol_placement_password_context = 0;
                recovered = recover_metadata_free_single_page(
                    ppm_stream,
                    public_placement_mapping,
                    no_metadata,
                    frame_bits,
                    frame_erasure_bits,
                    frame_bit_count,
                    single_page_capacity_bits,
                    single_state,
                    recovered_values);
            }
            if (!recovered) {
                console_line(2, "decode: invalid ppm input");
                return 1;
            }
            metadata_free_layout_autodetect = true;
            frame_extracted = true;
            single_color_rotation = false;
        }
        if (debug_requested &&
            !append_affine_csv_row(debug_affine_csv, 1u, single_state.has_affine_transform,
                                   single_state.affine_transform.a00, single_state.affine_transform.a01,
                                   single_state.affine_transform.a10, single_state.affine_transform.a11,
                                   single_state.affine_transform.tx, single_state.affine_transform.ty)) {
            console_line(2, "decode: failed to append affine CSV");
            return 1;
        }
        if (debug_requested &&
            !write_bytes_to_file((const char*)debug_affine_path.data,
                                 debug_affine_csv.data,
                                 debug_affine_csv.size)) {
            console_line(2, "decode: failed to write affine CSV");
            return 1;
        }
        if (debug_requested &&
            !write_bytes_to_file((const char*)debug_fiducial_path.data,
                                 debug_fiducial_csv.data,
                                 debug_fiducial_csv.size)) {
            console_line(2, "decode: failed to write fiducial CSV");
            return 1;
        }
        if (single_state.has_encoded_tile &&
            metadata_schema_is_current(single_state.encoded_tile.schema_version) &&
            single_state.encoded_tile.page_bits && single_page_capacity_bits) {
            const MetadataTile::Values& tile = single_state.encoded_tile;
            u64 page_index = tile.page_index ? tile.page_index : 1u;
            if (page_index - 1u > U64_MAX_VALUE / single_page_capacity_bits) {
                console_line(2, "decode: single page index overflows frame offset");
                return 1;
            }
            u64 page_offset = (page_index - 1u) * single_page_capacity_bits;
            bool page_has_payload = page_offset < tile.page_bits;
            if ((page_index <= tile.page_count) != page_has_payload) {
                console_line(2, "decode: single page index conflicts with payload length");
                return 1;
            }
        }
        if (single_state.has_encoded_tile &&
            metadata_schema_is_current(single_state.encoded_tile.schema_version) &&
            single_state.encoded_tile.page_count > 1u) {
            const MetadataTile::Values& tile = single_state.encoded_tile;
            u64 page_index = tile.page_index ? tile.page_index : 1u;
            if (!single_page_capacity_bits || !tile.page_bits) {
                console_line(2, "decode: single page has invalid missing-page metadata");
                return 1;
            }
            makocode::BitWriter assembled_bits;
            makocode::ByteBuffer assembled_symbol_erasures;
            auto append_missing_bits = [&](u64 bits) -> bool {
                u64 symbol_count_u64 = bits / 32u;
                if ((bits & 31u) != 0u || symbol_count_u64 > (u64)USIZE_MAX_VALUE ||
                    assembled_symbol_erasures.size >
                        USIZE_MAX_VALUE - (usize)symbol_count_u64 ||
                    !assembled_symbol_erasures.ensure(
                        assembled_symbol_erasures.size + (usize)symbol_count_u64)) return false;
                usize first_symbol = assembled_symbol_erasures.size;
                assembled_symbol_erasures.size += (usize)symbol_count_u64;
                if (symbol_count_u64) {
                    memset(assembled_symbol_erasures.data + first_symbol, 1u,
                           (usize)symbol_count_u64);
                }
                while (bits > 0u) {
                    u32 chunk = bits >= 64u ? 64u : (u32)bits;
                    if (!assembled_bits.write_bits(0u, chunk)) return false;
                    bits -= chunk;
                }
                return true;
            };
            for (u64 current_page = 1u; current_page <= tile.page_count; ++current_page) {
                if (current_page - 1u > U64_MAX_VALUE / single_page_capacity_bits) {
                    console_line(2, "decode: single page frame offset overflow");
                    return 1;
                }
                u64 page_offset = (current_page - 1u) * single_page_capacity_bits;
                if (page_offset >= tile.page_bits) break;
                u64 segment_bits = tile.page_bits - page_offset;
                if (segment_bits > single_page_capacity_bits) segment_bits = single_page_capacity_bits;
                if (current_page != page_index) {
                    if (!append_missing_bits(segment_bits)) {
                        console_line(2, "decode: failed to mark missing stdin page as erased");
                        return 1;
                    }
                    continue;
                }
                u64 observed_bits = frame_bit_count < segment_bits ? frame_bit_count : segment_bits;
                if (!append_bits_from_buffer(assembled_bits, frame_bits.data, observed_bits)) {
                    console_line(2, "decode: failed to assemble stdin page bits");
                    return 1;
                }
                if (frame_erasure_bits.data && frame_erasure_bits.size) {
                    if ((observed_bits & 31u) != 0u ||
                        observed_bits / 32u > frame_erasure_bits.size ||
                        assembled_symbol_erasures.size > USIZE_MAX_VALUE -
                            (usize)(observed_bits / 32u) ||
                        !assembled_symbol_erasures.append_bytes(
                            frame_erasure_bits.data,
                            (usize)(observed_bits / 32u))) {
                        console_line(2, "decode: failed to assemble stdin page erasures");
                        return 1;
                    }
                } else {
                    if ((observed_bits & 31u) != 0u ||
                        observed_bits / 32u > (u64)USIZE_MAX_VALUE ||
                        assembled_symbol_erasures.size > USIZE_MAX_VALUE -
                            (usize)(observed_bits / 32u) ||
                        !assembled_symbol_erasures.ensure(
                            assembled_symbol_erasures.size + (usize)(observed_bits / 32u))) {
                        console_line(2, "decode: failed to assemble stdin page erasures");
                        return 1;
                    }
                    usize first_symbol = assembled_symbol_erasures.size;
                    usize observed_symbols = (usize)(observed_bits / 32u);
                    assembled_symbol_erasures.size += observed_symbols;
                    if (observed_symbols) {
                        memset(assembled_symbol_erasures.data + first_symbol, 0,
                               observed_symbols);
                    }
                }
                if (observed_bits < segment_bits &&
                    !append_missing_bits(segment_bits - observed_bits)) {
                    console_line(2, "decode: failed to mark truncated stdin page as erased");
                    return 1;
                }
            }
            usize assembled_bytes = assembled_bits.byte_size();
            frame_bits.release();
            frame_erasure_bits.release();
            if ((assembled_bytes && !frame_bits.ensure(assembled_bytes)) ||
                (assembled_symbol_erasures.size &&
                 !frame_erasure_bits.ensure(assembled_symbol_erasures.size))) {
                console_line(2, "decode: failed to allocate assembled stdin pages");
                return 1;
            }
            if (assembled_bytes) memcpy(frame_bits.data, assembled_bits.data(), assembled_bytes);
            if (assembled_symbol_erasures.size) {
                memcpy(frame_erasure_bits.data,
                       assembled_symbol_erasures.data,
                       assembled_symbol_erasures.size);
            }
            frame_bits.size = assembled_bytes;
            frame_erasure_bits.size = assembled_symbol_erasures.size;
            frame_bit_count = assembled_bits.bit_size();
        }
        if (single_color_rotation) {
            for (usize i = 0u; i < frame_bits.size; ++i) {
                u8 rotate = (u8)((i % 3u) + 1u);
                frame_bits.data[i] = rotate_right_u8(frame_bits.data[i], rotate);
            }
        }
        bool payload_extracted = frame_bits_to_payload(frame_bits.data,
                                                       frame_bit_count,
                                                       single_state,
                                                       bitstream,
                                                       bit_count,
                                                       &encoded_frame_bits,
                                                       frame_erasure_bits.data,
                                                       frame_bit_count,
                                                       &erased_payload_symbols);

        encoded_page_present[0] = single_state.has_encoded_tile;
        encoded_pages[0] = single_state.encoded_tile;
        encoded_page_indices[0] = single_state.has_page_index &&
                                  single_state.page_index_value > 0u
                                      ? single_state.page_index_value : 1u;
        encoded_page_count = 1u;
        if (single_state.has_encoded_tile) {
            single_retry_tile = single_state.encoded_tile;
            single_retry_tile_available = true;
        }

        if (!payload_extracted) {
            if (!force_disable_subgrid && !retried_subgrid) {

                console_line(2, "decode: retrying without fiducial subgrid (payload header unreadable)");
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            console_line(2, "decode: failed to extract payload bits");
            return 1;
        }
        aggregate_state = single_state;
        have_metadata = true;

    } else {
        makocode::BitWriter frame_aggregator;
        makocode::ByteBuffer symbol_erasure_aggregator;
        frame_aggregator.reset();
        makocode::ByteBuffer indexed_frame_bits;
        makocode::ByteBuffer indexed_symbol_erasures;
        bool aggregate_initialized = false;
        bool page_assembly_mode_initialized = false;
        bool indexed_page_assembly = false;
        bool first_page_has_selector_tile = false;
        bool have_first_page_selector_tile = false;
        bool aggregate_color_rotation = false;
        bool have_aggregate_color_rotation = false;
        MetadataTile::Values first_page_selector_tile;
        u64 expected_page_index = 1u;
        u64 indexed_frame_bit_total = 0u;
        u64 aggregate_page_capacity_bits = 0u;
        u64 common_page_capacity_override = 0u;
        AffineTransform previous_page_affine_hint = {};
        bool previous_page_affine_hint_available = false;
        AffineTransform center_probe_page_affines[MAX_INPUT_FILES] = {};
        bool center_probe_affine_present[MAX_INPUT_FILES] = {};
        if (!no_metadata && !center_probe_attempted && !force_disable_subgrid &&
            !force_tile_center_sampling && !retried_subgrid) {
            center_probe_attempted = true;
            for (usize probe_index = 0u; probe_index < file_count; ++probe_index) {
                makocode::ByteBuffer probe_stream;
                if (!read_entire_file(input_files[probe_index], probe_stream)) {
                    console_write(2, "decode: failed to read ");
                    console_line(2, input_files[probe_index]);
                    return 1;
                }
                makocode::ByteBuffer probe_bits;
                makocode::ByteBuffer probe_erasures;
                u64 probe_bit_count = 0u;
                u64 probe_page_capacity_bits = 0u;
                PpmParserState probe_state;
                bool probe_found = ppm_extract_frame_bits(
                    probe_stream, mapping, probe_bits, probe_bit_count, probe_state,
                    false, 0, 0, (u64)probe_index + 1u, &probe_erasures,
                    0, 0, 0, 0, 0, 0, false, &probe_page_capacity_bits,
                    0, 0, 0, 0, 0, false, true, false, true);
                if (!probe_found) {
                    probe_bits.release();
                    probe_erasures.release();
                    probe_state = PpmParserState();
                    probe_bit_count = 0u;
                    probe_page_capacity_bits = 0u;
                    probe_found = ppm_extract_frame_bits(
                        probe_stream, mapping, probe_bits, probe_bit_count, probe_state,
                        false, 0, 0, (u64)probe_index + 1u, &probe_erasures,
                        0, 0, 0, 0, 0, 0, false, &probe_page_capacity_bits,
                        0, 0, 0, 0, 0, false, true, true, false);
                }
                if (probe_found && probe_state.has_encoded_tile &&
                    metadata_schema_is_current(probe_state.encoded_tile.schema_version)) {
                    center_probe_page_present[probe_index] = true;
                    center_probe_page_capacity[probe_index] = probe_page_capacity_bits;
                    encoded_pages[probe_index] = probe_state.encoded_tile;
                    if (probe_state.has_affine_transform &&
                        probe_state.affine_fit_covers_page) {
                        center_probe_page_affines[probe_index] = probe_state.affine_transform;
                        center_probe_affine_present[probe_index] = true;
                    }
                    if (!center_probe_available) {
                        center_probe_tile = probe_state.encoded_tile;
                        center_probe_available = true;
                    }
                }
            }
            if (center_probe_available && file_count > 1u) {
                u64 minimum_page_capacity_bits = U64_MAX_VALUE;
                bool complete_page_set = true;
                for (usize page = 0u; page < file_count; ++page) {
                    if (!center_probe_page_present[page]) {
                        complete_page_set = false;
                    }
                }
                for (usize page = 0u; page < file_count && complete_page_set; ++page) {
                    const MetadataTile::Values& tile = encoded_pages[page];
                    if (tile.page_bits != center_probe_tile.page_bits ||
                        tile.page_count != file_count ||
                        tile.page_index == 0u) {
                        complete_page_set = false;
                        break;
                    }
                    for (usize prior = 0u; prior < page; ++prior) {
                        if (encoded_pages[prior].page_index == tile.page_index) {
                            complete_page_set = false;
                            break;
                        }
                    }
                }
                for (usize page = 0u; page < file_count && complete_page_set; ++page) {
                    if (encoded_pages[page].page_index > file_count) {
                        complete_page_set = false;
                    }
                }
                if (!complete_page_set) {
                    for (usize page = 0u; page < file_count; ++page) {
                        if (!center_probe_page_present[page] ||
                            center_probe_page_capacity[page] > 0u) continue;
                        makocode::ByteBuffer capacity_stream;
                        if (!read_entire_file(input_files[page], capacity_stream)) {
                            console_write(2, "decode: failed to read ");
                            console_line(2, input_files[page]);
                            return 1;
                        }
                        makocode::ByteBuffer capacity_bits;
                        makocode::ByteBuffer capacity_erasures;
                        u64 capacity_bit_count = 0u;
                        u64 page_capacity_bits = 0u;
                        PpmParserState capacity_state;
                        bool capacity_found = ppm_extract_frame_bits(
                            capacity_stream, mapping, capacity_bits, capacity_bit_count,
                            capacity_state, false, 0, 0, (u64)page + 1u,
                            &capacity_erasures, 0, 0, 0, 0, 0, 0, false,
                            &page_capacity_bits, 0, 0, 0, 0, 0, false, true,
                            false, true);
                        if (capacity_found && capacity_state.has_encoded_tile &&
                            metadata_schema_is_current(
                                capacity_state.encoded_tile.schema_version)) {
                            center_probe_page_capacity[page] = page_capacity_bits;
                        }
                    }
                    for (usize page = 0u; page < file_count; ++page) {
                        if (!center_probe_page_present[page] ||
                            center_probe_page_capacity[page] == 0u) continue;
                        if (center_probe_page_capacity[page] < minimum_page_capacity_bits) {
                            minimum_page_capacity_bits = center_probe_page_capacity[page];
                        }
                    }
                }
                if (complete_page_set) {
                    usize valid_affine_count = 0u;
                    usize best_cluster_count = 0u;
                    usize best_cluster_page = 0u;
                    const double max_angle_delta =
                        0.25 * (3.14159265358979323846 / 180.0);
                    for (usize page = 0u; page < file_count; ++page) {
                        if (!center_probe_affine_present[page]) continue;
                        ++valid_affine_count;
                        const AffineTransform& candidate = center_probe_page_affines[page];
                        double candidate_angle = atan2(candidate.a10 - candidate.a01,
                                                       candidate.a00 + candidate.a11);
                        double candidate_scale_x = sqrt(candidate.a00 * candidate.a00 +
                                                         candidate.a10 * candidate.a10);
                        double candidate_scale_y = sqrt(candidate.a01 * candidate.a01 +
                                                         candidate.a11 * candidate.a11);
                        usize cluster_count = 0u;
                        for (usize other_page = 0u; other_page < file_count; ++other_page) {
                            if (!center_probe_affine_present[other_page]) continue;
                            const AffineTransform& other = center_probe_page_affines[other_page];
                            double other_angle = atan2(other.a10 - other.a01,
                                                       other.a00 + other.a11);
                            double other_scale_x = sqrt(other.a00 * other.a00 +
                                                        other.a10 * other.a10);
                            double other_scale_y = sqrt(other.a01 * other.a01 +
                                                        other.a11 * other.a11);
                            double angle_delta = fabs(atan2(
                                sin(candidate_angle - other_angle),
                                cos(candidate_angle - other_angle)));
                            if (angle_delta <= max_angle_delta &&
                                fabs(candidate_scale_x - other_scale_x) <=
                                    candidate_scale_x * 0.02 &&
                                fabs(candidate_scale_y - other_scale_y) <=
                                    candidate_scale_y * 0.02) {
                                ++cluster_count;
                            }
                        }
                        if (cluster_count > best_cluster_count) {
                            best_cluster_count = cluster_count;
                            best_cluster_page = page;
                        }
                    }
                    if (valid_affine_count > 1u &&
                        best_cluster_count > valid_affine_count / 2u) {
                        previous_page_affine_hint =
                            center_probe_page_affines[best_cluster_page];
                        previous_page_affine_hint.tx = 0.0;
                        previous_page_affine_hint.ty = 0.0;
                        previous_page_affine_hint_available = true;
                    }
                }
                if (!complete_page_set &&
                    minimum_page_capacity_bits != U64_MAX_VALUE &&
                    minimum_page_capacity_bits > 0u) {
                    // Pages without metadata still use the dimensions and encoding
                    // profile from the readable page. Clamp their usable capacity to
                    // the smallest capacity observed on a metadata-bearing page.
                    common_page_capacity_override = minimum_page_capacity_bits;
                }
            }
        }
        for (usize file_index = 0u; file_index < file_count; ++file_index) {
            makocode::ByteBuffer ppm_stream;
            if (!read_entire_file(input_files[file_index], ppm_stream)) {
                console_write(2, "decode: failed to read ");
                console_line(2, input_files[file_index]);
                return 1;
            }

            
            
            makocode::ByteBuffer page_bits;
            makocode::ByteBuffer page_erasure_bits;
            makocode::ByteBuffer page_overlay_selector_counts;
            u64 page_overlay_selector_seed = 0u;
            u64 page_bit_count = 0u;
            u64 page_capacity_bits = 0u;
            PpmParserState page_state;
            u16 page_unknown_parity_budget = 0u;
            u16 page_overlay_erasure_budget = 0u;
            bool page_color_rotation = false;
            bool retry_without_subgrid_allowed = false;
            MetadataTile::Values page_tile_override;
            const MetadataTile::Values* page_tile_override_ptr = 0;
            bool page_tile_present = false;
            if (center_probe_available && !center_probe_page_present[file_index]) {
                page_tile_override = center_probe_tile;
                page_tile_override.page_index = (u64)file_index + 1u;
                page_tile_override_ptr = &page_tile_override;
            }
            bool frame_extracted = !no_metadata && ppm_extract_frame_bits(ppm_stream,
                                                          mapping,
                                                          page_bits,
                                                          page_bit_count,
                                                          page_state,
                                                          force_disable_subgrid,
                                                          &retry_without_subgrid_allowed,
                                                          debug_requested ? &debug_fiducial_csv : (makocode::ByteBuffer*)0,
                                                          (u64)file_index + 1u,
                                                          &page_erasure_bits,
                                                          &page_overlay_selector_seed,
                                                          &page_overlay_selector_counts,
                                                          &page_unknown_parity_budget,
                                                          &page_overlay_erasure_budget,
                                                          &page_color_rotation,
                                                          previous_page_affine_hint_available
                                                              ? &previous_page_affine_hint
                                                              : (const AffineTransform*)0,
                                                          force_tile_center_sampling,
                                                          &page_capacity_bits,
                                                          page_tile_override_ptr
                                                              ? page_tile_override_ptr
                                                              : (file_count == 1u && single_retry_tile_available
                                                                     ? &single_retry_tile
                                                                     : (const MetadataTile::Values*)0),
                                                          (const u8*)0,
                                                          0u,
                                                          0u,
                                                          small_scan_geometry_variant,
                                                          true,
                                                          page_tile_override_ptr
                                                              ? page_tile_present
                                                              : true,
                                                          false,
                                                          false,
                                                          page_tile_override_ptr
                                                              ? common_page_capacity_override
                                                              : 0u);

            if (!frame_extracted) {
                if (retry_without_subgrid_allowed && !force_disable_subgrid && !retried_subgrid) {

                    console_line(2, "decode: retrying without fiducial subgrid (frame extraction failed)");
                    force_disable_subgrid = true;
                    retried_subgrid = true;
                    goto retry_decode;
                }
                if (file_count == 1u) {
                    MetadataTile::Values recovered_values;
                    bool recovered = allow_metadata_free_recovery &&
                        recover_metadata_free_single_page(ppm_stream,
                                                          mapping_overrides,
                                                          no_metadata,
                                                          page_bits,
                                                          page_erasure_bits,
                                                          page_bit_count,
                                                          page_capacity_bits,
                                                          page_state,
                                                          recovered_values);
                    if (!recovered && allow_metadata_free_recovery && have_password &&
                        mapping_overrides.symbol_placement_password_context) {
                        ImageMappingConfig public_placement_mapping = mapping_overrides;
                        public_placement_mapping.symbol_placement_seed =
                            makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED;
                        public_placement_mapping.symbol_placement_password_context = 0;
                        recovered = recover_metadata_free_single_page(
                            ppm_stream,
                            public_placement_mapping,
                            no_metadata,
                            page_bits,
                            page_erasure_bits,
                            page_bit_count,
                            page_capacity_bits,
                            page_state,
                            recovered_values);
                    }
                    if (recovered) {
                        metadata_free_layout_autodetect = true;
                        frame_extracted = true;
                        page_color_rotation = false;
                    }
                }
                if (!frame_extracted) {
                    if (!allow_metadata_free_recovery) {
                        console_line(2, "decode: metadata tile missing; aborting");
                        return 1;
                    }
                    console_write(2, "decode: invalid ppm in ");
                    console_line(2, input_files[file_index]);
                    return 1;
                }
            }
            bool has_explicit_page_index = page_state.has_page_index &&
                                           page_state.page_index_value > 0u;
            if (!has_explicit_page_index) {
                page_state.has_page_index = true;
                page_state.page_index_value = expected_page_index;
            }
            bool page_has_indexed_metadata = page_state.has_encoded_tile &&
                metadata_schema_is_current(page_state.encoded_tile.schema_version) &&
                page_state.has_page_bits && has_explicit_page_index &&
                page_capacity_bits > 0u;
            if (!page_assembly_mode_initialized) {
                page_assembly_mode_initialized = true;
            }
            if (page_has_indexed_metadata && !indexed_page_assembly) {
                indexed_page_assembly = true;
                {
                    indexed_frame_bit_total = page_state.page_bits_value;
                    if ((indexed_frame_bit_total & 31u) != 0u) {
                        console_line(2, "decode: field-symbol frame length is not aligned");
                        return 1;
                    }
                    u64 frame_bytes_u64 = (indexed_frame_bit_total >> 3u) +
                                          ((indexed_frame_bit_total & 7u) ? 1u : 0u);
                    u64 frame_symbols_u64 = indexed_frame_bit_total / 32u;
                    if (frame_bytes_u64 > (u64)USIZE_MAX_VALUE ||
                        frame_symbols_u64 > (u64)USIZE_MAX_VALUE ||
                        (frame_bytes_u64 &&
                         !indexed_frame_bits.ensure((usize)frame_bytes_u64)) ||
                        (frame_symbols_u64 &&
                         !indexed_symbol_erasures.ensure((usize)frame_symbols_u64))) {
                        console_line(2, "decode: failed to allocate metadata-indexed pages");
                        return 1;
                    }
                    indexed_frame_bits.size = (usize)frame_bytes_u64;
                    indexed_symbol_erasures.size = (usize)frame_symbols_u64;
                    if (frame_bytes_u64) {
                        memset(indexed_frame_bits.data, 0, (usize)frame_bytes_u64);
                    }
                    if (frame_symbols_u64) {
                        memset(indexed_symbol_erasures.data, 1, (usize)frame_symbols_u64);
                    }
                    u64 prior_bits = frame_aggregator.bit_size();
                    if (prior_bits > indexed_frame_bit_total ||
                        (prior_bits & 31u) != 0u ||
                        prior_bits / 32u > symbol_erasure_aggregator.size) {
                        console_line(2, "decode: preceding metadata-free pages exceed indexed payload length");
                        return 1;
                    }
                    makocode::BitReader prior_frame_reader;
                    prior_frame_reader.reset(frame_aggregator.data(), prior_bits);
                    for (u64 bit_index = 0u; bit_index < prior_bits; ++bit_index) {
                        u8 bit = prior_frame_reader.read_bit();
                        if (prior_frame_reader.failed) {
                            console_line(2, "decode: failed to migrate preceding metadata-free pages");
                            return 1;
                        }
                        usize destination_byte = (usize)(bit_index >> 3u);
                        u8 destination_mask = (u8)(1u << (bit_index & 7u));
                        if (bit) indexed_frame_bits.data[destination_byte] |= destination_mask;
                    }
                    if (prior_bits) {
                        memcpy(indexed_symbol_erasures.data,
                               symbol_erasure_aggregator.data,
                               (usize)(prior_bits / 32u));
                    }
                }
            }
            if (!aggregate_page_capacity_bits) {
                aggregate_page_capacity_bits = page_capacity_bits;
            }
            if (page_state.affine_fit_covers_page && page_state.has_affine_transform) {
                previous_page_affine_hint = page_state.affine_transform;
                previous_page_affine_hint_available = true;
            }
            if (!have_aggregate_color_rotation) {
                aggregate_color_rotation = page_color_rotation;
                have_aggregate_color_rotation = true;
            } else if (aggregate_color_rotation != page_color_rotation) {
                console_line(2, "decode: conflicting color rotation settings between pages");
                return 1;
            }
            bool page_has_selector_tile = page_state.has_encoded_tile &&
                metadata_schema_is_current(page_state.encoded_tile.schema_version) &&
                page_state.encoded_tile.overwrite_layout;
            if (page_has_selector_tile && !have_first_page_selector_tile) {
                have_first_page_selector_tile = true;
                first_page_has_selector_tile = true;
                first_page_selector_tile = page_state.encoded_tile;
            }
            if (page_has_selector_tile && first_page_has_selector_tile &&
                (page_state.encoded_tile.overlay_selector_present !=
                     first_page_selector_tile.overlay_selector_present ||
                 page_state.encoded_tile.overlay_selector_seed !=
                     first_page_selector_tile.overlay_selector_seed ||
                 page_state.encoded_tile.unknown_parity_budget !=
                     first_page_selector_tile.unknown_parity_budget ||
                 page_state.encoded_tile.overlay_erasure_budget !=
                     first_page_selector_tile.overlay_erasure_budget ||
                 page_state.encoded_tile.ecc_enabled != first_page_selector_tile.ecc_enabled ||
                 page_state.encoded_tile.ecc_block_data != first_page_selector_tile.ecc_block_data ||
                 page_state.encoded_tile.ecc_parity != first_page_selector_tile.ecc_parity ||
                 page_state.encoded_tile.ecc_block_count != first_page_selector_tile.ecc_block_count ||
                 page_state.encoded_tile.ecc_original_bytes != first_page_selector_tile.ecc_original_bytes)) {
                console_line(2, "decode: conflicting overlay selector settings between pages");
                return 1;
            }
            if (aggregate_page_capacity_bits != page_capacity_bits) {
                console_line(2, "decode: page capacity differs between pages");
                return 1;
            }
            if (page_overlay_selector_counts.size) {
                if (!overlay_selector_counts.size) {
                    overlay_selector_seed = page_overlay_selector_seed;
                    overlay_unknown_parity_budget = page_unknown_parity_budget;
                    overlay_erasure_budget = page_overlay_erasure_budget;
                    if (!overlay_selector_counts.ensure(page_overlay_selector_counts.size)) {
                        console_line(2, "decode: failed to allocate overlay selector map");
                        return 1;
                    }
                    overlay_selector_counts.size = page_overlay_selector_counts.size;
                    for (usize i = 0u; i < overlay_selector_counts.size; ++i) {
                        overlay_selector_counts.data[i] = page_overlay_selector_counts.data[i];
                    }
                } else {
                    if (overlay_selector_seed != page_overlay_selector_seed ||
                        overlay_unknown_parity_budget != page_unknown_parity_budget ||
                        overlay_erasure_budget != page_overlay_erasure_budget ||
                        overlay_selector_counts.size != page_overlay_selector_counts.size) {
                        console_line(2, "decode: conflicting overlay selector settings between pages");
                        return 1;
                    }
                    for (usize i = 0u; i < overlay_selector_counts.size; ++i) {
                        if (page_overlay_selector_counts.data[i] > overlay_selector_counts.data[i]) {
                            overlay_selector_counts.data[i] = page_overlay_selector_counts.data[i];
                        }
                    }
                }
            }
            if (debug_requested &&
                !append_affine_csv_row(debug_affine_csv, (u64)file_index + 1u,
                                       page_state.has_affine_transform,
                                       page_state.affine_transform.a00, page_state.affine_transform.a01,
                                       page_state.affine_transform.a10, page_state.affine_transform.a11,
                                       page_state.affine_transform.tx, page_state.affine_transform.ty)) {
                console_line(2, "decode: failed to append affine CSV");
                return 1;
            }
            if (debug_requested &&
                !write_bytes_to_file((const char*)debug_affine_path.data,
                                     debug_affine_csv.data,
                                     debug_affine_csv.size)) {
                console_line(2, "decode: failed to write affine CSV");
                return 1;
            }
            if (debug_requested &&
                !write_bytes_to_file((const char*)debug_fiducial_path.data,
                                     debug_fiducial_csv.data,
                                     debug_fiducial_csv.size)) {
                console_line(2, "decode: failed to write fiducial CSV");
                return 1;
            }
            if (!aggregate_initialized) {
                merge_parser_state(aggregate_state, page_state);
                aggregate_initialized = true;
            } else {
                if (!merge_parser_state(aggregate_state, page_state)) {
                    console_line(2, "decode: conflicting metadata between pages");
                    return 1;
                }
            }
            for (usize previous_page = 0u; previous_page < encoded_page_count; ++previous_page) {
                if (encoded_page_indices[previous_page] == page_state.page_index_value) {
                    console_line(2, "decode: duplicate page index");
                    return 1;
                }
            }
            if (!indexed_page_assembly &&
                page_state.page_index_value != expected_page_index) {
                console_line(2, "decode: pages without current metadata must be supplied in order");
                return 1;
            }
            usize encoded_page_slot = encoded_page_count++;
            encoded_page_present[encoded_page_slot] = page_state.has_encoded_tile;
            encoded_pages[encoded_page_slot] = page_state.encoded_tile;
            encoded_page_indices[encoded_page_slot] = page_state.page_index_value;
            if (page_state.page_index_value == U64_MAX_VALUE) {
                console_line(2, "decode: page index is too large");
                return 1;
            }
            u64 effective_bits = page_bit_count;
            bool canonical_exact_frame_length =
                page_state.has_encoded_tile &&
                metadata_schema_is_current(page_state.encoded_tile.schema_version) &&
                page_state.encoded_tile.overwrite_layout &&
                page_state.has_page_bits;
            if (canonical_exact_frame_length) {
                u64 page_number = page_state.has_page_index && page_state.page_index_value > 0u
                                      ? page_state.page_index_value
                                      : expected_page_index;
                u64 page_offset = 0u;
                if (page_capacity_bits == 0u ||
                    (page_number > 1u &&
                     page_capacity_bits > U64_MAX_VALUE / (page_number - 1u))) {
                    console_line(2, "decode: page frame offset overflow");
                    return 1;
                }
                page_offset = (page_number - 1u) * page_capacity_bits;
                if (page_offset >= page_state.page_bits_value) {
                    effective_bits = 0u;
                } else {
                    u64 remaining_bits = page_state.page_bits_value - page_offset;
                    if (remaining_bits < effective_bits) effective_bits = remaining_bits;
                }
            } else if (page_state.has_page_bits &&
                       page_state.page_bits_value <= effective_bits) {
                effective_bits = page_state.page_bits_value;
            }
            if (indexed_page_assembly) {
                u64 page_index = page_state.page_index_value;
                if (page_index - 1u > U64_MAX_VALUE / page_capacity_bits) {
                    console_line(2, "decode: page frame offset overflow");
                    return 1;
                }
                u64 page_offset = (page_index - 1u) * page_capacity_bits;
                bool page_has_payload = page_offset < indexed_frame_bit_total;
                if ((page_index <= aggregate_state.encoded_tile.page_count) != page_has_payload) {
                    console_line(2, "decode: page index conflicts with payload length");
                    return 1;
                }
                if (page_offset >= indexed_frame_bit_total) {
                    effective_bits = 0u;
                } else if (effective_bits > indexed_frame_bit_total - page_offset) {
                    effective_bits = indexed_frame_bit_total - page_offset;
                }
                if ((effective_bits & 31u) != 0u || (page_offset & 31u) != 0u) {
                    console_line(2, "decode: page field-symbol erasure map is not aligned");
                    return 1;
                }
                for (u64 bit_index = 0u; bit_index < effective_bits; ++bit_index) {
                    u64 destination_bit = page_offset + bit_index;
                    usize destination_byte = (usize)(destination_bit >> 3u);
                    u8 destination_mask = (u8)(1u << (destination_bit & 7u));
                    usize source_byte = (usize)(bit_index >> 3u);
                    u8 source_mask = (u8)(1u << (bit_index & 7u));
                    bool bit_value = page_bits.data && source_byte < page_bits.size &&
                                     (page_bits.data[source_byte] & source_mask) != 0u;
                    if (bit_value) indexed_frame_bits.data[destination_byte] |= destination_mask;
                    else indexed_frame_bits.data[destination_byte] &= (u8)~destination_mask;
                }
                u64 destination_symbol = page_offset / 32u;
                u64 page_symbol_count = effective_bits / 32u;
                if (destination_symbol > indexed_symbol_erasures.size ||
                    page_symbol_count > indexed_symbol_erasures.size - destination_symbol ||
                    (page_erasure_bits.data &&
                     page_symbol_count > page_erasure_bits.size)) {
                    console_line(2, "decode: page field-symbol erasure map is truncated");
                    return 1;
                }
                for (u64 symbol = 0u; symbol < page_symbol_count; ++symbol) {
                    indexed_symbol_erasures.data[(usize)(destination_symbol + symbol)] =
                        page_erasure_bits.data
                            ? (page_erasure_bits.data[(usize)symbol] ? 1u : 0u)
                            : 0u;
                }
            } else {
                if (!append_bits_from_buffer(frame_aggregator, page_bits.data, effective_bits)) {
                    console_line(2, "decode: failed to assemble bitstream");
                    return 1;
                }
                if ((effective_bits & 31u) != 0u ||
                    effective_bits / 32u > (u64)USIZE_MAX_VALUE) {
                    console_line(2, "decode: field-symbol erasure map is not aligned");
                    return 1;
                }
                usize page_symbol_count = (usize)(effective_bits / 32u);
                if (page_erasure_bits.data && page_erasure_bits.size) {
                    if (page_symbol_count > page_erasure_bits.size ||
                        symbol_erasure_aggregator.size >
                            USIZE_MAX_VALUE - page_symbol_count ||
                        !symbol_erasure_aggregator.append_bytes(
                            page_erasure_bits.data, page_symbol_count)) {
                        console_line(2, "decode: failed to assemble field-symbol erasures");
                        return 1;
                    }
                } else {
                    if (symbol_erasure_aggregator.size >
                            USIZE_MAX_VALUE - page_symbol_count ||
                        !symbol_erasure_aggregator.ensure(
                            symbol_erasure_aggregator.size + page_symbol_count)) {
                        console_line(2, "decode: failed to assemble field-symbol erasures");
                        return 1;
                    }
                    usize first_symbol = symbol_erasure_aggregator.size;
                    symbol_erasure_aggregator.size += page_symbol_count;
                    if (page_symbol_count) {
                        memset(symbol_erasure_aggregator.data + first_symbol, 0,
                               page_symbol_count);
                    }
                }
            }
            if (page_state.page_index_value >= expected_page_index) {
                expected_page_index = page_state.page_index_value + 1u;
            }
        }
        u64 frame_bit_total = indexed_page_assembly
                                  ? indexed_frame_bit_total
                                  : frame_aggregator.bit_size();
        if (indexed_page_assembly && aggregate_state.has_encoded_tile) {
            aggregate_state.encoded_tile.page_index = 1u;
            aggregate_state.has_page_index = true;
            aggregate_state.page_index_value = 1u;
        }
        if (aggregate_color_rotation) {
            usize frame_bytes = indexed_page_assembly
                                    ? indexed_frame_bits.size
                                    : frame_aggregator.byte_size();
            for (usize i = 0u; i < frame_bytes; ++i) {
                u8 rotate = (u8)((i % 3u) + 1u);
                u8* frame_byte = indexed_page_assembly
                                     ? &indexed_frame_bits.data[i]
                                     : &frame_aggregator.buffer.data[i];
                *frame_byte = rotate_right_u8(*frame_byte, rotate);
            }
        }
        const u8* frame_data = indexed_page_assembly
                                   ? indexed_frame_bits.data : frame_aggregator.data();
        const u8* erasure_data = indexed_page_assembly
                                     ? indexed_symbol_erasures.data
                                     : symbol_erasure_aggregator.data;
        bool payload_extracted = frame_bits_to_payload(frame_data,
                                                       frame_bit_total,
                                                       aggregate_state,
                                                       bitstream,
                                                       bit_count,
                                                       &encoded_frame_bits,
                                                       erasure_data,
                                                       frame_bit_total,
                                                       &erased_payload_symbols);

        if (!payload_extracted) {
            if (!force_disable_subgrid && !retried_subgrid) {

                console_line(2, "decode: retrying without fiducial subgrid (payload header unreadable)");
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            console_line(2, "decode: failed to extract payload bits");
            return 1;
        }
        have_metadata = true;

    }
    if (have_metadata &&
        aggregate_state.has_palette_text &&
        aggregate_state.palette_text_length &&
        !mapping.palette_set) {
        image_mapping_set_palette_text(mapping,
                                       aggregate_state.palette_text,
                                       aggregate_state.palette_text_length,
                                       "decode");
        if (!image_mapping_build_custom_palette(mapping, "decode")) {
            return 1;
        }
    }
    bool metadata_v2_layout = aggregate_state.has_encoded_tile &&
                              metadata_schema_is_current(aggregate_state.encoded_tile.schema_version);
    u32 palette_symbol_palette_size = mapping_has_custom_palette(mapping)
                                          ? mapping.custom_palette_count
                                          : (1u << bits_per_sample(mapping.color_channels));
    makocode::PaletteSymbolProfile palette_symbol_profile;
    bool palette_symbol_metadata_layout =
        metadata_v2_layout &&
        makocode::palette_symbol_profile(palette_symbol_palette_size,
                                         palette_symbol_profile) &&
        aggregate_state.encoded_tile.ecc_block_data > 0u &&
        (u32)aggregate_state.encoded_tile.ecc_block_data +
                aggregate_state.encoded_tile.ecc_parity <=
            palette_symbol_profile.block_length &&
        (bit_count & 31u) == 0u;
    bool palette_symbol_bootstrap_omitted = false;
    if (palette_symbol_metadata_layout &&
        aggregate_state.encoded_tile.ecc_block_count > 0u &&
        aggregate_state.encoded_tile.ecc_original_bytes <= (u64)USIZE_MAX_VALUE) {
        u64 source_symbol_count = makocode::palette_symbol_count_for_bytes(
            palette_symbol_profile,
            (usize)aggregate_state.encoded_tile.ecc_original_bytes);
        if (source_symbol_count != U64_MAX_VALUE &&
            (!aggregate_state.encoded_tile.ecc_parity ||
             aggregate_state.encoded_tile.ecc_block_count <=
                 (U64_MAX_VALUE - source_symbol_count) /
                     aggregate_state.encoded_tile.ecc_parity)) {
            u64 payload_symbol_count = source_symbol_count +
                aggregate_state.encoded_tile.ecc_block_count *
                    aggregate_state.encoded_tile.ecc_parity;
            palette_symbol_bootstrap_omitted =
                payload_symbol_count == bit_count / 32u;
        }
    }
    bool palette_symbol_stream_input = palette_symbol_metadata_layout ||
        (!metadata_v2_layout && !aggregate_state.has_encoded_tile &&
         mapping_has_custom_palette(mapping) && (bit_count & 31u) == 0u);
    if (palette_symbol_stream_input && !aggregate_state.has_encoded_tile) {
        metadata_free_layout_autodetect = true;
    }
    if (!palette_symbol_stream_input) {
        console_line(2, "decode: unsupported payload format; expected version-zero field symbols");
        return 1;
    }
    makocode::DecoderContext decoder;
    const char* password_ptr = have_password ? (const char*)password_buffer.data : (const char*)0;
    usize password_length = have_password ? password_buffer.size : 0u;
    bool decoder_parse_succeeded = false;
    auto restore_validated_metadata_free_payload = [&]() -> bool {
        decoder.payload.release();
        if (!have_validated_metadata_free_payload ||
            !decoder.payload.append_bytes(validated_metadata_free_payload.data,
                                         validated_metadata_free_payload.size)) return false;
        decoder.has_payload = true;
        decoder.ecc_failed = false;
        decoder.partial_payload = false;
        decoder.password_attempted = validated_metadata_free_password_attempted;
        decoder.password_failed = false;
        decoder.password_not_encrypted = validated_metadata_free_password_not_encrypted;
        decoder.ecc_stats = validated_metadata_free_ecc_stats;
        decoder.encoded_metadata = validated_metadata_free_payload_metadata;
        decoder_parse_succeeded = true;
        return true;
    };
    if (palette_symbol_stream_input) {
        makocode::ByteBuffer compressed_or_container;
        makocode::EccDecodeStats symbol_stats;
        bool password_protected = false;
        makocode::PaletteSymbolHeaderRecovery header_recovery;
        if (metadata_v2_layout && aggregate_state.encoded_tile.ecc_original_bytes > 0u &&
            aggregate_state.encoded_tile.ecc_block_count > 0u) {
            header_recovery.available = true;
            header_recovery.password_protected = false;
            header_recovery.byte_count = aggregate_state.encoded_tile.ecc_original_bytes;
            header_recovery.block_count = aggregate_state.encoded_tile.ecc_block_count;
            header_recovery.data_symbols_per_block =
                aggregate_state.encoded_tile.ecc_block_data;
            header_recovery.parity_symbols = aggregate_state.encoded_tile.ecc_parity;
            header_recovery.overlay_erasure_budget =
                aggregate_state.encoded_tile.overlay_erasure_budget;
            header_recovery.unknown_error_budget =
                aggregate_state.encoded_tile.unknown_parity_budget;
        }
        decoder_parse_succeeded =
            makocode::palette_symbol_profile(palette_symbol_palette_size,
                                             palette_symbol_profile) &&
            makocode::palette_symbol_decode_stream(palette_symbol_profile,
                                                   bitstream,
                                                   erased_payload_symbols,
                                                   have_password ? (const char*)0 : password_ptr,
                                                   have_password ? 0u : password_length,
                                                   compressed_or_container,
                                                   &symbol_stats,
                                                   &password_protected,
                                                   &header_recovery,
                                                   !have_password,
                                                   palette_symbol_bootstrap_omitted,
                                                   0);
        if (!decoder_parse_succeeded && have_password && header_recovery.available) {
            header_recovery.password_protected = false;
            decoder_parse_succeeded =
                makocode::palette_symbol_decode_stream(palette_symbol_profile,
                                                       bitstream,
                                                       erased_payload_symbols,
                                                       password_ptr,
                                                       password_length,
                                                       compressed_or_container,
                                                       &symbol_stats,
                                                       &password_protected,
                                                       &header_recovery,
                                                       false,
                                                       palette_symbol_bootstrap_omitted,
                                                       &palette_password_context);
        }
        if (!decoder_parse_succeeded && have_password && header_recovery.available) {
            header_recovery.password_protected = true;
            decoder_parse_succeeded =
                makocode::palette_symbol_decode_stream(palette_symbol_profile,
                                                       bitstream,
                                                       erased_payload_symbols,
                                                       password_ptr,
                                                       password_length,
                                                       compressed_or_container,
                                                       &symbol_stats,
                                                       &password_protected,
                                                       &header_recovery,
                                                       false,
                                                       palette_symbol_bootstrap_omitted,
                                                       &palette_password_context);
        }
        if (!decoder_parse_succeeded && have_password &&
            !header_recovery.available) {
            decoder_parse_succeeded =
                makocode::palette_symbol_decode_stream(palette_symbol_profile,
                                                       bitstream,
                                                       erased_payload_symbols,
                                                       password_ptr,
                                                       password_length,
                                                       compressed_or_container,
                                                       &symbol_stats,
                                                       &password_protected,
                                                       0,
                                                       false,
                                                       palette_symbol_bootstrap_omitted,
                                                       &palette_password_context);
        }
        bool field_symbols_decoded = decoder_parse_succeeded;
        if (decoder_parse_succeeded) {
            if (password_protected || have_password) {
                makocode::ByteBuffer archive;
                bool is_container = false;
                usize authenticated_body_end = 0u;
                bool container_opened = makocode::decode_stealth_container(
                    compressed_or_container.data,
                    compressed_or_container.size,
                    password_ptr,
                    password_length,
                    archive,
                    is_container,
                    0,
                    &authenticated_body_end);
                if (container_opened && is_container) {
                    decoder.payload = archive;
                    archive.data = 0;
                    archive.size = 0u;
                    archive.capacity = 0u;
                    decoder.has_payload = true;
                    decoder.password_attempted = true;
                    decoder_parse_succeeded = true;
                    decoder.encoded_metadata.encryption_present = true;
                    memcpy(decoder.encoded_metadata.encryption_salt,
                           compressed_or_container.data,
                           makocode::ENCRYPTION_SALT_BYTES);
                    memcpy(decoder.encoded_metadata.encryption_nonce,
                           compressed_or_container.data + makocode::ENCRYPTION_SALT_BYTES,
                           makocode::ENCRYPTION_NONCE_BYTES);
                    if (authenticated_body_end >= makocode::ENCRYPTION_TAG_BYTES) {
                        memcpy(decoder.encoded_metadata.encryption_tag,
                               compressed_or_container.data + authenticated_body_end -
                                   makocode::ENCRYPTION_TAG_BYTES,
                               makocode::ENCRYPTION_TAG_BYTES);
                    }
                    decoder.encoded_metadata.lzma_present = true;
                } else if (have_password && header_recovery.available &&
                           (password_protected || is_container)) {
                    header_recovery.password_protected = false;
                    makocode::ByteBuffer unmasked_bytes;
                    makocode::EccDecodeStats unmasked_stats;
                    bool unmasked_protected = false;
                    bool unmasked_decoded = makocode::palette_symbol_decode_stream(
                        palette_symbol_profile, bitstream, erased_payload_symbols,
                        (const char*)0, 0u, unmasked_bytes, &unmasked_stats,
                        &unmasked_protected, &header_recovery, false,
                        palette_symbol_bootstrap_omitted, 0);
                    if (unmasked_decoded && !unmasked_protected &&
                        makocode::lzma_decompress(unmasked_bytes.data,
                                                  unmasked_bytes.size * 8u,
                                                  decoder.payload,
                                                  makocode::LZMA_SIZE_FROM_HEADER,
                                                  false)) {
                        decoder.has_payload = true;
                        decoder.password_attempted = true;
                        decoder.password_not_encrypted = true;
                        decoder_parse_succeeded = true;
                        symbol_stats = unmasked_stats;
                    } else {
                        decoder_parse_succeeded = false;
                        decoder.password_attempted = true;
                        decoder.password_failed = true;
                    }
                } else if (password_protected || is_container) {
                    decoder_parse_succeeded = false;
                    decoder.password_attempted = true;
                    decoder.password_failed = true;
                } else {
                    decoder_parse_succeeded = makocode::lzma_decompress(
                        compressed_or_container.data,
                        compressed_or_container.size * 8u,
                        decoder.payload,
                        makocode::LZMA_SIZE_FROM_HEADER,
                        false);
                    if (decoder_parse_succeeded) {
                        decoder.has_payload = true;
                        decoder.password_attempted = have_password;
                        decoder.password_not_encrypted = have_password;
                    }
                }
            } else {
                decoder_parse_succeeded = makocode::lzma_decompress(
                    compressed_or_container.data,
                    compressed_or_container.size * 8u,
                    decoder.payload,
                    makocode::LZMA_SIZE_FROM_HEADER,
                    false,
                    0,
                    !have_password && symbol_stats.unrecoverable_blocks > 0u);
                if (decoder_parse_succeeded) {
                    decoder.has_payload = true;
                    decoder.password_attempted = have_password;
                    decoder.password_not_encrypted = have_password;
                    decoder.partial_payload =
                        !have_password && symbol_stats.unrecoverable_blocks > 0u;
                }
            }
        }
        decoder.ecc_stats = symbol_stats;
        decoder.ecc_failed = !decoder_parse_succeeded;
        if (!decoder_parse_succeeded) {
            if (debug_requested) {
                char corrected[32];
                char failed[32];
                char failed_errors[32];
                char failed_erasures[32];
                char failed_stage[32];
                char failed_index[32];
                char decoded_blocks[32];
                u64_to_ascii(symbol_stats.corrected_symbols, corrected, sizeof(corrected));
                u64_to_ascii(symbol_stats.unrecoverable_blocks, failed, sizeof(failed));
                u64_to_ascii(symbol_stats.failed_block_errors, failed_errors, sizeof(failed_errors));
                u64_to_ascii(symbol_stats.failed_block_erasures, failed_erasures, sizeof(failed_erasures));
                u64_to_ascii(symbol_stats.failed_block_stage, failed_stage, sizeof(failed_stage));
                u64_to_ascii(symbol_stats.failed_block_index, failed_index, sizeof(failed_index));
                u64_to_ascii(symbol_stats.total_blocks, decoded_blocks, sizeof(decoded_blocks));
                console_write(2, "decode: field-symbol ECC corrected=");
                console_write(2, corrected);
                console_write(2, " unrecoverable_blocks=");
                console_write(2, failed);
                console_write(2, " failed_block_errors=");
                console_write(2, failed_errors);
                console_write(2, " failed_block_erasures=");
                console_write(2, failed_erasures);
                console_write(2, " stage=");
                console_write(2, failed_stage);
                console_write(2, " failed_block_index=");
                console_write(2, failed_index);
                console_write(2, " completed_blocks=");
                console_line(2, decoded_blocks);
            }
            if (have_password && !field_symbols_decoded) {
                decoder.password_attempted = true;
                decoder.password_failed = true;
            }
            console_line(2, "decode: field-symbol ECC, password, or decompression failure");
        }
    }
    if (!decoder_parse_succeeded) {
        if (advance_scatter_layout()) {
            console_line(2, "decode: metadata absent; retrying alternate payload layout");
            goto retry_decode;
        }
        if (metadata_free_layout_autodetect &&
            !metadata_free_candidates_ambiguous &&
            restore_validated_metadata_free_payload()) {
            // A prior layout passed ECC and complete payload validation.
        } else {
            if (!metadata_free_layout_autodetect && have_password &&
                !retried_public_symbol_placement) {
                retried_public_symbol_placement = true;
                mapping.symbol_placement_seed =
                    makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED;
                mapping.symbol_placement_password_context = 0;
                mapping_overrides.symbol_placement_seed =
                    makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED;
                mapping_overrides.symbol_placement_password_context = 0;
                goto retry_decode;
            }
            if (decoder.password_auth_failed()) {
                console_line(2, "decode: decryption failed (password mismatch or corrupted data)");
                return 1;
            }
            if (!force_disable_subgrid && !retried_subgrid) {
                console_line(2, "decode: parse failure; retrying without fiducial subgrid");
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            if (decoder.ecc_correction_failed() && !force_tile_center_sampling &&
                aggregate_state.has_encoded_tile &&
                aggregate_state.encoded_tile.page_width_pixels <= 96u &&
                aggregate_state.encoded_tile.page_height_pixels <= 96u) {
                force_tile_center_sampling = true;
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            if (decoder.ecc_correction_failed() && force_tile_center_sampling &&
                aggregate_state.has_encoded_tile &&
                aggregate_state.encoded_tile.palette_count == 2u &&
                aggregate_state.encoded_tile.page_width_pixels <= 96u &&
                aggregate_state.encoded_tile.page_height_pixels <= 96u &&
                small_scan_geometry_variant < 10u) {
                ++small_scan_geometry_variant;
                console_line(2, "decode: ECC could not repair payload; retrying adjacent small-scan geometry");
                goto retry_decode;
            }
            if (decoder.ecc_correction_failed()) {
                console_line(2, "decode: ECC could not repair the payload");
            } else {
                console_line(2, "decode: parse failure");
            }
            return 1;
        }
    }
    if (metadata_free_layout_autodetect &&
        (decoder.partial_payload || !archive_is_well_formed(decoder.payload))) {
        if (advance_scatter_layout()) {
            console_line(2, "decode: metadata absent; retrying alternate payload layout after ECC/archive validation");
            goto retry_decode;
        }
        if (!metadata_free_candidates_ambiguous &&
            restore_validated_metadata_free_payload()) {
            // A prior layout passed ECC and complete payload validation.
        } else {
            console_line(2, "decode: metadata-free payload failed ECC/archive validation");
            return 1;
        }
    } else if (metadata_free_layout_autodetect) {
        if (!have_validated_metadata_free_payload) {
            if (!validated_metadata_free_payload.append_bytes(decoder.payload.data,
                                                              decoder.payload.size)) {
                console_line(2, "decode: failed to retain validated metadata-free payload");
                return 1;
            }
            validated_metadata_free_ecc_stats = decoder.ecc_stats;
            validated_metadata_free_payload_metadata = decoder.encoded_metadata;
            validated_metadata_free_password_attempted = decoder.password_attempted;
            validated_metadata_free_password_not_encrypted = decoder.password_not_encrypted;
            have_validated_metadata_free_payload = true;
        } else if (decoder.payload.size != validated_metadata_free_payload.size ||
                   (decoder.payload.size &&
                    memcmp(decoder.payload.data, validated_metadata_free_payload.data,
                           decoder.payload.size) != 0)) {
            metadata_free_candidates_ambiguous = true;
        }
        if (advance_scatter_layout()) {
            console_line(2, "decode: metadata absent; checking alternate payload layout");
            goto retry_decode;
        }
        if (metadata_free_candidates_ambiguous) {
            console_line(2, "decode: metadata-free payload layout is ambiguous");
            return 1;
        }
    }
    if (decoder.has_payload &&
        !decoder.partial_payload &&
        !archive_is_well_formed(decoder.payload) &&
        force_tile_center_sampling &&
        aggregate_state.has_encoded_tile &&
        aggregate_state.encoded_tile.palette_count == 2u &&
        aggregate_state.encoded_tile.page_width_pixels <= 96u &&
        aggregate_state.encoded_tile.page_height_pixels <= 96u &&
        small_scan_geometry_variant < 10u) {
        ++small_scan_geometry_variant;
        console_line(2, "decode: recovered archive is invalid; retrying adjacent small-scan geometry");
        goto retry_decode;
    }
    if (decoder.partial_payload) {
        console_line(2, "decode: warning: ECC could not repair all payload blocks; recovered partial data");
    }
    if (decoder.password_attempt_made() && decoder.password_was_ignored()) {
        console_line(2, "decode: warning: payload was not encrypted; password ignored");
    }
    const makocode::EccDecodeStats& ecc_stats = decoder.ecc_statistics();
    if (ecc_stats.total_parity_symbols > 0u) {
        u64 corrected_bits = ecc_stats.corrected_symbols * 8u;
        u64 available_bits = ecc_stats.total_parity_symbols * 8u;
        double usage = 0.0;
        if (available_bits > 0u) {
            usage = ((double)corrected_bits * 100.0) / (double)available_bits;
        }
        char count_buffer[32];
        char bits_buffer[32];
        char percent_buffer[32];
        u64_to_ascii(ecc_stats.corrected_symbols, count_buffer, sizeof(count_buffer));
        u64_to_ascii(corrected_bits, bits_buffer, sizeof(bits_buffer));
        format_fixed_3(usage, percent_buffer, sizeof(percent_buffer));
        int ecc_output = decoded_stdout_requested ? 2 : 1;
        console_write(ecc_output, "decode: ECC detected ");
        console_write(ecc_output, count_buffer);
        console_write(ecc_output, (ecc_stats.corrected_symbols == 1u) ? " symbol error (" : " symbol errors (");
        console_write(ecc_output, bits_buffer);
        console_write(ecc_output, (corrected_bits == 1u) ? " bit corrected, " : " bits corrected, ");
        console_write(ecc_output, percent_buffer);
        console_line(ecc_output, "% of available correction bits)");
    }
    if (decoded_file_requested || decoded_stdout_requested) {
        const makocode::ByteBuffer& archive = decoder.payload;
        makocode::ByteBuffer decoded;
        bool ok = decoded.append_ascii("MAKOCODE-DECODED\n");
        char number[32];
        for (usize page = 0; ok && page < encoded_page_count; ++page) {
            u64_to_ascii(encoded_page_indices[page] ? encoded_page_indices[page] : page + 1u,
                         number,
                         sizeof(number));
            ok = decoded.append_ascii("page ") && decoded.append_ascii(number) && decoded.append_char('\n');
            if (encoded_page_present[page]) {
                const MetadataTile::Values& tile = encoded_pages[page];
                const char* names[] = {"tile_schema", "page_bits", "page_count", "page_index", "width", "height", "footer_rows", "fiducial_spacing", "ecc_enabled", "ecc_block_data", "ecc_parity", "ecc_block_count", "ecc_original_bytes", "palette_count"};
                u64 values[] = {MetadataTile::TILE_SCHEMA_VERSION, tile.page_bits, tile.page_count, tile.page_index, tile.page_width_pixels, tile.page_height_pixels, tile.footer_rows, tile.fiducial_spacing_pixels, tile.ecc_enabled ? 1u : 0u, tile.ecc_block_data, tile.ecc_parity, tile.ecc_block_count, tile.ecc_original_bytes, tile.palette_count};
                for (usize i = 0; ok && i < sizeof(values) / sizeof(values[0]); ++i) {
                    u64_to_ascii(values[i], number, sizeof(number));
                    ok = decoded.append_ascii("metadata ") && decoded.append_ascii(names[i]) && decoded.append_char(' ') && decoded.append_ascii(number) && decoded.append_char('\n');
                }
                for (u32 i = 0; ok && i < tile.palette_count; ++i) {
                    static const char hex[] = "0123456789ABCDEF";
                    const u8 rgb[] = {tile.palette[i].r, tile.palette[i].g, tile.palette[i].b};
                    ok = decoded.append_ascii("metadata palette ");
                    for (usize j = 0; ok && j < 3u; ++j) {
                        ok = decoded.append_char(hex[rgb[j] >> 4u]) && decoded.append_char(hex[rgb[j] & 15u]);
                    }
                    ok = ok && decoded.append_char('\n');
                }
            }
            ok = ok && decoded.append_ascii("end-page\n");
        }
        ok = ok && decoded.append_ascii("payload\n");
        u64 reported_frame_bits = (encoded_frame_bits == bit_count)
                                      ? encoded_frame_bits
                                      : (aggregate_state.has_page_bits
                                             ? aggregate_state.page_bits_value
                                             : ((encoded_page_count > 0u &&
                                                 encoded_page_present[0u])
                                                    ? encoded_pages[0u].page_bits
                                                    : 0u));
        if (reported_frame_bits > 0u) {
            u64_to_ascii(reported_frame_bits, number, sizeof(number));
            ok = ok && decoded.append_ascii("metadata frame_bits ") && decoded.append_ascii(number) && decoded.append_char('\n');
        }
        const makocode::EncodedPayloadMetadata& payload_metadata = decoder.encoded_metadata;
        if (payload_metadata.encryption_present) {
            const char* names[] = {"encryption_salt", "encryption_nonce", "encryption_tag"};
            const u8* bytes[] = {payload_metadata.encryption_salt,
                                 payload_metadata.encryption_nonce,
                                 payload_metadata.encryption_tag};
            const usize sizes[] = {makocode::ENCRYPTION_SALT_BYTES,
                                   makocode::ENCRYPTION_NONCE_BYTES,
                                   makocode::ENCRYPTION_TAG_BYTES};
            static const char digits[] = "0123456789ABCDEF";
            for (usize field = 0u; ok && field < 3u; ++field) {
                ok = decoded.append_ascii("metadata ") &&
                     decoded.append_ascii(names[field]) &&
                     decoded.append_char(' ');
                for (usize byte = 0u; ok && byte < sizes[field]; ++byte) {
                    ok = decoded.append_char(digits[bytes[field][byte] >> 4u]) &&
                         decoded.append_char(digits[bytes[field][byte] & 15u]);
                }
                ok = ok && decoded.append_char('\n');
            }
        }
        if (payload_metadata.lzma_present) {
            u64_to_ascii((u64)decoder.payload.size, number, sizeof(number));
            ok = decoded.append_ascii("metadata lzma_uncompressed_bytes ") && decoded.append_ascii(number) && decoded.append_char('\n');
        }
        struct ArchiveOutputContext {
            makocode::ByteBuffer* decoded;
            bool started;
        } archive_output = {&decoded, false};
        auto append_decoded_archive_entry = [](u8 type,
                                               const char* path,
                                               usize path_size,
                                               const u8* file_data,
                                               usize file_size,
                                               u64 entries,
                                               void* context) -> bool {
            ArchiveOutputContext* output = (ArchiveOutputContext*)context;
            makocode::ByteBuffer& target = *output->decoded;
            char number_buffer[32];
            static const char hex_digits[] = "0123456789ABCDEF";
            if (!output->started) {
                u64_to_ascii(entries, number_buffer, sizeof(number_buffer));
                if (!target.append_ascii("end-payload\narchive\nentries ") ||
                    !target.append_ascii(number_buffer) || !target.append_char('\n')) {
                    return false;
                }
                output->started = true;
            }
            if (type == 2u) {
                u64_to_ascii((u64)file_size, number_buffer, sizeof(number_buffer));
                if (!target.append_ascii("stream ") || !target.append_ascii(number_buffer) ||
                    !target.append_char('\n')) return false;
                for (usize i = 0u; i < file_size; ++i) {
                    u8 c = file_data[i];
                    if (c >= 32u && c <= 126u && c != '\\') {
                        if (!target.append_char((char)c)) return false;
                    } else if (!target.append_ascii("\\x") ||
                               !target.append_char(hex_digits[c >> 4u]) ||
                               !target.append_char(hex_digits[c & 15u])) {
                        return false;
                    }
                }
                return target.append_char('\n');
            }
            if (!target.append_ascii(type == 1u ? "directory " : "file ")) return false;
            const u8* path_bytes = (const u8*)path;
            for (usize i = 0u; i < path_size; ++i) {
                u8 c = path_bytes[i];
                if (c >= 33u && c <= 126u && c != '\\') {
                    if (!target.append_char((char)c)) return false;
                } else if (!target.append_ascii("\\x") ||
                           !target.append_char(hex_digits[c >> 4u]) ||
                           !target.append_char(hex_digits[c & 15u])) {
                    return false;
                }
            }
            if (type == 0u) {
                u64_to_ascii((u64)file_size, number_buffer, sizeof(number_buffer));
                if (!target.append_char(' ') || !target.append_ascii(number_buffer) ||
                    !target.append_char('\n')) return false;
                for (usize i = 0u; i < file_size; ++i) {
                    u8 c = file_data[i];
                    if (c >= 32u && c <= 126u && c != '\\') {
                        if (!target.append_char((char)c)) return false;
                    } else if (!target.append_ascii("\\x") ||
                               !target.append_char(hex_digits[c >> 4u]) ||
                               !target.append_char(hex_digits[c & 15u])) {
                        return false;
                    }
                }
            }
            return target.append_char('\n');
        };
        ok = ok && archive.data && archive_parse(archive,
                                                  append_decoded_archive_entry,
                                                  &archive_output,
                                                  (u64*)0,
                                                  decoder.partial_payload);
        ok = ok && archive_output.started && decoded.append_ascii("end-archive\nend\n");
        if (decoded_stdout_requested) {
            if (!ok || !write_all_fd(1, decoded.data, decoded.size)) {
                console_line(2, "decode: failed to write decoded stdout");
                return 1;
            }
            return 0;
        }
        char filename[64];
        if (!makocode_platform_local_timestamp(filename, sizeof(filename))) ok = false;
        makocode::ByteBuffer output_path;
        if (!ok || !join_output_path(output_dir, filename, output_path) ||
            !ensure_parent_directories((const char*)output_path.data)) {
            console_line(2, "decode: failed to write decoded file");
            return 1;
        }
        int output_fd = makocode_platform_open((const char*)output_path.data, MAKOCODE_OPEN_WRITE | MAKOCODE_OPEN_CREATE | MAKOCODE_OPEN_EXCLUSIVE, 0644);
        if (output_fd < 0) {
            console_line(2, "decode: failed to create decoded file");
            return 1;
        }
        bool written = write_all_fd(output_fd, decoded.data, decoded.size);
        if (makocode_platform_close(output_fd) != 0) written = false;
        if (!written) {
            makocode_platform_unlink((const char*)output_path.data);
            console_line(2, "decode: failed to write decoded file");
            return 1;
        }
        console_write(1, "decode: wrote ");
        console_line(1, (const char*)output_path.data);
        return 0;
    }
    if (!unpack_archive_to_directory(decoder.payload,
                                    output_dir,
                                    decoder.partial_payload)) {
        const char* raw_fallback = makocode_platform_getenv("MAKOCODE_DECODE_RAW_FALLBACK");
        bool allow_raw_fallback = (raw_fallback && *raw_fallback && raw_fallback[0] != '0');
        if (allow_raw_fallback && output_dir) {
            // Fall back to emitting the raw payload (even if empty) only when explicitly enabled.
            char path_buffer[4096];
            usize dir_len = ascii_length(output_dir);
            const char* filename = "payload.bin";
            usize file_len = (usize)sizeof("payload.bin") - 1u;
            bool need_sep = (dir_len > 0u) && (output_dir[dir_len - 1u] != '/' && output_dir[dir_len - 1u] != '\\');
            usize total_len = dir_len + (need_sep ? 1u : 0u) + file_len;
            if (total_len < sizeof(path_buffer)) {
                usize cursor = 0u;
                for (usize i = 0u; i < dir_len; ++i) {
                    char c = output_dir[i];
                    if (c == '\\') {
                        c = '/';
                    }
                    path_buffer[cursor++] = c;
                }
                if (need_sep) {
                    path_buffer[cursor++] = '/';
                }
                for (usize i = 0u; i < file_len; ++i) {
                    path_buffer[cursor++] = filename[i];
                }
                path_buffer[cursor] = '\0';
                write_buffer_to_file(path_buffer, decoder.payload);
            }
            console_line(1, "decode: wrote raw payload.bin (MAKOCODE_DECODE_RAW_FALLBACK enabled)");
            return 0;
        }
        return 1;
    }
    console_write(1, "decode: wrote files to ");
    console_line(1, output_dir);
    return 0;
}

