/*
 * Encoder and overlay command implementation.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_common.hpp"
struct OverlayPage {
    makocode::ByteBuffer original;
    makocode::ByteBuffer pixels;
    PpmParserState      metadata;
    u32                 width;
    u32                 height;
    u32                 data_height;
    u32                 footer_rows;
    u8                  color_mode;
    bool                use_custom_palette;
    ImageMappingConfig  custom_mapping;
    u8                  custom_symbol_bits;

    OverlayPage()
        : original(),
          pixels(),
          metadata(),
          width(0u),
          height(0u),
          data_height(0u),
          footer_rows(0u),
          color_mode(1u),
          use_custom_palette(false),
          custom_mapping(),
          custom_symbol_bits(0u) {}

    ~OverlayPage() {
        original.release();
        pixels.release();
        metadata = PpmParserState();
        width = 0u;
        height = 0u;
        data_height = 0u;
        footer_rows = 0u;
        color_mode = 1u;
        use_custom_palette = false;
        custom_symbol_bits = 0u;
    }
};

struct EncodeOverlay {
    const char* path;
    const char* pages;
    usize pages_length;
    OverlayPage image;
    bool pages_specified;

    EncodeOverlay()
        : path(0), pages(0), pages_length(0u), image(), pages_specified(false) {}
};

static bool load_overlay_page(const char* path, OverlayPage& page);
static bool merge_overlay_into_encoded_page(const char* page_path,
                                            const OverlayPage& overlay_page,
                                            const char* ignore_colors,
                                            usize ignore_colors_length,
                                            bool debug_requested,
                                            const MetadataTile::Values* tile_override = 0,
                                            const ImageMappingConfig* mapping_override = 0,
                                            bool include_metadata_overwrites = true);

int command_encode(int arg_count, char** args) {
    if (arguments_request_help(arg_count, args)) {
        write_encode_help();
        return 0;
    }
    g_fiducial_defaults.spacing_pixels = 24u;
    g_encode_metadata_disabled = false;
    ImageMappingConfig mapping;
    PageFooterConfig footer_config;
    enum EccMode { ECC_AUTO_MINIMUM, ECC_EXPLICIT_RATIO, ECC_FILL };
    EccMode ecc_mode = ECC_AUTO_MINIMUM;
    double overlay_fraction = 0.0;
    u64 overlay_fraction_numerator = 0u;
    u64 overlay_fraction_denominator = 1u;
    static const usize MAX_OVERLAY_IMAGES = 64u;
    EncodeOverlay overlays[MAX_OVERLAY_IMAGES];
    usize overlay_count = 0u;
    int pending_overlay = -1;
    makocode::ByteBuffer overlay_page_assignments;
    makocode::ByteBuffer metadata_page_assignments;
    const char* metadata_pages_value = 0;
    usize metadata_pages_length = 0u;
    const char* overlay_ignore_colors_text = 0;
    usize overlay_ignore_colors_length = 0u;
    double ecc_redundancy = 0.0;
    static const usize MAX_INPUT_ITEMS = 512u;
    const char* input_paths[MAX_INPUT_ITEMS];
    usize input_count = 0u;
    const char* hidden_input_paths[MAX_INPUT_ITEMS];
    usize hidden_input_count = 0u;
    makocode::ByteBuffer footer_buffer;
    makocode::ByteBuffer password_buffer;
    makocode::ByteBuffer hidden_password_buffer;
    makocode::ByteBuffer random_data_buffer;
    makocode::ByteBuffer output_dir_buffer;
    makocode::ByteBuffer prefix_buffer;
    bool have_password = false;
    bool have_hidden_password = false;
    bool have_random_data = false;
    bool hidden_option_specified = false;
    bool hidden_fill = false;
    double hidden_ratio = 0.0;
    bool fill_pages_specified = false;
    u64 fill_pages = 0u;
    bool have_footer = false;
    const char* output_dir = ".";
    bool have_output_dir = false;
    bool have_prefix = false;
    bool ecc_option_specified = false;
    bool overlay_fraction_specified = false;
    bool overlay_ignore_colors_specified = false;
    bool no_metadata = false;
    bool no_fiducials = false;
    bool debug_requested = false;
    for (int i = 0; i < arg_count; ++i) {
        bool handled = false;
        if (!process_image_mapping_option(arg_count, args, &i, mapping, "encode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        handled = false;
        if (!process_fiducial_spacing_option(arg_count, args, &i, g_fiducial_defaults, "encode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        const char* arg = args[i];
        if (!arg) {
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--no-metadata")) {
            no_metadata = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--no-fiducials")) {
            no_fiducials = true;
            continue;
        }
        const char metadata_pages_prefix[] = "--metadata-pages=";
        const char* selected_metadata_pages = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--metadata-pages")) {
            if ((i + 1) >= arg_count || !args[i + 1] || !args[i + 1][0]) {
                console_line(2, "encode: --metadata-pages requires all, page numbers, or ranges");
                return 1;
            }
            selected_metadata_pages = args[++i];
        } else if (ascii_starts_with(arg, metadata_pages_prefix)) {
            selected_metadata_pages = arg + sizeof(metadata_pages_prefix) - 1u;
        }
        if (selected_metadata_pages) {
            if (metadata_pages_value) {
                console_line(2, "encode: --metadata-pages specified multiple times");
                return 1;
            }
            metadata_pages_value = selected_metadata_pages;
            metadata_pages_length = ascii_length(metadata_pages_value);
            if (!metadata_pages_length) {
                console_line(2, "encode: --metadata-pages requires all, page numbers, or ranges");
                return 1;
            }
            if (metadata_pages_length == 3u &&
                memcmp(metadata_pages_value, "all", 3u) == 0) continue;
            usize item_start = 0u;
            while (item_start < metadata_pages_length) {
                usize item_end = item_start;
                while (item_end < metadata_pages_length && metadata_pages_value[item_end] != ',') {
                    ++item_end;
                }
                usize dash = item_start;
                while (dash < item_end && metadata_pages_value[dash] != '-') ++dash;
                bool keyword = (item_end - item_start == 6u &&
                                memcmp(metadata_pages_value + item_start, "middle", 6u) == 0) ||
                               (item_end - item_start == 4u &&
                                memcmp(metadata_pages_value + item_start, "last", 4u) == 0);
                u64 first_page = 0u;
                u64 last_page = 0u;
                bool numeric = dash > item_start &&
                    ascii_to_u64(metadata_pages_value + item_start,
                                 dash - item_start,
                                 &first_page) && first_page > 0u;
                if (numeric) {
                    last_page = first_page;
                    if (dash < item_end) {
                        numeric = dash + 1u < item_end &&
                                  ascii_to_u64(metadata_pages_value + dash + 1u,
                                               item_end - dash - 1u,
                                               &last_page) && last_page >= first_page;
                    }
                }
                if ((!keyword && !numeric) || item_end == item_start) {
                    console_line(2, "encode: invalid --metadata-pages item");
                    return 1;
                }
                if (item_end == metadata_pages_length) break;
                item_start = item_end + 1u;
                if (item_start == metadata_pages_length) {
                    console_line(2, "encode: invalid --metadata-pages item");
                    return 1;
                }
            }
            continue;
        }
        const char output_prefix[] = "--output-dir=";
        const char* output_value = 0;
        usize output_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--output-dir")) {
            if (have_output_dir) {
                console_line(2, "encode: output directory specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --output-dir requires a non-empty path");
                return 1;
            }
            output_value = args[i + 1];
            output_length = ascii_length(output_value);
            i += 1;
        } else if (ascii_starts_with(arg, output_prefix)) {
            if (have_output_dir) {
                console_line(2, "encode: output directory specified multiple times");
                return 1;
            }
            output_value = arg + (sizeof(output_prefix) - 1u);
            output_length = ascii_length(output_value);
        }
        if (output_value) {
            if (output_length == 0u) {
                console_line(2, "encode: --output-dir requires a non-empty path");
                return 1;
            }
            if (!output_dir_buffer.ensure(output_length + 1u)) {
                console_line(2, "encode: failed to allocate output directory buffer");
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
        const char prefix_prefix[] = "--prefix=";
        const char* prefix_value = 0;
        usize prefix_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--prefix")) {
            if (have_prefix) {
                console_line(2, "encode: --prefix specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --prefix requires a non-empty value");
                return 1;
            }
            prefix_value = args[i + 1];
            prefix_length = ascii_length(prefix_value);
            i += 1;
        } else if (ascii_starts_with(arg, prefix_prefix)) {
            if (have_prefix) {
                console_line(2, "encode: --prefix specified multiple times");
                return 1;
            }
            prefix_value = arg + (sizeof(prefix_prefix) - 1u);
            prefix_length = ascii_length(prefix_value);
        }
        if (prefix_value) {
            if (prefix_length == 0u) {
                console_line(2, "encode: --prefix requires a non-empty value");
                return 1;
            }
            if (!prefix_buffer.ensure(prefix_length + 1u)) {
                console_line(2, "encode: failed to allocate prefix buffer");
                return 1;
            }
            for (usize j = 0u; j < prefix_length; ++j) {
                char c = prefix_value[j];
                if (c == '/' || c == '\\') {
                    console_line(2, "encode: --prefix must not contain '/' or '\\\\'");
                    return 1;
                }
                prefix_buffer.data[j] = (u8)c;
            }
            prefix_buffer.data[prefix_length] = 0u;
            prefix_buffer.size = prefix_length;
            have_prefix = true;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--debug")) {
            debug_requested = true;
            continue;
        }
        const char overlay_ignore_prefix[] = "--overlay-ignore-colors=";
        const char* overlay_ignore_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--overlay-ignore-colors")) {
            if (overlay_ignore_colors_specified || (i + 1) >= arg_count ||
                !args[i + 1] || args[i + 1][0] == '\0') {
                console_line(2, "encode: --overlay-ignore-colors requires a color list");
                return 1;
            }
            overlay_ignore_value = args[i + 1];
            i += 1;
        } else if (ascii_starts_with(arg, overlay_ignore_prefix)) {
            if (overlay_ignore_colors_specified) {
                console_line(2, "encode: --overlay-ignore-colors specified multiple times");
                return 1;
            }
            overlay_ignore_value = arg + (sizeof(overlay_ignore_prefix) - 1u);
            if (*overlay_ignore_value == '\0') {
                console_line(2, "encode: --overlay-ignore-colors requires a color list");
                return 1;
            }
        }
        if (overlay_ignore_value) {
            overlay_ignore_colors_text = overlay_ignore_value;
            overlay_ignore_colors_length = ascii_length(overlay_ignore_value);
            overlay_ignore_colors_specified = true;
            continue;
        }
        const char overlay_ppm_prefix[] = "--overlay-ppm=";
        const char* overlay_ppm_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--overlay-ppm")) {
            if (pending_overlay >= 0) {
                console_line(2, "encode: each --overlay-ppm requires a following --overlay-pages");
                return 1;
            }
            if ((i + 1) >= arg_count || !args[i + 1] || args[i + 1][0] == '\0') {
                console_line(2, "encode: --overlay-ppm requires a PPM path");
                return 1;
            }
            overlay_ppm_value = args[i + 1];
            i += 1;
        } else if (ascii_starts_with(arg, overlay_ppm_prefix)) {
            overlay_ppm_value = arg + (sizeof(overlay_ppm_prefix) - 1u);
            if (*overlay_ppm_value == '\0') {
                console_line(2, "encode: --overlay-ppm requires a PPM path");
                return 1;
            }
        }
        if (overlay_ppm_value) {
            if (pending_overlay >= 0) {
                console_line(2, "encode: each --overlay-ppm requires a following --overlay-pages");
                return 1;
            }
            if (overlay_count >= MAX_OVERLAY_IMAGES) {
                console_line(2, "encode: too many overlay images (maximum 64)");
                return 1;
            }
            overlays[overlay_count].path = overlay_ppm_value;
            pending_overlay = (int)overlay_count;
            ++overlay_count;
            continue;
        }
        const char overlay_pages_prefix[] = "--overlay-pages=";
        const char* overlay_pages_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--overlay-pages")) {
            if ((i + 1) >= arg_count || !args[i + 1] || !args[i + 1][0]) {
                console_line(2, "encode: --overlay-pages requires all, page numbers, or ranges");
                return 1;
            }
            overlay_pages_value = args[++i];
        } else if (ascii_starts_with(arg, overlay_pages_prefix)) {
            overlay_pages_value = arg + sizeof(overlay_pages_prefix) - 1u;
        }
        if (overlay_pages_value) {
            if (pending_overlay < 0) {
                console_line(2, "encode: --overlay-pages must follow an --overlay-ppm image");
                return 1;
            }
            EncodeOverlay& overlay = overlays[(usize)pending_overlay];
            overlay.pages_length = ascii_length(overlay_pages_value);
            if (!overlay.pages_length || overlay.pages_specified) {
                console_line(2, "encode: --overlay-pages requires a non-empty page list");
                return 1;
            }
            overlay.pages = overlay_pages_value;
            if (overlay.pages_length == 3u && memcmp(overlay.pages, "all", 3u) == 0) {
                overlay.pages_specified = true;
                pending_overlay = -1;
                continue;
            }
            usize item_start = 0u;
            while (item_start < overlay.pages_length) {
                usize item_end = item_start;
                while (item_end < overlay.pages_length && overlay.pages[item_end] != ',') ++item_end;
                usize dash = item_start;
                while (dash < item_end && overlay.pages[dash] != '-') ++dash;
                bool keyword = (item_end - item_start == 6u &&
                                memcmp(overlay.pages + item_start, "middle", 6u) == 0) ||
                               (item_end - item_start == 4u &&
                                memcmp(overlay.pages + item_start, "last", 4u) == 0);
                u64 first_page = 0u;
                u64 last_page = 0u;
                bool numeric = dash > item_start &&
                    ascii_to_u64(overlay.pages + item_start, dash - item_start, &first_page) &&
                    first_page > 0u;
                if (numeric) {
                    last_page = first_page;
                    if (dash < item_end) {
                        numeric = dash + 1u < item_end &&
                                  ascii_to_u64(overlay.pages + dash + 1u,
                                               item_end - dash - 1u,
                                               &last_page) &&
                                  last_page >= first_page;
                    }
                }
                if ((!keyword && !numeric) || item_end == item_start) {
                    console_line(2, "encode: invalid --overlay-pages item");
                    return 1;
                }
                if (item_end == overlay.pages_length) break;
                item_start = item_end + 1u;
                if (item_start == overlay.pages_length) {
                    console_line(2, "encode: invalid --overlay-pages item");
                    return 1;
                }
            }
            overlay.pages_specified = true;
            pending_overlay = -1;
            continue;
        }
        const char overlay_fraction_prefix[] = "--overlay-fraction=";
        const char* overlay_fraction_value = 0;
        usize overlay_fraction_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--overlay-fraction")) {
            if (overlay_fraction_specified) {
                console_line(2, "encode: overlay fraction specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --overlay-fraction requires a fraction from 0.0 to 1.0");
                return 1;
            }
            overlay_fraction_value = args[i + 1];
            overlay_fraction_length = ascii_length(overlay_fraction_value);
            i += 1;
        } else if (ascii_starts_with(arg, overlay_fraction_prefix)) {
            if (overlay_fraction_specified) {
                console_line(2, "encode: overlay fraction specified multiple times");
                return 1;
            }
            overlay_fraction_value = arg + (sizeof(overlay_fraction_prefix) - 1u);
            overlay_fraction_length = ascii_length(overlay_fraction_value);
        }
        if (overlay_fraction_value) {
            if (!ascii_to_fraction(overlay_fraction_value,
                                   overlay_fraction_length,
                                   overlay_fraction_numerator,
                                   overlay_fraction_denominator) ||
                overlay_fraction_numerator > overlay_fraction_denominator ||
                overlay_fraction_numerator * 255u > overlay_fraction_denominator * 254u) {
                console_line(2, "encode: --overlay-fraction must be between 0.0 and 254/255");
                return 1;
            }
            overlay_fraction = (double)overlay_fraction_numerator /
                               (double)overlay_fraction_denominator;
            overlay_fraction_specified = true;
            continue;
        }
        const char ecc_prefix[] = "--ecc=";
        const char* ecc_value = 0;
        usize ecc_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--ecc")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --ecc requires a ratio or 'fill'");
                return 1;
            }
            ecc_value = args[i + 1];
            ecc_length = ascii_length(ecc_value);
            i += 1;
        } else if (ascii_starts_with(arg, ecc_prefix)) {
            ecc_value = arg + (sizeof(ecc_prefix) - 1u);
            ecc_length = ascii_length(ecc_value);
        }
        if (ecc_value) {
            if (ecc_length == 0u) {
                console_line(2, "encode: --ecc requires a ratio or 'fill'");
                return 1;
            }
            if (ecc_option_specified) {
                console_line(2, "encode: --ecc may be specified only once");
                return 1;
            }
            ecc_option_specified = true;
            if (ascii_equals_token(ecc_value, ecc_length, "fill")) {
                ecc_mode = ECC_FILL;
                continue;
            }
            u64 ratio_numerator = 0u;
            u64 ratio_denominator = 1u;
            if (!ascii_to_fraction(ecc_value, ecc_length,
                                   ratio_numerator, ratio_denominator) ||
                (long double)ratio_numerator / (long double)ratio_denominator > 254.0L) {
                console_line(2, "encode: --ecc ratio must be between 0 and 254");
                return 1;
            }
            ecc_redundancy = (double)ratio_numerator / (double)ratio_denominator;
            ecc_mode = ECC_EXPLICIT_RATIO;
            continue;
        }
        const char hidden_prefix[] = "--hidden=";
        const char* hidden_value = 0;
        usize hidden_value_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--hidden")) {
            if (hidden_option_specified || (i + 1) >= arg_count) {
                console_line(2, "encode: --hidden requires a ratio or 'fill' and may be specified only once");
                return 1;
            }
            hidden_value = args[++i];
            hidden_value_length = ascii_length(hidden_value);
        } else if (ascii_starts_with(arg, hidden_prefix)) {
            if (hidden_option_specified) {
                console_line(2, "encode: --hidden may be specified only once");
                return 1;
            }
            hidden_value = arg + sizeof(hidden_prefix) - 1u;
            hidden_value_length = ascii_length(hidden_value);
        }
        if (hidden_value) {
            if (!hidden_value_length) {
                console_line(2, "encode: --hidden requires a ratio or 'fill'");
                return 1;
            }
            hidden_option_specified = true;
            if (ascii_equals_token(hidden_value, hidden_value_length, "fill")) {
                hidden_fill = true;
            } else {
                u64 numerator = 0u;
                u64 denominator = 1u;
                if (!ascii_to_fraction(hidden_value, hidden_value_length, numerator, denominator) ||
                    (long double)numerator / (long double)denominator > 64.0L) {
                    console_line(2, "encode: --hidden ratio must be between 0 and 64, or 'fill'");
                    return 1;
                }
                hidden_ratio = (double)numerator / (double)denominator;
            }
            continue;
        }
        const char fill_pages_prefix[] = "--fill-pages=";
        const char* fill_pages_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--fill-pages")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --fill-pages requires a positive page count and may be specified once");
                return 1;
            }
            fill_pages_value = args[++i];
        } else if (ascii_starts_with(arg, fill_pages_prefix)) {
            fill_pages_value = arg + sizeof(fill_pages_prefix) - 1u;
        }
        if (fill_pages_value) {
            if (fill_pages_specified) {
                console_line(2, "encode: --fill-pages may be specified only once");
                return 1;
            }
            usize length = ascii_length(fill_pages_value);
            if (!ascii_to_u64(fill_pages_value, length, &fill_pages) || !fill_pages || fill_pages > 1000000u) {
                console_line(2, "encode: --fill-pages must be between 1 and 1000000");
                return 1;
            }
            fill_pages_specified = true;
            continue;
        }
        const char input_prefix[] = "--input=";
        const char* input_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--input")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --input requires a file path");
                return 1;
            }
            input_value = args[i + 1];
            if (input_value[0] == '\0') {
                console_line(2, "encode: --input requires a file path");
                return 1;
            }
            i += 1;
        } else if (ascii_starts_with(arg, input_prefix)) {
            input_value = arg + (sizeof(input_prefix) - 1u);
        }
        if (input_value) {
            if (input_value[0] == '\0') {
                console_line(2, "encode: --input requires a file path");
                return 1;
            }
            if (input_count >= MAX_INPUT_ITEMS) {
                console_line(2, "encode: too many input paths specified");
                return 1;
            }
            input_paths[input_count++] = input_value;
            continue;
        }
        const char hidden_input_prefix[] = "--hidden-input=";
        const char* hidden_input_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--hidden-input")) {
            if ((i + 1) >= arg_count || !args[i + 1] || args[i + 1][0] == '\0') {
                console_line(2, "encode: --hidden-input requires a file or directory path");
                return 1;
            }
            hidden_input_value = args[++i];
        } else if (ascii_starts_with(arg, hidden_input_prefix)) {
            hidden_input_value = arg + sizeof(hidden_input_prefix) - 1u;
        }
        if (hidden_input_value) {
            if (!hidden_input_value[0] || hidden_input_count >= MAX_INPUT_ITEMS) {
                console_line(2, "encode: invalid or too many --hidden-input paths");
                return 1;
            }
            hidden_input_paths[hidden_input_count++] = hidden_input_value;
            continue;
        }
        const char footer_prefix[] = "--footer=";
        const char* footer_value = 0;
        usize footer_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--footer")) {
            if (have_footer) {
                console_line(2, "encode: --footer specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --footer requires a value");
                return 1;
            }
            footer_value = args[i + 1];
            footer_length = ascii_length(footer_value);
            i += 1;
        } else if (ascii_starts_with(arg, footer_prefix)) {
            if (have_footer) {
                console_line(2, "encode: --footer specified multiple times");
                return 1;
            }
            footer_value = arg + (sizeof(footer_prefix) - 1u);
            footer_length = ascii_length(footer_value);
        }
        if (footer_value) {
            if (!footer_buffer.ensure(footer_length + 1u)) {
                console_line(2, "encode: failed to allocate footer buffer");
                return 1;
            }
            for (usize j = 0u; j < footer_length; ++j) {
                char c = footer_value[j];
                if (!footer_char_is_allowed(c)) {
                    console_line(2, "encode: footer contains an unsupported character");
                    return 1;
                }
                footer_buffer.data[j] = (u8)c;
            }
            footer_buffer.data[footer_length] = 0u;
            footer_buffer.size = footer_length;
            footer_config.text = (const char*)footer_buffer.data;
            footer_config.text_length = footer_length;
            have_footer = true;
            continue;
        }
        const char footer_pages_prefix[] = "--footer-pages=";
        const char* footer_pages_value = 0;
        if (ascii_equals_token(arg, ascii_length(arg), "--footer-pages")) {
            if ((i + 1) >= arg_count || !args[i + 1] || !args[i + 1][0]) {
                console_line(2, "encode: --footer-pages requires a page list");
                return 1;
            }
            footer_pages_value = args[++i];
        } else if (ascii_starts_with(arg, footer_pages_prefix)) {
            footer_pages_value = arg + sizeof(footer_pages_prefix) - 1u;
        }
        if (footer_pages_value) {
            if (footer_config.pages) {
                console_line(2, "encode: --footer-pages specified multiple times");
                return 1;
            }
            footer_config.pages = footer_pages_value;
            footer_config.pages_length = ascii_length(footer_pages_value);
            if (!footer_config.pages_length) {
                console_line(2, "encode: --footer-pages must contain page numbers, ranges, middle, or last");
                return 1;
            }
            usize item_start = 0u;
            while (item_start < footer_config.pages_length) {
                usize item_end = item_start;
                while (item_end < footer_config.pages_length && footer_pages_value[item_end] != ',') {
                    ++item_end;
                }
                usize dash = item_start;
                while (dash < item_end && footer_pages_value[dash] != '-') ++dash;
                bool keyword = (item_end - item_start == 6u &&
                                memcmp(footer_pages_value + item_start, "middle", 6u) == 0) ||
                               (item_end - item_start == 4u &&
                                memcmp(footer_pages_value + item_start, "last", 4u) == 0);
                u64 first_page = 0u;
                u64 last_page = 0u;
                bool numeric = dash > item_start &&
                    ascii_to_u64(footer_pages_value + item_start, dash - item_start, &first_page) &&
                    first_page > 0u;
                if (numeric) {
                    last_page = first_page;
                    if (dash < item_end) {
                        numeric = dash + 1u < item_end &&
                                  ascii_to_u64(footer_pages_value + dash + 1u,
                                               item_end - dash - 1u,
                                               &last_page) &&
                                  last_page >= first_page;
                    }
                }
                if ((!keyword && !numeric) || item_end == item_start) {
                    console_line(2, "encode: invalid --footer-pages item");
                    return 1;
                }
                if (item_end == footer_config.pages_length) break;
                item_start = item_end + 1u;
                if (item_start == footer_config.pages_length) {
                    console_line(2, "encode: invalid --footer-pages item");
                    return 1;
                }
            }
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--footer-over-barcode")) {
            if (footer_config.over_barcode) {
                console_line(2, "encode: --footer-over-barcode specified multiple times");
                return 1;
            }
            footer_config.over_barcode = true;
            continue;
        }
        const char font_prefix[] = "--font-size=";
        const char* font_value = 0;
        usize font_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--font-size")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --font-size requires a positive integer value");
                return 1;
            }
            font_value = args[i + 1];
            font_length = ascii_length(font_value);
            i += 1;
        } else if (ascii_starts_with(arg, font_prefix)) {
            font_value = arg + (sizeof(font_prefix) - 1u);
            font_length = ascii_length(font_value);
        }
        if (font_value) {
            if (font_length == 0u) {
                console_line(2, "encode: --font-size requires a positive integer value");
                return 1;
            }
            u64 value = 0u;
            if (!ascii_to_u64(font_value, font_length, &value) || value == 0u || value > 2048u) {
                console_line(2, "encode: --font-size must be between 1 and 2048");
                return 1;
            }
            footer_config.font_size = (u32)value;
            continue;
        }
        const char random_prefix[] = "--random-data=";
        const char* random_value = 0;
        usize random_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--random-data")) {
            if (have_random_data || (i + 1) >= arg_count) {
                console_line(2, "encode: --random-data requires exactly 56 hex characters");
                return 1;
            }
            random_value = args[++i];
            random_length = ascii_length(random_value);
        } else if (ascii_starts_with(arg, random_prefix)) {
            if (have_random_data) return 1;
            random_value = arg + sizeof(random_prefix) - 1u;
            random_length = ascii_length(random_value);
        }
        if (random_value) {
            if (random_length != 56u || !random_data_buffer.ensure(28u)) {
                console_line(2, "encode: --random-data requires exactly 56 hex characters");
                return 1;
            }
            for (usize j = 0u; j < 28u; ++j) {
                u8 value = 0u;
                for (u32 k = 0u; k < 2u; ++k) {
                    char c = random_value[j * 2u + k];
                    u8 digit = (c >= '0' && c <= '9') ? (u8)(c - '0') :
                               (c >= 'a' && c <= 'f') ? (u8)(c - 'a' + 10) :
                               (c >= 'A' && c <= 'F') ? (u8)(c - 'A' + 10) : 255u;
                    if (digit == 255u) { console_line(2, "encode: --random-data must be hexadecimal"); return 1; }
                    value = (u8)((value << 4u) | digit);
                }
                random_data_buffer.data[j] = value;
            }
            random_data_buffer.size = 28u;
            have_random_data = true;
            continue;
        }
        const char password_prefix[] = "--password=";
        const char* password_value = 0;
        usize password_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--password")) {
            if (have_password) {
                console_line(2, "encode: password specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --password requires a non-empty value");
                return 1;
            }
            password_value = args[i + 1];
            password_length = ascii_length(password_value);
            i += 1;
        } else if (ascii_starts_with(arg, password_prefix)) {
            if (have_password) {
                console_line(2, "encode: password specified multiple times");
                return 1;
            }
            password_value = arg + (sizeof(password_prefix) - 1u);
            password_length = ascii_length(password_value);
        }
        if (password_value) {
            if (password_length == 0u) {
                console_line(2, "encode: --password requires a non-empty value");
                return 1;
            }
            if (!password_buffer.ensure(password_length)) {
                console_line(2, "encode: failed to allocate password buffer");
                return 1;
            }
            for (usize j = 0u; j < password_length; ++j) {
                password_buffer.data[j] = (u8)password_value[j];
            }
            password_buffer.size = password_length;
            have_password = true;
            continue;
        }
        const char hidden_password_prefix[] = "--hidden-password=";
        const char* hidden_password_value = 0;
        usize hidden_password_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--hidden-password")) {
            if (have_hidden_password || (i + 1) >= arg_count) {
                console_line(2, "encode: --hidden-password requires a non-empty value and may be specified once");
                return 1;
            }
            hidden_password_value = args[++i];
            hidden_password_length = ascii_length(hidden_password_value);
        } else if (ascii_starts_with(arg, hidden_password_prefix)) {
            if (have_hidden_password) {
                console_line(2, "encode: --hidden-password may be specified only once");
                return 1;
            }
            hidden_password_value = arg + sizeof(hidden_password_prefix) - 1u;
            hidden_password_length = ascii_length(hidden_password_value);
        }
        if (hidden_password_value) {
            if (!hidden_password_length) {
                console_line(2, "encode: --hidden-password requires a non-empty value");
                return 1;
            }
            if (!hidden_password_buffer.ensure(hidden_password_length)) {
                console_line(2, "encode: failed to allocate hidden password buffer");
                return 1;
            }
            for (usize j = 0u; j < hidden_password_length; ++j) {
                hidden_password_buffer.data[j] = (u8)hidden_password_value[j];
            }
            hidden_password_buffer.size = hidden_password_length;
            have_hidden_password = true;
            continue;
        }
        console_write(2, "encode: unknown option: ");
        console_line(2, arg);
        return 1;
    }
    if ((footer_config.pages || footer_config.over_barcode) && !have_footer) {
        console_line(2, "encode: --footer-pages and --footer-over-barcode require --footer");
        return 1;
    }
    if (pending_overlay >= 0) {
        console_line(2, "encode: each --overlay-ppm requires a following --overlay-pages");
        return 1;
    }
    if (overlay_fraction_specified != (overlay_count > 0u)) {
        console_line(2, "encode: --overlay-fraction and overlay images must be used together");
        return 1;
    }
    if (overlay_ignore_colors_specified && overlay_count == 0u) {
        console_line(2, "encode: --overlay-ignore-colors requires an overlay image");
        return 1;
    }
    if (have_password && !have_random_data) {
        console_line(2, "encode: --password requires --random-data");
        return 1;
    }
    if (have_random_data && !have_password && !fill_pages_specified) {
        console_line(2, "encode: --random-data requires --password or --fill-pages");
        return 1;
    }
    if (fill_pages_specified && !have_random_data) {
        console_line(2, "encode: --fill-pages requires --random-data for cryptographic filler");
        return 1;
    }
    bool hidden_mode_enabled = hidden_fill || hidden_ratio > 0.0;
    if ((hidden_input_count > 0u) != have_hidden_password) {
        console_line(2, "encode: --hidden-input and --hidden-password must be supplied together");
        return 1;
    }
    if ((hidden_input_count > 0u || have_hidden_password) && !hidden_mode_enabled) {
        console_line(2, "encode: hidden inputs and password require --hidden with a positive ratio or 'fill'");
        return 1;
    }
    if (hidden_mode_enabled && (!have_password || !have_random_data)) {
        console_line(2, "encode: --hidden requires --password and --random-data to protect the visible volume length");
        return 1;
    }
    if (have_hidden_password && password_buffer.size == hidden_password_buffer.size &&
        MetadataTile::constant_time_equal(password_buffer.data, hidden_password_buffer.data, password_buffer.size)) {
        console_line(2, "encode: visible and hidden passwords must differ");
        return 1;
    }
    if (hidden_fill && ecc_mode == ECC_FILL) {
        console_line(2, "encode: --hidden fill and --ecc fill compete for the same page capacity");
        return 1;
    }
    if (fill_pages_specified && !hidden_fill && ecc_mode != ECC_FILL) {
        console_line(2, "encode: --fill-pages requires --hidden fill or --ecc fill");
        return 1;
    }
    if (hidden_fill && (!mapping.page_width_set || !mapping.page_height_set)) {
        console_line(2, "encode: --hidden fill requires --page-width and --page-height");
        return 1;
    }
    g_encode_metadata_disabled = no_metadata;
    if (no_fiducials) g_fiducial_defaults.spacing_pixels = 0u;
    if (mapping.palette_set) {
        if (!image_mapping_build_custom_palette(mapping, "encode")) {
            return 1;
        }
        if (mapping.custom_palette_valid) {
            u32 background_index = 0u;
            u32 brightest_luminance = MetadataTile::rgb_luminance_u8(
                mapping.custom_palette[0u].r,
                mapping.custom_palette[0u].g,
                mapping.custom_palette[0u].b);
            for (u32 i = 1u; i < mapping.custom_palette_count; ++i) {
                u32 luminance = MetadataTile::rgb_luminance_u8(
                    mapping.custom_palette[i].r,
                    mapping.custom_palette[i].g,
                    mapping.custom_palette[i].b);
                if (luminance > brightest_luminance) {
                    brightest_luminance = luminance;
                    background_index = i;
                }
            }
            if (background_index > 0u) {
                PaletteColor reordered[MAX_CUSTOM_PALETTE_COLORS];
                u8 reordered_ids[MAX_CUSTOM_PALETTE_COLORS];
                for (u32 i = 0u; i < mapping.custom_palette_count; ++i) {
                    u32 source = (background_index + i) % mapping.custom_palette_count;
                    reordered[i] = mapping.custom_palette[source];
                    reordered_ids[i] = mapping.custom_palette_ids[source];
                }
                for (u32 i = 0u; i < mapping.custom_palette_count; ++i) {
                    mapping.custom_palette[i] = reordered[i];
                    mapping.custom_palette_ids[i] = reordered_ids[i];
                }
            }
        }
        // Downgrade custom palettes that exactly match built-in palettes so we
        // encode using the simpler fixed color modes (avoids palette-digit
        // packing for standard BW/CMYW/RGB pages).
        if (mapping.custom_palette_valid) {
            auto palette_matches = [&](const PaletteColor* builtin, u32 count) -> bool {
                if (!builtin || count == 0u || mapping.custom_palette_count != count) {
                    return false;
                }
                for (u32 i = 0u; i < count; ++i) {
                    const PaletteColor& a = mapping.custom_palette[i];
                    const PaletteColor& b = builtin[i];
                    if (a.r != b.r || a.g != b.g || a.b != b.b) {
                        return false;
                    }
                }
                return true;
            };
            bool background_first_gray = mapping.custom_palette_count == 2u &&
                mapping.custom_palette[0u].r == 255u &&
                mapping.custom_palette[0u].g == 255u &&
                mapping.custom_palette[0u].b == 255u &&
                mapping.custom_palette[1u].r == 0u &&
                mapping.custom_palette[1u].g == 0u &&
                mapping.custom_palette[1u].b == 0u;
            if (palette_matches(PALETTE_GRAY, (u32)(sizeof(PALETTE_GRAY) / sizeof(PALETTE_GRAY[0]))) ||
                background_first_gray) {
                mapping.color_channels = 1u;
                mapping.custom_palette_valid = false;
            } else if (palette_matches(PALETTE_CMYW, (u32)(sizeof(PALETTE_CMYW) / sizeof(PALETTE_CMYW[0])))) {
                mapping.color_channels = 2u;
                mapping.custom_palette_valid = false;
            } else if (palette_matches(PALETTE_RGB_CMY_WB, (u32)(sizeof(PALETTE_RGB_CMY_WB) / sizeof(PALETTE_RGB_CMY_WB[0])))) {
                mapping.color_channels = 3u;
                mapping.custom_palette_valid = false;
            }
        }
    }
    ArchiveBuildContext archive;
    if (!archive_init(archive)) {
        console_line(2, "encode: failed to initialize archive buffer");
        return 1;
    }
    if (input_count == 0u) {
        makocode::ByteBuffer stdin_data;
        if (!read_entire_stdin(stdin_data)) {
            console_line(2, "encode: failed to read stdin");
            return 1;
        }
        if (!MetadataTile::write_compact_u64(archive.entries, (u64)stdin_data.size) ||
            !archive_append_bytes(archive.entries, stdin_data.data, stdin_data.size)) {
            console_line(2, "encode: failed to buffer stdin payload");
            return 1;
        }
        archive.raw_stream = true;
    }
    for (usize path_index = 0u; path_index < input_count; ++path_index) {
        const char* input_path = input_paths[path_index];
        MakoPlatformFileInfo path_info;
        if (makocode_platform_stat(input_path, &path_info) != 0) {
            console_write(2, "encode: unable to stat ");
            console_line(2, input_path);
            return 1;
        }
        makocode::ByteBuffer normalized_path;
        if (!normalize_store_path(input_path, normalized_path)) {
            console_write(2, "encode: failed to normalize input path ");
            console_line(2, input_path);
            return 1;
        }
        const char* stored_rel = (const char*)normalized_path.data;
        if (path_info.is_directory) {
            if (!archive_collect_directory(input_path, stored_rel, archive)) {
                return 1;
            }
        } else if (path_info.is_regular) {
            makocode::ByteBuffer file_data;
            if (!read_entire_file(input_path, file_data)) {
                console_write(2, "encode: failed to read ");
                console_line(2, input_path);
                return 1;
            }
            if (!archive_add_file(archive, stored_rel, file_data.data, file_data.size)) {
                file_data.release();
                return 1;
            }
            file_data.release();
        } else {
            console_write(2, "encode: unsupported input type for ");
            console_line(2, input_path);
            return 1;
        }
    }
    if (archive.entry_count == 0u && !archive.raw_stream) {
        console_line(2, "encode: no files or directories to encode");
        return 1;
    }
    if (!archive_finalize(archive)) {
        console_line(2, "encode: failed to finalize archive");
        return 1;
    }
    ArchiveBuildContext hidden_archive;
    if (hidden_input_count > 0u) {
        if (!archive_init(hidden_archive)) {
            console_line(2, "encode: failed to initialize hidden archive buffer");
            return 1;
        }
        for (usize path_index = 0u; path_index < hidden_input_count; ++path_index) {
            const char* input_path = hidden_input_paths[path_index];
            MakoPlatformFileInfo path_info;
            if (makocode_platform_stat(input_path, &path_info) != 0) {
                console_write(2, "encode: unable to stat hidden input ");
                console_line(2, input_path);
                return 1;
            }
            makocode::ByteBuffer normalized_path;
            if (!normalize_store_path(input_path, normalized_path)) {
                console_write(2, "encode: failed to normalize hidden input path ");
                console_line(2, input_path);
                return 1;
            }
            const char* stored_rel = (const char*)normalized_path.data;
            if (path_info.is_directory) {
                if (!archive_collect_directory(input_path, stored_rel, hidden_archive)) return 1;
            } else if (path_info.is_regular) {
                makocode::ByteBuffer file_data;
                if (!read_entire_file(input_path, file_data)) {
                    console_write(2, "encode: failed to read hidden input ");
                    console_line(2, input_path);
                    return 1;
                }
                if (!archive_add_file(hidden_archive, stored_rel, file_data.data, file_data.size)) {
                    file_data.release();
                    return 1;
                }
                file_data.release();
            } else {
                console_write(2, "encode: unsupported hidden input type for ");
                console_line(2, input_path);
                return 1;
            }
        }
        if (!hidden_archive.entry_count || !archive_finalize(hidden_archive)) {
            console_line(2, "encode: failed to finalize hidden archive");
            return 1;
        }
    }
    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (mapping.page_width_set) width_pixels = mapping.page_width_pixels;
    if (mapping.page_height_set) height_pixels = mapping.page_height_pixels;
    makocode::ByteBuffer compressed_for_sizing;
    if (!lzma_compress(archive.buffer.data,
                       archive.buffer.size,
                       compressed_for_sizing,
                       true)) {
        console_line(2, "encode: failed to compress payload for ECC sizing");
        return 1;
    }
    usize ecc_payload_bytes = compressed_for_sizing.size;
    if (!mapping.page_width_set || !mapping.page_height_set) {
        makocode::EccSummary initial_sizing_ecc;
        initial_sizing_ecc.original_bytes = (u64)ecc_payload_bytes;
        if ((u64)ecc_payload_bytes > U64_MAX_VALUE / 8u ||
            !fit_page_dimensions(mapping,
                                 footer_config,
                                 (u64)ecc_payload_bytes * 8u,
                                 !mapping.page_width_set,
                                 !mapping.page_height_set,
                                 width_pixels,
                                 height_pixels,
                                 &initial_sizing_ecc)) {
            console_line(2, "encode: failed to fit automatic page dimensions");
            return 1;
        }
    }
    makocode::ByteBuffer stealth_container;
    if (hidden_mode_enabled) {
        makocode::ByteBuffer visible_body;
        makocode::ByteBuffer hidden_compressed;
        makocode::ByteBuffer hidden_body;
        makocode::ByteBuffer visible_descriptor;
        makocode::ByteBuffer hidden_descriptor;
        u8 visible_key[32];
        u8 hidden_key[32];
        const u8* salt = random_data_buffer.data;
        const u8* nonce_seed = random_data_buffer.data + MetadataTile::ENCRYPTION_SALT_BYTES;
        u8 prefix[MetadataTile::STEALTH_CONTAINER_PREFIX_BYTES];
        for (usize i = 0u; i < MetadataTile::ENCRYPTION_SALT_BYTES; ++i) prefix[i] = salt[i];
        for (usize i = 0u; i < MetadataTile::ENCRYPTION_NONCE_BYTES; ++i) {
            prefix[MetadataTile::ENCRYPTION_SALT_BYTES + i] = nonce_seed[i];
        }
        bool container_ok = MetadataTile::stealth_derive_base_key((const char*)password_buffer.data,
                                                    password_buffer.size, salt, visible_key) &&
                            MetadataTile::stealth_encrypt_blob(compressed_for_sizing.data,
                                                 compressed_for_sizing.size,
                                                 visible_key, nonce_seed, prefix, sizeof(prefix),
                                                 "visible body", visible_body);
        if (container_ok && hidden_input_count) {
            container_ok = lzma_compress(hidden_archive.buffer.data,
                                         hidden_archive.buffer.size,
                                         hidden_compressed,
                                         true) &&
                           MetadataTile::stealth_derive_base_key((const char*)hidden_password_buffer.data,
                                                   hidden_password_buffer.size, salt, hidden_key) &&
                           MetadataTile::stealth_encrypt_blob(hidden_compressed.data,
                                                hidden_compressed.size,
                                                hidden_key, nonce_seed, prefix, sizeof(prefix),
                                                "hidden body", hidden_body);
        }
        u64 hidden_capacity = 0u;
        if (container_ok && hidden_fill) {
            PageFooterConfig capacity_footer = footer_config;
            FooterLayout capacity_layout;
            u64 capacity_bits_per_page = 0u;
            u64 capacity_pages = fill_pages_specified ? fill_pages : 1u;
            capacity_footer.max_text_length = footer_compute_max_text_length(capacity_footer, capacity_pages);
            if (!compute_footer_layout(width_pixels, height_pixels, capacity_footer, capacity_layout) ||
                !compute_bits_per_page(mapping, width_pixels, height_pixels,
                                       capacity_layout.data_height_pixels, capacity_bits_per_page)) {
                container_ok = false;
            } else {
                if (capacity_bits_per_page > U64_MAX_VALUE / capacity_pages) {
                    container_ok = false;
                }
                u64 total_capacity_bits = container_ok
                                              ? capacity_bits_per_page * capacity_pages
                                              : 0u;
                u64 used = (u64)MetadataTile::STEALTH_CONTAINER_HEADER_BYTES + (u64)visible_body.size;
                double capacity_redundancy = ecc_mode == ECC_EXPLICIT_RATIO
                                                 ? ecc_redundancy
                                                 : (ecc_mode == ECC_AUTO_MINIMUM ? 0.5 : 0.0);
                u64 low = 0u;
                u64 high = total_capacity_bits / 8u;
                if (high > (u64)USIZE_MAX_VALUE) high = (u64)USIZE_MAX_VALUE;
                while (container_ok && low < high) {
                    u64 middle = low + (high - low + 1u) / 2u;
                    u64 required_bits = 0u;
                    if (capacity_redundancy > 0.0) {
                        u16 block_data = 0u;
                        u16 parity_symbols = 0u;
                        u64 block_count = 0u;
                        if (!makocode::compute_ecc_layout((usize)middle,
                                                          capacity_redundancy,
                                                          block_data,
                                                          parity_symbols,
                                                          block_count) ||
                            !block_data || !parity_symbols || !block_count) {
                            high = middle - 1u;
                            continue;
                        }
                        u64 block_total = (u64)block_data + parity_symbols;
                        if (block_count > U64_MAX_VALUE / block_total / 8u) {
                            high = middle - 1u;
                            continue;
                        }
                        required_bits = block_count * block_total * 8u;
                    } else {
                        required_bits = middle * 8u;
                    }
                    if (required_bits <= total_capacity_bits) low = middle;
                    else high = middle - 1u;
                }
                if (container_ok && low > used) {
                    hidden_capacity = low - used;
                } else {
                    container_ok = false;
                }
            }
        } else if (container_ok && hidden_ratio > 0.0) {
            long double requested = ceill((long double)visible_body.size *
                                          (long double)hidden_ratio);
            if (requested > (long double)U64_MAX_VALUE) container_ok = false;
            else hidden_capacity = (u64)requested;
        }
        if (container_ok && hidden_capacity < (u64)hidden_body.size) {
            console_line(2, "encode: hidden payload exceeds the --hidden capacity");
            container_ok = false;
        }
        u64 carrier_minimum_size = 0u;
        if (container_ok &&
            ((u64)visible_body.size > U64_MAX_VALUE -
                 (u64)MetadataTile::STEALTH_CONTAINER_HEADER_BYTES ||
             hidden_capacity > U64_MAX_VALUE -
                 (u64)MetadataTile::STEALTH_CONTAINER_HEADER_BYTES -
                 (u64)visible_body.size)) {
            container_ok = false;
        } else if (container_ok) {
            carrier_minimum_size = (u64)MetadataTile::STEALTH_CONTAINER_HEADER_BYTES +
                                   (u64)visible_body.size + hidden_capacity;
        }
        u64 carrier_size = carrier_minimum_size;
        if (container_ok && !hidden_fill) {
            double capacity_redundancy = ecc_mode == ECC_EXPLICIT_RATIO
                                             ? ecc_redundancy
                                             : (ecc_mode == ECC_AUTO_MINIMUM ? 0.5 : 3.0);
            u64 target_page_count = 1u;
            u64 capacity_bits_per_page = 0u;
            u64 total_capacity_bits = 0u;
            for (u64 pass = 0u; pass < 1000000u; ++pass) {
                PageFooterConfig capacity_footer = footer_config;
                capacity_footer.max_text_length = footer_compute_max_text_length(
                    capacity_footer, target_page_count);
                FooterLayout capacity_layout;
                if (!compute_footer_layout(width_pixels, height_pixels,
                                           capacity_footer, capacity_layout) ||
                    !compute_bits_per_page(mapping, width_pixels, height_pixels,
                                           capacity_layout.data_height_pixels,
                                           capacity_bits_per_page) ||
                    !capacity_bits_per_page ||
                    capacity_bits_per_page > U64_MAX_VALUE / target_page_count) {
                    container_ok = false;
                    break;
                }
                total_capacity_bits = capacity_bits_per_page * target_page_count;
                u64 required_bits = 0u;
                if (capacity_redundancy > 0.0) {
                    u16 block_data = 0u;
                    u16 parity_symbols = 0u;
                    u64 block_count = 0u;
                    u64 block_total = 0u;
                    if (!makocode::compute_ecc_layout((usize)carrier_minimum_size,
                                                      capacity_redundancy,
                                                      block_data,
                                                      parity_symbols,
                                                      block_count) ||
                        !block_data || !parity_symbols || !block_count) {
                        container_ok = false;
                        break;
                    }
                    block_total = (u64)block_data + parity_symbols;
                    if (block_count > U64_MAX_VALUE / block_total / 8u) {
                        container_ok = false;
                        break;
                    }
                    required_bits = block_count * block_total * 8u;
                } else {
                    if (carrier_minimum_size > U64_MAX_VALUE / 8u) {
                        container_ok = false;
                        break;
                    }
                    required_bits = carrier_minimum_size * 8u;
                }
                if (required_bits <= total_capacity_bits) break;
                ++target_page_count;
                if (target_page_count == 1000000u) container_ok = false;
            }
            if (container_ok) {
                u64 low = 0u;
                u64 high = total_capacity_bits / 8u;
                if (high > (u64)USIZE_MAX_VALUE) high = (u64)USIZE_MAX_VALUE;
                while (low < high) {
                    u64 middle = low + (high - low + 1u) / 2u;
                    u64 required_bits = 0u;
                    if (capacity_redundancy > 0.0) {
                        u16 block_data = 0u;
                        u16 parity_symbols = 0u;
                        u64 block_count = 0u;
                        if (!makocode::compute_ecc_layout((usize)middle,
                                                          capacity_redundancy,
                                                          block_data,
                                                          parity_symbols,
                                                          block_count) ||
                            !block_data || !parity_symbols || !block_count) {
                            high = middle - 1u;
                            continue;
                        }
                        u64 block_total = (u64)block_data + parity_symbols;
                        if (block_count > U64_MAX_VALUE / block_total / 8u) {
                            high = middle - 1u;
                            continue;
                        }
                        required_bits = block_count * block_total * 8u;
                    } else {
                        required_bits = middle * 8u;
                    }
                    if (required_bits <= total_capacity_bits) low = middle;
                    else high = middle - 1u;
                }
                if (low < carrier_minimum_size) container_ok = false;
                else carrier_size = low;
            }
        }
        if (container_ok) {
            if (carrier_size > (u64)USIZE_MAX_VALUE ||
                !MetadataTile::stealth_fill_random(stealth_container, (usize)carrier_size,
                                     random_data_buffer.data, random_data_buffer.size,
                                     "makocode hidden-region fill v1")) {
                container_ok = false;
            }
        }
        if (container_ok) {
            memcpy(stealth_container.data, prefix, sizeof(prefix));
            u8 visible_descriptor_plain[8];
            write_le_u64(visible_descriptor_plain, (u64)visible_body.size);
            container_ok = MetadataTile::stealth_encrypt_blob(visible_descriptor_plain,
                                                sizeof(visible_descriptor_plain),
                                                visible_key, nonce_seed, prefix, sizeof(prefix),
                                                "visible descriptor", visible_descriptor);
            if (container_ok && hidden_input_count) {
                u8 hidden_descriptor_plain[24];
                write_le_u64(hidden_descriptor_plain,
                             (u64)MetadataTile::STEALTH_CONTAINER_HEADER_BYTES + (u64)visible_body.size);
                write_le_u64(hidden_descriptor_plain + 8u, (u64)hidden_body.size);
                write_le_u64(hidden_descriptor_plain + 16u, hidden_capacity);
                container_ok = MetadataTile::stealth_encrypt_blob(hidden_descriptor_plain,
                                                    sizeof(hidden_descriptor_plain),
                                                    hidden_key, nonce_seed, prefix, sizeof(prefix),
                                                    "hidden descriptor", hidden_descriptor);
            }
            if (container_ok && visible_descriptor.size == MetadataTile::STEALTH_VISIBLE_DESCRIPTOR_BYTES &&
                (hidden_descriptor.size == 0u || hidden_descriptor.size == MetadataTile::STEALTH_HIDDEN_DESCRIPTOR_BYTES)) {
                memcpy(stealth_container.data + MetadataTile::STEALTH_CONTAINER_PREFIX_BYTES,
                       visible_descriptor.data, visible_descriptor.size);
                if (hidden_descriptor.size) {
                    memcpy(stealth_container.data + MetadataTile::STEALTH_CONTAINER_PREFIX_BYTES +
                               MetadataTile::STEALTH_VISIBLE_DESCRIPTOR_BYTES,
                           hidden_descriptor.data, hidden_descriptor.size);
                }
                memcpy(stealth_container.data + MetadataTile::STEALTH_CONTAINER_HEADER_BYTES,
                       visible_body.data, visible_body.size);
                if (hidden_body.size) {
                    memcpy(stealth_container.data + MetadataTile::STEALTH_CONTAINER_HEADER_BYTES +
                               visible_body.size,
                           hidden_body.data, hidden_body.size);
                }
            } else {
                container_ok = false;
            }
        }
        for (usize i = 0u; i < sizeof(visible_key); ++i) visible_key[i] = 0u;
        for (usize i = 0u; i < sizeof(hidden_key); ++i) hidden_key[i] = 0u;
        visible_body.release();
        hidden_compressed.release();
        hidden_body.release();
        visible_descriptor.release();
        hidden_descriptor.release();
        compressed_for_sizing.release();
        if (!container_ok) {
            console_line(2, "encode: failed to construct hidden-capable data section");
            return 1;
        }
        ecc_payload_bytes = stealth_container.size;
    } else {
        compressed_for_sizing.release();
    }
    if (have_password && !hidden_mode_enabled) {
        usize encryption_overhead = makocode::ENCRYPTION_HEADER_BYTES + makocode::ENCRYPTION_TAG_BYTES;
        if (ecc_payload_bytes > USIZE_MAX_VALUE - encryption_overhead) {
            console_line(2, "encode: encrypted payload size overflow");
            return 1;
        }
        ecc_payload_bytes += encryption_overhead;
    }
    double structural_erasure_fraction = 0.0;
    double unknown_error_redundancy = ecc_redundancy;
    double metadata_ecc_redundancy = 0.0;
    // Keep auto-minimum scans resilient after removing the fixed LZMA size header.
    u32 metadata_ecc_minimum_parity_bytes =
        ecc_mode == ECC_AUTO_MINIMUM ? 8u : 2u;
    u16 unknown_parity_budget = 0u;
    u16 overlay_erasure_budget = 0u;
    if (width_pixels < MetadataTile::TILE_MARKER_WIDTH ||
        height_pixels < MetadataTile::TILE_HEADER_ROWS + 1u) {
        console_line(2, "encode: page is too small for the metadata bootstrap");
        return 1;
    }
    PageFooterConfig estimate_footer_config = footer_config;
    estimate_footer_config.max_text_length = footer_compute_max_text_length(estimate_footer_config, 1u);
    FooterLayout estimate_footer_layout;
    makocode::ByteBuffer canonical_mask;
    u64 canonical_reserved = 0u;
    if (!compute_footer_layout(width_pixels,
                               height_pixels,
                               estimate_footer_config,
                               estimate_footer_layout) ||
        estimate_footer_layout.data_height_pixels < MetadataTile::TILE_HEADER_ROWS + 1u ||
        !compute_fiducial_marker_mask(width_pixels,
                                      height_pixels,
                                      estimate_footer_layout.data_height_pixels,
                                      canonical_reserved,
                                      &canonical_mask,
                                      false,
                                      0u,
                                      0u,
                                      0u)) {
        console_line(2, "encode: canonical centered metadata layout does not fit page geometry");
        return 1;
    }
    u64 overwritten_pixels = 0u;
    for (usize i = 0u; i < canonical_mask.size; ++i) {
        if (canonical_mask.data[i] == OVERWRITE_MASK_PIXEL) {
            ++overwritten_pixels;
        }
    }
    u64 data_pixels = (u64)width_pixels *
                      (u64)estimate_footer_layout.data_height_pixels;
    if (data_pixels == 0u) {
        console_line(2, "encode: canonical layout has no payload capacity");
        return 1;
    }
    structural_erasure_fraction =
        mapping_has_custom_palette(mapping) &&
                !is_power_of_two_u32(mapping.custom_palette_count)
            ? 0.0
            : (double)overwritten_pixels / (double)data_pixels;
    if (ecc_mode != ECC_EXPLICIT_RATIO) {
        EccFillCandidate minimum_candidate;
        double low_ratio = overlay_fraction >= 0.9 ? 254.0 : 0.01;
        double high_ratio = low_ratio;
        bool found_minimum = evaluate_ecc_fill_candidate(ecc_payload_bytes,
                                          high_ratio,
                                          mapping,
                                          footer_config,
                                          width_pixels,
                                          height_pixels,
                                          !mapping.page_width_set,
                                          !mapping.page_height_set,
                                          structural_erasure_fraction,
                                          overlay_fraction,
                                          minimum_candidate,
                                          metadata_ecc_minimum_parity_bytes);
        if (!found_minimum) {
            for (u32 attempt = 0u; attempt < 16u && high_ratio < 254.0; ++attempt) {
                low_ratio = high_ratio;
                high_ratio *= 2.0;
                if (high_ratio > 254.0) high_ratio = 254.0;
                found_minimum = evaluate_ecc_fill_candidate(ecc_payload_bytes,
                                          high_ratio,
                                          mapping,
                                          footer_config,
                                          width_pixels,
                                          height_pixels,
                                          !mapping.page_width_set,
                                          !mapping.page_height_set,
                                          structural_erasure_fraction,
                                          overlay_fraction,
                                          minimum_candidate,
                                          metadata_ecc_minimum_parity_bytes);
                if (found_minimum) break;
            }
            if (found_minimum) {
                for (u32 iteration = 0u; iteration < 64u; ++iteration) {
                    double middle = (low_ratio + high_ratio) * 0.5;
                    if (middle == low_ratio || middle == high_ratio) break;
                    EccFillCandidate candidate;
                    if (evaluate_ecc_fill_candidate(ecc_payload_bytes,
                                                    middle,
                                                    mapping,
                                                    footer_config,
                                                    width_pixels,
                                                    height_pixels,
                                                    !mapping.page_width_set,
                                                    !mapping.page_height_set,
                                                    structural_erasure_fraction,
                                                    overlay_fraction,
                                                    candidate,
                                                    metadata_ecc_minimum_parity_bytes)) {
                        high_ratio = middle;
                        minimum_candidate = candidate;
                    } else {
                        low_ratio = middle;
                    }
                }
            }
        }
        if (!found_minimum) {
            console_line(2, "encode: unable to determine the minimum ECC for this payload and page layout");
            return 1;
        }
        ecc_redundancy = minimum_candidate.ratio;
        unknown_error_redundancy = ecc_redundancy;
    }
    if (ecc_mode == ECC_FILL) {
        double best_ratio = -1.0;
        double best_requested_ratio = -1.0;
        u16 best_block_data = 0u;
        u16 best_parity = 0u;
        u64 best_frame_bits = 0u;
        u64 best_bits_needed = 0u;
        u64 best_page_count = 0u;
        bool found_fill = find_best_ecc_fill(ecc_payload_bytes,
                                ecc_redundancy,
                                254.0,
                                mapping,
                                footer_config,
                                width_pixels,
                                height_pixels,
                                !mapping.page_width_set,
                                !mapping.page_height_set,
                                structural_erasure_fraction,
                                overlay_fraction,
                                best_ratio,
                                best_requested_ratio,
                                best_block_data,
                                best_parity,
                                best_frame_bits,
                                best_bits_needed,
                                best_page_count);
        if (!found_fill) {
            console_line(2, "encode: failed to derive ECC fill candidate");
            return 1;
        }
        double derived_redundancy = 0.0;
        if (!derive_ecc_redundancy_with_overwrite(ecc_payload_bytes,
                                           best_requested_ratio,
                                           structural_erasure_fraction,
                                           derived_redundancy,
                                           unknown_parity_budget,
                                           overlay_erasure_budget,
                                           overlay_fraction)) {
            console_line(2, "encode: canonical ECC fill cannot preserve structural and unknown-error budgets");
            return 1;
        }
        double effective_redundancy = 0.0;
        if (!derive_ecc_redundancy_with_overwrite(ecc_payload_bytes,
                                                   best_ratio,
                                                   structural_erasure_fraction,
                                                   effective_redundancy,
                                                   unknown_parity_budget,
                                                   overlay_erasure_budget,
                                                   overlay_fraction)) {
            console_line(2, "encode: ECC fill target is invalid");
            return 1;
        }
        ecc_redundancy = effective_redundancy;
        unknown_error_redundancy = ecc_redundancy;
        char ratio_buffer[32];
        format_fixed_3(ecc_redundancy, ratio_buffer, sizeof(ratio_buffer));
        char block_buffer[32];
        u64_to_ascii(best_block_data, block_buffer, sizeof(block_buffer));
        char parity_buffer[32];
        u64_to_ascii(best_parity, parity_buffer, sizeof(parity_buffer));
        char bits_buffer[32];
        u64_to_ascii(best_bits_needed, bits_buffer, sizeof(bits_buffer));
        char page_buffer[32];
        u64_to_ascii(best_page_count, page_buffer, sizeof(page_buffer));
        console_write(1, "encode: --ecc fill -> --ecc=");
        console_write(1, ratio_buffer);
        console_write(1, " (blocks=");
        console_write(1, block_buffer);
        console_write(1, " parity=");
        console_write(1, parity_buffer);
        console_write(1, " pages=");
        console_write(1, page_buffer);
        console_write(1, " bits_needed=");
        console_write(1, bits_buffer);
        console_line(1, ")");
    } else {
        double derived_redundancy = 0.0;
        if (!derive_ecc_redundancy_with_overwrite(ecc_payload_bytes,
                                           unknown_error_redundancy,
                                           0.0,
                                           derived_redundancy,
                                           unknown_parity_budget,
                                           overlay_erasure_budget,
                                           overlay_fraction)) {
            console_line(2, "encode: requested ECC cannot preserve structural and unknown-error budgets");
            return 1;
        } else {
            ecc_redundancy = derived_redundancy;
        }
    }
    metadata_ecc_redundancy = ecc_redundancy < 0.25 ? ecc_redundancy : 0.25;
    if (overlay_fraction > 0.0 && ecc_redundancy == 0.0) {
        char ratio_buffer[32] = {};
        for (u32 candidate_hundredths = 1u;
             candidate_hundredths <= 25400u;
             ++candidate_hundredths) {
            u16 candidate_data = 0u;
            u16 candidate_parity = 0u;
            u64 candidate_blocks = 0u;
            if (!makocode::compute_ecc_layout(ecc_payload_bytes,
                                              (double)candidate_hundredths / 100.0,
                                              candidate_data,
                                              candidate_parity,
                                              candidate_blocks) ||
                !candidate_data || !candidate_parity || !candidate_blocks) continue;
            u64 per_block = (u64)ceill(
                (long double)(candidate_data + candidate_parity) * overlay_fraction);
            if (per_block <= candidate_parity) {
                format_fixed_3((double)candidate_hundredths / 100.0,
                               ratio_buffer,
                               sizeof(ratio_buffer));
                break;
            }
        }
        console_write(2, "encode: overlay requires ECC");
        if (ratio_buffer[0]) {
            console_write(2, "; use at least --ecc ");
            console_write(2, ratio_buffer);
        }
        console_line(2, " or use --ecc fill");
        return 1;
    }
    if (overlay_fraction > 0.0) {
        u16 planned_data = 0u;
        u16 planned_parity = 0u;
        u64 planned_blocks = 0u;
        if (!makocode::compute_ecc_layout(ecc_payload_bytes,
                                          ecc_redundancy,
                                          planned_data,
                                          planned_parity,
                                          planned_blocks) ||
            !planned_data || !planned_parity || !planned_blocks) {
            console_line(2, "encode: failed to determine ECC layout for overlay budget");
            return 1;
        }
        if (overlay_erasure_budget > planned_parity) {
            char needed_buffer[32];
            char parity_buffer[32];
            char ratio_buffer[32] = {};
            char supported_buffer[32];
            for (u32 candidate_hundredths = 1u;
                 candidate_hundredths <= 25400u;
                 ++candidate_hundredths) {
                u16 candidate_data = 0u;
                u16 candidate_parity = 0u;
                u64 candidate_blocks = 0u;
                if (!makocode::compute_ecc_layout(ecc_payload_bytes,
                                                  (double)candidate_hundredths / 100.0,
                                                  candidate_data,
                                                  candidate_parity,
                                                  candidate_blocks) ||
                    !candidate_data || !candidate_parity || !candidate_blocks) continue;
                u64 per_block = (u64)ceill(
                    (long double)(candidate_data + candidate_parity) * overlay_fraction);
                if (per_block <= candidate_parity) {
                    format_fixed_3((double)candidate_hundredths / 100.0,
                                   ratio_buffer,
                                   sizeof(ratio_buffer));
                    break;
                }
            }
            long double supported_fraction =
                ((long double)planned_parity) /
                ((long double)planned_data + (long double)planned_parity);
            if (supported_fraction > 254.0L / 255.0L) {
                supported_fraction = 254.0L / 255.0L;
            }
            supported_fraction = (long double)floor((double)(supported_fraction * 1000.0L)) /
                                 1000.0L;
            format_fixed_3((double)supported_fraction,
                           supported_buffer,
                           sizeof(supported_buffer));
            u64_to_ascii(overlay_erasure_budget, needed_buffer, sizeof(needed_buffer));
            u64_to_ascii(planned_parity, parity_buffer, sizeof(parity_buffer));
            console_write(2, "encode: overlay needs ");
            console_write(2, needed_buffer);
            console_write(2, " erasures per block; ECC provides ");
            console_write(2, parity_buffer);
            if (ratio_buffer[0]) {
                console_write(2, "; use at least --ecc ");
                console_write(2, ratio_buffer);
            }
            console_write(2, " or reduce --overlay-fraction to at most ");
            console_line(2, supported_buffer);
            return 1;
        }
    }
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = ecc_redundancy;
    encoder.config.overlay_erasure_budget = overlay_erasure_budget;
    if (have_password && !hidden_mode_enabled) {
        if (!encoder.set_password((const char*)password_buffer.data, password_buffer.size)) {
            console_line(2, "encode: failed to set encryption password");
            return 1;
        }
        if (!encoder.set_random_data(random_data_buffer.data, random_data_buffer.size)) {
            console_line(2, "encode: failed to set encryption random data");
            return 1;
        }
    }
    if (hidden_mode_enabled) {
        encoder.payload_is_preencoded = true;
    }
    if (!encoder.set_payload(hidden_mode_enabled ? stealth_container.data : archive.buffer.data,
                             hidden_mode_enabled ? stealth_container.size : archive.buffer.size)) {
        console_line(2, "encode: failed to set payload");
        return 1;
    }
    if (!encoder.build()) {
        console_line(2, "encode: build failed");
        return 1;
    }
    makocode::EccSummary page_ecc_summary = encoder.ecc_info();
    page_ecc_summary.metadata_ecc_minimum_parity_bytes =
        metadata_ecc_minimum_parity_bytes;
    if (debug_requested) {
        char data_buffer[32];
        char parity_buffer[32];
        u64_to_ascii(encoder.ecc_info().block_data_symbols, data_buffer, sizeof(data_buffer));
        u64_to_ascii(encoder.ecc_info().parity_symbols, parity_buffer, sizeof(parity_buffer));
        console_write(1, "debug: ECC data symbols=");
        console_write(1, data_buffer);
        console_write(1, " parity symbols=");
        console_line(1, parity_buffer);
    }
    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count)) {
        console_line(2, "encode: failed to build frame");
        return 1;
    }
    if (!fit_page_dimensions(mapping,
                             footer_config,
                             frame_bit_count,
                             !mapping.page_width_set,
                             !mapping.page_height_set,
                             width_pixels,
                             height_pixels,
                             &page_ecc_summary,
                             metadata_ecc_redundancy,
                             unknown_parity_budget,
                             overlay_erasure_budget)) {
        console_line(2, "encode: payload does not fit the maximum automatic page dimensions");
        return 1;
    }
    if (fill_pages_specified || hidden_fill) {
        u64 target_page_count = fill_pages_specified ? fill_pages : 1u;
        PageFooterConfig fill_footer = footer_config;
        fill_footer.max_text_length = footer_compute_max_text_length(fill_footer, target_page_count);
        FooterLayout fill_layout;
        u64 fill_bits_per_page = 0u;
        if (!compute_footer_layout(width_pixels, height_pixels, fill_footer, fill_layout) ||
            !compute_bits_per_page(mapping,
                                   width_pixels,
                                   height_pixels,
                                   fill_layout.data_height_pixels,
                                   fill_bits_per_page) ||
            !fill_bits_per_page || fill_bits_per_page > U64_MAX_VALUE / target_page_count) {
            console_line(2, "encode: unable to determine --fill-pages capacity");
            return 1;
        }
        u64 target_bits = (fill_bits_per_page * target_page_count) & ~7ull;
        if (frame_bit_count > target_bits) {
            console_line(2, "encode: encoded data exceeds the requested --fill-pages capacity");
            return 1;
        }
        u64 target_bytes_u64 = target_bits >> 3u;
        if (target_bytes_u64 > (u64)USIZE_MAX_VALUE ||
            (frame_bit_count & 7u) != 0u ||
            frame_bits.size != (usize)(frame_bit_count >> 3u)) {
            console_line(2, "encode: --fill-pages size is too large");
            return 1;
        }
        usize original_bytes = frame_bits.size;
        usize target_bytes = (usize)target_bytes_u64;
        if (!frame_bits.ensure(target_bytes)) {
            console_line(2, "encode: failed to prepare random page filler");
            return 1;
        }
        makocode::ByteBuffer random_fill;
        if (!MetadataTile::stealth_fill_random(random_fill,
                                               target_bytes,
                                               random_data_buffer.data,
                                               random_data_buffer.size,
                                               "makocode page fill v1")) {
            console_line(2, "encode: failed to generate random page filler");
            return 1;
        }
        if (target_bytes > original_bytes) {
            memcpy(frame_bits.data + original_bytes,
                   random_fill.data + original_bytes,
                   target_bytes - original_bytes);
            if (mapping.color_channels == 3u) {
                for (usize i = original_bytes; i < target_bytes; ++i) {
                    u8 rotate = (u8)((i % 3u) + 1u);
                    frame_bits.data[i] = rotate_left_u8(frame_bits.data[i], rotate);
                }
            }
        }
        frame_bits.size = target_bytes;
        frame_bit_count = target_bits;
    }
    FooterLayout footer_layout;
    u32 layout_data_height = height_pixels;
    u64 bits_per_page = 0u;
    u64 page_count = 0u;
    if (!compute_page_layout(mapping,
                             footer_config,
                             frame_bit_count,
                             width_pixels,
                             height_pixels,
                             footer_layout,
                             layout_data_height,
                             bits_per_page,
                             page_count,
                             &page_ecc_summary,
                             metadata_ecc_redundancy,
                             unknown_parity_budget,
                             overlay_erasure_budget)) {
        console_line(2, "encode: footer layout did not converge");
        return 1;
    }
    if (footer_config.over_barcode && footer_layout.has_text) {
        makocode::ByteBuffer budget_footer_text;
        u64 maximum_text_pixels = 0u;
        for (u64 page = 1u; page <= page_count; ++page) {
            if (!footer_build_page_text(footer_config, page, page_count, budget_footer_text)) {
                console_line(2, "encode: failed to measure over-barcode footer");
                return 1;
            }
            u64 page_text_pixels = 0u;
            for (usize i = 0u; i < budget_footer_text.size; ++i) {
                const GlyphPattern* glyph = footer_lookup_glyph((char)budget_footer_text.data[i]);
                if (!glyph) {
                    console_line(2, "encode: failed to count over-barcode footer pixels");
                    return 1;
                }
                u32 lit_pixels = 0u;
                for (u32 row = 0u; row < FOOTER_BASE_GLYPH_HEIGHT; ++row) {
                    for (u32 column = 0u; column < FOOTER_BASE_GLYPH_WIDTH; ++column) {
                        if (glyph->rows[row][column] == '1') ++lit_pixels;
                    }
                }
                u64 scale_area = (u64)footer_config.font_size * footer_config.font_size;
                if (lit_pixels && scale_area > (U64_MAX_VALUE - page_text_pixels) / lit_pixels) {
                    console_line(2, "encode: over-barcode footer pixel count overflow");
                    return 1;
                }
                page_text_pixels += (u64)lit_pixels * scale_area;
            }
            if (page_text_pixels > maximum_text_pixels) maximum_text_pixels = page_text_pixels;
        }
        if (maximum_text_pixels > 0xFFFFu) {
            console_line(2, "encode: over-barcode footer exceeds the supported ECC error budget");
            return 1;
        }
        if (maximum_text_pixels > unknown_parity_budget) {
            unknown_parity_budget = (u16)maximum_text_pixels;
        }
    }
    if (page_count == 0u || page_count > (u64)USIZE_MAX_VALUE ||
        !metadata_page_assignments.ensure((usize)page_count)) {
        console_line(2, "encode: failed to allocate metadata page selection");
        return 1;
    }
    metadata_page_assignments.size = (usize)page_count;
    memset(metadata_page_assignments.data,
           metadata_pages_value &&
                   !(metadata_pages_length == 3u &&
                     memcmp(metadata_pages_value, "all", 3u) == 0)
               ? 0u : 1u,
           metadata_page_assignments.size);
    if (metadata_pages_value &&
        !(metadata_pages_length == 3u &&
          memcmp(metadata_pages_value, "all", 3u) == 0)) {
        usize item_start = 0u;
        while (item_start < metadata_pages_length) {
            usize item_end = item_start;
            while (item_end < metadata_pages_length && metadata_pages_value[item_end] != ',') {
                ++item_end;
            }
            usize dash = item_start;
            while (dash < item_end && metadata_pages_value[dash] != '-') ++dash;
            u64 first_page = 0u;
            u64 last_page = 0u;
            if (item_end - item_start == 6u &&
                memcmp(metadata_pages_value + item_start, "middle", 6u) == 0) {
                first_page = page_count / 2u + page_count % 2u;
                last_page = first_page;
            } else if (item_end - item_start == 4u &&
                       memcmp(metadata_pages_value + item_start, "last", 4u) == 0) {
                first_page = page_count;
                last_page = first_page;
            } else {
                ascii_to_u64(metadata_pages_value + item_start,
                             dash - item_start,
                             &first_page);
                last_page = first_page;
                if (dash < item_end) {
                    ascii_to_u64(metadata_pages_value + dash + 1u,
                                 item_end - dash - 1u,
                                 &last_page);
                }
                if (last_page > page_count) {
                    console_line(2, "encode: --metadata-pages contains a page beyond the encoded page count");
                    return 1;
                }
            }
            for (u64 page = first_page; page <= last_page; ++page) {
                metadata_page_assignments.data[(usize)(page - 1u)] = 1u;
            }
            if (item_end == metadata_pages_length) break;
            item_start = item_end + 1u;
        }
    }
    if (no_metadata) {
        memset(metadata_page_assignments.data, 0u, metadata_page_assignments.size);
    }
    if (overlay_fraction > 0.0 && page_count > 1u &&
        ecc_mode == ECC_EXPLICIT_RATIO && ecc_redundancy < 0.5) {
        console_line(2, "encode: multi-page overlays require --ecc of at least 0.5; larger overlays may require more");
        return 1;
    }
    if (overlay_count > 0u) {
        if (page_count > (u64)USIZE_MAX_VALUE ||
            !overlay_page_assignments.ensure((usize)page_count)) {
            console_line(2, "encode: failed to allocate overlay page assignments");
            return 1;
        }
        overlay_page_assignments.size = (usize)page_count;
        memset(overlay_page_assignments.data, 0, overlay_page_assignments.size);
        for (usize overlay_index = 0u; overlay_index < overlay_count; ++overlay_index) {
            EncodeOverlay& overlay = overlays[overlay_index];
            if (!load_overlay_page(overlay.path, overlay.image)) return 1;
            if (overlay.image.width != width_pixels || overlay.image.height != height_pixels) {
                console_line(2, "encode: --overlay-ppm dimensions must match the encoded pages");
                return 1;
            }
            if (overlay.pages_length == 3u && memcmp(overlay.pages, "all", 3u) == 0) {
                for (u64 page = 1u; page <= page_count; ++page) {
                    usize page_offset = (usize)(page - 1u);
                    u8 assignment = (u8)(overlay_index + 1u);
                    if (overlay_page_assignments.data[page_offset] != 0u &&
                        overlay_page_assignments.data[page_offset] != assignment) {
                        console_line(2, "encode: a page cannot have more than one overlay image");
                        return 1;
                    }
                    overlay_page_assignments.data[page_offset] = assignment;
                }
                continue;
            }
            usize item_start = 0u;
            while (item_start < overlay.pages_length) {
                usize item_end = item_start;
                while (item_end < overlay.pages_length && overlay.pages[item_end] != ',') ++item_end;
                usize dash = item_start;
                while (dash < item_end && overlay.pages[dash] != '-') ++dash;
                u64 first_page = 0u;
                u64 last_page = 0u;
                if (item_end - item_start == 6u &&
                    memcmp(overlay.pages + item_start, "middle", 6u) == 0) {
                    first_page = page_count / 2u + page_count % 2u;
                    last_page = first_page;
                } else if (item_end - item_start == 4u &&
                           memcmp(overlay.pages + item_start, "last", 4u) == 0) {
                    first_page = page_count;
                    last_page = page_count;
                } else {
                    ascii_to_u64(overlay.pages + item_start, dash - item_start, &first_page);
                    last_page = first_page;
                    if (dash < item_end) {
                        ascii_to_u64(overlay.pages + dash + 1u,
                                     item_end - dash - 1u,
                                     &last_page);
                    }
                    if (last_page > page_count) {
                        console_line(2, "encode: --overlay-pages contains a page beyond the encoded page count");
                        return 1;
                    }
                }
                for (u64 page = first_page; page <= last_page; ++page) {
                    usize page_offset = (usize)(page - 1u);
                    u8 assignment = (u8)(overlay_index + 1u);
                    if (overlay_page_assignments.data[page_offset] != 0u &&
                        overlay_page_assignments.data[page_offset] != assignment) {
                        console_line(2, "encode: a page cannot have more than one overlay image");
                        return 1;
                    }
                    overlay_page_assignments.data[page_offset] = assignment;
                }
                if (item_end == overlay.pages_length) break;
                item_start = item_end + 1u;
            }
        }
    }
    u16 required_parity = 0u;
    if (!validate_overwrite_budget(mapping,
                                   frame_bits,
                                   frame_bit_count,
                                   width_pixels,
                                   height_pixels,
                                   layout_data_height,
                                   bits_per_page,
                                   page_count,
                                   page_ecc_summary,
                                   metadata_ecc_redundancy,
                                   unknown_parity_budget,
                                   overlay_erasure_budget,
                                   &required_parity,
                                   !g_encode_metadata_disabled)) {
        char required_parity_buffer[32];
        char target_parity_buffer[32];
        char sufficient_target_buffer[32] = {};
        char supported_overlay_buffer[32] = {};
        u32 required_without_current_overlay =
            required_parity > overlay_erasure_budget
                ? (u32)required_parity - (u32)overlay_erasure_budget
                : 0u;
        for (u32 candidate_hundredths = 1u;
             candidate_hundredths <= 25400u;
             ++candidate_hundredths) {
            u16 candidate_data = 0u;
            u16 candidate_parity = 0u;
            u64 candidate_blocks = 0u;
            if (!makocode::compute_ecc_layout(
                    ecc_payload_bytes,
                    (double)candidate_hundredths / 100.0,
                    candidate_data,
                    candidate_parity,
                    candidate_blocks) ||
                candidate_data == 0u || candidate_parity == 0u || candidate_blocks == 0u) {
                continue;
            }
            u64 candidate_overlay = (u64)ceill(
                (long double)(candidate_data + candidate_parity) * overlay_fraction);
            if ((u32)candidate_parity >= required_without_current_overlay + candidate_overlay) {
                format_fixed_3((double)candidate_hundredths / 100.0,
                               sufficient_target_buffer,
                               sizeof(sufficient_target_buffer));
                break;
            }
        }
        u32 available_overlay_per_block =
            encoder.ecc_info().parity_symbols > required_without_current_overlay
                ? (u32)encoder.ecc_info().parity_symbols - required_without_current_overlay
                : 0u;
        u64 current_block_total = (u64)encoder.ecc_info().block_data_symbols +
                                  encoder.ecc_info().parity_symbols;
        long double max_overlay_fraction = current_block_total
            ? (long double)available_overlay_per_block / (long double)current_block_total
            : 0.0L;
        if (max_overlay_fraction > 254.0L / 255.0L) {
            max_overlay_fraction = 254.0L / 255.0L;
        }
        max_overlay_fraction = (long double)floor((double)(max_overlay_fraction * 1000.0L)) /
                               1000.0L;
        format_fixed_3((double)max_overlay_fraction,
                       supported_overlay_buffer,
                       sizeof(supported_overlay_buffer));
        if (supported_overlay_buffer[0] == '\0') {
            strcpy(supported_overlay_buffer, "0.000");
        }
        u64_to_ascii(required_parity, required_parity_buffer, sizeof(required_parity_buffer));
        u64_to_ascii(encoder.ecc_info().parity_symbols,
                     target_parity_buffer,
                     sizeof(target_parity_buffer));
        console_write(2, "encode: ECC cannot cover structural erasures and requested overlay (needs ");
        console_write(2, required_parity_buffer);
        console_write(2, " symbols per block; target provides ");
        console_write(2, target_parity_buffer);
        if (sufficient_target_buffer[0] != '\0') {
            console_write(2, "; use at least base --ecc ");
            console_write(2, sufficient_target_buffer);
        }
        console_write(2, "; current ECC supports at most --overlay-fraction ");
        console_write(2, supported_overlay_buffer);
        console_line(2, ")");
        return 1;
    }
    char timestamp_name[32];
    const char* page_name_prefix = 0;
    if (have_prefix) {
        page_name_prefix = (const char*)prefix_buffer.data;
    } else {
        if (!utc_timestamp_string(timestamp_name, sizeof(timestamp_name))) {
            console_line(2, "encode: failed to construct timestamped filename");
            return 1;
        }
        page_name_prefix = timestamp_name;
    }
    bool debug_reapplied_by_filename = debug_requested &&
                                       path_contains_substring(page_name_prefix, "_encoded");
    u8 fiducial_rgb[3] = {0u, 0u, 0u};
    fiducial_select_color(mapping, fiducial_rgb);
    makocode::ByteBuffer debug_fiducial_csv;
    makocode::ByteBuffer debug_fiducial_path;
    if (debug_requested) {
        if (!debug_fiducial_csv.append_ascii("page,row,column,x,y,logical_x,logical_y\n")) {
            console_line(2, "encode: failed to prepare fiducial CSV");
            return 1;
        }
        makocode::ByteBuffer debug_fiducial_name;
        if (!debug_fiducial_name.append_ascii(page_name_prefix) ||
            !debug_fiducial_name.append_ascii("_encode_fiducials.csv") ||
            !join_output_path(output_dir,
                              (const char*)debug_fiducial_name.data,
                              debug_fiducial_path)) {
            console_line(2, "encode: failed to prepare fiducial CSV path");
            return 1;
        }
    }
    makocode::ByteBuffer footer_text_buffer;
    const makocode::EccSummary* ecc_summary = &page_ecc_summary;
    if (page_count == 1u) {
        u32 output_height_pixels = height_pixels;
        FooterLayout output_footer_layout = footer_layout;
        u32 output_data_height_pixels = layout_data_height;
        u64 output_bits_per_page = bits_per_page;
        makocode::ByteBuffer page_output;
        MetadataTile::Values overlay_tile_values;
        if (!footer_build_page_text(footer_config, 1u, page_count, footer_text_buffer)) {
            console_line(2, "encode: failed to build footer text");
            return 1;
        }
        const char* footer_text = output_footer_layout.has_text ? (const char*)footer_text_buffer.data : 0;
        usize footer_length = output_footer_layout.has_text ? footer_text_buffer.size : 0u;
        if (!encode_page_to_ppm(mapping,
                                frame_bits,
                                frame_bit_count,
                                0u,
                                width_pixels,
                                output_height_pixels,
                                1u,
                                output_bits_per_page,
                                ecc_summary,
                                metadata_ecc_redundancy,
                                footer_text,
                                footer_length,
                                output_footer_layout,
                                page_output,
                                unknown_parity_budget,
                                overlay_erasure_budget,
                                metadata_page_assignments.data[0u] != 0u,
                                &overlay_tile_values)) {
           console_line(2, "encode: failed to format ppm");
           return 1;
       }
        makocode::ByteBuffer fiducial_page;
        // Clip fiducials to the data area so they don't overlap footer text.
        if (no_fiducials) {
            byte_buffer_move(fiducial_page, page_output);
        } else {
            if (!apply_default_fiducial_grid(page_output,
                                             fiducial_page,
                                             output_data_height_pixels,
                                             fiducial_rgb,
                                             (debug_requested && !debug_reapplied_by_filename)
                                                 ? &debug_fiducial_csv
                                                 : (makocode::ByteBuffer*)0,
                                             1u)) {
                console_line(2, "encode: failed to embed fiducial grid");
                return 1;
            }
        }
        byte_buffer_move(page_output, fiducial_page);
        makocode::ByteBuffer output_name;
        makocode::ByteBuffer output_path;
        if (!build_page_filename(output_name, page_name_prefix, 1u, 1u)) {
            console_line(2, "encode: failed to build output filename");
            return 1;
        }
        if (!join_output_path(output_dir, (const char*)output_name.data, output_path)) {
            console_line(2, "encode: failed to prepare output path");
            return 1;
        }
        if (!ensure_parent_directories((const char*)output_path.data)) {
            console_line(2, "encode: failed to prepare output directories");
            return 1;
        }
        if (!write_buffer_to_file((const char*)output_path.data,
                                  page_output,
                                  debug_reapplied_by_filename ? &debug_fiducial_csv : (makocode::ByteBuffer*)0,
                                  1u,
                                  output_data_height_pixels,
                                  fiducial_rgb,
                                  !no_fiducials)) {
            console_line(2, "encode: failed to write ppm file");
            return 1;
        }
        u8 overlay_assignment = overlay_page_assignments.size
                                   ? overlay_page_assignments.data[0u]
                                   : 0u;
        if (overlay_assignment && overlay_erasure_budget > 0u &&
            !merge_overlay_into_encoded_page((const char*)output_path.data,
                                             overlays[(usize)overlay_assignment - 1u].image,
                                             overlay_ignore_colors_text,
                                             overlay_ignore_colors_length,
                                             debug_requested,
                                             no_metadata ? &overlay_tile_values : 0,
                                             no_metadata ? &mapping : 0,
                                             !no_metadata)) return 1;
        console_write(1, "encode: wrote 1 page (");
        console_write(1, (const char*)output_path.data);
        console_line(1, ")");
    } else {
        makocode::ByteBuffer name_buffer;
        makocode::ByteBuffer path_buffer;
        for (u64 page = 0u; page < page_count; ++page) {
            makocode::ByteBuffer page_output;
            MetadataTile::Values overlay_tile_values;
            u64 bit_offset = page * bits_per_page;
            if (!footer_build_page_text(footer_config, page + 1u, page_count, footer_text_buffer)) {
                console_line(2, "encode: failed to build footer text");
                return 1;
            }
            const char* footer_text = footer_layout.has_text ? (const char*)footer_text_buffer.data : 0;
            usize footer_length = footer_layout.has_text ? footer_text_buffer.size : 0u;
            if (!encode_page_to_ppm(mapping,
                                    frame_bits,
                                    frame_bit_count,
                                    bit_offset,
                                    width_pixels,
                                    height_pixels,
                                    page + 1u,
                                    bits_per_page,
                                    ecc_summary,
                                    metadata_ecc_redundancy,
                                    footer_text,
                                    footer_length,
                                    footer_layout,
                                    page_output,
                                    unknown_parity_budget,
                                    overlay_erasure_budget,
                                    metadata_page_assignments.data[(usize)page] != 0u,
                                    &overlay_tile_values)) {
                console_line(2, "encode: failed to format ppm page");
                return 1;
            }
            makocode::ByteBuffer fiducial_page;
            // Clip fiducials to the data area so they don't overlap footer text.
            if (no_fiducials) {
                byte_buffer_move(fiducial_page, page_output);
            } else {
                if (!apply_default_fiducial_grid(page_output,
                                                 fiducial_page,
                                                 footer_layout.data_height_pixels,
                                                 fiducial_rgb,
                                                 (debug_requested && !debug_reapplied_by_filename)
                                                     ? &debug_fiducial_csv
                                                     : (makocode::ByteBuffer*)0,
                                                 page + 1u)) {
                    console_line(2, "encode: failed to embed fiducial grid");
                    return 1;
                }
            }
            byte_buffer_move(page_output, fiducial_page);
            if (!build_page_filename(name_buffer, page_name_prefix, page + 1u, page_count)) {
                console_line(2, "encode: failed to build filename");
                return 1;
            }
            if (!join_output_path(output_dir, (const char*)name_buffer.data, path_buffer)) {
                console_line(2, "encode: failed to prepare output path");
                return 1;
            }
            if (!ensure_parent_directories((const char*)path_buffer.data)) {
                console_line(2, "encode: failed to prepare output directories");
                return 1;
            }
            if (!write_buffer_to_file((const char*)path_buffer.data,
                                      page_output,
                                      debug_reapplied_by_filename ? &debug_fiducial_csv : (makocode::ByteBuffer*)0,
                                      page + 1u,
                                      footer_layout.data_height_pixels,
                                      fiducial_rgb,
                                      !no_fiducials)) {
                console_line(2, "encode: failed to write ppm file");
                return 1;
            }
            u8 overlay_assignment = overlay_page_assignments.size
                                        ? overlay_page_assignments.data[(usize)page]
                                        : 0u;
            if (overlay_assignment && overlay_erasure_budget > 0u &&
                !merge_overlay_into_encoded_page((const char*)path_buffer.data,
                                                 overlays[(usize)overlay_assignment - 1u].image,
                                                 overlay_ignore_colors_text,
                                                 overlay_ignore_colors_length,
                                                 debug_requested,
                                                 no_metadata ? &overlay_tile_values : 0,
                                                 no_metadata ? &mapping : 0,
                                                 !no_metadata)) return 1;
        }
        makocode::ByteBuffer sample_name;
        if (!build_page_filename(sample_name, page_name_prefix, 1u, page_count)) {
            console_line(2, "encode: failed to summarize filenames");
            return 1;
        }
        makocode::ByteBuffer sample_path;
        const char* summary_name = (const char*)sample_name.data;
        if (join_output_path(output_dir, summary_name, sample_path)) {
            summary_name = (const char*)sample_path.data;
        }
        char digits[32];
        u64_to_ascii(page_count, digits, sizeof(digits));
        console_write(1, "encode: wrote ");
        console_write(1, digits);
        console_write(1, " pages (");
        console_write(1, summary_name);
        console_line(1, " ...)");
    }
    if (debug_requested &&
        (!ensure_parent_directories((const char*)debug_fiducial_path.data) ||
         !write_bytes_to_file((const char*)debug_fiducial_path.data,
                              debug_fiducial_csv.data,
                              debug_fiducial_csv.size))) {
        console_line(2, "encode: failed to write fiducial CSV");
        return 1;
    }
    return 0;
}

static bool build_overlay_payload_rank_reservation_mask(
    const OverlayPage& page,
    makocode::ByteBuffer& mask_out) {
    const MetadataTile::Values& tile = page.metadata.encoded_tile;
    bool use_metadata_fiducial_grid = true;
    u64 reserved_pixels = 0u;
    return compute_fiducial_marker_mask(page.width,
                                       page.height,
                                       page.data_height,
                                       reserved_pixels,
                                       &mask_out,
                                       use_metadata_fiducial_grid,
                                       tile.fiducial_columns,
                                       tile.fiducial_rows,
                                       tile.fiducial_spacing_pixels);
}

static const usize MAX_OVERLAY_IGNORE_COLORS = 64u;

struct OverlayIgnoreColorList {
    PaletteColor colors[MAX_OVERLAY_IGNORE_COLORS];
    usize count;
};

static bool overlay_ignore_char_is_delimiter(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case ',':
        case ';':
            return true;
        default:
            return false;
    }
}

static bool overlay_color_matches_ignore(const OverlayIgnoreColorList& list,
                                        u8 r,
                                        u8 g,
                                        u8 b) {
    for (usize i = 0u; i < list.count; ++i) {
        const PaletteColor& candidate = list.colors[i];
        if (candidate.r == r && candidate.g == g && candidate.b == b) {
            return true;
        }
    }
    return false;
}

static bool parse_overlay_ignore_colors(const char* text,
                                        usize length,
                                        OverlayIgnoreColorList& list) {
    list.count = 0u;
    usize cursor = 0u;
    while (cursor < length) {
        while (cursor < length && overlay_ignore_char_is_delimiter(text[cursor])) {
            ++cursor;
        }
        if (cursor >= length) {
            break;
        }
        usize start = cursor;
        while (cursor < length && !overlay_ignore_char_is_delimiter(text[cursor])) {
            ++cursor;
        }
        usize token_length = cursor - start;
        if (list.count >= MAX_OVERLAY_IGNORE_COLORS) {
            console_line(2, "overlay: --ignore-colors supports at most 64 entries");
            return false;
        }
        PaletteColor color;
        u8 color_id = NAMED_COLOR_INVALID;
        if (!decode_named_palette_color(text + start, token_length, color_id, color)) {
            console_line(2, "overlay: --ignore-colors contains an invalid color");
            return false;
        }
        bool duplicate = false;
        for (usize i = 0u; i < list.count; ++i) {
            if (palette_colors_equal(list.colors[i], color)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        list.colors[list.count++] = color;
    }
    return true;
}

static bool load_overlay_page(const char* path, OverlayPage& page) {
    page.original.release();
    page.pixels.release();
    page.metadata = PpmParserState();
    page.width = 0u;
    page.height = 0u;
    page.data_height = 0u;
    page.footer_rows = 0u;
    page.color_mode = 1u;
    page.use_custom_palette = false;
    page.custom_symbol_bits = 0u;
    if (!read_entire_file(path, page.original)) {
        console_write(2, "overlay: failed to read ");
        console_line(2, path);
        return false;
    }
    if (!page.original.data || page.original.size == 0u) {
        console_write(2, "overlay: empty file ");
        console_line(2, path);
        return false;
    }
    PpmParserState state;
    state.data = page.original.data;
    state.size = page.original.size;
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length) ||
        !ascii_equals_token(token, token_length, "P6")) {
        console_write(2, "overlay: ");
        console_write(2, path);
        console_line(2, " is not a P6 binary PPM");
        return false;
    }
    u64 width_value = 0u;
    if (!ppm_next_token(state, &token, &token_length) ||
        !ascii_to_u64(token, token_length, &width_value) ||
        width_value == 0u ||
        width_value > 0xFFFFFFFFull) {
        console_write(2, "overlay: invalid width in ");
        console_line(2, path);
        return false;
    }
    u64 height_value = 0u;
    if (!ppm_next_token(state, &token, &token_length) ||
        !ascii_to_u64(token, token_length, &height_value) ||
        height_value == 0u ||
        height_value > 0xFFFFFFFFull) {
        console_write(2, "overlay: invalid height in ");
        console_line(2, path);
        return false;
    }
    u64 max_value = 0u;
    if (!ppm_next_token(state, &token, &token_length) ||
        !ascii_to_u64(token, token_length, &max_value) ||
        max_value != 255u ||
        width_value > U64_MAX_VALUE / height_value ||
        width_value * height_value > (u64)USIZE_MAX_VALUE / 3u ||
        !ppm_consume_binary_separator(state)) {
        console_write(2, "overlay: unsupported max pixel value in ");
        console_line(2, path);
        return false;
    }
    u64 pixel_count = width_value * height_value;
    if (pixel_count > ((u64)USIZE_MAX_VALUE / 3u)) {
        console_write(2, "overlay: pixel count overflow in ");
        console_line(2, path);
        return false;
    }
    makocode::ByteBuffer pixel_data;
    if (!ppm_read_rgb_pixels(state, pixel_count, pixel_data)) {
        console_write(2, "overlay: failed to parse pixel data from ");
        console_line(2, path);
        return false;
    }
    page.metadata = state;
    page.metadata.data = 0;
    page.metadata.size = 0u;
    page.metadata.cursor = 0u;

    // The canonical protected tile is centered in the logical data area.
    MetadataTile::Values tile_values = {};
    makocode::ByteBuffer tile_palette_text;
    page.metadata.has_footer_rows = false;
    page.metadata.footer_rows_value = 0u;
    for (u32 footer_rows = 0u;
         footer_rows < height_value && !page.metadata.has_encoded_tile;
         footer_rows += 2u) {
        u32 candidate_data_height = (u32)height_value - footer_rows;
        MetadataTile::Placement center_placement =
            MetadataTile::compute_center_tile_placement((u32)width_value, candidate_data_height);
        MetadataTile::AffineParams native_tile_affine = {};
        native_tile_affine.center_x = (double)center_placement.x0 +
            ((double)MetadataTile::TILE_SIDE - 1.0) * 0.5 -
            (double)center_placement.offset_x;
        native_tile_affine.center_y = (double)center_placement.y0 +
            ((double)MetadataTile::TILE_SIDE - 1.0) * 0.5 -
            (double)center_placement.offset_y;
        native_tile_affine.pitch_pixels = 1.0;
        native_tile_affine.pitch_x_pixels = 1.0;
        native_tile_affine.pitch_y_pixels = 1.0;
        u64 native_tile_tag = 0u;
        bool native_tile_inverted = false;
        double native_tile_tag_confidence = 0.0;
        bool tile_tag_found = center_placement.valid &&
            MetadataTile::match_tile_tag_affine(pixel_data.data,
                                                (u32)width_value,
                                                (u32)height_value,
                                                candidate_data_height,
                                                native_tile_affine,
                                                native_tile_tag,
                                                native_tile_inverted,
                                                native_tile_tag_confidence,
                                                true,
                                                true);
        bool decoded = tile_tag_found &&
                       MetadataTile::decode_tile(pixel_data.data,
                                                 (u32)width_value,
                                                 (u32)height_value,
                                                 candidate_data_height,
                                                 center_placement,
                                                 tile_values,
                                                 tile_palette_text);
        if (decoded) {
            tile_values.footer_rows = (u32)height_value - candidate_data_height;
            apply_metadata_tile_metadata(page.metadata, tile_values, tile_palette_text);
        }
    }

    byte_buffer_move(page.pixels, pixel_data);
    page.width = (u32)width_value;
    page.height = (u32)height_value;

    u32 footer_rows = 0u;
    if (page.metadata.has_footer_rows) {
        if (page.metadata.footer_rows_value > height_value) {
            console_write(2, "overlay: footer metadata exceeds image height in ");
            console_line(2, path);
            return false;
        }
        footer_rows = (u32)page.metadata.footer_rows_value;
    }
    page.footer_rows = footer_rows;
    page.data_height = page.height - footer_rows;
    page.color_mode = 1u;
    if (page.metadata.has_palette_base) {
        const u64 palette_base = page.metadata.palette_base_value;
        if (palette_base >= 8u) {
            page.color_mode = 3u;
        } else if (palette_base >= 4u) {
            page.color_mode = 2u;
        }
    } else if (page.pixels.data && page.pixels.size >= (usize)page.width * (usize)page.height * 3u) {
        // When palette metadata is unavailable (e.g., synthetic overlay masks),
        // infer whether the image uses chroma at all. Overlay behavior only
        // needs a coarse "grayscale vs. color" distinction to decide between
    // raw-pixel overlay and data-bit overlay. Avoid depending on container metadata
        // comments, which are not preserved across print/scan workflows.
        usize total_pixels = (usize)page.width * (usize)page.height;
        bool has_color = false;
        for (usize i = 0u; i < total_pixels; ++i) {
            const u8* rgb = page.pixels.data + i * 3u;
            if (rgb[0] != rgb[1] || rgb[1] != rgb[2]) {
                has_color = true;
                break;
            }
        }
        if (has_color) {
            page.color_mode = 2u;
        }
    }
    bool palette_matches_builtin = false;
    u8 builtin_mode = 1u;
    if (page.metadata.has_encoded_tile) {
        const MetadataTile::Values& values = page.metadata.encoded_tile;
        auto matches_palette = [&](const PaletteColor* builtin, u32 count, u8 mode) -> bool {
            if (!builtin || values.palette_count != count) return false;
            for (u32 i = 0u; i < count; ++i) {
                if (!palette_colors_equal(values.palette[i], builtin[i])) return false;
            }
            builtin_mode = mode;
            return true;
        };
        palette_matches_builtin =
            matches_palette(PALETTE_GRAY,
                            (u32)(sizeof(PALETTE_GRAY) / sizeof(PALETTE_GRAY[0])),
                            1u) ||
            matches_palette(PALETTE_CMYW,
                            (u32)(sizeof(PALETTE_CMYW) / sizeof(PALETTE_CMYW[0])),
                            2u) ||
            matches_palette(PALETTE_RGB_CMY_WB,
                            (u32)(sizeof(PALETTE_RGB_CMY_WB) /
                                  sizeof(PALETTE_RGB_CMY_WB[0])),
                            3u);
        if (palette_matches_builtin) page.color_mode = builtin_mode;
    }
    bool should_use_custom = page.metadata.has_palette_base ||
                             (page.metadata.has_encoded_tile && !palette_matches_builtin);
    if (should_use_custom && page.metadata.has_palette_text &&
        page.metadata.palette_text_length > 0u &&
        image_mapping_set_palette_text(page.custom_mapping,
                                       page.metadata.palette_text,
                                       page.metadata.palette_text_length,
                                       "overlay") &&
        image_mapping_build_custom_palette(page.custom_mapping, "overlay")) {
        page.use_custom_palette = true;
        u32 palette_count = page.custom_mapping.custom_palette_count;
        while (palette_count >= 2u) {
            palette_count >>= 1u;
            ++page.custom_symbol_bits;
        }
    }
    return true;
}

static bool build_overlay_mask(const OverlayPage& page,
                               makocode::ByteBuffer& mask_out,
                               u64& reserved_pixels,
                               bool protect_metadata_tile = true) {
    bool use_metadata_grid = page.metadata.has_fiducial_spacing &&
                             page.metadata.has_fiducial_columns &&
                             page.metadata.has_fiducial_rows;
    if (!compute_fiducial_marker_mask(page.width,
                                      page.height,
                                      page.data_height,
                                      reserved_pixels,
                                      &mask_out,
                                      use_metadata_grid,
                                      (u32)page.metadata.fiducial_columns_value,
                                      (u32)page.metadata.fiducial_rows_value,
                                      (u32)page.metadata.fiducial_spacing_value)) {
        return false;
    }

    // Protect only the written metadata barcode and codeword modules.
    if (protect_metadata_tile && page.metadata.has_encoded_tile) {
        MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(
            page.width, page.data_height);
        const MetadataTile::Values& values = page.metadata.encoded_tile;
        auto reserve_tile_module = [&](u32 tile_x, u32 tile_y) {
            i64 page_x = (i64)placement.x0 + (i64)tile_x - (i64)placement.offset_x;
            i64 page_y = (i64)placement.y0 + (i64)tile_y - (i64)placement.offset_y;
            if (!placement.valid || page_x < 0 || page_y < 0 ||
                page_x >= (i64)page.width || page_y >= (i64)page.data_height) return;
            usize index = (usize)page_y * (usize)page.width + (usize)page_x;
            if (!mask_out.data[index]) {
                mask_out.data[index] = 1u;
                ++reserved_pixels;
            }
        };
        if (placement.valid && values.metadata_square_side > 0u &&
            values.metadata_square_side <= MetadataTile::TILE_MAX_METADATA_SQUARE_SIDE) {
            u32 tag_y = values.metadata_tag_y0;
            u32 tag_width = values.palette_count + 1u;
            u32 palette_tag_x = MetadataTile::palette_tag_x0(values.palette_count);
            for (u32 x = 0u; x < tag_width && x < MetadataTile::TILE_TAG_BITS; ++x) {
                reserve_tile_module(palette_tag_x + x, tag_y);
            }
            u32 marker_x0 = MetadataTile::tag_marker_x0();
            for (u32 dy = 1u; dy < MetadataTile::TILE_TAG_ROWS; ++dy) {
                for (u32 x = 0u; x < 8u; ++x) {
                    reserve_tile_module(marker_x0 + x, tag_y + dy);
                }
            }
            u32 length_x0 = (MetadataTile::TILE_SIDE -
                             values.metadata_outer_length_bits) / 2u;
            for (u32 x = 0u; x < values.metadata_outer_length_bits; ++x) {
                reserve_tile_module(length_x0 + x,
                                    tag_y + MetadataTile::TILE_LENGTH_ROW);
            }
            usize metadata_cells = (usize)values.metadata_length_prefix_bits +
                                   values.metadata_symbol_count;
            usize square_cells = (usize)values.metadata_square_side *
                                 (usize)values.metadata_square_side;
            if (metadata_cells <= square_cells) {
                for (usize cell = 0u; cell < metadata_cells; ++cell) {
                    u32 x = values.metadata_square_x0 + (u32)(cell % values.metadata_square_side);
                    u32 y = values.metadata_square_y0 + (u32)(cell / values.metadata_square_side);
                    reserve_tile_module(x, y);
                }
            }
        }
    }
    return true;
}



struct OverlaySelector {
    u64 seed;
    u16 overlay_erasure_budget;
    u16 block_total;
    u64 block_count;
    makocode::ByteBuffer counts;
    makocode::ByteBuffer ranks;

    OverlaySelector()
        : seed(0u),
          overlay_erasure_budget(0u),
          block_total(0u),
          block_count(0u),
          counts(),
          ranks() {}
};

static bool apply_overlay_pixels_with_selector(const OverlayPage& overlay_page,
                                               const makocode::ByteBuffer& base_mask,
                                               const OverlayIgnoreColorList* ignore_list,
                                               OverlayPage& base_page,
                                               OverlaySelector& overlay_selector,
                                               bool include_metadata_overwrites = true,
                                               bool metadata_tile_present = true) {
    if (!overlay_page.pixels.data || !base_page.pixels.data ||
        overlay_page.width != base_page.width ||
        overlay_page.height != base_page.height ||
        overlay_selector.overlay_erasure_budget == 0u ||
        overlay_selector.block_total == 0u ||
        overlay_selector.block_count == 0u) {
        return false;
    }
    if (base_page.height &&
        (usize)base_page.width > USIZE_MAX_VALUE / (usize)base_page.height) {
        return false;
    }
    usize total_pixels = (usize)base_page.width * (usize)base_page.height;
    if (total_pixels > USIZE_MAX_VALUE / 3u ||
        base_page.pixels.size < total_pixels * 3u ||
        overlay_page.pixels.size < total_pixels * 3u ||
        base_mask.size != total_pixels ||
        !base_mask.data) {
        return false;
    }
    u32 apply_height = base_page.data_height;
    if (apply_height == 0u) return true;

    const MetadataTile::Values& tile = base_page.metadata.encoded_tile;
    if (!base_page.metadata.has_encoded_tile ||
        !metadata_schema_is_current(tile.schema_version) ||
        !tile.overwrite_layout || !tile.ecc_enabled ||
        tile.ecc_block_data == 0u || tile.ecc_parity == 0u ||
        tile.ecc_block_count != overlay_selector.block_count ||
        tile.ecc_block_data + tile.ecc_parity != overlay_selector.block_total ||
        tile.page_bits == 0u || tile.page_index == 0u) {
        return false;
    }
    include_metadata_overwrites = include_metadata_overwrites &&
        (!base_page.use_custom_palette ||
         is_power_of_two_u32(base_page.custom_mapping.custom_palette_count));
    u64 bits_per_pixel = base_page.use_custom_palette
                             ? (u64)base_page.custom_symbol_bits
                             : (u64)bits_per_sample(base_page.color_mode) *
                                   (u64)color_mode_samples_per_pixel(base_page.color_mode);
    if (!bits_per_pixel || bits_per_pixel > 64u) return false;
    bool grouped_base_palette = base_page.use_custom_palette &&
        !is_power_of_two_u32(base_page.custom_mapping.custom_palette_count);
    u32 grouped_base_bits = grouped_base_palette
                                ? custom_palette_group_bit_count(
                                      base_page.custom_mapping.custom_palette_count)
                                : 0u;
    u64 overlay_unit_bits = grouped_base_palette ? grouped_base_bits : bits_per_pixel;
    if (!overlay_unit_bits) return false;

    u64 block_total_symbols = (u64)overlay_selector.block_total;
    if (block_total_symbols > U64_MAX_VALUE / overlay_selector.block_count) return false;
    u64 encoded_block_bytes = block_total_symbols * overlay_selector.block_count;
    if (encoded_block_bytes > (u64)USIZE_MAX_VALUE / (u64)sizeof(usize) ||
        !overlay_selector.ranks.data ||
        overlay_selector.ranks.size != (usize)encoded_block_bytes) return false;
    usize shuffle_bytes = (usize)(encoded_block_bytes * (u64)sizeof(usize));
    makocode::ByteBuffer shuffle_map;
    if (!shuffle_map.ensure(shuffle_bytes)) return false;
    shuffle_map.size = shuffle_bytes;
    usize* shuffle_ptr = (usize*)shuffle_map.data;
    for (u64 entry = 0u; entry < encoded_block_bytes; ++entry) {
        shuffle_ptr[entry] = (usize)entry;
    }
    makocode::Pcg64Generator shuffle_rng;
    shuffle_rng.seed(0u);
    for (u64 remaining = encoded_block_bytes; remaining > 1u; --remaining) {
        usize i = (usize)(remaining - 1u);
        usize j = (usize)(shuffle_rng.next() % remaining);
        usize temp = shuffle_ptr[i];
        shuffle_ptr[i] = shuffle_ptr[j];
        shuffle_ptr[j] = temp;
    }

    u64 rs_region_start = 0u;
    u64 rs_region_end = encoded_block_bytes;

    makocode::ByteBuffer payload_rank_map;
    makocode::ByteBuffer payload_rank_reservation_mask;
    const u8* reservation_data = base_mask.data;
    usize reservation_size = base_mask.size;
    if (metadata_tile_present && include_metadata_overwrites) {
        if (!build_overlay_payload_rank_reservation_mask(base_page,
                                                         payload_rank_reservation_mask)) {
            return false;
        }
        reservation_data = payload_rank_reservation_mask.data;
        reservation_size = payload_rank_reservation_mask.size;
    }
    u64 total_data_pixels = (u64)base_page.width * (u64)apply_height;
    if (total_data_pixels > (u64)USIZE_MAX_VALUE ||
        !reservation_data || reservation_size < (usize)total_data_pixels) {
        return false;
    }
    u64 fiducial_pixels = 0u;
    for (usize pixel = 0u; pixel < (usize)total_data_pixels; ++pixel) {
        if (reservation_data[pixel]) ++fiducial_pixels;
    }
    if (fiducial_pixels > total_data_pixels) return false;
    u64 usable_data_pixels = total_data_pixels - fiducial_pixels;
    if (usable_data_pixels > U64_MAX_VALUE / bits_per_pixel) return false;
    u64 bits_per_page = usable_data_pixels * bits_per_pixel;
    if (bits_per_page == 0u ||
        tile.page_index - 1u > U64_MAX_VALUE / bits_per_page) {
        return false;
    }
    u64 page_bit_offset = (tile.page_index - 1u) * bits_per_page;
    if (page_bit_offset >= tile.page_bits) {
        for (u32 row = 0u; row < apply_height; ++row) {
            for (u32 column = 0u; column < base_page.width; ++column) {
                usize pixel_index = (usize)row * (usize)base_page.width + (usize)column;
                if (base_mask.data[pixel_index]) continue;
                usize byte_index = pixel_index * 3u;
                if (ignore_list &&
                    overlay_color_matches_ignore(*ignore_list,
                                                 overlay_page.pixels.data[byte_index],
                                                 overlay_page.pixels.data[byte_index + 1u],
                                                 overlay_page.pixels.data[byte_index + 2u])) {
                    continue;
                }
                memcpy(base_page.pixels.data + byte_index,
                       overlay_page.pixels.data + byte_index,
                       3u);
            }
        }
        return true;
    }
    u64 centered_segment_bits = tile.page_bits - page_bit_offset;
    if (centered_segment_bits > bits_per_page) centered_segment_bits = bits_per_page;
    MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(
        base_page.width, apply_height);
    u64 payload_pixel_count = 0u;
    if (!MetadataTile::build_payload_rank_map(base_page.width,
                                              apply_height,
                                              placement,
                                              tile,
                                              reservation_data,
                                              reservation_size,
                                              payload_rank_map,
                                              payload_pixel_count,
                                              metadata_tile_present
                                                  ? include_metadata_overwrites
                                                  : true) ||
        payload_pixel_count == 0u) {
        return false;
    }
    u64 required_payload_pixels = centered_segment_bits / overlay_unit_bits +
        ((centered_segment_bits % overlay_unit_bits) ? 1u : 0u);
    if (grouped_base_palette) {
        if (required_payload_pixels > U64_MAX_VALUE / 4u) return false;
        required_payload_pixels *= 4u;
    }
    if (payload_pixel_count < required_payload_pixels) return false;
    const u64* payload_ranks = (const u64*)payload_rank_map.data;

    struct OverlayPixelCandidate {
        u64 bit_start;
        usize pixel_index;
    };
    if (total_pixels > USIZE_MAX_VALUE / sizeof(OverlayPixelCandidate)) return false;
    makocode::ByteBuffer candidate_buffer;
    if (!candidate_buffer.ensure(total_pixels * sizeof(OverlayPixelCandidate))) return false;
    OverlayPixelCandidate* candidates = (OverlayPixelCandidate*)candidate_buffer.data;
    usize candidate_count = 0u;
    for (u32 row = 0u; row < apply_height; ++row) {
        for (u32 column = 0u; column < base_page.width; ++column) {
            usize pixel_index = (usize)row * (usize)base_page.width + (usize)column;
            if (base_mask.data[pixel_index] == OVERWRITE_MASK_PIXEL) continue;
            u64 payload_rank = payload_ranks[pixel_index];
            if (payload_rank == U64_MAX_VALUE) {
                continue;
            }
            if ((grouped_base_palette &&
                 payload_rank / 4u > U64_MAX_VALUE / grouped_base_bits) ||
                (!grouped_base_palette &&
                 payload_rank > U64_MAX_VALUE / bits_per_pixel)) continue;
            u64 local_bit = grouped_base_palette
                                ? (payload_rank / 4u) * grouped_base_bits
                                : payload_rank * bits_per_pixel;
            if (local_bit >= centered_segment_bits ||
                local_bit > U64_MAX_VALUE - page_bit_offset) continue;
            usize byte_index = pixel_index * 3u;
            if (ignore_list &&
                overlay_color_matches_ignore(*ignore_list,
                                             overlay_page.pixels.data[byte_index],
                                             overlay_page.pixels.data[byte_index + 1u],
                                             overlay_page.pixels.data[byte_index + 2u])) {
                continue;
            }
            candidates[candidate_count].pixel_index = pixel_index;
            candidates[candidate_count].bit_start = page_bit_offset + local_bit;
            ++candidate_count;
        }
    }
    if (candidate_count > 1u) {
        makocode::Pcg64Generator candidate_rng;
        candidate_rng.seed(0u);
        for (usize remaining = candidate_count; remaining > 1u; --remaining) {
            usize i = remaining - 1u;
            usize j = (usize)(candidate_rng.next() % remaining);
            OverlayPixelCandidate temp = candidates[i];
            candidates[i] = candidates[j];
            candidates[j] = temp;
        }
    }
    if (overlay_selector.counts.size != (usize)overlay_selector.block_count) return false;
    for (usize candidate_index = 0u; candidate_index < candidate_count; ++candidate_index) {
        const OverlayPixelCandidate& candidate = candidates[candidate_index];
        usize pixel_index = candidate.pixel_index;
        usize byte_index = pixel_index * 3u;
        if (base_page.pixels.data[byte_index] == overlay_page.pixels.data[byte_index] &&
            base_page.pixels.data[byte_index + 1u] == overlay_page.pixels.data[byte_index + 1u] &&
            base_page.pixels.data[byte_index + 2u] == overlay_page.pixels.data[byte_index + 2u]) {
            continue;
        }
        if (candidate.bit_start > U64_MAX_VALUE - overlay_unit_bits) continue;
        u64 byte_start = candidate.bit_start >> 3u;
        u64 bit_end = candidate.bit_start + overlay_unit_bits;
        u64 segment_end = page_bit_offset + centered_segment_bits;
        if (bit_end > segment_end) bit_end = segment_end;
        if (bit_end > U64_MAX_VALUE - 7u) continue;
        u64 byte_end = (bit_end + 7u) >> 3u;
        if (byte_start < rs_region_start || byte_end > rs_region_end || byte_end <= byte_start) {
            continue;
        }
        u64 pending_blocks[8];
        u8 pending_counts[8];
        usize pending_count = 0u;
        bool selector_allowed = true;
        for (u64 byte_index = byte_start; byte_index < byte_end; ++byte_index) {
            u64 relative = byte_index - rs_region_start;
            if (relative >= encoded_block_bytes) {
                selector_allowed = false;
                break;
            }
            u64 original_index = (u64)shuffle_ptr[(usize)relative];
            if (original_index >= overlay_selector.ranks.size) {
                selector_allowed = false;
                break;
            }
            u64 block_index = original_index / overlay_selector.block_total;
            u8 rank = overlay_selector.ranks.data[(usize)original_index];
            if (rank == 0u || rank > overlay_selector.overlay_erasure_budget) {
                selector_allowed = false;
                break;
            }
            usize pending_index = 0u;
            while (pending_index < pending_count && pending_blocks[pending_index] != block_index) {
                ++pending_index;
            }
            if (pending_index == pending_count) {
                if (pending_count >= sizeof(pending_blocks) / sizeof(pending_blocks[0])) {
                    selector_allowed = false;
                    break;
                }
                pending_blocks[pending_count] = block_index;
                pending_counts[pending_count] = rank;
                ++pending_count;
            } else if (rank > pending_counts[pending_index]) {
                pending_counts[pending_index] = rank;
            }
        }
        for (usize i = 0u; selector_allowed && i < pending_count; ++i) {
            if (pending_blocks[i] >= overlay_selector.counts.size) selector_allowed = false;
        }
        if (!selector_allowed) continue;
        for (usize i = 0u; i < pending_count; ++i) {
            usize block = (usize)pending_blocks[i];
            if (pending_counts[i] > overlay_selector.counts.data[block]) {
                overlay_selector.counts.data[block] = pending_counts[i];
            }
        }
        memcpy(base_page.pixels.data + byte_index,
               overlay_page.pixels.data + byte_index,
               3u);
    }
    return true;
}

static bool serialize_overlay_page(const OverlayPage& page,
                                   makocode::ByteBuffer& output) {
    output.release();
    if (!ppm_write_metadata_header(page.metadata, output)) {
        return false;
    }
    if (!ppm_write_dimensions((u64)page.width, (u64)page.height, output)) {
        return false;
    }
    usize total_bytes = (usize)page.width * (usize)page.height * 3u;
    return output.append_bytes(page.pixels.data, total_bytes);
}


static bool merge_overlay_into_encoded_page(const char* page_path,
                                            const OverlayPage& overlay_page,
                                            const char* ignore_colors,
                                            usize ignore_colors_length,
                                            bool debug_requested,
                                            const MetadataTile::Values* tile_override,
                                            const ImageMappingConfig* mapping_override,
                                            bool include_metadata_overwrites) {
    OverlayPage base_page;
    if (!load_overlay_page(page_path, base_page)) return false;
    if (base_page.width != overlay_page.width ||
        base_page.height != overlay_page.height) {
        console_line(2, "encode: --overlay-ppm dimensions must match the encoded pages");
        return false;
    }
    PpmParserState output_metadata = base_page.metadata;
    if (tile_override) {
        base_page.metadata.has_encoded_tile = true;
        base_page.metadata.encoded_tile = *tile_override;
        base_page.metadata.has_fiducial_spacing = true;
        base_page.metadata.fiducial_spacing_value = tile_override->fiducial_spacing_pixels;
        base_page.metadata.has_fiducial_columns = true;
        base_page.metadata.fiducial_columns_value = tile_override->fiducial_columns;
        base_page.metadata.has_fiducial_rows = true;
        base_page.metadata.fiducial_rows_value = tile_override->fiducial_rows;
        base_page.data_height = tile_override->page_height_pixels - tile_override->footer_rows;
        base_page.footer_rows = tile_override->footer_rows;
        if (mapping_override) {
            base_page.use_custom_palette = mapping_has_custom_palette(*mapping_override);
            base_page.custom_mapping = *mapping_override;
            base_page.color_mode = mapping_override->color_channels;
            base_page.custom_symbol_bits = 0u;
            if (base_page.use_custom_palette) {
                u32 palette_count = mapping_override->custom_palette_count;
                while (palette_count >= 2u) {
                    palette_count >>= 1u;
                    ++base_page.custom_symbol_bits;
                }
            }
        }
    }
    OverlayIgnoreColorList ignored_colors;
    ignored_colors.count = 0u;
    if (ignore_colors && ignore_colors_length > 0u &&
        !parse_overlay_ignore_colors(ignore_colors,
                                     ignore_colors_length,
                                     ignored_colors)) return false;
    const OverlayIgnoreColorList* ignored_colors_ptr = ignored_colors.count
                                                          ? &ignored_colors
                                                          : 0;

    makocode::ByteBuffer base_mask;
    u64 base_reserved = 0u;
    if (!build_overlay_mask(base_page, base_mask, base_reserved, !tile_override)) {
        console_line(2, "encode: failed to prepare overlay masks");
        return false;
    }
    if (!base_page.metadata.has_encoded_tile ||
        !metadata_schema_is_current(base_page.metadata.encoded_tile.schema_version) ||
        !base_page.metadata.encoded_tile.overwrite_layout) {
        console_line(2, "encode: overlay requires canonical encoded pages");
        return false;
    }
    const MetadataTile::Values& tile = base_page.metadata.encoded_tile;
    if (!tile.ecc_enabled || tile.ecc_block_count == 0u || tile.ecc_parity == 0u ||
        tile.ecc_block_data == 0u || tile.overlay_erasure_budget == 0u ||
        (u64)tile.ecc_block_data + tile.ecc_parity > makocode::RS_FIELD_SIZE ||
        tile.overlay_erasure_budget > tile.ecc_parity) {
        console_line(2, "encode: overlay requires a nonzero, valid ECC selector budget");
        return false;
    }

    OverlaySelector overlay_selector;
    overlay_selector.seed = tile.overlay_selector_seed;
    overlay_selector.overlay_erasure_budget = tile.overlay_erasure_budget;
    overlay_selector.block_total = (u16)(tile.ecc_block_data + tile.ecc_parity);
    overlay_selector.block_count = tile.ecc_block_count;
    if ((u32)overlay_selector.overlay_erasure_budget > tile.ecc_parity ||
        overlay_selector.block_count > (u64)USIZE_MAX_VALUE ||
        overlay_selector.block_count > (u64)USIZE_MAX_VALUE / overlay_selector.block_total ||
        !overlay_selector.counts.ensure((usize)overlay_selector.block_count) ||
        !overlay_selector.ranks.ensure((usize)(overlay_selector.block_count *
                                               overlay_selector.block_total))) {
        console_line(2, "encode: overlay selector map is too large or exceeds ECC capacity");
        return false;
    }
    overlay_selector.counts.size = (usize)overlay_selector.block_count;
    memset(overlay_selector.counts.data, 0, overlay_selector.counts.size);
    overlay_selector.ranks.size = (usize)(overlay_selector.block_count *
                                          overlay_selector.block_total);
    u8 selector_mask[makocode::RS_FIELD_SIZE];
    u8 selector_ranks[makocode::RS_FIELD_SIZE];
    for (u64 block = 0u; block < overlay_selector.block_count; ++block) {
        if (!makocode::overlay_selector_mask(overlay_selector.seed,
                                             block,
                                             overlay_selector.block_total,
                                             (u8)overlay_selector.overlay_erasure_budget,
                                             selector_mask,
                                             selector_ranks)) {
            console_line(2, "encode: failed to generate deterministic overlay selector map");
            return false;
        }
        usize offset = (usize)block * (usize)overlay_selector.block_total;
        memcpy(overlay_selector.ranks.data + offset,
               selector_ranks,
               overlay_selector.block_total);
    }
    if (!apply_overlay_pixels_with_selector(overlay_page,
                                             base_mask,
                                             ignored_colors_ptr,
                                             base_page,
                                             overlay_selector,
                                             include_metadata_overwrites,
                                             !tile_override)) {
        console_line(2, "encode: failed to apply overlay PPM");
        return false;
    }
    if (debug_requested) {
        char budget_buffer[32];
        u64_to_ascii(tile.overlay_erasure_budget, budget_buffer, sizeof(budget_buffer));
        console_write(2, "debug: overlay erasure budget per block=");
        console_line(2, budget_buffer);
    }
    makocode::ByteBuffer output;
    if (tile_override) base_page.metadata = output_metadata;
    if (!serialize_overlay_page(base_page, output) ||
        !write_bytes_to_file(page_path, output.data, output.size)) {
        console_line(2, "encode: failed to write overlay page");
        return false;
    }
    return true;
}

