/*
 * Per-file source minification and PPM rendering.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_common.hpp"
static bool read_file_into_buffer(const char* path, makocode::ByteBuffer& buffer) {
    buffer.release();
    int fd = makocode_platform_open(path, MAKOCODE_OPEN_READ);
    if (fd < 0) {
        return false;
    }
    u8 chunk[4096];
    while (1) {
        long read_result = makocode_platform_read(fd, chunk, sizeof(chunk));
        if (read_result < 0 || (unsigned long)read_result > sizeof(chunk)) {
            makocode_platform_close(fd);
            buffer.release();
            return false;
        }
        if (read_result == 0) {
            break;
        }
        if (!buffer.append_bytes(chunk, (usize)read_result)) {
            makocode_platform_close(fd);
            buffer.release();
            return false;
        }
    }
    if (makocode_platform_close(fd) != 0) {
        buffer.release();
        return false;
    }
    return true;
}

static bool strip_cpp_comments(const makocode::ByteBuffer& input,
                               makocode::ByteBuffer& output) {
    output.release();
    if (!input.data || input.size == 0u) {
        return true;
    }
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    bool in_char = false;
    const u8* data = input.data;
    usize size = input.size;
    usize index = 0u;
    while (index < size) {
        u8 ch = data[index];
        if (in_line_comment) {
            if (ch == '\n' || ch == '\r') {
                if (!output.push(ch)) {
                    return false;
                }
                in_line_comment = false;
            }
            index += 1u;
            continue;
        }
        if (in_block_comment) {
            if (ch == '*' && (index + 1u) < size && data[index + 1u] == '/') {
                in_block_comment = false;
                index += 2u;
                continue;
            }
            if (ch == '\n' || ch == '\r') {
                if (!output.push(ch)) {
                    return false;
                }
            }
            index += 1u;
            continue;
        }
        if (in_string) {
            if (!output.push(ch)) {
                return false;
            }
            if (ch == '\\') {
                if ((index + 1u) < size) {
                    u8 next = data[index + 1u];
                    if (!output.push(next)) {
                        return false;
                    }
                    index += 2u;
                    continue;
                }
            } else if (ch == '"') {
                in_string = false;
            }
            index += 1u;
            continue;
        }
        if (in_char) {
            if (!output.push(ch)) {
                return false;
            }
            if (ch == '\\') {
                if ((index + 1u) < size) {
                    u8 next = data[index + 1u];
                    if (!output.push(next)) {
                        return false;
                    }
                    index += 2u;
                    continue;
                }
            } else if (ch == '\'') {
                in_char = false;
            }
            index += 1u;
            continue;
        }
        if (ch == '/' && (index + 1u) < size) {
            u8 next = data[index + 1u];
            if (next == '/') {
                in_line_comment = true;
                index += 2u;
                continue;
            }
            if (next == '*') {
                in_block_comment = true;
                index += 2u;
                continue;
            }
        }
        if (ch == '"') {
            in_string = true;
            if (!output.push(ch)) {
                return false;
            }
            index += 1u;
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            if (!output.push(ch)) {
                return false;
            }
            index += 1u;
            continue;
        }
        if (!output.push(ch)) {
            return false;
        }
        index += 1u;
    }
    return true;
}

int command_minify(int arg_count, char** args) {
    if (arguments_request_help(arg_count, args)) {
        write_minify_help();
        return 0;
    }
    bool have_width = false;
    bool have_height = false;
    bool have_output = false;
    const char* input_path = 0;
    const char* requested_output_path = 0;
    u32 requested_width = 0u;
    u32 requested_height = 0u;
    for (int i = 0; i < arg_count; ++i) {
        const char* arg = args[i];
        const char* value_text = 0;
        usize value_length = 0u;
        bool is_width = false;
        bool is_height = false;
        bool is_output = false;
        if (ascii_equals_token(arg, ascii_length(arg), "--width") ||
            ascii_equals_token(arg, ascii_length(arg), "--height")) {
            if (i + 1 >= arg_count || !args[i + 1][0]) {
                console_line(2, "minify: option requires a value");
                return 1;
            }
            if (ascii_equals_token(arg, ascii_length(arg), "--width")) {
                is_width = true;
            } else {
                is_height = true;
            }
            value_text = args[++i];
            value_length = ascii_length(value_text);
        } else if (ascii_equals_token(arg, ascii_length(arg), "--output")) {
            if (i + 1 >= arg_count || !args[i + 1][0]) {
                console_line(2, "minify: --output requires a value");
                return 1;
            }
            is_output = true;
            value_text = args[++i];
            value_length = ascii_length(value_text);
        } else if (ascii_starts_with(arg, "--width=")) {
            is_width = true;
            value_text = arg + 8u;
            value_length = ascii_length(value_text);
        } else if (ascii_starts_with(arg, "--height=")) {
            is_height = true;
            value_text = arg + 9u;
            value_length = ascii_length(value_text);
        } else if (ascii_starts_with(arg, "--output=")) {
            is_output = true;
            value_text = arg + 9u;
            value_length = ascii_length(value_text);
        } else {
            if (arg[0] == '-') {
                console_line(2, "minify: unrecognized option");
                return 1;
            }
            if (input_path) {
                console_line(2, "minify: only one input filename is accepted");
                return 1;
            }
            input_path = arg;
            continue;
        }
        if (!value_length) {
            console_line(2, "minify: option requires a non-empty value");
            return 1;
        }
        if (is_output) {
            if (have_output) {
                console_line(2, "minify: --output may only be specified once");
                return 1;
            }
            have_output = true;
            requested_output_path = value_text;
        } else if (is_width || is_height) {
            u64 parsed_value = 0u;
            if (!ascii_to_u64(value_text, value_length, &parsed_value) ||
                parsed_value == 0u || parsed_value > 0xFFFFFFFFu) {
                console_line(2, "minify: width and height must be positive 32-bit integers");
                return 1;
            }
            if (is_width) {
                if (have_width) {
                    console_line(2, "minify: --width may only be specified once");
                    return 1;
                }
                have_width = true;
                requested_width = (u32)parsed_value;
            } else {
                if (have_height) {
                    console_line(2, "minify: --height may only be specified once");
                    return 1;
                }
                have_height = true;
                requested_height = (u32)parsed_value;
            }
        }
    }
    if (!input_path) {
        console_line(2, "minify: an input filename is required");
        return 1;
    }
    if (!have_width || !have_height) {
        console_line(2, "minify: both --width and --height are required");
        return 1;
    }
    makocode::ByteBuffer output_path;
    if (requested_output_path) {
        if (!output_path.append_bytes((const u8*)requested_output_path,
                                      ascii_length(requested_output_path))) {
            console_line(2, "minify: failed to build output path");
            return 1;
        }
    } else {
        usize input_length = ascii_length(input_path);
        usize basename_start = 0u;
        usize dot_position = USIZE_MAX_VALUE;
        for (usize i = 0u; i < input_length; ++i) {
            if (input_path[i] == '/' || input_path[i] == '\\') {
                basename_start = i + 1u;
            } else if (input_path[i] == '.') {
                dot_position = i;
            }
        }
        if (dot_position < basename_start) dot_position = input_length;
        if (!output_path.append_bytes((const u8*)input_path, dot_position) ||
            !output_path.append_bytes((const u8*)"_minified", 9u)) {
            console_line(2, "minify: failed to build output path");
            return 1;
        }
        if (dot_position < input_length) {
            if (!output_path.append_bytes((const u8*)input_path + dot_position,
                                          input_length - dot_position)) {
                console_line(2, "minify: failed to build output path");
                return 1;
            }
        } else if (!output_path.append_bytes((const u8*)".cpp", 4u)) {
            console_line(2, "minify: failed to build output path");
            return 1;
        }
    }
    if (!output_path.append_char('\0')) {
        console_line(2, "minify: failed to build output path");
        return 1;
    }
    if (ascii_compare(input_path, (const char*)output_path.data) == 0) {
        console_line(2, "minify: input and output paths must differ");
        return 1;
    }
    makocode::ByteBuffer page_prefix;
    usize output_length = output_path.size - 1u;
    usize output_basename = 0u;
    usize output_dot = output_length;
    for (usize i = 0u; i < output_length; ++i) {
        if (output_path.data[i] == '/' || output_path.data[i] == '\\') {
            output_basename = i + 1u;
        } else if (output_path.data[i] == '.') {
            output_dot = i;
        }
    }
    if (output_dot < output_basename) output_dot = output_length;
    if (!page_prefix.append_bytes(output_path.data, output_dot) ||
        !page_prefix.append_char('_')) {
        console_line(2, "minify: failed to build page filename prefix");
        return 1;
    }
    makocode::ByteBuffer source;
    if (!read_file_into_buffer(input_path, source)) {
        console_line(2, "minify: failed to read input file");
        return 1;
    }
    makocode::ByteBuffer stripped;
    if (!strip_cpp_comments(source, stripped)) {
        console_line(2, "minify: failed to strip comments");
        return 1;
    }
    makocode::ByteBuffer minified;
    bool last_was_space = true;
    bool line_start = true;
    bool in_preprocessor = false;
    for (usize i = 0u; i < stripped.size; ++i) {
        u8 ch = stripped.data[i];
        if (ch == '\r') {
            ch = '\n';
        }
        if (ch == '\n') {
            if (in_preprocessor) {
                if (!minified.push('\n')) {
                    minified.release();
                    console_line(2, "minify: failed to compact whitespace");
                    return 1;
                }
                last_was_space = true;
            } else if (!last_was_space) {
                if (!minified.push(' ')) {
                    minified.release();
                    console_line(2, "minify: failed to compact whitespace");
                    return 1;
                }
                last_was_space = true;
            }
            line_start = true;
            in_preprocessor = false;
            continue;
        }
        bool is_space = (ch == ' ') || (ch == '\t') || (ch == '\f') || (ch == '\v');
        if (is_space) {
            if (line_start) {
                continue;
            }
            if (!last_was_space) {
                if (!minified.push(' ')) {
                    minified.release();
                    console_line(2, "minify: failed to compact whitespace");
                    return 1;
                }
                last_was_space = true;
            }
            continue;
        }
        if (line_start && ch == '#') {
            if (minified.size > 0u) {
                u8& previous = minified.data[minified.size - 1u];
                if (previous == ' ') {
                    previous = '\n';
                }
            }
            if (!minified.push('#')) {
                minified.release();
                console_line(2, "minify: failed to compact whitespace");
                return 1;
            }
            last_was_space = false;
            line_start = false;
            in_preprocessor = true;
            continue;
        }
        if (!minified.push(ch)) {
            minified.release();
            console_line(2, "minify: failed to compact whitespace");
            return 1;
        }
        last_was_space = false;
        line_start = false;
    }
    stripped.release();
    stripped.data = minified.data;
    stripped.size = minified.size;
    stripped.capacity = minified.capacity;
    minified.data = 0;
    minified.size = 0;
    minified.capacity = 0;
    usize end_index = stripped.size;
    while (end_index > 0u &&
           (stripped.data[end_index - 1u] == ' ' || stripped.data[end_index - 1u] == '\n')) {
        end_index -= 1u;
    }
    stripped.size = end_index;
    if (!write_bytes_to_file((const char*)output_path.data, stripped.data, stripped.size)) {
        console_write(2, "minify: failed to write ");
        console_line(2, (const char*)output_path.data);
        return 1;
    }
    console_write(1, "minify: wrote ");
    console_line(1, (const char*)output_path.data);

    const u64 cell_width = (u64)FOOTER_BASE_GLYPH_WIDTH + 1u;
    const u64 cell_height = (u64)FOOTER_BASE_GLYPH_HEIGHT;
    u64 text_cells = (u64)stripped.size;
    u64 image_width = (u64)requested_width;
    u64 image_height = (u64)requested_height;
    if (image_width < cell_width || image_height < cell_height) {
        console_line(2, "minify: page dimensions must fit at least one 5x8 character cell");
        return 1;
    }
    u64 columns = image_width / cell_width;
    u64 rows = image_height / cell_height;
    u64 page_capacity = columns * rows;
    u64 page_count = text_cells / page_capacity + ((text_cells % page_capacity) ? 1u : 0u);
    if (!page_count) page_count = 1u;
    u64 pixel_count = image_width * image_height;
    if (pixel_count > U64_MAX_VALUE / 3u ||
        image_width > (u64)(USIZE_MAX_VALUE / 3u)) {
        console_line(2, "minify: rendered PPM is too large");
        return 1;
    }
    for (usize i = 0u; i < stripped.size; ++i) {
        u8 ch = stripped.data[i];
        if (ch != '\n' && (ch < 32u || ch > 126u)) {
            console_line(2, "minify: PPM output supports printable ASCII and LF only");
            return 1;
        }
    }
    usize scanline_size = (usize)(image_width * 3u);
    u8* scanline = (u8*)makocode_platform_malloc((unsigned long)scanline_size);
    if (!scanline) {
        console_line(2, "minify: failed to allocate PPM scanline");
        return 1;
    }
    bool ppm_ok = true;
    for (u64 page_index = 0u; ppm_ok && page_index < page_count; ++page_index) {
        char page_index_text[32];
        int page_index_length = makocode_platform_snprintf(page_index_text,
                                         sizeof(page_index_text),
                                         "%04llu",
                                         (unsigned long long)page_index);
        makocode::ByteBuffer page_path;
        if (page_index_length <= 0 || (usize)page_index_length >= sizeof(page_index_text) ||
            !page_path.append_bytes(page_prefix.data, page_prefix.size) ||
            !page_path.append_bytes((const u8*)page_index_text, (usize)page_index_length) ||
            !page_path.append_bytes((const u8*)".ppm", 4u) ||
            !page_path.append_char('\0')) {
            ppm_ok = false;
            break;
        }
        int ppm_fd = makocode_platform_create_file((const char*)page_path.data, 0644);
        if (ppm_fd < 0) {
            ppm_ok = false;
            break;
        }
        char ppm_header[64];
        int ppm_header_length = makocode_platform_snprintf(ppm_header,
                                         sizeof(ppm_header),
                                         "P6\n%u %u\n255\n",
                                         (u32)image_width,
                                         (u32)image_height);
        ppm_ok = ppm_header_length > 0 &&
                 (usize)ppm_header_length < sizeof(ppm_header) &&
                 write_all_fd(ppm_fd, (const u8*)ppm_header, (usize)ppm_header_length);
        u64 page_start = page_index * page_capacity;
        for (u64 y = 0u; ppm_ok && y < image_height; ++y) {
            memset(scanline, 255, scanline_size);
            if (y < rows * cell_height) {
                u64 glyph_row = y % cell_height;
                u64 text_row = y / cell_height;
                for (u64 column = 0u; column < columns; ++column) {
                    u64 character_index = page_start + text_row * columns + column;
                    if (character_index >= text_cells) {
                        break;
                    }
                    u8 ch = stripped.data[(usize)character_index];
                    const char* glyph_row_pattern = 0;
                    if (ch == '\n') {
                        if (glyph_row < 7u) {
                            glyph_row_pattern = "11111";
                        }
                    } else {
                        const GlyphPattern* glyph = footer_lookup_glyph((char)ch);
                        if (glyph) {
                            glyph_row_pattern = glyph->rows[glyph_row];
                        }
                    }
                    if (glyph_row_pattern) {
                        u64 pixel_left = column * cell_width;
                        for (u32 glyph_column = 0u;
                             glyph_column < FOOTER_BASE_GLYPH_WIDTH;
                             ++glyph_column) {
                            if (glyph_row_pattern[glyph_column] == '1') {
                                u64 pixel_offset = (pixel_left + glyph_column) * 3u;
                                scanline[(usize)pixel_offset] = 0u;
                                scanline[(usize)pixel_offset + 1u] = 0u;
                                scanline[(usize)pixel_offset + 2u] = 0u;
                            }
                        }
                    }
                }
            }
            ppm_ok = write_all_fd(ppm_fd, scanline, scanline_size);
        }
        makocode_platform_close(ppm_fd);
        if (!ppm_ok) {
            makocode_platform_unlink((const char*)page_path.data);
        }
    }
    makocode_platform_free(scanline);
    if (!ppm_ok) {
        console_line(2, "minify: failed to write PPM output");
        return 1;
    }
    for (u64 stale_page = page_count; ; ++stale_page) {
        char stale_index_text[32];
        int stale_index_length = makocode_platform_snprintf(stale_index_text,
                                          sizeof(stale_index_text),
                                          "%04llu",
                                          (unsigned long long)stale_page);
        makocode::ByteBuffer stale_path;
        if (stale_index_length <= 0 || (usize)stale_index_length >= sizeof(stale_index_text) ||
            !stale_path.append_bytes(page_prefix.data, page_prefix.size) ||
            !stale_path.append_bytes((const u8*)stale_index_text, (usize)stale_index_length) ||
            !stale_path.append_bytes((const u8*)".ppm", 4u) ||
            !stale_path.append_char('\0') ||
            makocode_platform_unlink((const char*)stale_path.data) != 0) {
            break;
        }
    }
    char page_count_text[32];
    u64_to_ascii(page_count, page_count_text, sizeof(page_count_text));
    console_write(1, "minify: wrote ");
    console_write(1, page_count_text);
    console_line(1, " PPM page(s)");
    return 0;
}

