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
    if (mapping.palette_set) {
        if (!image_mapping_build_custom_palette(mapping, "decode")) {
            return 1;
        }
    }
    ImageMappingConfig mapping_overrides = mapping;
    makocode::ByteBuffer bitstream;
    makocode::ByteBuffer erased_payload_symbols;
    makocode::ByteBuffer overlay_selector_counts;
    u64 overlay_selector_seed = 0u;
    u16 overlay_unknown_parity_budget = 0u;
    u16 overlay_erasure_budget = 0u;
    u64 bit_count = 0u;
    makocode::ByteBuffer frame_bits_for_length_recovery;
    u64 frame_bit_count_for_length_recovery = 0u;
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
    frame_bits_for_length_recovery.release();
    frame_bit_count_for_length_recovery = 0u;
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
                                                      small_scan_geometry_variant);

        if (!frame_extracted) {
            if (retry_without_subgrid_allowed && !force_disable_subgrid && !retried_subgrid) {

                console_line(2, "decode: retrying without fiducial subgrid (frame extraction failed)");
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            MetadataTile::Values recovered_values;
            if (!recover_metadata_free_single_page(ppm_stream,
                                                   mapping_overrides,
                                                   no_metadata,
                                                   have_password ? (const char*)password_buffer.data : (const char*)0,
                                                   have_password ? password_buffer.size : 0u,
                                                   frame_bits,
                                                   frame_erasure_bits,
                                                   frame_bit_count,
                                                   single_page_capacity_bits,
                                                   single_state,
                                                   recovered_values)) {
                console_line(2, "decode: invalid ppm input");
                return 1;
            }
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
            makocode::BitWriter assembled_erasures;
            auto append_missing_bits = [&](u64 bits) -> bool {
                while (bits > 0u) {
                    u32 chunk = bits >= 64u ? 64u : (u32)bits;
                    u64 ones = chunk == 64u ? U64_MAX_VALUE : (((u64)1u << chunk) - 1u);
                    if (!assembled_bits.write_bits(0u, chunk) ||
                        !assembled_erasures.write_bits(ones, chunk)) return false;
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
                    if (!append_bits_from_buffer(assembled_erasures,
                                                 frame_erasure_bits.data,
                                                 observed_bits)) {
                        console_line(2, "decode: failed to assemble stdin page erasures");
                        return 1;
                    }
                } else {
                    u64 clean_bits = observed_bits;
                    while (clean_bits > 0u) {
                        u32 chunk = clean_bits >= 64u ? 64u : (u32)clean_bits;
                        if (!assembled_erasures.write_bits(0u, chunk)) {
                            console_line(2, "decode: failed to assemble stdin page erasures");
                            return 1;
                        }
                        clean_bits -= chunk;
                    }
                }
                if (observed_bits < segment_bits &&
                    !append_missing_bits(segment_bits - observed_bits)) {
                    console_line(2, "decode: failed to mark truncated stdin page as erased");
                    return 1;
                }
            }
            usize assembled_bytes = assembled_bits.byte_size();
            usize assembled_erasure_bytes = assembled_erasures.byte_size();
            frame_bits.release();
            frame_erasure_bits.release();
            if ((assembled_bytes && !frame_bits.ensure(assembled_bytes)) ||
                (assembled_erasure_bytes && !frame_erasure_bits.ensure(assembled_erasure_bytes))) {
                console_line(2, "decode: failed to allocate assembled stdin pages");
                return 1;
            }
            if (assembled_bytes) memcpy(frame_bits.data, assembled_bits.data(), assembled_bytes);
            if (assembled_erasure_bytes) {
                memcpy(frame_erasure_bits.data,
                       assembled_erasures.data(),
                       assembled_erasure_bytes);
            }
            frame_bits.size = assembled_bytes;
            frame_erasure_bits.size = assembled_erasure_bytes;
            frame_bit_count = assembled_bits.bit_size();
        }
        if (single_color_rotation) {
            for (usize i = 0u; i < frame_bits.size; ++i) {
                u8 rotate = (u8)((i % 3u) + 1u);
                frame_bits.data[i] = rotate_right_u8(frame_bits.data[i], rotate);
            }
            for (usize i = 0u; i < frame_erasure_bits.size; ++i) {
                u8 rotate = (u8)((i % 3u) + 1u);
                frame_erasure_bits.data[i] = rotate_right_u8(frame_erasure_bits.data[i], rotate);
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
        if (single_state.has_ecc_flag && !single_state.ecc_flag_value) {
            byte_buffer_move(frame_bits_for_length_recovery, frame_bits);
            frame_bit_count_for_length_recovery = frame_bit_count;
        }
        aggregate_state = single_state;
        have_metadata = true;

    } else {
        makocode::BitWriter frame_aggregator;
        makocode::BitWriter erasure_aggregator;
        frame_aggregator.reset();
        erasure_aggregator.reset();
        makocode::ByteBuffer indexed_frame_bits;
        makocode::ByteBuffer indexed_erasure_bits;
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
        AffineTransform previous_page_affine_hint = {};
        bool previous_page_affine_hint_available = false;
        if (!center_probe_attempted && !force_disable_subgrid &&
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
                PpmParserState probe_state;
                bool probe_found = ppm_extract_frame_bits(
                    probe_stream, mapping, probe_bits, probe_bit_count, probe_state,
                    false, 0, 0, (u64)probe_index + 1u, &probe_erasures,
                    0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0, 0, false, true, true, true);
                if (probe_found && probe_state.has_encoded_tile &&
                    metadata_schema_is_current(probe_state.encoded_tile.schema_version)) {
                    center_probe_page_present[probe_index] = true;
                    encoded_pages[probe_index] = probe_state.encoded_tile;
                    if (!center_probe_available) {
                        center_probe_tile = probe_state.encoded_tile;
                        center_probe_available = true;
                    }
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
                                                              : true);

            if (!frame_extracted) {
                if (retry_without_subgrid_allowed && !force_disable_subgrid && !retried_subgrid) {

                    console_line(2, "decode: retrying without fiducial subgrid (frame extraction failed)");
                    force_disable_subgrid = true;
                    retried_subgrid = true;
                    goto retry_decode;
                }
                if (file_count == 1u) {
                    MetadataTile::Values recovered_values;
                    if (recover_metadata_free_single_page(ppm_stream,
                                                           mapping_overrides,
                                                           no_metadata,
                                                           have_password ? (const char*)password_buffer.data : (const char*)0,
                                                           have_password ? password_buffer.size : 0u,
                                                           page_bits,
                                                           page_erasure_bits,
                                                           page_bit_count,
                                                           page_capacity_bits,
                                                           page_state,
                                                           recovered_values)) {
                        frame_extracted = true;
                        page_color_rotation = false;
                    }
                }
                if (!frame_extracted) {
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
                    u64 frame_bytes_u64 = (indexed_frame_bit_total >> 3u) +
                                          ((indexed_frame_bit_total & 7u) ? 1u : 0u);
                    if (frame_bytes_u64 > (u64)USIZE_MAX_VALUE ||
                        (frame_bytes_u64 &&
                         (!indexed_frame_bits.ensure((usize)frame_bytes_u64) ||
                          !indexed_erasure_bits.ensure((usize)frame_bytes_u64)))) {
                        console_line(2, "decode: failed to allocate metadata-indexed pages");
                        return 1;
                    }
                    indexed_frame_bits.size = (usize)frame_bytes_u64;
                    indexed_erasure_bits.size = (usize)frame_bytes_u64;
                    if (frame_bytes_u64) {
                        memset(indexed_frame_bits.data, 0, (usize)frame_bytes_u64);
                        memset(indexed_erasure_bits.data, 0xFF, (usize)frame_bytes_u64);
                    }
                    u64 prior_bits = frame_aggregator.bit_size();
                    if (prior_bits > indexed_frame_bit_total ||
                        prior_bits > erasure_aggregator.bit_size()) {
                        console_line(2, "decode: preceding metadata-free pages exceed indexed payload length");
                        return 1;
                    }
                    makocode::BitReader prior_frame_reader;
                    makocode::BitReader prior_erasure_reader;
                    prior_frame_reader.reset(frame_aggregator.data(), prior_bits);
                    prior_erasure_reader.reset(erasure_aggregator.data(), prior_bits);
                    for (u64 bit_index = 0u; bit_index < prior_bits; ++bit_index) {
                        u8 bit = prior_frame_reader.read_bit();
                        u8 erased = prior_erasure_reader.read_bit();
                        if (prior_frame_reader.failed || prior_erasure_reader.failed) {
                            console_line(2, "decode: failed to migrate preceding metadata-free pages");
                            return 1;
                        }
                        usize destination_byte = (usize)(bit_index >> 3u);
                        u8 destination_mask = (u8)(1u << (bit_index & 7u));
                        if (bit) indexed_frame_bits.data[destination_byte] |= destination_mask;
                        if (!erased) indexed_erasure_bits.data[destination_byte] &= (u8)~destination_mask;
                    }
                }
            }
            if (aggregate_page_capacity_bits &&
                aggregate_page_capacity_bits != page_capacity_bits) {
                console_line(2, "decode: page capacity differs between pages");
                return 1;
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
                    bool erased_value = page_erasure_bits.data &&
                                        source_byte < page_erasure_bits.size &&
                                        (page_erasure_bits.data[source_byte] & source_mask) != 0u;
                    if (erased_value) indexed_erasure_bits.data[destination_byte] |= destination_mask;
                    else indexed_erasure_bits.data[destination_byte] &= (u8)~destination_mask;
                }
            } else {
                if (!append_bits_from_buffer(frame_aggregator, page_bits.data, effective_bits)) {
                    console_line(2, "decode: failed to assemble bitstream");
                    return 1;
                }
                if (page_erasure_bits.data && page_erasure_bits.size) {
                    if (!append_bits_from_buffer(erasure_aggregator,
                                                 page_erasure_bits.data,
                                                 effective_bits)) {
                        console_line(2, "decode: failed to assemble erasure map");
                        return 1;
                    }
                } else {
                    for (u64 bit_index = 0u; bit_index < effective_bits; ++bit_index) {
                        if (!erasure_aggregator.write_bit(0u)) {
                            console_line(2, "decode: failed to assemble erasure map");
                            return 1;
                        }
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
            usize erasure_bytes = indexed_page_assembly
                                      ? indexed_erasure_bits.size
                                      : erasure_aggregator.byte_size();
            for (usize i = 0u; i < erasure_bytes; ++i) {
                u8 rotate = (u8)((i % 3u) + 1u);
                u8* erasure_byte = indexed_page_assembly
                                       ? &indexed_erasure_bits.data[i]
                                       : &erasure_aggregator.buffer.data[i];
                *erasure_byte = rotate_right_u8(*erasure_byte, rotate);
            }
        }
        const u8* frame_data = indexed_page_assembly
                                   ? indexed_frame_bits.data : frame_aggregator.data();
        const u8* erasure_data = indexed_page_assembly
                                     ? indexed_erasure_bits.data : erasure_aggregator.data();
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
        if (aggregate_state.has_ecc_flag && !aggregate_state.ecc_flag_value) {
            if (indexed_page_assembly) {
                byte_buffer_move(frame_bits_for_length_recovery, indexed_frame_bits);
            } else {
                byte_buffer_move(frame_bits_for_length_recovery, frame_aggregator.buffer);
            }
            frame_bit_count_for_length_recovery = frame_bit_total;
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
    bool ecc_header_repaired = false;
    makocode::EccHeaderInfo bitstream_header;
    bool bitstream_header_present = false;
    bool bitstream_header_valid = false;
    if (!metadata_v2_layout && bitstream.data &&
        bitstream.size >= makocode::ECC_HEADER_TOTAL_BYTES) {
        bitstream_header_present = makocode::parse_ecc_header(bitstream.data, bitstream.size, bitstream_header);
        bitstream_header_valid = bitstream_header_present && bitstream_header.valid && bitstream_header.enabled;
    }
    bool ecc_metadata_available = have_metadata &&
                                  aggregate_state.has_ecc_flag &&
                                  aggregate_state.ecc_flag_value;
    bool ecc_metadata_complete = ecc_metadata_available &&
                                 aggregate_state.has_ecc_block_data &&
                                 aggregate_state.has_ecc_parity &&
                                 aggregate_state.has_ecc_block_count &&
                                 aggregate_state.has_ecc_original_bytes;

    if (metadata_v2_layout && ecc_metadata_complete) {
        u64 block_total = aggregate_state.ecc_block_data_value +
                          aggregate_state.ecc_parity_value;
        if (!block_total || aggregate_state.ecc_block_count_value > U64_MAX_VALUE / block_total) {
            console_line(2, "decode: invalid metadata ECC block layout");
            return 1;
        }
        u64 protected_bytes_u64 = block_total * aggregate_state.ecc_block_count_value;
        if (protected_bytes_u64 > (u64)USIZE_MAX_VALUE ||
            protected_bytes_u64 > U64_MAX_VALUE / 8u ||
            protected_bytes_u64 > bitstream.size ||
            protected_bytes_u64 * 8u > bit_count) {
            console_line(2, "decode: metadata ECC payload length exceeds extracted data");
            return 1;
        }
        bitstream.size = (usize)protected_bytes_u64;
        bit_count = protected_bytes_u64 * 8u;
        if (erased_payload_symbols.data) {
            if (protected_bytes_u64 > erased_payload_symbols.size) {
                console_line(2, "decode: metadata ECC erasure map is truncated");
                return 1;
            }
            erased_payload_symbols.size = (usize)protected_bytes_u64;
        }
    }
    
    if (!metadata_v2_layout && !bitstream_header_valid) {
        if (ecc_metadata_complete &&
            bitstream.data &&
            bitstream.size >= makocode::ECC_HEADER_TOTAL_BYTES &&
            bit_count >= (u64)makocode::ECC_HEADER_TOTAL_BITS) {
            u64 block_data_value = aggregate_state.ecc_block_data_value;
            u64 parity_value = aggregate_state.ecc_parity_value;
            if (block_data_value <= 0xFFFFu && parity_value <= 0xFFFFu) {
                u8 header_bytes[makocode::ECC_HEADER_TOTAL_BYTES];
                if (makocode::build_ecc_header_bytes(header_bytes,
                                                     (u16)block_data_value,
                                                     (u16)parity_value,
                                                     aggregate_state.ecc_block_count_value,
                                                     aggregate_state.ecc_original_bytes_value,
                                                     aggregate_state.has_encoded_tile
                                                         ? aggregate_state.encoded_tile.ecc_header_magic
                                                         : makocode::ECC_HEADER_MAGIC,
                                                     aggregate_state.has_encoded_tile
                                                         ? aggregate_state.encoded_tile.ecc_header_version
                                                         : makocode::ECC_HEADER_VERSION,
                                                     aggregate_state.has_encoded_tile
                                                         ? aggregate_state.encoded_tile.ecc_header_flags
                                                         : 0x01u,
                                                     aggregate_state.has_encoded_tile
                                                         ? aggregate_state.encoded_tile.ecc_header_reserved
                                                         : 0u)) {
                    bool differs = false;
                    for (usize i = 0u; i < makocode::ECC_HEADER_TOTAL_BYTES; ++i) {
                        if (bitstream.data[i] != header_bytes[i]) {
                            differs = true;
                            break;
                        }
                    }
                    if (differs) {
                        for (usize i = 0u; i < makocode::ECC_HEADER_TOTAL_BYTES; ++i) {
                            bitstream.data[i] = header_bytes[i];
                        }
                        ecc_header_repaired = true;
                    }
                }
            }
        } else if (ecc_metadata_available) {
            
            console_line(2, "decode: warning: ECC metadata incomplete; header reconstruction skipped");
        }
    }
    if (ecc_header_repaired) {
        console_line(2, "decode: repaired ECC header from metadata");
    }
    makocode::ByteBuffer synthetic_header_stream;
    makocode::ByteBuffer synthetic_header_erasures;
    const u8* decoder_stream_data = bitstream.data;
    u64 decoder_stream_bits = bit_count;
    const u8* decoder_erasure_data = erased_payload_symbols.data;
    usize decoder_erasure_bytes = erased_payload_symbols.size;
    bool synthetic_ecc_header = false;
    if (metadata_v2_layout && ecc_metadata_complete) {
        if (!bitstream.data || (bit_count & 7u) != 0u ||
            bit_count > U64_MAX_VALUE - (u64)makocode::ECC_HEADER_TOTAL_BITS ||
            bitstream.size > USIZE_MAX_VALUE - makocode::ECC_HEADER_TOTAL_BYTES ||
            aggregate_state.ecc_block_data_value > 0xFFFFu ||
            aggregate_state.ecc_parity_value > 0xFFFFu ||
            !synthetic_header_stream.ensure(bitstream.size + makocode::ECC_HEADER_TOTAL_BYTES)) {
            console_line(2, "decode: invalid metadata ECC header");
            return 1;
        }
        u8 header_bytes[makocode::ECC_HEADER_TOTAL_BYTES];
        if (!makocode::build_ecc_header_bytes(header_bytes,
                                              (u16)aggregate_state.ecc_block_data_value,
                                              (u16)aggregate_state.ecc_parity_value,
                                              aggregate_state.ecc_block_count_value,
                                              aggregate_state.ecc_original_bytes_value,
                                              aggregate_state.encoded_tile.ecc_header_magic,
                                              aggregate_state.encoded_tile.ecc_header_version,
                                              aggregate_state.encoded_tile.ecc_header_flags,
                                              aggregate_state.encoded_tile.ecc_header_reserved)) {
            console_line(2, "decode: invalid metadata ECC header");
            return 1;
        }
        synthetic_header_stream.size = makocode::ECC_HEADER_TOTAL_BYTES + bitstream.size;
        for (usize i = 0u; i < makocode::ECC_HEADER_TOTAL_BYTES; ++i) {
            synthetic_header_stream.data[i] = header_bytes[i];
        }
        for (usize i = 0u; i < bitstream.size; ++i) {
            synthetic_header_stream.data[makocode::ECC_HEADER_TOTAL_BYTES + i] = bitstream.data[i];
        }
        decoder_stream_data = synthetic_header_stream.data;
        decoder_stream_bits = bit_count + (u64)makocode::ECC_HEADER_TOTAL_BITS;
        synthetic_ecc_header = true;
        if (erased_payload_symbols.data) {
            if (erased_payload_symbols.size < bitstream.size ||
                !synthetic_header_erasures.ensure(synthetic_header_stream.size)) {
                console_line(2, "decode: invalid payload erasure map");
                return 1;
            }
            synthetic_header_erasures.size = synthetic_header_stream.size;
            for (usize i = 0u; i < makocode::ECC_HEADER_TOTAL_BYTES; ++i) {
                synthetic_header_erasures.data[i] = 0u;
            }
            for (usize i = 0u; i < bitstream.size; ++i) {
                synthetic_header_erasures.data[makocode::ECC_HEADER_TOTAL_BYTES + i] =
                    erased_payload_symbols.data[i];
            }
            decoder_erasure_data = synthetic_header_erasures.data;
            decoder_erasure_bytes = synthetic_header_erasures.size;
        }
    }
    makocode::DecoderContext decoder;
    const char* password_ptr = have_password ? (const char*)password_buffer.data : (const char*)0;
    usize password_length = have_password ? password_buffer.size : 0u;
    if (!decoder.parse((u8*)decoder_stream_data,
                       decoder_stream_bits,
                       password_ptr,
                       password_length,
                       decoder_erasure_data,
                       decoder_erasure_bytes,
                       overlay_selector_seed,
                       overlay_selector_counts.data,
                       overlay_selector_counts.size,
                       overlay_unknown_parity_budget,
                       overlay_erasure_budget,
                       metadata_v2_layout,
                       synthetic_ecc_header,
                       makocode::LZMA_SIZE_FROM_HEADER,
                       false)) {
        if (decoder.password_auth_failed()) {
            console_line(2, "decode: decryption failed (password mismatch or corrupted data)");
            return 1;
        }
        bool frame_length_recovered = false;
        if (!decoder.ecc_correction_failed() &&
            !have_password &&
            !metadata_v2_layout &&
            aggregate_state.has_ecc_flag &&
            !aggregate_state.ecc_flag_value &&
            frame_bits_for_length_recovery.data &&
            frame_bit_count_for_length_recovery > 64u) {
            makocode::BitReader length_reader;
            length_reader.reset(frame_bits_for_length_recovery.data,
                                frame_bit_count_for_length_recovery);
            u64 observed_bits = length_reader.read_bits(64u);
            u64 available_bits = frame_bit_count_for_length_recovery - 64u;
            makocode::ByteBuffer recovered_stream;
            u64 recovered_bits = 0u;
            u32 valid_candidates = 0u;
            for (u32 header_bit = 0u; header_bit < 64u; ++header_bit) {
                u64 candidate_bits = observed_bits ^ (1ull << header_bit);
                if (candidate_bits == 0u ||
                    (candidate_bits & 7u) != 0u ||
                    candidate_bits > available_bits) {
                    continue;
                }
                u8& candidate_header_byte =
                    frame_bits_for_length_recovery.data[header_bit >> 3u];
                candidate_header_byte ^= (u8)(1u << (header_bit & 7u));
                makocode::ByteBuffer candidate_stream;
                u64 candidate_stream_bits = 0u;
                bool candidate_extracted = frame_bits_to_payload(
                    frame_bits_for_length_recovery.data,
                    frame_bit_count_for_length_recovery,
                    aggregate_state,
                    candidate_stream,
                    candidate_stream_bits);
                candidate_header_byte ^= (u8)(1u << (header_bit & 7u));
                if (!candidate_extracted || candidate_stream_bits != candidate_bits) {
                    continue;
                }
                makocode::ByteBuffer candidate_input;
                if (!candidate_input.ensure(candidate_stream.size)) {
                    continue;
                }
                candidate_input.size = candidate_stream.size;
                for (usize i = 0u; i < candidate_stream.size; ++i) {
                    candidate_input.data[i] = candidate_stream.data[i];
                }
                makocode::DecoderContext candidate_decoder;
                if (!candidate_decoder.parse(candidate_input.data,
                                             candidate_stream_bits,
                                             (const char*)0,
                                             0u)) {
                    continue;
                }
                const makocode::ByteBuffer& archive = candidate_decoder.payload;
                bool archive_valid = archive_is_well_formed(archive);
                if (archive_valid) {
                    ++valid_candidates;
                    if (valid_candidates == 1u) {
                        byte_buffer_move(recovered_stream, candidate_stream);
                        recovered_bits = candidate_stream_bits;
                    }
                }
            }
            if (valid_candidates == 1u) {
                byte_buffer_move(bitstream, recovered_stream);
                bit_count = recovered_bits;
                frame_length_recovered = decoder.parse(bitstream.data,
                                                       bit_count,
                                                       password_ptr,
                                                       password_length);
                if (frame_length_recovered) {
                    console_line(2, "decode: recovered frame length from a one-bit header correction");
                }
            }
        }
        if (!frame_length_recovered) {
            if (!decoder.ecc_correction_failed() && !force_disable_subgrid && !retried_subgrid) {
                console_line(2, "decode: parse failure; retrying without fiducial subgrid");
                force_disable_subgrid = true;
                retried_subgrid = true;
                goto retry_decode;
            }
            if (decoder.ecc_correction_failed() && !force_disable_subgrid && !retried_subgrid) {
                console_line(2, "decode: ECC could not repair the payload; retrying without fiducial subgrid");
                if (file_count == 1u && aggregate_state.has_encoded_tile &&
                    aggregate_state.encoded_tile.page_width_pixels > 96u &&
                    aggregate_state.encoded_tile.page_height_pixels > 96u) {
                    single_retry_tile = aggregate_state.encoded_tile;
                    single_retry_tile_available = true;
                }
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
    frame_bits_for_length_recovery.release();
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
        if (payload_metadata.ecc_present) {
            const makocode::EccHeader& ecc = payload_metadata.ecc;
            const char* names[] = {"ecc_magic", "ecc_version", "ecc_flags", "ecc_reserved", "ecc_block_data", "ecc_parity", "ecc_block_count", "ecc_original_bytes"};
            u64 values[] = {ecc.magic, ecc.version, ecc.flags, ecc.reserved, ecc.block_data, ecc.parity, ecc.block_count, ecc.original_bytes};
            for (usize i = 0; ok && i < sizeof(values) / sizeof(values[0]); ++i) {
                u64_to_ascii(values[i], number, sizeof(number));
                ok = decoded.append_ascii("metadata ") && decoded.append_ascii(names[i]) && decoded.append_char(' ') && decoded.append_ascii(number) && decoded.append_char('\n');
            }
        }
        if (payload_metadata.encryption_present) {
            const u8* header = payload_metadata.encryption_header;
            const char* names[] = {"encryption_magic", "encryption_version", "encryption_flags", "encryption_kdf", "encryption_reserved", "encryption_iterations", "encryption_plaintext_bytes"};
            u64 values[] = {read_le_u32(header), header[4], header[5], header[6], header[7], read_le_u32(header + 8u), read_le_u64(header + 12u)};
            for (usize i = 0; ok && i < sizeof(values) / sizeof(values[0]); ++i) {
                u64_to_ascii(values[i], number, sizeof(number));
                ok = decoded.append_ascii("metadata ") && decoded.append_ascii(names[i]) && decoded.append_char(' ') && decoded.append_ascii(number) && decoded.append_char('\n');
            }
            const char* byte_names[] = {"encryption_salt", "encryption_nonce", "encryption_tag"};
            const u8* bytes[] = {header + 20u, header + 36u, payload_metadata.encryption_tag};
            usize sizes[] = {16u, 12u, 16u};
            static const char digits[] = "0123456789ABCDEF";
            for (usize i = 0; ok && i < 3u; ++i) {
                ok = decoded.append_ascii("metadata ") && decoded.append_ascii(byte_names[i]) && decoded.append_char(' ');
                for (usize j = 0; ok && j < sizes[i]; ++j) ok = decoded.append_char(digits[bytes[i][j] >> 4u]) && decoded.append_char(digits[bytes[i][j] & 15u]);
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

