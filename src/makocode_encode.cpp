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
                                            const char* selector_password,
                                            usize selector_password_length,
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
            const double maximum_overlay_fraction = 254.0 / 255.0;
            if (maximum_overlay_fraction - overlay_fraction < 1.0e-9) {
                overlay_fraction = maximum_overlay_fraction;
            }
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
                (long double)ratio_numerator / (long double)ratio_denominator > 764.0L) {
                console_line(2, "encode: --ecc ratio must be between 0 and 764");
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
    if (ecc_mode == ECC_EXPLICIT_RATIO && ecc_redundancy == 0.0) {
        console_line(2, overlay_fraction > 0.0
                              ? "encode: overlay requires ECC"
                              : "encode: ECC cannot cover structural erasures or scan errors when --ecc=0");
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
    if (have_password && !hidden_mode_enabled &&
        !makocode::palette_symbol_password_selector_seed(
            (const char*)password_buffer.data, password_buffer.size,
            mapping.symbol_placement_seed)) {
        console_line(2, "encode: failed to derive password-keyed symbol placement");
        return 1;
    }
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
    if (have_password) {
        usize container_overhead = MetadataTile::STEALTH_CONTAINER_HEADER_BYTES +
                                   MetadataTile::ENCRYPTION_TAG_BYTES;
        if (ecc_payload_bytes > USIZE_MAX_VALUE - container_overhead) {
            console_line(2, "encode: encrypted payload size overflow");
            return 1;
        }
        ecc_payload_bytes += container_overhead;
    }
    if (!mapping.page_width_set || !mapping.page_height_set) {
        u32 sizing_palette_size = mapping_has_custom_palette(mapping)
                                      ? mapping.custom_palette_count
                                      : (1u << bits_per_sample(mapping.color_channels));
        makocode::PaletteSymbolProfile sizing_profile;
        makocode::EccSummary initial_sizing_ecc;
        if (!makocode::palette_symbol_profile(sizing_palette_size, sizing_profile)) {
            console_line(2, "encode: unsupported palette-symbol profile");
            return 1;
        }
        u32 sizing_parity = 0u;
        if (ecc_redundancy > 0.0) {
            sizing_parity = (u32)ceill((long double)sizing_profile.block_length *
                                       ecc_redundancy / (1.0L + ecc_redundancy));
            if (sizing_parity < 2u) sizing_parity = 2u;
        } else if (ecc_mode != ECC_EXPLICIT_RATIO) {
            sizing_parity = 8u;
        }
        u32 sizing_overlay = (u32)floor((double)sizing_profile.block_length *
                                         overlay_fraction);
        if (sizing_overlay > sizing_parity) sizing_parity = sizing_overlay;
        if (sizing_parity >= sizing_profile.block_length) {
            sizing_parity = sizing_profile.block_length - 1u;
        }
        u32 sizing_data = sizing_profile.block_length - sizing_parity;
        u64 sizing_source_symbols = makocode::palette_symbol_count_for_bytes(
            sizing_profile, ecc_payload_bytes);
        if (!sizing_data || sizing_source_symbols == U64_MAX_VALUE ||
            !sizing_source_symbols ||
            sizing_source_symbols > U64_MAX_VALUE - (sizing_data - 1u)) {
            console_line(2, "encode: failed to size palette-symbol stream");
            return 1;
        }
        u64 sizing_block_count =
            (sizing_source_symbols + sizing_data - 1u) / sizing_data;
        if (!sizing_block_count ||
            sizing_block_count > (U64_MAX_VALUE - makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS) /
                                     sizing_profile.block_length ||
            sizing_block_count * sizing_profile.block_length +
                    makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS > U64_MAX_VALUE / 32u) {
            console_line(2, "encode: palette-symbol stream size overflow");
            return 1;
        }
        initial_sizing_ecc.original_bytes = (u64)ecc_payload_bytes;
        initial_sizing_ecc.enabled = sizing_parity > 0u;
        initial_sizing_ecc.palette_symbol_stream = true;
        initial_sizing_ecc.block_data_symbols = (u16)sizing_data;
        initial_sizing_ecc.parity_symbols = (u16)sizing_parity;
        initial_sizing_ecc.block_count = sizing_block_count;
        initial_sizing_ecc.overlay_erasure_budget = (u16)sizing_overlay;
        initial_sizing_ecc.unknown_error_budget =
            (u16)((sizing_parity - sizing_overlay) / 2u);
        u64 estimated_frame_bits =
            (makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS +
             sizing_block_count * sizing_profile.block_length) * 32u;
        if (!fit_page_dimensions(mapping,
                                 footer_config,
                                 estimated_frame_bits,
                                 !mapping.page_width_set,
                                 !mapping.page_height_set,
                                 width_pixels,
                                 height_pixels,
                                 &initial_sizing_ecc,
                                 0.0,
                                 initial_sizing_ecc.unknown_error_budget,
                                 initial_sizing_ecc.overlay_erasure_budget)) {
            console_line(2, "encode: failed to fit automatic page dimensions");
            return 1;
        }
    }
    makocode::ByteBuffer stealth_container;
    if (have_password) {
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
        u32 capacity_palette_size = mapping_has_custom_palette(mapping)
                                        ? mapping.custom_palette_count
                                        : (1u << bits_per_sample(mapping.color_channels));
        makocode::PaletteSymbolProfile capacity_profile;
        if (!makocode::palette_symbol_profile(capacity_palette_size, capacity_profile)) {
            console_line(2, "encode: unsupported palette-symbol profile");
            return 1;
        }
        u32 capacity_overlay = (u32)floor(
            (double)capacity_profile.block_length * overlay_fraction);
        u32 capacity_available_errors =
            (capacity_profile.block_length - 1u - capacity_overlay) / 2u;
        u16 capacity_required_errors = 0u;
        if (ecc_mode == ECC_AUTO_MINIMUM) {
            capacity_required_errors = (u16)(capacity_available_errors < 85u
                                                 ? capacity_available_errors : 85u);
        }
        u32 metadata_ecc_minimum_parity_bytes =
            ecc_mode == ECC_AUTO_MINIMUM ? 8u : 2u;
        double automatic_capacity_redundancy =
            (double)(2u * (u32)capacity_required_errors + capacity_overlay) /
            (double)(capacity_profile.block_length -
                     2u * (u32)capacity_required_errors - capacity_overlay);
        double capacity_redundancy = ecc_mode == ECC_EXPLICIT_RATIO
                                         ? ecc_redundancy
                                         : (ecc_mode == ECC_AUTO_MINIMUM
                                                ? automatic_capacity_redundancy : 3.0);
        auto container_fits_pages = [&](u64 byte_count, u64 target_page_count) {
            EccFillCandidate candidate;
            return byte_count <= (u64)USIZE_MAX_VALUE &&
                   evaluate_ecc_fill_candidate((usize)byte_count,
                                               capacity_redundancy,
                                               mapping,
                                               footer_config,
                                               width_pixels,
                                               height_pixels,
                                               !mapping.page_width_set,
                                               !mapping.page_height_set,
                                               0.0,
                                               overlay_fraction,
                                               candidate,
                                               metadata_ecc_minimum_parity_bytes,
                                               capacity_required_errors) &&
                   candidate.page_count <= target_page_count;
        };
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
                u64 low = 0u;
                u64 high = total_capacity_bits / 8u;
                if (high > (u64)USIZE_MAX_VALUE) high = (u64)USIZE_MAX_VALUE;
                while (container_ok && low < high) {
                    u64 middle = low + (high - low + 1u) / 2u;
                    if (container_fits_pages(middle, capacity_pages)) low = middle;
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
        if (container_ok && hidden_mode_enabled && !hidden_fill) {
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
                if (container_fits_pages(carrier_minimum_size, target_page_count)) break;
                ++target_page_count;
                if (target_page_count == 1000000u) container_ok = false;
            }
            if (container_ok) {
                u64 low = 0u;
                u64 high = total_capacity_bits / 8u;
                if (high > (u64)USIZE_MAX_VALUE) high = (u64)USIZE_MAX_VALUE;
                while (low < high) {
                    u64 middle = low + (high - low + 1u) / 2u;
                    if (container_fits_pages(middle, target_page_count)) low = middle;
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
            u8 visible_descriptor_plain[MetadataTile::STEALTH_VISIBLE_DESCRIPTOR_PLAINTEXT_BYTES];
            write_le_u64(visible_descriptor_plain, (u64)visible_body.size);
            write_le_u64(visible_descriptor_plain + 8u, carrier_size);
            container_ok = MetadataTile::stealth_encrypt_blob(visible_descriptor_plain,
                                                sizeof(visible_descriptor_plain),
                                                visible_key, nonce_seed, prefix, sizeof(prefix),
                                                "visible descriptor", visible_descriptor);
            if (container_ok && hidden_input_count) {
                u8 hidden_descriptor_plain[MetadataTile::STEALTH_HIDDEN_DESCRIPTOR_PLAINTEXT_BYTES];
                write_le_u64(hidden_descriptor_plain,
                             (u64)MetadataTile::STEALTH_CONTAINER_HEADER_BYTES + (u64)visible_body.size);
                write_le_u64(hidden_descriptor_plain + 8u, (u64)hidden_body.size);
                write_le_u64(hidden_descriptor_plain + 16u, hidden_capacity);
                write_le_u64(hidden_descriptor_plain + 24u, carrier_size);
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
            console_line(2, "encode: failed to construct protected data section");
            return 1;
        }
        ecc_payload_bytes = stealth_container.size;
    } else {
        compressed_for_sizing.release();
    }
    double structural_erasure_fraction = 0.0;
    double metadata_ecc_redundancy = 0.0;
    // Keep auto-minimum scans resilient after removing the fixed LZMA size header.
    u32 metadata_ecc_minimum_parity_bytes =
        ecc_mode == ECC_AUTO_MINIMUM ? 8u : 2u;
    u32 requirement_palette_size = mapping_has_custom_palette(mapping)
                                       ? mapping.custom_palette_count
                                       : (1u << bits_per_sample(mapping.color_channels));
    makocode::PaletteSymbolProfile requirement_profile;
    if (!makocode::palette_symbol_profile(requirement_palette_size,
                                          requirement_profile)) {
        console_line(2, "encode: unsupported palette-symbol profile");
        return 1;
    }
    double maximum_ecc_ratio = (double)(requirement_profile.block_length - 1u);
    u16 required_unknown_errors = 0u;
    if (ecc_mode == ECC_AUTO_MINIMUM) {
        u32 requirement_overlay = (u32)floor(
            (double)requirement_profile.block_length * overlay_fraction);
        u32 maximum_parity = requirement_profile.block_length - 1u;
        u32 available_errors = (maximum_parity - requirement_overlay) / 2u;
        required_unknown_errors = (u16)(available_errors < 85u ? available_errors : 85u);
    }
    if (footer_config.over_barcode && footer_config.text_length) {
        u64 maximum_digit_pixels = 0u;
        for (char digit = '0'; digit <= '9'; ++digit) {
            const GlyphPattern* glyph = footer_lookup_glyph(digit);
            if (!glyph) {
                console_line(2, "encode: over-barcode footer contains an unsupported glyph");
                return 1;
            }
            u64 lit_pixels = 0u;
            for (u32 row = 0u; row < FOOTER_BASE_GLYPH_HEIGHT; ++row) {
                for (u32 column = 0u; column < FOOTER_BASE_GLYPH_WIDTH; ++column) {
                    if (glyph->rows[row][column] == '1') ++lit_pixels;
                }
            }
            if (lit_pixels > maximum_digit_pixels) maximum_digit_pixels = lit_pixels;
        }
        u64 footer_lit_pixels = 0u;
        for (usize i = 0u; i < footer_config.text_length;) {
            if (footer_config.text[i] == '\\' && i + 1u < footer_config.text_length &&
                footer_config.text[i + 1u] == '%') {
                ++i;
            } else if (footer_config.text[i] == '%' && i + 4u < footer_config.text_length &&
                       footer_config.text[i + 1u] == 'p' && footer_config.text[i + 2u] == 'a' &&
                       footer_config.text[i + 3u] == 'g' && footer_config.text[i + 4u] == 'e') {
                if (maximum_digit_pixels && footer_lit_pixels >
                    U64_MAX_VALUE - maximum_digit_pixels * 20u) return 1;
                footer_lit_pixels += maximum_digit_pixels * 20u;
                i += 5u;
                continue;
            } else if (footer_config.text[i] == '%' && i + 10u < footer_config.text_length &&
                       footer_config.text[i + 1u] == 't' && footer_config.text[i + 2u] == 'o' &&
                       footer_config.text[i + 3u] == 't' && footer_config.text[i + 4u] == 'a' &&
                       footer_config.text[i + 5u] == 'l' && footer_config.text[i + 6u] == 'p' &&
                       footer_config.text[i + 7u] == 'a' && footer_config.text[i + 8u] == 'g' &&
                       footer_config.text[i + 9u] == 'e' && footer_config.text[i + 10u] == 's') {
                if (maximum_digit_pixels && footer_lit_pixels >
                    U64_MAX_VALUE - maximum_digit_pixels * 20u) return 1;
                footer_lit_pixels += maximum_digit_pixels * 20u;
                i += 11u;
                continue;
            }
            const GlyphPattern* glyph = footer_lookup_glyph(footer_config.text[i]);
            if (!glyph) {
                console_line(2, "encode: over-barcode footer contains an unsupported glyph");
                return 1;
            }
            u64 lit_pixels = 0u;
            for (u32 row = 0u; row < FOOTER_BASE_GLYPH_HEIGHT; ++row) {
                for (u32 column = 0u; column < FOOTER_BASE_GLYPH_WIDTH; ++column) {
                    if (glyph->rows[row][column] == '1') ++lit_pixels;
                }
            }
            if (footer_lit_pixels > U64_MAX_VALUE - lit_pixels) return 1;
            footer_lit_pixels += lit_pixels;
            ++i;
        }
        u64 font_area = (u64)footer_config.font_size * footer_config.font_size;
        if (font_area && footer_lit_pixels > U64_MAX_VALUE / font_area) return 1;
        footer_lit_pixels *= font_area;
        if (footer_lit_pixels > 0xFFFFu) {
            console_line(2, "encode: over-barcode footer exceeds the supported ECC error budget");
            return 1;
        }
        if (footer_lit_pixels > required_unknown_errors) {
            required_unknown_errors = (u16)footer_lit_pixels;
        }
    }
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
        double low_ratio = overlay_fraction >= 0.9 ? maximum_ecc_ratio : 0.01;
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
                                          metadata_ecc_minimum_parity_bytes,
                                          required_unknown_errors);
        if (!found_minimum) {
            for (u32 attempt = 0u; attempt < 16u && high_ratio < maximum_ecc_ratio; ++attempt) {
                low_ratio = high_ratio;
                high_ratio *= 2.0;
                if (high_ratio > maximum_ecc_ratio) high_ratio = maximum_ecc_ratio;
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
                                          metadata_ecc_minimum_parity_bytes,
                                          required_unknown_errors);
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
                                                    metadata_ecc_minimum_parity_bytes,
                                                    required_unknown_errors)) {
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
                                maximum_ecc_ratio,
                                mapping,
                                footer_config,
                                width_pixels,
                                height_pixels,
                                !mapping.page_width_set,
                                !mapping.page_height_set,
                                structural_erasure_fraction,
                                overlay_fraction,
                                required_unknown_errors,
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
        ecc_redundancy = best_ratio;
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
    }
    metadata_ecc_redundancy = ecc_redundancy < 0.25 ? ecc_redundancy : 0.25;
    u32 symbol_palette_size = mapping_has_custom_palette(mapping)
                                  ? mapping.custom_palette_count
                                  : (1u << bits_per_sample(mapping.color_channels));
    makocode::PaletteSymbolProfile symbol_profile;
    if (!makocode::palette_symbol_profile(symbol_palette_size, symbol_profile)) {
        console_line(2, "encode: unsupported palette-symbol profile");
        return 1;
    }
    u32 symbol_parity = 0u;
    if (!makocode::palette_symbol_parity_for_redundancy(
            symbol_profile, ecc_redundancy, symbol_parity)) {
        console_line(2, "encode: invalid palette-symbol ECC ratio");
        return 1;
    }
    u16 symbol_overlay_budget = 0u;
    if (overlay_fraction > 0.0) {
        symbol_overlay_budget = (u16)floor((double)symbol_profile.block_length *
                                           overlay_fraction);
    }
    if (symbol_overlay_budget > symbol_parity) {
        console_line(2, "encode: palette-symbol ECC cannot cover the requested overlay");
        return 1;
    }
    u16 symbol_unknown_budget = (u16)((symbol_parity - symbol_overlay_budget) / 2u);
    overlay_erasure_budget = symbol_overlay_budget;
    unknown_parity_budget = symbol_unknown_budget;
    if (unknown_parity_budget < required_unknown_errors) {
        console_line(2, "encode: ECC cannot cover over-barcode footer errors");
        return 1;
    }

    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = ecc_redundancy;
    encoder.config.overlay_erasure_budget = overlay_erasure_budget;
    encoder.config.unknown_error_budget = unknown_parity_budget;
    encoder.config.palette_size = symbol_palette_size;
    encoder.config.password = have_password && !hidden_mode_enabled
                                  ? (const char*)password_buffer.data : (const char*)0;
    encoder.config.password_length = have_password && !hidden_mode_enabled
                                        ? password_buffer.size : 0u;
    if (have_password) {
        encoder.payload_is_preencoded = true;
    }
    if (!encoder.set_payload(have_password ? stealth_container.data : archive.buffer.data,
                             have_password ? stealth_container.size : archive.buffer.size)) {
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
        bool fill_capacity_ready = false;
        if (page_ecc_summary.palette_symbol_stream) {
            u32 fill_data_height = height_pixels;
            u64 fill_page_count = 0u;
            fill_capacity_ready = compute_page_layout(mapping,
                                                       fill_footer,
                                                       frame_bit_count,
                                                       width_pixels,
                                                       height_pixels,
                                                       fill_layout,
                                                       fill_data_height,
                                                       fill_bits_per_page,
                                                       fill_page_count,
                                                       &page_ecc_summary,
                                                       metadata_ecc_redundancy,
                                                       unknown_parity_budget,
                                                       overlay_erasure_budget) &&
                                  fill_page_count <= target_page_count;
        } else {
            fill_capacity_ready = compute_footer_layout(width_pixels,
                                                        height_pixels,
                                                        fill_footer,
                                                        fill_layout) &&
                                  compute_bits_per_page(mapping,
                                                        width_pixels,
                                                        height_pixels,
                                                        fill_layout.data_height_pixels,
                                                        fill_bits_per_page);
        }
        if (!fill_capacity_ready ||
            !fill_bits_per_page || fill_bits_per_page > U64_MAX_VALUE / target_page_count) {
            console_line(2, "encode: unable to determine --fill-pages capacity");
            return 1;
        }
        u64 fill_alignment = page_ecc_summary.palette_symbol_stream ? 31ull : 7ull;
        u64 target_bits = (fill_bits_per_page * target_page_count) & ~fill_alignment;
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
            if (page_ecc_summary.palette_symbol_stream) {
                makocode::PaletteSymbolProfile fill_profile;
                u32 palette_size = mapping_has_custom_palette(mapping)
                                       ? mapping.custom_palette_count
                                       : (1u << bits_per_sample(mapping.color_channels));
                if (!makocode::palette_symbol_profile(palette_size, fill_profile)) {
                    console_line(2, "encode: unsupported palette-symbol filler profile");
                    return 1;
                }
                for (usize i = original_bytes; i < target_bytes; i += 4u) {
                    u32 random_value = (u32)random_fill.data[i] |
                                       ((u32)random_fill.data[i + 1u] << 8u) |
                                       ((u32)random_fill.data[i + 2u] << 16u) |
                                       ((u32)random_fill.data[i + 3u] << 24u);
                    u32 symbol = random_value % fill_profile.field_prime;
                    frame_bits.data[i] = (u8)symbol;
                    frame_bits.data[i + 1u] = (u8)(symbol >> 8u);
                    frame_bits.data[i + 2u] = (u8)(symbol >> 16u);
                    frame_bits.data[i + 3u] = (u8)(symbol >> 24u);
                }
            } else {
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
        }
        frame_bits.size = target_bytes;
        frame_bit_count = target_bits;
        char fill_capacity_text[32], fill_target_text[32], fill_pages_text[32];
        u64_to_ascii(fill_bits_per_page, fill_capacity_text, sizeof(fill_capacity_text));
        u64_to_ascii(frame_bit_count, fill_target_text, sizeof(fill_target_text));
        u64_to_ascii(target_page_count, fill_pages_text, sizeof(fill_pages_text));
        console_write(2, "debug fill capacity="); console_write(2, fill_capacity_text);
        console_write(2, " target="); console_write(2, fill_target_text);
        console_write(2, " pages="); console_line(2, fill_pages_text);
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
    char final_frame_text[32], final_capacity_text[32], final_pages_text[32];
    u64_to_ascii(frame_bit_count, final_frame_text, sizeof(final_frame_text));
    u64_to_ascii(bits_per_page, final_capacity_text, sizeof(final_capacity_text));
    u64_to_ascii(page_count, final_pages_text, sizeof(final_pages_text));
    console_write(2, "debug final frame="); console_write(2, final_frame_text);
    console_write(2, " capacity="); console_write(2, final_capacity_text);
    console_write(2, " pages="); console_line(2, final_pages_text);
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
        makocode::PaletteSymbolProfile candidate_profile;
        if (!makocode::palette_symbol_profile(symbol_palette_size,
                                              candidate_profile)) {
            console_line(2, "encode: unsupported palette-symbol profile");
            return 1;
        }
        for (u32 candidate_hundredths = 1u;
             candidate_hundredths <= 25400u;
             ++candidate_hundredths) {
            u32 candidate_parity = 0u;
            u32 candidate_overlay = (u32)floor(
                (double)candidate_profile.block_length * overlay_fraction);
            if (!makocode::palette_symbol_parity_for_redundancy(
                    candidate_profile,
                    (double)candidate_hundredths / 100.0,
                    candidate_parity) ||
                candidate_parity == 0u ||
                candidate_parity < candidate_overlay ||
                candidate_parity < required_without_current_overlay + candidate_overlay) {
                continue;
            }
            format_fixed_3((double)candidate_hundredths / 100.0,
                           sufficient_target_buffer,
                           sizeof(sufficient_target_buffer));
            break;
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
                                             have_password && !hidden_mode_enabled
                                                 ? (const char*)password_buffer.data : (const char*)0,
                                             have_password && !hidden_mode_enabled
                                                 ? password_buffer.size : 0u,
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
                                                 have_password && !hidden_mode_enabled
                                                     ? (const char*)password_buffer.data : (const char*)0,
                                                 have_password && !hidden_mode_enabled
                                                     ? password_buffer.size : 0u,
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
    u64 placement_seed;
    u16 overlay_erasure_budget;
    u16 block_total;
    u64 block_count;

    OverlaySelector()
        : seed(0u),
          placement_seed(makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED),
          overlay_erasure_budget(0u),
          block_total(0u),
          block_count(0u) {}
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
    makocode::PaletteSymbolProfile profile;
    if (!base_page.metadata.has_encoded_tile ||
        !metadata_schema_is_current(tile.schema_version) ||
        !tile.overwrite_layout || !tile.ecc_enabled ||
        !makocode::palette_symbol_profile(tile.palette_count, profile) ||
        tile.ecc_block_data == 0u || tile.ecc_parity == 0u ||
        tile.ecc_block_data + tile.ecc_parity != profile.block_length ||
        tile.ecc_block_count != overlay_selector.block_count ||
        overlay_selector.block_total != profile.block_length ||
        overlay_selector.overlay_erasure_budget > tile.ecc_parity ||
        2u * (u32)tile.unknown_parity_budget +
                (u32)overlay_selector.overlay_erasure_budget > tile.ecc_parity ||
        tile.page_bits == 0u || (tile.page_bits & 31u) != 0u ||
        tile.page_index == 0u) {
        return false;
    }
    u64 total_data_pixels = (u64)base_page.width * apply_height;
    if (total_data_pixels > (u64)USIZE_MAX_VALUE ||
        base_mask.size < (usize)total_data_pixels ||
        overlay_selector.block_count >
            (U64_MAX_VALUE - makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS) /
                overlay_selector.block_total) return false;
    MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(
        base_page.width, apply_height);
    makocode::ByteBuffer payload_rank_map;
    u64 payload_pixel_count = 0u;
    if (!MetadataTile::build_payload_rank_map(base_page.width,
                                              apply_height,
                                              placement,
                                              tile,
                                              base_mask.data,
                                              base_mask.size,
                                              payload_rank_map,
                                              payload_pixel_count,
                                              metadata_tile_present
                                                  ? include_metadata_overwrites
                                                  : false,
                                              overlay_selector.placement_seed) ||
        payload_pixel_count < 8u || payload_pixel_count > (u64)USIZE_MAX_VALUE / 8u) {
        return false;
    }
    u64 page_symbol_capacity = payload_pixel_count / 8u;
    if (!page_symbol_capacity ||
        tile.page_index - 1u > U64_MAX_VALUE / page_symbol_capacity) return false;
    u64 page_symbol_offset = (tile.page_index - 1u) * page_symbol_capacity;
    u64 stream_symbol_count = tile.page_bits / 32u;
    if (page_symbol_offset >= stream_symbol_count) return false;
    u64 page_symbol_count = stream_symbol_count - page_symbol_offset;
    if (page_symbol_count > page_symbol_capacity) page_symbol_count = page_symbol_capacity;
    u64 payload_stream_end = (u64)makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS +
                             overlay_selector.block_count * profile.block_length;
    u64 page_stream_end = page_symbol_offset + page_symbol_count;
    if (page_stream_end > payload_stream_end) page_stream_end = payload_stream_end;
    u64 page_dot_count = page_symbol_count * 8u;
    if (payload_pixel_count > (u64)USIZE_MAX_VALUE ||
        !payload_rank_map.data) return false;
    makocode::ByteBuffer rank_to_pixel;
    if (payload_pixel_count > (u64)USIZE_MAX_VALUE / sizeof(u64) ||
        !rank_to_pixel.ensure((usize)payload_pixel_count * sizeof(u64))) return false;
    rank_to_pixel.size = (usize)payload_pixel_count * sizeof(u64);
    u64* pixel_for_rank = (u64*)rank_to_pixel.data;
    for (u64 rank = 0u; rank < payload_pixel_count; ++rank) {
        pixel_for_rank[rank] = U64_MAX_VALUE;
    }
    const u64* payload_ranks = (const u64*)payload_rank_map.data;
    for (u64 pixel = 0u; pixel < total_data_pixels; ++pixel) {
        u64 rank = payload_ranks[pixel];
        if (rank < payload_pixel_count) pixel_for_rank[rank] = pixel;
    }
    u8 selector_mask[765];
    for (u64 block = 0u; block < overlay_selector.block_count; ++block) {
        if (!makocode::palette_symbol_selector_mask(
                overlay_selector.seed, block, profile.block_length,
                overlay_selector.overlay_erasure_budget, selector_mask)) return false;
        for (u32 position = 0u; position < profile.block_length; ++position) {
            if (!selector_mask[position]) continue;
            u64 interleaved_offset = (u64)position * overlay_selector.block_count + block;
            u64 stream_symbol = makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS +
                                interleaved_offset;
            if (stream_symbol < page_symbol_offset) continue;
            if (stream_symbol >= page_stream_end) continue;
            u64 local_symbol = stream_symbol - page_symbol_offset;
            if (local_symbol >= page_symbol_count) continue;
            u64 first_dot = local_symbol * 8u;
            if (first_dot > page_dot_count || page_dot_count - first_dot < 8u) return false;
            for (u32 dot = 0u; dot < 8u; ++dot) {
                u64 rank = first_dot + dot;
                u64 pixel = pixel_for_rank[rank];
                if (pixel == U64_MAX_VALUE || pixel >= total_data_pixels) return false;
                usize pixel_byte = (usize)pixel * 3u;
                if (ignore_list &&
                    overlay_color_matches_ignore(*ignore_list,
                        overlay_page.pixels.data[pixel_byte],
                        overlay_page.pixels.data[pixel_byte + 1u],
                        overlay_page.pixels.data[pixel_byte + 2u])) continue;
                memcpy(base_page.pixels.data + pixel_byte,
                       overlay_page.pixels.data + pixel_byte, 3u);
            }
        }
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
                                            const char* selector_password,
                                            usize selector_password_length,
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
    makocode::PaletteSymbolProfile profile;
    if (!tile.ecc_enabled || tile.ecc_block_count == 0u || tile.ecc_parity == 0u ||
        tile.ecc_block_data == 0u || tile.overlay_erasure_budget == 0u ||
        !makocode::palette_symbol_profile(tile.palette_count, profile) ||
        (u32)tile.ecc_block_data + tile.ecc_parity != profile.block_length ||
        tile.overlay_erasure_budget > tile.ecc_parity ||
        2u * (u32)tile.unknown_parity_budget + tile.overlay_erasure_budget >
            tile.ecc_parity) {
        console_line(2, "encode: overlay requires a nonzero, valid ECC selector budget");
        return false;
    }

    OverlaySelector overlay_selector;
    overlay_selector.seed = 0u;
    overlay_selector.placement_seed = mapping_override
        ? mapping_override->symbol_placement_seed
        : makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED;
    if (selector_password_length &&
        !makocode::palette_symbol_password_selector_seed(selector_password,
                                                        selector_password_length,
                                                        overlay_selector.seed)) {
        console_line(2, "encode: failed to derive password-keyed overlay selector");
        return false;
    }
    if (selector_password_length) {
        overlay_selector.placement_seed = overlay_selector.seed;
    }
    overlay_selector.overlay_erasure_budget = tile.overlay_erasure_budget;
    overlay_selector.block_total = (u16)profile.block_length;
    overlay_selector.block_count = tile.ecc_block_count;
    if ((u32)overlay_selector.overlay_erasure_budget > tile.ecc_parity ||
        overlay_selector.block_count >
            (U64_MAX_VALUE - makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS) /
                overlay_selector.block_total) {
        console_line(2, "encode: overlay selector exceeds ECC capacity");
        return false;
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
        char block_buffer[32];
        u64_to_ascii(tile.overlay_erasure_budget, budget_buffer, sizeof(budget_buffer));
        u64_to_ascii(profile.block_length, block_buffer, sizeof(block_buffer));
        console_write(2, "debug: overlay erasure budget per block=");
        console_write(2, budget_buffer);
        console_write(2, " actual fraction=");
        console_write(2, budget_buffer);
        console_write(2, "/");
        console_line(2, block_buffer);
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

