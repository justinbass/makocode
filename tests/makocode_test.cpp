/*
 * MakoCode CLI regression tests.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 *
 * This test executable drives the compiled CLI through argv, stdin, files,
 * stdout, and stderr. It deliberately uses only the C++ standard library and
 * POSIX process/file APIs already used by makocode.cpp.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <iterator>
#include <tuple>
#include <vector>

static unsigned long minify_allocation_count=0;
static unsigned long minify_allocation_failure_at=0;
extern "C" void* makocode_test_malloc(unsigned long size);

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define main makocode_embedded_cli_main
#include "../src/makocode_common.cpp"
#include "../src/makocode_encode.cpp"
#include "../src/makocode_decode.cpp"
#include "../src/makocode_minify.cpp"
#include "../src/makocode_checkpoint.cpp"
#include "../src/makocode.cpp"
#undef main

extern "C" void* makocode_test_malloc(unsigned long size) {
    if(minify_allocation_failure_at&&++minify_allocation_count==minify_allocation_failure_at)return nullptr;
    return std::malloc(static_cast<size_t>(size));
}

#define main ppm_transform_cli_main
/* Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */

// Intentionally avoids C++ standard library headers (no <vector>, <string>, etc.).
// Uses only C/POSIX headers so it builds in minimal environments.

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

static const double kEpsilon = 1e-9;
static const double kPi = 3.141592653589793238462643383279502884;

[[noreturn]] void die(const char* message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

[[noreturn]] void die2(const char* prefix, const char* value) {
    fprintf(stderr, "%s%s\n", prefix, value);
    exit(1);
}

static int nearly_equal(double a, double b) {
    return fabs(a - b) < kEpsilon;
}

static bool read_canonical_makocode_barcode(const int* pixels,
                                           int width,
                                           int tile_x0,
                                           int tile_y0,
                                           double threshold,
                                           uint64_t& tag_out) {
    if (!pixels || width <= 0) return false;
    const unsigned char marker[4] = {0xD1u, 0xBFu, 0x3Fu, 0x45u};
    auto marker_bit = [&](unsigned int bit) -> bool {
        return ((marker[bit >> 3u] >> (7u - (bit & 7u))) & 1u) != 0u;
    };
    const int max_tag_y = (48 - 4) / 2 - 1;
    for (int tag_y = 0; tag_y <= max_tag_y; ++tag_y) {
        unsigned int colors = 0u;
        for (unsigned int candidate = 2u; candidate <= 16u; ++candidate) {
            unsigned int color_x0 = (48u - (candidate + 1u)) / 2u;
            size_t first = ((size_t)(tile_y0 + tag_y) * (size_t)width +
                            (size_t)(tile_x0 + (int)color_x0)) * 3u;
            size_t end = first + (size_t)candidate * 3u;
            long dr = (long)pixels[first] - pixels[end];
            long dg = (long)pixels[first + 1u] - pixels[end + 1u];
            long db = (long)pixels[first + 2u] - pixels[end + 2u];
            uint64_t distance = (uint64_t)(dr * dr + dg * dg + db * db);
            if (distance > 3u * 64u * 64u) continue;
            bool distinct = true;
            for (unsigned int i = 1u; i < candidate && distinct; ++i) {
                size_t left = first + (size_t)i * 3u;
                for (unsigned int j = 0u; j < i; ++j) {
                    size_t right = first + (size_t)j * 3u;
                    long rr = (long)pixels[left] - pixels[right];
                    long gg = (long)pixels[left + 1u] - pixels[right + 1u];
                    long bb = (long)pixels[left + 2u] - pixels[right + 2u];
                    if ((uint64_t)(rr * rr + gg * gg + bb * bb) < 3u * 12u * 12u) {
                        distinct = false;
                        break;
                    }
                }
            }
            if (distinct) { colors = candidate; break; }
        }
        if (!colors) continue;
        double low = 255.0, high = 0.0;
        double samples[32];
        for (unsigned int bit = 0u; bit < 32u; ++bit) {
            unsigned int x = (48u - 8u) / 2u + (bit & 7u);
            unsigned int y = (unsigned int)tag_y + 1u + bit / 8u;
            size_t rgb = ((size_t)(tile_y0 + (int)y) * (size_t)width +
                          (size_t)(tile_x0 + (int)x)) * 3u;
            samples[bit] = 0.2126 * (double)pixels[rgb] +
                           0.7152 * (double)pixels[rgb + 1u] +
                           0.0722 * (double)pixels[rgb + 2u];
            if (samples[bit] < low) low = samples[bit];
            if (samples[bit] > high) high = samples[bit];
        }
        if (high <= low + 12.0) continue;
        double marker_threshold = (low + high) * 0.5;
        for (unsigned int inversion = 0u; inversion < 2u; ++inversion) {
            bool matches = true;
            for (unsigned int bit = 0u; bit < 32u; ++bit) {
                bool observed = samples[bit] < marker_threshold;
                if (inversion) observed = !observed;
                if (observed != marker_bit(bit)) { matches = false; break; }
            }
            if (matches) {
                (void)threshold;
                tag_out = ((uint64_t)colors << 32u) | MetadataTile::ASCII_LOW_NIBBLE_MARKER;
                return true;
            }
        }
    }
    return false;
}

struct StrVec {
    char** data = nullptr;
    size_t size = 0;
    size_t cap = 0;
};

static void strvec_free(StrVec* v) {
    if (!v) return;
    for (size_t i = 0; i < v->size; i++) free(v->data[i]);
    free(v->data);
    v->data = nullptr;
    v->size = 0;
    v->cap = 0;
}

static void strvec_push(StrVec* v, const char* s) {
    if (v->size == v->cap) {
        size_t new_cap = v->cap ? (v->cap * 2) : 16;
        auto* next = (char**)realloc(v->data, new_cap * sizeof(char*));
        if (!next) die("ppm_transform: OOM");
        v->data = next;
        v->cap = new_cap;
    }
    char* dup = strdup(s ? s : "");
    if (!dup) die("ppm_transform: OOM");
    v->data[v->size++] = dup;
}

struct IntVec {
    int* data = nullptr;
    size_t size = 0;
    size_t cap = 0;
};

static void intvec_free(IntVec* v) {
    if (!v) return;
    free(v->data);
    v->data = nullptr;
    v->size = 0;
    v->cap = 0;
}

static void intvec_push(IntVec* v, int value) {
    if (v->size == v->cap) {
        size_t new_cap = v->cap ? (v->cap * 2) : 4096;
        auto* next = (int*)realloc(v->data, new_cap * sizeof(int));
        if (!next) die("ppm_transform: OOM");
        v->data = next;
        v->cap = new_cap;
    }
    v->data[v->size++] = value;
}

static char* trim_left(char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

static void trim_right_in_place(char* s) {
    size_t len = strlen(s);
    while (len > 0) {
        char ch = s[len - 1];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            s[len - 1] = '\0';
            len--;
        } else {
            break;
        }
    }
}

static int parse_i32(const char* text, const char* what) {
    if (!text || !*text) die2("ppm_transform: missing ", what);
    errno = 0;
    char* end = nullptr;
    long v = strtol(text, &end, 10);
    if (errno != 0 || !end || *trim_left(end) != '\0') die2("ppm_transform: invalid integer: ", what);
    if (v < -2147483648L || v > 2147483647L) die2("ppm_transform: out of range integer: ", what);
    return (int)v;
}

static double parse_f64(const char* text, const char* what) {
    if (!text || !*text) die2("ppm_transform: missing ", what);
    errno = 0;
    char* end = nullptr;
    double v = strtod(text, &end);
    if (errno != 0 || !end || *trim_left(end) != '\0') die2("ppm_transform: invalid float: ", what);
    return v;
}

static void format_float(double value, char out[64]) {
    // Fixed 6 decimals, trim trailing zeros and dot, normalize -0.
    snprintf(out, 64, "%.6f", value);
    char* dot = strchr(out, '.');
    if (dot) {
        size_t len = strlen(out);
        while (len > 0 && out[len - 1] == '0') {
            out[len - 1] = '\0';
            len--;
        }
        if (len > 0 && out[len - 1] == '.') {
            out[len - 1] = '\0';
        }
    }
    if (strcmp(out, "-0") == 0 || strcmp(out, "-0.0") == 0) strcpy(out, "0");
}

struct Ppm {
    int width = 0;
    int height = 0;
    IntVec pixels;   // width*height*3
    StrVec comments;
};

static void ppm_free(Ppm* ppm) {
    if (!ppm) return;
    intvec_free(&ppm->pixels);
    strvec_free(&ppm->comments);
}

static Ppm read_ppm_p6(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) die2("ppm_transform: failed to open ", path);

    Ppm ppm;

    char* line = nullptr;
    size_t line_cap = 0;

    ssize_t nread = getline(&line, &line_cap, f);
    if (nread <= 0) {
        free(line);
        fclose(f);
        die2("ppm_transform: ", "truncated header");
    }
    trim_right_in_place(line);
    if (strcmp(line, "P6") != 0) {
        free(line);
        fclose(f);
        die2("ppm_transform: ", "not a binary P6 PPM");
    }

    int header_values[3] = {0, 0, 0};
    int header_count = 0;

    while ((nread = getline(&line, &line_cap, f)) != -1) {
        trim_right_in_place(line);
        char* stripped = trim_left(line);
        if (!*stripped) continue;
        if (stripped[0] == '#') {
            strvec_push(&ppm.comments, stripped);
            continue;
        }
        char* saveptr = nullptr;
        for (char* tok = strtok_r(stripped, " \t\r\n", &saveptr); tok; tok = strtok_r(nullptr, " \t\r\n", &saveptr)) {
            if (header_count >= 3) die("ppm_transform: malformed P6 header");
            header_values[header_count++] = parse_i32(tok, "header value");
        }
        if (header_count == 3) break;
    }

    free(line);

    if (header_count != 3) die("ppm_transform: truncated header");
    ppm.width = header_values[0];
    ppm.height = header_values[1];
    if (ppm.width <= 0 || ppm.height <= 0 || header_values[2] != 255)
        die("ppm_transform: invalid P6 dimensions or maxval");

    size_t expected = (size_t)ppm.width * (size_t)ppm.height * 3;
    for (size_t i = 0; i < expected; ++i) {
        int c = fgetc(f);
        if (c == EOF) die("ppm_transform: truncated pixel data");
        intvec_push(&ppm.pixels, c);
    }
    if (fgetc(f) != EOF) die("ppm_transform: pixel count mismatch");
    fclose(f);
    return ppm;
}

static void write_ppm_p6(const char* path,
                               const StrVec* comments,
                               int width,
                               int height,
                               const IntVec* pixels,
                               int row_compact) {
    bool write_stdout = strcmp(path, "-") == 0;
    FILE* f = write_stdout ? stdout : fopen(path, "wb");
    if (!f) die2("ppm_transform: failed to write ", path);

    fprintf(f, "P6\n");
    if (comments) {
        for (size_t i = 0; i < comments->size; i++) fprintf(f, "%s\n", comments->data[i]);
    }
    fprintf(f, "%d %d\n", width, height);
    fprintf(f, "255\n");

    (void)row_compact;
    for (size_t i = 0; i < pixels->size; ++i) fputc(pixels->data[i], f);
    if (write_stdout) {
        if (fflush(f) != 0) die("ppm_transform: failed to flush stdout");
    } else {
        fclose(f);
    }
}

static void bilinear_sample(const int* pixels, int width, int height, double fx, double fy, int out_rgb[3]) {
    if (fx < 0.0) fx = 0.0;
    if (fy < 0.0) fy = 0.0;
    if (fx > (double)(width - 1)) fx = (double)(width - 1);
    if (fy > (double)(height - 1)) fy = (double)(height - 1);
    int x0 = (int)floor(fx);
    int y0 = (int)floor(fy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    if (x1 >= width) x1 = width - 1;
    if (y1 >= height) y1 = height - 1;
    double dx = fx - x0;
    double dy = fy - y0;

    for (int c = 0; c < 3; c++) {
        size_t idx00 = ((size_t)y0 * width + x0) * 3 + (size_t)c;
        size_t idx10 = ((size_t)y0 * width + x1) * 3 + (size_t)c;
        size_t idx01 = ((size_t)y1 * width + x0) * 3 + (size_t)c;
        size_t idx11 = ((size_t)y1 * width + x1) * 3 + (size_t)c;
        double top = pixels[idx00] + (pixels[idx10] - pixels[idx00]) * dx;
        double bottom = pixels[idx01] + (pixels[idx11] - pixels[idx01]) * dx;
        double value = top + (bottom - top) * dy;
        if (value < 0.0) value = 0.0;
        if (value > 255.0) value = 255.0;
        out_rgb[c] = (int)llround(value);
    }
}

static void scale_image(const IntVec* in, int width, int height, double scale_x, double scale_y, IntVec* out, int* out_w, int* out_h) {
    if (nearly_equal(scale_x, 1.0) && nearly_equal(scale_y, 1.0)) {
        *out_w = width;
        *out_h = height;
        out->data = (int*)malloc(in->size * sizeof(int));
        if (!out->data) die("ppm_transform: OOM");
        memcpy(out->data, in->data, in->size * sizeof(int));
        out->size = in->size;
        out->cap = in->size;
        return;
    }
    int new_w = (int)llround(width * scale_x);
    int new_h = (int)llround(height * scale_y);
    if (new_w < 1) new_w = 1;
    if (new_h < 1) new_h = 1;
    size_t out_sz = (size_t)new_w * (size_t)new_h * 3;
    out->data = (int*)malloc(out_sz * sizeof(int));
    if (!out->data) die("ppm_transform: OOM");
    out->size = out_sz;
    out->cap = out_sz;
    for (size_t i = 0; i < out_sz; i++) out->data[i] = 255;
    double inv_x = 1.0 / scale_x;
    double inv_y = 1.0 / scale_y;
    int sample[3] = {255, 255, 255};
    for (int row = 0; row < new_h; row++) {
        double src_y = ((row + 0.5) * inv_y) - 0.5;
        for (int col = 0; col < new_w; col++) {
            double src_x = ((col + 0.5) * inv_x) - 0.5;
            bilinear_sample(in->data, width, height, src_x, src_y, sample);
            size_t dst_idx = ((size_t)row * new_w + (size_t)col) * 3;
            out->data[dst_idx] = sample[0];
            out->data[dst_idx + 1] = sample[1];
            out->data[dst_idx + 2] = sample[2];
        }
    }
    *out_w = new_w;
    *out_h = new_h;
}

static void skew_horizontal(const IntVec* in, int width, int height, double skew_amount, IntVec* out, int* out_w, int* out_h) {
    if (nearly_equal(skew_amount, 0.0) || height == 0) {
        *out_w = width;
        *out_h = height;
        out->data = (int*)malloc(in->size * sizeof(int));
        if (!out->data) die("ppm_transform: OOM");
        memcpy(out->data, in->data, in->size * sizeof(int));
        out->size = in->size;
        out->cap = in->size;
        return;
    }
    double slope = (height == 1) ? 0.0 : (skew_amount / (height - 1));
    double min_shift = (skew_amount < 0.0) ? skew_amount : 0.0;
    double max_shift = (skew_amount > 0.0) ? skew_amount : 0.0;
    int new_w = width + (int)ceil(max_shift - min_shift);
    size_t out_sz = (size_t)new_w * (size_t)height * 3;
    out->data = (int*)malloc(out_sz * sizeof(int));
    if (!out->data) die("ppm_transform: OOM");
    out->size = out_sz;
    out->cap = out_sz;
    for (size_t i = 0; i < out_sz; i++) out->data[i] = 255;
    for (int y = 0; y < height; y++) {
        double shift = slope * y;
        for (int x = 0; x < width; x++) {
            int dest_x = (int)llround(x + shift - min_shift);
            if (dest_x < 0 || dest_x >= new_w) continue;
            size_t src_idx = ((size_t)y * width + (size_t)x) * 3;
            size_t dst_idx = ((size_t)y * new_w + (size_t)dest_x) * 3;
            out->data[dst_idx] = in->data[src_idx];
            out->data[dst_idx + 1] = in->data[src_idx + 1];
            out->data[dst_idx + 2] = in->data[src_idx + 2];
        }
    }
    *out_w = new_w;
    *out_h = height;
}

static void skew_vertical(const IntVec* in, int width, int height, double skew_amount, IntVec* out, int* out_w, int* out_h) {
    if (nearly_equal(skew_amount, 0.0) || width == 0) {
        *out_w = width;
        *out_h = height;
        out->data = (int*)malloc(in->size * sizeof(int));
        if (!out->data) die("ppm_transform: OOM");
        memcpy(out->data, in->data, in->size * sizeof(int));
        out->size = in->size;
        out->cap = in->size;
        return;
    }
    double slope = (width == 1) ? 0.0 : (skew_amount / (width - 1));
    double min_shift = (skew_amount < 0.0) ? skew_amount : 0.0;
    double max_shift = (skew_amount > 0.0) ? skew_amount : 0.0;
    int new_h = height + (int)ceil(max_shift - min_shift);
    size_t out_sz = (size_t)new_h * (size_t)width * 3;
    out->data = (int*)malloc(out_sz * sizeof(int));
    if (!out->data) die("ppm_transform: OOM");
    out->size = out_sz;
    out->cap = out_sz;
    for (size_t i = 0; i < out_sz; i++) out->data[i] = 255;
    for (int x = 0; x < width; x++) {
        double shift = slope * x;
        for (int y = 0; y < height; y++) {
            int dest_y = (int)llround(y + shift - min_shift);
            if (dest_y < 0 || dest_y >= new_h) continue;
            size_t src_idx = ((size_t)y * width + (size_t)x) * 3;
            size_t dst_idx = ((size_t)dest_y * width + (size_t)x) * 3;
            out->data[dst_idx] = in->data[src_idx];
            out->data[dst_idx + 1] = in->data[src_idx + 1];
            out->data[dst_idx + 2] = in->data[src_idx + 2];
        }
    }
    *out_w = width;
    *out_h = new_h;
}

static void rotate_image(const IntVec* in, int width, int height, double degrees, IntVec* out, int* out_w, int* out_h) {
    if (nearly_equal(degrees, 0.0)) {
        *out_w = width;
        *out_h = height;
        out->data = (int*)malloc(in->size * sizeof(int));
        if (!out->data) die("ppm_transform: OOM");
        memcpy(out->data, in->data, in->size * sizeof(int));
        out->size = in->size;
        out->cap = in->size;
        return;
    }
    double radians = degrees * (kPi / 180.0);
    double cos_a = cos(radians);
    double sin_a = sin(radians);
    int new_w = (int)llround(fabs(width * cos_a) + fabs(height * sin_a));
    int new_h = (int)llround(fabs(width * sin_a) + fabs(height * cos_a));
    if (new_w <= 0) new_w = 1;
    if (new_h <= 0) new_h = 1;
    size_t out_sz = (size_t)new_w * (size_t)new_h * 3;
    out->data = (int*)malloc(out_sz * sizeof(int));
    if (!out->data) die("ppm_transform: OOM");
    out->size = out_sz;
    out->cap = out_sz;
    for (size_t i = 0; i < out_sz; i++) out->data[i] = 255;

    double cx = (width - 1) / 2.0;
    double cy = (height - 1) / 2.0;
    double nx = (new_w - 1) / 2.0;
    double ny = (new_h - 1) / 2.0;

    int sample[3] = {255, 255, 255};
    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            double rx = x - nx;
            double ry = y - ny;
            double src_x = cos_a * rx + sin_a * ry + cx;
            double src_y = -sin_a * rx + cos_a * ry + cy;
            if (src_x >= 0.0 && src_x <= (double)(width - 1) && src_y >= 0.0 && src_y <= (double)(height - 1)) {
                bilinear_sample(in->data, width, height, src_x, src_y, sample);
                size_t base = ((size_t)y * new_w + (size_t)x) * 3;
                out->data[base] = sample[0];
                out->data[base + 1] = sample[1];
                out->data[base + 2] = sample[2];
            }
        }
    }
    *out_w = new_w;
    *out_h = new_h;
}

static uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static int rand_u8(uint32_t* state) {
    return (int)(xorshift32(state) & 0xFFu);
}

static double rand_unit(uint32_t* state) {
    return (double)(xorshift32(state) & 0xFFFFFFu) / (double)0x1000000u;
}

static void add_border_noise_in_place(IntVec* pixels, int width, int height, int thickness, double density, int seed) {
    if (thickness <= 0 || density <= 0.0) return;
    if (density < 0.0) density = 0.0;
    if (density > 1.0) density = 1.0;
    uint32_t rng = (uint32_t)seed ^ 0xA5A5A5A5u;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int on_border = (x < thickness || x >= width - thickness || y < thickness || y >= height - thickness);
            if (!on_border) continue;
            if (rand_unit(&rng) < density) {
                int v = rand_u8(&rng);
                size_t idx = ((size_t)y * width + (size_t)x) * 3;
                pixels->data[idx] = v;
                pixels->data[idx + 1] = v;
                pixels->data[idx + 2] = v;
            }
        }
    }
}

static int clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

static double clamp_unit(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

static int compute_metadata_guard(int width,
                                  int height,
                                  int footer_height_px,
                                  int* x0,
                                  int* y0,
                                  int* x1,
                                  int* y1) {
    if (!x0 || !y0 || !x1 || !y1) return 0;
    if (width <= 0 || height <= 0) return 0;
    int usable_height = height - (footer_height_px > 0 ? footer_height_px : 0);
    if (usable_height <= 0) return 0;
    int side = 96; // Protect metadata tile (~48px) plus margin.
    if (side > width) side = width;
    if (side > usable_height) side = usable_height;
    if (side <= 0) return 0;
    int cx = width / 2;
    int cy = usable_height / 2;
    int half = side / 2;
    *x0 = cx - half;
    *y0 = cy - half;
    *x1 = *x0 + side;
    *y1 = *y0 + side;
    return (*x1 > *x0) && (*y1 > *y0);
}

static int parse_color_rgb(const char* value, int out_rgb[3]) {
    if (!value) return 0;
    const char* s = value;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
    if (n == 0) return 0;

    // case-insensitive White/Black
    if (n == 5 && (strncasecmp(s, "white", 5) == 0)) {
        out_rgb[0] = 255;
        out_rgb[1] = 255;
        out_rgb[2] = 255;
        return 1;
    }
    if (n == 5 && (strncasecmp(s, "black", 5) == 0)) {
        out_rgb[0] = 0;
        out_rgb[1] = 0;
        out_rgb[2] = 0;
        return 1;
    }

    if (s[0] == '#') {
        s++;
        n--;
    }
    if (n != 6) die("ppm_transform: ink blot color must be White, Black, or RRGGBB hex");

    char buf[3] = {0, 0, 0};
    auto hex_byte = [&](int off) -> int {
        buf[0] = s[off];
        buf[1] = s[off + 1];
        char* end = nullptr;
        long v = strtol(buf, &end, 16);
        if (!end || *end != '\0' || v < 0 || v > 255) die("ppm_transform: invalid ink blot color format");
        return (int)v;
    };
    out_rgb[0] = hex_byte(0);
    out_rgb[1] = hex_byte(2);
    out_rgb[2] = hex_byte(4);
    return 1;
}

static void apply_paper_tint_in_place(IntVec* pixels,
                                      int width,
                                      int height,
                                      const int paper_rgb[3],
                                      double paper_alpha,
                                      double splotch_alpha,
                                      double splotch_shade,
                                      int splotch_px,
                                      int seed) {
    if (!pixels || !pixels->data) return;
    if (width <= 0 || height <= 0) return;
    paper_alpha = clamp_unit(paper_alpha);
    splotch_alpha = clamp_unit(splotch_alpha);
    splotch_shade = clamp_unit(splotch_shade);
    if (paper_alpha <= 0.0 && splotch_alpha <= 0.0 && splotch_shade <= 0.0) return;

    // No splotch field: apply uniform tint/shade.
    if (splotch_px <= 0) {
        double alpha = paper_alpha;
        for (size_t i = 0; i + 2 < pixels->size; i += 3) {
            for (int c = 0; c < 3; c++) {
                double v = pixels->data[i + (size_t)c];
                double mixed = v * (1.0 - alpha) + (double)paper_rgb[c] * alpha;
                pixels->data[i + (size_t)c] = clamp_u8((int)llround(mixed));
            }
        }
        return;
    }

    int cell = splotch_px;
    if (cell < 1) cell = 1;
    int grid_w = width / cell + 2;
    int grid_h = height / cell + 2;
    size_t grid_sz = (size_t)grid_w * (size_t)grid_h;
    uint8_t* grid = (uint8_t*)malloc(grid_sz);
    if (!grid) die("ppm_transform: OOM");

    uint32_t rng = (uint32_t)seed ^ 0xBADC0FFEu;
    for (size_t i = 0; i < grid_sz; i++) grid[i] = (uint8_t)rand_u8(&rng);

    int denom = cell * cell;
    if (denom <= 0) denom = 1;

    for (int y = 0; y < height; y++) {
        int gy = y / cell;
        int ry = y - gy * cell;
        int wy1 = ry;
        int wy0 = cell - ry;
        if (gy + 1 >= grid_h) gy = grid_h - 2;
        size_t row_base = (size_t)y * (size_t)width * 3;
        for (int x = 0; x < width; x++) {
            int gx = x / cell;
            int rx = x - gx * cell;
            int wx1 = rx;
            int wx0 = cell - rx;
            if (gx + 1 >= grid_w) gx = grid_w - 2;

            size_t idx00 = (size_t)gy * (size_t)grid_w + (size_t)gx;
            size_t idx10 = idx00 + 1;
            size_t idx01 = idx00 + (size_t)grid_w;
            size_t idx11 = idx01 + 1;

            int w00 = wx0 * wy0;
            int w10 = wx1 * wy0;
            int w01 = wx0 * wy1;
            int w11 = wx1 * wy1;
            int acc = (int)grid[idx00] * w00 + (int)grid[idx10] * w10 + (int)grid[idx01] * w01 + (int)grid[idx11] * w11;
            double n = (double)acc / ((double)denom * 255.0);
            if (n < 0.0) n = 0.0;
            if (n > 1.0) n = 1.0;

            double alpha = clamp_unit(paper_alpha + splotch_alpha * n);
            double shade = clamp_unit(1.0 - splotch_shade * n);

            size_t px = row_base + (size_t)x * 3;
            for (int c = 0; c < 3; c++) {
                double v = pixels->data[px + (size_t)c];
                double mixed = v * (1.0 - alpha) + (double)paper_rgb[c] * alpha;
                mixed *= shade;
                pixels->data[px + (size_t)c] = clamp_u8((int)llround(mixed));
            }
        }
    }

    free(grid);
}

static void apply_ink_blot_in_place(IntVec* pixels, int width, int height, int radius, const int* rgb_or_null) {
    if (radius <= 0 || !rgb_or_null || width <= 0 || height <= 0) return;
    int guard_x0 = 0, guard_y0 = 0, guard_x1 = 0, guard_y1 = 0;
    int have_guard = compute_metadata_guard(width, height, 0, &guard_x0, &guard_y0, &guard_x1, &guard_y1);
    double radius_sq = (double)radius * (double)radius;
    double cx = (width - 1) / 2.0;
    double cy = (height - 1) / 2.0;
    for (int y = 0; y < height; y++) {
        double dy = y - cy;
        double dy_sq = dy * dy;
        size_t row_base = (size_t)y * width * 3;
        for (int x = 0; x < width; x++) {
            double dx = x - cx;
            int protect = have_guard && x >= guard_x0 && x < guard_x1 && y >= guard_y0 && y < guard_y1;
            if (!protect && (dx * dx + dy_sq <= radius_sq)) {
                size_t idx = row_base + (size_t)x * 3;
                pixels->data[idx] = rgb_or_null[0];
                pixels->data[idx + 1] = rgb_or_null[1];
                pixels->data[idx + 2] = rgb_or_null[2];
            }
        }
    }
}

static int parse_rgb_triplet(const char* text, int out_rgb[3]) {
    if (!text) return 0;
    int r = -1, g = -1, b = -1;
    char tail[2] = {0, 0};
    if (sscanf(text, "%d %d %d%1s", &r, &g, &b, tail) < 3) return 0;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return 0;
    out_rgb[0] = r;
    out_rgb[1] = g;
    out_rgb[2] = b;
    return 1;
}

static uint32_t pack_rgb(int r, int g, int b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int u32_cmp(const void* a, const void* b) {
    uint32_t aa = *(const uint32_t*)a;
    uint32_t bb = *(const uint32_t*)b;
    if (aa < bb) return -1;
    if (aa > bb) return 1;
    return 0;
}

static void usage() {
    fprintf(stderr,
            "Usage:\n"
            "  ppm_transform transform --input IN --output OUT|- [--scale-x F] [--scale-y F] [--rotate DEG]\n"
            "                       [--skew-x PX] [--skew-y PX] [--border-thickness PX] [--border-density R]\n"
            "                       [--seed N] [--ink-blot-radius PX] [--ink-blot-color C]\n"
            "                       [--paper-color C] [--paper-alpha A] [--paper-splotch-alpha A]\n"
            "                       [--paper-splotch-shade A] [--paper-splotch-px PX]\n"
            "                       [--white-checker] [--preserve-canonical-control]\n"
            "                       [--corrupt-former-header-slots N]\n"
            "  ppm_transform solid --output OUT --width W --height H --r R --g G --b B\n"
            "  ppm_transform noise --output OUT --width W --height H --seed N\n"
            "  ppm_transform corrupt-footer-data-destroyed --input IN --output OUT [--seed N] [--footer-height-px N]\n"
            "  ppm_transform corrupt-footer-valid-data-too-corrupt --input IN --output OUT [--seed N] [--footer-height-px N] [--border-keep N]\n"
            "  ppm_transform overlay-mask --output OUT --circle-color \"R G B\" --background-color \"R G B\" [--width W] [--height H]\n"
            "  ppm_transform overlay-check --base IN --merged IN [--skip-grayscale 0|1]\n"
            "  ppm_transform bytes --output OUT|- --size N --seed N\n"
            "  ppm_transform bytes-len TEXT\n");
}

static void cmd_bytes_len(int argc, char** argv) {
    if (argc < 3) die("ppm_transform: bytes-len requires TEXT");
    // argv strings are already byte sequences; strlen is the byte length.
    printf("%zu\n", strlen(argv[2]));
}

static void cmd_solid(int argc, char** argv) {
    const char* output = nullptr;
    int width = 0, height = 0;
    int r = 0, g = 0, b = 0;
    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--output") == 0) output = require_value("--output");
        else if (strcmp(arg, "--width") == 0) width = parse_i32(require_value("--width"), "width");
        else if (strcmp(arg, "--height") == 0) height = parse_i32(require_value("--height"), "height");
        else if (strcmp(arg, "--r") == 0) r = parse_i32(require_value("--r"), "r");
        else if (strcmp(arg, "--g") == 0) g = parse_i32(require_value("--g"), "g");
        else if (strcmp(arg, "--b") == 0) b = parse_i32(require_value("--b"), "b");
        else die2("ppm_transform: unknown flag ", arg);
    }
    if (!output || width <= 0 || height <= 0) die("ppm_transform: solid requires --output/--width/--height");
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) die("ppm_transform: solid RGB out of range");
    IntVec pixels;
    size_t sz = (size_t)width * (size_t)height * 3;
    pixels.data = (int*)malloc(sz * sizeof(int));
    if (!pixels.data) die("ppm_transform: OOM");
    pixels.size = sz;
    pixels.cap = sz;
    for (size_t i = 0; i < sz; i += 3) {
        pixels.data[i] = r;
        pixels.data[i + 1] = g;
        pixels.data[i + 2] = b;
    }
    write_ppm_p6(output, nullptr, width, height, &pixels, 0);
    intvec_free(&pixels);
}

static void cmd_noise(int argc, char** argv) {
    const char* output = nullptr;
    int width = 0, height = 0;
    int seed = 0;
    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--output") == 0) output = require_value("--output");
        else if (strcmp(arg, "--width") == 0) width = parse_i32(require_value("--width"), "width");
        else if (strcmp(arg, "--height") == 0) height = parse_i32(require_value("--height"), "height");
        else if (strcmp(arg, "--seed") == 0) seed = parse_i32(require_value("--seed"), "seed");
        else die2("ppm_transform: unknown flag ", arg);
    }
    if (!output || width <= 0 || height <= 0) die("ppm_transform: noise requires --output/--width/--height");
    uint32_t rng = (uint32_t)seed ^ 0xC0FFEEu;
    IntVec pixels;
    size_t sz = (size_t)width * (size_t)height * 3;
    pixels.data = (int*)malloc(sz * sizeof(int));
    if (!pixels.data) die("ppm_transform: OOM");
    pixels.size = sz;
    pixels.cap = sz;
    for (size_t i = 0; i < sz; i++) pixels.data[i] = rand_u8(&rng);
    write_ppm_p6(output, nullptr, width, height, &pixels, 0);
    intvec_free(&pixels);
}

static void cmd_corrupt_footer_data_destroyed(int argc, char** argv) {
    const char* input = nullptr;
    const char* output = nullptr;
    int seed = 424242;
    int footer_height_px = 0;
    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--input") == 0) input = require_value("--input");
        else if (strcmp(arg, "--output") == 0) output = require_value("--output");
        else if (strcmp(arg, "--seed") == 0) seed = parse_i32(require_value("--seed"), "seed");
        else if (strcmp(arg, "--footer-height-px") == 0) footer_height_px = parse_i32(require_value("--footer-height-px"), "footer-height-px");
        else die2("ppm_transform: unknown flag ", arg);
    }
    if (!input || !output) die("ppm_transform: corrupt-footer-data-destroyed requires --input/--output");
    Ppm ppm = read_ppm_p6(input);
    if (ppm.height <= footer_height_px) die("ppm_transform: corrupt-footer-data-destroyed: image too short");
    uint32_t rng = (uint32_t)seed ^ 0x12345678u;
    int stripe_top = ppm.height - footer_height_px;
    int guard_x0 = 0, guard_y0 = 0, guard_x1 = 0, guard_y1 = 0;
    int have_guard = compute_metadata_guard(ppm.width, ppm.height, footer_height_px, &guard_x0, &guard_y0, &guard_x1, &guard_y1);
    for (int y = 0; y < stripe_top; y++) {
        size_t row_base = (size_t)y * ppm.width * 3;
        for (int x = 0; x < ppm.width; x++) {
            if (have_guard && x >= guard_x0 && x < guard_x1 && y >= guard_y0 && y < guard_y1) continue;
            size_t idx = row_base + (size_t)x * 3;
            ppm.pixels.data[idx] = rand_u8(&rng);
            ppm.pixels.data[idx + 1] = rand_u8(&rng);
            ppm.pixels.data[idx + 2] = rand_u8(&rng);
        }
    }
    write_ppm_p6(output, &ppm.comments, ppm.width, ppm.height, &ppm.pixels, 0);
    ppm_free(&ppm);
}

static void cmd_corrupt_footer_valid_data_too_corrupt(int argc, char** argv) {
    const char* input = nullptr;
    const char* output = nullptr;
    int seed = 20251215;
    int footer_height_px = 0;
    int border_keep = 80;
    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--input") == 0) input = require_value("--input");
        else if (strcmp(arg, "--output") == 0) output = require_value("--output");
        else if (strcmp(arg, "--seed") == 0) seed = parse_i32(require_value("--seed"), "seed");
        else if (strcmp(arg, "--footer-height-px") == 0) footer_height_px = parse_i32(require_value("--footer-height-px"), "footer-height-px");
        else if (strcmp(arg, "--border-keep") == 0) border_keep = parse_i32(require_value("--border-keep"), "border-keep");
        else die2("ppm_transform: unknown flag ", arg);
    }
    if (!input || !output) die("ppm_transform: corrupt-footer-valid-data-too-corrupt requires --input/--output");
    Ppm ppm = read_ppm_p6(input);
    int data_bottom = ppm.height - footer_height_px;
    if (data_bottom <= border_keep + 1) die("ppm_transform: corrupt-footer-valid-data-too-corrupt: image too short");
    int y0 = border_keep;
    int y1 = data_bottom - border_keep;
    if (y1 < y0 + 1) y1 = y0 + 1;
    int x0 = border_keep;
    int x1 = ppm.width - border_keep;
    if (x1 < x0 + 1) x1 = x0 + 1;
    int guard_x0 = 0, guard_y0 = 0, guard_x1 = 0, guard_y1 = 0;
    int have_guard = compute_metadata_guard(ppm.width, ppm.height, footer_height_px, &guard_x0, &guard_y0, &guard_x1, &guard_y1);
    uint32_t rng = (uint32_t)seed ^ 0xDEADBEEFu;
    for (int y = y0; y < y1; y++) {
        size_t row_base = (size_t)y * ppm.width * 3;
        for (int x = x0; x < x1; x++) {
            if (have_guard && x >= guard_x0 && x < guard_x1 && y >= guard_y0 && y < guard_y1) continue;
            size_t idx = row_base + (size_t)x * 3;
            ppm.pixels.data[idx] = rand_u8(&rng);
            ppm.pixels.data[idx + 1] = rand_u8(&rng);
            ppm.pixels.data[idx + 2] = rand_u8(&rng);
        }
    }
    write_ppm_p6(output, &ppm.comments, ppm.width, ppm.height, &ppm.pixels, 0);
    ppm_free(&ppm);
}

static void cmd_overlay_mask(int argc, char** argv) {
    const char* output = nullptr;
    const char* circle_color_text = nullptr;
    const char* background_color_text = nullptr;
    int width = 1000;
    int height = 1000;

    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--output") == 0) output = require_value("--output");
        else if (strcmp(arg, "--circle-color") == 0) circle_color_text = require_value("--circle-color");
        else if (strcmp(arg, "--background-color") == 0) background_color_text = require_value("--background-color");
        else if (strcmp(arg, "--width") == 0) width = parse_i32(require_value("--width"), "width");
        else if (strcmp(arg, "--height") == 0) height = parse_i32(require_value("--height"), "height");
        else die2("ppm_transform: unknown flag ", arg);
    }

    if (!output) die("ppm_transform: overlay-mask requires --output");
    if (!circle_color_text || !background_color_text) die("ppm_transform: overlay-mask requires --circle-color/--background-color");

    int circle_color[3] = {0, 0, 0};
    int bg_color[3] = {255, 255, 255};
    if (!parse_rgb_triplet(circle_color_text, circle_color)) die("ppm_transform: overlay-mask invalid --circle-color");
    if (!parse_rgb_triplet(background_color_text, bg_color)) die("ppm_transform: overlay-mask invalid --background-color");

    StrVec comments;

    // Optional per-segment circle palette via env var.
    uint32_t* circle_palette = nullptr;
    size_t circle_palette_count = 0;
    size_t circle_palette_cap = 0;
    const char* circle_colors_text = getenv("MAKO_OVERLAY_CIRCLE_COLORS");
    if (circle_colors_text && *circle_colors_text) {
        char* text = strdup(circle_colors_text);
        if (!text) die("ppm_transform: OOM");
        char* saveptr = nullptr;
        for (char* seg = strtok_r(text, ";", &saveptr); seg; seg = strtok_r(nullptr, ";", &saveptr)) {
            char* t = trim_left(seg);
            trim_right_in_place(t);
            if (!*t) continue;
            int rgb[3] = {0, 0, 0};
            if (!parse_rgb_triplet(t, rgb)) die("ppm_transform: overlay-mask invalid circle palette entry");
            if (circle_palette_count == circle_palette_cap) {
                size_t new_cap = circle_palette_cap ? (circle_palette_cap * 2) : 8;
                auto* next = (uint32_t*)realloc(circle_palette, new_cap * sizeof(uint32_t));
                if (!next) die("ppm_transform: OOM");
                circle_palette = next;
                circle_palette_cap = new_cap;
            }
            circle_palette[circle_palette_count++] = pack_rgb(rgb[0], rgb[1], rgb[2]);
        }
        free(text);
    }

    IntVec pixels;
    size_t sz = (size_t)width * (size_t)height * 3;
    pixels.data = (int*)malloc(sz * sizeof(int));
    if (!pixels.data) die("ppm_transform: OOM");
    pixels.size = sz;
    pixels.cap = sz;

    int cx = width / 2;
    int cy = height / 2;
    int radius = (int)(width * 0.45);
    long long radius_sq = (long long)radius * (long long)radius;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int inside = ((long long)dx * dx + (long long)dy * dy) <= radius_sq;
            int rgb[3] = {bg_color[0], bg_color[1], bg_color[2]};
            if (inside) {
                if (circle_palette_count > 0) {
                    double angle = atan2((double)dy, (double)dx);
                    if (angle < 0.0) angle += 2.0 * kPi;
                    size_t seg = (size_t)((angle / (2.0 * kPi)) * (double)circle_palette_count);
                    seg %= circle_palette_count;
                    uint32_t packed = circle_palette[seg];
                    rgb[0] = (int)((packed >> 16) & 0xFFu);
                    rgb[1] = (int)((packed >> 8) & 0xFFu);
                    rgb[2] = (int)(packed & 0xFFu);
                } else {
                    rgb[0] = circle_color[0];
                    rgb[1] = circle_color[1];
                    rgb[2] = circle_color[2];
                }
            }
            size_t idx = ((size_t)y * width + (size_t)x) * 3;
            pixels.data[idx] = rgb[0];
            pixels.data[idx + 1] = rgb[1];
            pixels.data[idx + 2] = rgb[2];
        }
    }

    write_ppm_p6(output, &comments, width, height, &pixels, 1);

    free(circle_palette);
    intvec_free(&pixels);
    strvec_free(&comments);
}

static void cmd_overlay_check(int argc, char** argv) {
    const char* base_path = nullptr;
    const char* merged_path = nullptr;
    int skip_grayscale = 0;
    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--base") == 0) base_path = require_value("--base");
        else if (strcmp(arg, "--merged") == 0) merged_path = require_value("--merged");
        else if (strcmp(arg, "--skip-grayscale") == 0) skip_grayscale = parse_i32(require_value("--skip-grayscale"), "skip-grayscale") != 0;
        else die2("ppm_transform: unknown flag ", arg);
    }
    if (!base_path || !merged_path) die("ppm_transform: overlay-check requires --base/--merged");

    Ppm base = read_ppm_p6(base_path);
    Ppm merged = read_ppm_p6(merged_path);
    if (base.width != merged.width || base.height != merged.height) die("dimension mismatch");

    // Optional allowed colors set.
    uint32_t* allowed = nullptr;
    size_t allowed_count = 0;
    size_t allowed_cap = 0;
    const char* allowed_text = getenv("MAKO_OVERLAY_ALLOWED_COLORS");
    if (allowed_text && *allowed_text) {
        char* text = strdup(allowed_text);
        if (!text) die("ppm_transform: OOM");
        char* saveptr = nullptr;
        for (char* seg = strtok_r(text, ";", &saveptr); seg; seg = strtok_r(nullptr, ";", &saveptr)) {
            char* t = trim_left(seg);
            trim_right_in_place(t);
            if (!*t) continue;
            int rgb[3] = {0, 0, 0};
            if (!parse_rgb_triplet(t, rgb)) die("ppm_transform: overlay-check invalid allowed color entry");
            if (allowed_count == allowed_cap) {
                size_t new_cap = allowed_cap ? (allowed_cap * 2) : 8;
                auto* next = (uint32_t*)realloc(allowed, new_cap * sizeof(uint32_t));
                if (!next) die("ppm_transform: OOM");
                allowed = next;
                allowed_cap = new_cap;
            }
            allowed[allowed_count++] = pack_rgb(rgb[0], rgb[1], rgb[2]);
        }
        free(text);
        qsort(allowed, allowed_count, sizeof(uint32_t), u32_cmp);
    }

    size_t pixels = (size_t)base.width * (size_t)base.height;
    size_t diff = 0;
    for (size_t i = 0; i < pixels; i++) {
        int r = merged.pixels.data[i * 3];
        int g = merged.pixels.data[i * 3 + 1];
        int b = merged.pixels.data[i * 3 + 2];
        if (r != base.pixels.data[i * 3] || g != base.pixels.data[i * 3 + 1] || b != base.pixels.data[i * 3 + 2]) diff++;
        if (allowed_count > 0) {
            uint32_t key = pack_rgb(r, g, b);
            if (!bsearch(&key, allowed, allowed_count, sizeof(uint32_t), u32_cmp)) {
                fprintf(stderr, "pixel %d %d %d at index %zu not in allowed palette\n", r, g, b, i);
                exit(1);
            }
        }
        if (!skip_grayscale) {
            if (!(r == g && g == b)) {
                fprintf(stderr, "non-grayscale pixel %d %d %d at index %zu\n", r, g, b, i);
                exit(1);
            }
            if (!(r == 0 || r == 255)) {
                fprintf(stderr, "pixel %d %d %d is not pure black or white at index %zu\n", r, g, b, i);
                exit(1);
            }
        }
    }
    free(allowed);
    if (diff == 0) die("overlay did not modify any pixels");
    double ratio = (double)diff / (double)pixels;
    fprintf(stdout, "overlay pixels modified: %zu (%.6f)\n", diff, ratio);

    ppm_free(&base);
    ppm_free(&merged);
}

static void cmd_transform(int argc, char** argv) {
    const char* input = nullptr;
    const char* output = nullptr;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotate_deg = 0.0;
    double skew_x = 0.0;
    double skew_y = 0.0;
    int border_thickness = 0;
    double border_density = 0.35;
    int seed = 0;
    int ink_blot_radius = 0;
    const char* ink_blot_color = "";
    const char* paper_color = "";
    double paper_alpha = 0.0;
    double paper_splotch_alpha = 0.0;
    double paper_splotch_shade = 0.0;
    int paper_splotch_px = 0;
    int corrupt_former_header_slots = 0;
    bool white_checker = false;
    bool preserve_canonical_control = false;

    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--input") == 0) input = require_value("--input");
        else if (strcmp(arg, "--output") == 0) output = require_value("--output");
        else if (strcmp(arg, "--scale-x") == 0) scale_x = parse_f64(require_value("--scale-x"), "scale-x");
        else if (strcmp(arg, "--scale-y") == 0) scale_y = parse_f64(require_value("--scale-y"), "scale-y");
        else if (strcmp(arg, "--rotate") == 0) rotate_deg = parse_f64(require_value("--rotate"), "rotate");
        else if (strcmp(arg, "--skew-x") == 0) skew_x = parse_f64(require_value("--skew-x"), "skew-x");
        else if (strcmp(arg, "--skew-y") == 0) skew_y = parse_f64(require_value("--skew-y"), "skew-y");
        else if (strcmp(arg, "--border-thickness") == 0) border_thickness = parse_i32(require_value("--border-thickness"), "border-thickness");
        else if (strcmp(arg, "--border-density") == 0) border_density = parse_f64(require_value("--border-density"), "border-density");
        else if (strcmp(arg, "--seed") == 0) seed = parse_i32(require_value("--seed"), "seed");
        else if (strcmp(arg, "--ink-blot-radius") == 0) ink_blot_radius = parse_i32(require_value("--ink-blot-radius"), "ink-blot-radius");
        else if (strcmp(arg, "--ink-blot-color") == 0) ink_blot_color = require_value("--ink-blot-color");
        else if (strcmp(arg, "--paper-color") == 0) paper_color = require_value("--paper-color");
        else if (strcmp(arg, "--paper-alpha") == 0) paper_alpha = parse_f64(require_value("--paper-alpha"), "paper-alpha");
        else if (strcmp(arg, "--paper-splotch-alpha") == 0) paper_splotch_alpha = parse_f64(require_value("--paper-splotch-alpha"), "paper-splotch-alpha");
        else if (strcmp(arg, "--paper-splotch-shade") == 0) paper_splotch_shade = parse_f64(require_value("--paper-splotch-shade"), "paper-splotch-shade");
        else if (strcmp(arg, "--paper-splotch-px") == 0) paper_splotch_px = parse_i32(require_value("--paper-splotch-px"), "paper-splotch-px");
        else if (strcmp(arg, "--white-checker") == 0) white_checker = true;
        else if (strcmp(arg, "--preserve-canonical-control") == 0) preserve_canonical_control = true;
        else if (strcmp(arg, "--corrupt-former-header-slots") == 0) corrupt_former_header_slots = parse_i32(require_value("--corrupt-former-header-slots"), "corrupt-former-header-slots");
        else die2("ppm_transform: unknown flag ", arg);
    }

    if (!input || !output) die("ppm_transform: --input and --output are required");
    if (corrupt_former_header_slots < 0 || corrupt_former_header_slots > 3) {
        die("ppm_transform: --corrupt-former-header-slots must be between 0 and 3");
    }

    Ppm ppm = read_ppm_p6(input);
    unsigned char* canonical_control_source_mask = nullptr;
    unsigned char* canonical_payload_sample_mask = nullptr;
    int* canonical_source_pixels = nullptr;
    if (preserve_canonical_control) {
        if (!white_checker) {
            die("ppm_transform: --preserve-canonical-control requires --white-checker");
        }
        if (corrupt_former_header_slots != 0 || !nearly_equal(rotate_deg, 0.0) ||
            !nearly_equal(skew_x, 0.0) || !nearly_equal(skew_y, 0.0) ||
            border_thickness != 0 || ink_blot_radius != 0 || paper_alpha > 0.0 ||
            paper_splotch_alpha > 0.0 || paper_splotch_shade > 0.0) {
            ppm_free(&ppm);
            die("ppm_transform: --preserve-canonical-control supports scale-only white-checker transforms");
        }
        if (ppm.width < 48 || ppm.height < 48) {
            ppm_free(&ppm);
            die("ppm_transform: --preserve-canonical-control requires a 48x48 metadata tile");
        }

        int tile_x0 = (ppm.width - 48) / 2;
        int tile_y0 = (ppm.height - 48) / 2;
        double darkest_luminance = 256.0;
        double lightest_luminance = -1.0;
        size_t source_pixel_count = (size_t)ppm.width * (size_t)ppm.height;
        for (size_t pixel = 0u; pixel < source_pixel_count; ++pixel) {
            size_t rgb = pixel * 3u;
            double luminance = 0.2126 * (double)ppm.pixels.data[rgb] +
                               0.7152 * (double)ppm.pixels.data[rgb + 1u] +
                               0.0722 * (double)ppm.pixels.data[rgb + 2u];
            if (luminance < darkest_luminance) darkest_luminance = luminance;
            if (luminance > lightest_luminance) lightest_luminance = luminance;
        }
        if (!(lightest_luminance > darkest_luminance + 1.0)) {
            ppm_free(&ppm);
            die("ppm_transform: --preserve-canonical-control could not detect tile color endpoints");
        }
        double tile_threshold = (darkest_luminance + lightest_luminance) * 0.5;
        uint64_t barcode_tag = 0u;
        if (!read_canonical_makocode_barcode(ppm.pixels.data,
                                             ppm.width,
                                             tile_x0,
                                             tile_y0,
                                             tile_threshold,
                                             barcode_tag)) {
            ppm_free(&ppm);
            die("ppm_transform: --preserve-canonical-control expects a centered canonical metadata tile");
        }

        canonical_control_source_mask = (unsigned char*)calloc(source_pixel_count, 1u);
        if (!canonical_control_source_mask) {
            ppm_free(&ppm);
            die("ppm_transform: OOM");
        }
        for (int y = tile_y0; y < tile_y0 + 48; ++y) {
            for (int x = tile_x0; x < tile_x0 + 48; ++x) {
                canonical_control_source_mask[(size_t)y * (size_t)ppm.width + (size_t)x] = 1u;
            }
        }
        // Keep the encoded 2x2 centered fiducial lattice intact during the
        // transformation so the decoder test sees the original markers.
        const unsigned int marker_size = 2u;
        const unsigned int spacing = 24u;
        unsigned int columns = (unsigned int)ppm.width >= marker_size
            ? 2u * (((unsigned int)ppm.width - marker_size) / (2u * spacing)) + 1u : 0u;
        unsigned int rows = (unsigned int)ppm.height >= marker_size
            ? 2u * (((unsigned int)ppm.height - marker_size) / (2u * spacing)) + 1u : 0u;
        for (unsigned int row = 0u; row < rows; ++row) {
            double center_y = ((double)ppm.height - 1.0) * 0.5 +
                              ((double)row - ((double)rows - 1.0) * 0.5) * spacing;
            int start_y = (int)(center_y - ((double)marker_size - 1.0) * 0.5);
            for (unsigned int column = 0u; column < columns; ++column) {
                double center_x = ((double)ppm.width - 1.0) * 0.5 +
                                  ((double)column - ((double)columns - 1.0) * 0.5) * spacing;
                int start_x = (int)(center_x - ((double)marker_size - 1.0) * 0.5);
                for (unsigned int dy = 0u; dy < marker_size; ++dy) {
                    int y = start_y + (int)dy;
                    if (y < 0 || y >= ppm.height) continue;
                    for (unsigned int dx = 0u; dx < marker_size; ++dx) {
                        int x = start_x + (int)dx;
                        if (x < 0 || x >= ppm.width) continue;
                        canonical_control_source_mask[(size_t)y * (size_t)ppm.width + (size_t)x] = 1u;
                    }
                }
            }
        }
    }
    StrVec comments;
    StrVec metadata;

    int w = ppm.width;
    int h = ppm.height;
    if (preserve_canonical_control) {
        canonical_source_pixels = (int*)malloc(ppm.pixels.size * sizeof(int));
        if (!canonical_source_pixels) die("ppm_transform: OOM");
        memcpy(canonical_source_pixels, ppm.pixels.data, ppm.pixels.size * sizeof(int));
    }
    IntVec current = ppm.pixels;
    // Prevent double-free: current now owns ppm.pixels buffer.
    ppm.pixels.data = nullptr;
    ppm.pixels.size = ppm.pixels.cap = 0;

    IntVec tmp;
    int new_w = 0, new_h = 0;
    scale_image(&current, w, h, scale_x, scale_y, &tmp, &new_w, &new_h);
    intvec_free(&current);
    current = tmp;
    w = new_w;
    h = new_h;

    if (preserve_canonical_control) {
        size_t output_pixels = (size_t)w * (size_t)h;
        canonical_payload_sample_mask = (unsigned char*)calloc(output_pixels, 1u);
        if (!canonical_payload_sample_mask) die("ppm_transform: OOM");
        for (int source_y = 0; source_y < ppm.height; ++source_y) {
            for (int source_x = 0; source_x < ppm.width; ++source_x) {
                size_t source_pixel = (size_t)source_y * (size_t)ppm.width +
                                      (size_t)source_x;
                if (canonical_control_source_mask[source_pixel]) continue;
                size_t source_rgb = source_pixel * 3u;
                double center_x = ((double)source_x + 0.5) * scale_x - 0.5;
                double center_y = ((double)source_y + 0.5) * scale_y - 0.5;
                double half_width = 0.4 * scale_x;
                double half_height = 0.4 * scale_y;
                int x0 = (int)ceil(center_x - half_width);
                int x1 = (int)ceil(center_x + half_width);
                int y0 = (int)ceil(center_y - half_height);
                int y1 = (int)ceil(center_y + half_height);
                if (x0 < 0) x0 = 0;
                if (y0 < 0) y0 = 0;
                if (x1 > w) x1 = w;
                if (y1 > h) y1 = h;
                for (int target_y = y0; target_y < y1; ++target_y) {
                    double dy = (double)target_y - center_y;
                    if (dy < -half_height || dy >= half_height) continue;
                    for (int target_x = x0; target_x < x1; ++target_x) {
                        double dx = (double)target_x - center_x;
                        if (dx < -half_width || dx >= half_width) continue;
                        size_t target_pixel = (size_t)target_y * (size_t)w +
                                              (size_t)target_x;
                        size_t target_rgb = target_pixel * 3u;
                        for (int channel = 0; channel < 3; ++channel) {
                            current.data[target_rgb + (size_t)channel] =
                                canonical_source_pixels[source_rgb + (size_t)channel];
                        }
                        canonical_payload_sample_mask[target_pixel] = 1u;
                    }
                }
            }
        }
    }

    if (!nearly_equal(skew_x, 0.0)) {
        int skew_src_width = w;
        int skew_src_height = h;
        skew_horizontal(&current, w, h, skew_x, &tmp, &new_w, &new_h);
        intvec_free(&current);
        current = tmp;
        w = new_w;
        h = new_h;
        double skew_margin = -((skew_x < 0.0) ? skew_x : 0.0);
        char buf[64] = {0};
        char line[256] = {0};
        snprintf(line, sizeof(line), "# skew_src_width %d", skew_src_width);
        strvec_push(&metadata, line);
        snprintf(line, sizeof(line), "# skew_src_height %d", skew_src_height);
        strvec_push(&metadata, line);
        format_float(skew_margin, buf);
        snprintf(line, sizeof(line), "# skew_margin_x %s", buf);
        strvec_push(&metadata, line);
        format_float(0.0, buf);
        snprintf(line, sizeof(line), "# skew_x_pixels %s", buf);
        strvec_push(&metadata, line);
        format_float(skew_x, buf);
        snprintf(line, sizeof(line), "# skew_bottom_x %s", buf);
        strvec_push(&metadata, line);
    } else {
        skew_horizontal(&current, w, h, skew_x, &tmp, &new_w, &new_h);
        intvec_free(&current);
        current = tmp;
        w = new_w;
        h = new_h;
    }

    skew_vertical(&current, w, h, skew_y, &tmp, &new_w, &new_h);
    intvec_free(&current);
    current = tmp;
    w = new_w;
    h = new_h;

    rotate_image(&current, w, h, rotate_deg, &tmp, &new_w, &new_h);
    intvec_free(&current);
    current = tmp;
    w = new_w;
    h = new_h;

    add_border_noise_in_place(&current, w, h, border_thickness, border_density, seed);
    int blot_rgb[3] = {0, 0, 0};
    int have_blot = parse_color_rgb(ink_blot_color, blot_rgb);
    if (ink_blot_radius > 0 && !have_blot) die("ppm_transform: --ink-blot-radius requires --ink-blot-color");
    apply_ink_blot_in_place(&current, w, h, ink_blot_radius, have_blot ? blot_rgb : nullptr);

    int paper_rgb[3] = {255, 255, 255};
    int have_paper = parse_color_rgb(paper_color, paper_rgb);
    if ((paper_alpha > 0.0 || paper_splotch_alpha > 0.0 || paper_splotch_shade > 0.0) && !have_paper) {
        die("ppm_transform: --paper-alpha/--paper-splotch-* require --paper-color (White/Black or RRGGBB hex)");
    }
    apply_paper_tint_in_place(&current,
                              w,
                              h,
                              paper_rgb,
                              paper_alpha,
                              paper_splotch_alpha,
                              paper_splotch_shade,
                              paper_splotch_px,
                              seed);

    if (corrupt_former_header_slots > 0) {
        if (w != ppm.width || h != ppm.height ||
            !nearly_equal(scale_x, 1.0) || !nearly_equal(scale_y, 1.0) ||
            !nearly_equal(rotate_deg, 0.0) || !nearly_equal(skew_x, 0.0) ||
            !nearly_equal(skew_y, 0.0) || border_thickness != 0 ||
            ink_blot_radius != 0 || paper_alpha > 0.0 ||
            paper_splotch_alpha > 0.0 || paper_splotch_shade > 0.0) {
            die("ppm_transform: former-header-slot corruption requires an otherwise unmodified page");
        }
        if (w < 1 || h < 1 || (size_t)w > ((size_t)-1) / (size_t)h) {
            die("ppm_transform: invalid dimensions for former-header-slot corruption");
        }
        size_t total_pixels = (size_t)w * (size_t)h;
        bool canonical = false;
        if (w >= 48 && h >= 48) {
            int tile_x0 = (w - 48) / 2;
            int tile_y0 = (h - 48) / 2;
            uint64_t barcode_tag = 0u;
            canonical = read_canonical_makocode_barcode(current.data,
                                                        w,
                                                        tile_x0,
                                                        tile_y0,
                                                        128.0,
                                                        barcode_tag);
        }
        if (!canonical) {
            die("ppm_transform: former-header-slot corruption expects a centered canonical metadata tile");
        }
        unsigned char* reserved = (unsigned char*)calloc(total_pixels, 1u);
        if (!reserved) die("ppm_transform: OOM");

        // Match makocode's default 4 px markers on a 24 px grid with a 12 px margin.
        const unsigned int marker_size = 4u;
        const unsigned int spacing = 24u;
        const unsigned int margin = 12u;
        double min_x = (margin < (unsigned int)w) ? (double)margin : 0.0;
        double max_x = (w > (int)margin) ? (double)(w - 1 - (int)margin) : (double)(w - 1);
        if (max_x < min_x) max_x = min_x;
        double min_y = (margin < (unsigned int)h) ? (double)margin : 0.0;
        double max_y = (h > (int)margin) ? (double)(h - 1 - (int)margin) : (double)(h - 1);
        if (max_y < min_y) max_y = min_y;
        double available_width = max_x - min_x;
        double available_height = max_y - min_y;
        unsigned int columns = (unsigned int)ceil(available_width / (double)spacing) + 1u;
        unsigned int rows = (unsigned int)ceil(available_height / (double)spacing) + 1u;
        for (unsigned int grid_row = 0u; grid_row < rows; ++grid_row) {
            double t_y = (rows == 1u) ? 0.5 : (double)grid_row / (double)(rows - 1u);
            double center_y = min_y + (max_y - min_y) * t_y;
            int start_y = (int)(center_y - ((double)marker_size - 1.0) * 0.5);
            for (unsigned int grid_col = 0u; grid_col < columns; ++grid_col) {
                double t_x = (columns == 1u) ? 0.5 : (double)grid_col / (double)(columns - 1u);
                double center_x = min_x + (max_x - min_x) * t_x;
                int start_x = (int)(center_x - ((double)marker_size - 1.0) * 0.5);
                for (unsigned int dy = 0u; dy < marker_size; ++dy) {
                    int pixel_y = start_y + (int)dy;
                    if (pixel_y < 0 || pixel_y >= h) continue;
                    for (unsigned int dx = 0u; dx < marker_size; ++dx) {
                        int pixel_x = start_x + (int)dx;
                        if (pixel_x < 0 || pixel_x >= w) continue;
                        reserved[(size_t)pixel_y * (size_t)w + (size_t)pixel_x] = 1u;
                    }
                }
            }
        }

        int tile_x0 = (w - 48) / 2;
        int tile_y0 = (h - 48) / 2;
        for (int tile_y = tile_y0; tile_y < tile_y0 + 48; ++tile_y) {
            for (int tile_x = tile_x0; tile_x < tile_x0 + 48; ++tile_x) {
                reserved[(size_t)tile_y * (size_t)w + (size_t)tile_x] = 1u;
            }
        }
        size_t target_bits[3u * 6u * 8u];
        size_t target_count = 0u;
        for (int group = 0; group < corrupt_former_header_slots; ++group) {
            for (size_t parity_byte = 0u; parity_byte < 6u; ++parity_byte) {
                size_t byte_index = (size_t)group * 32u + 26u + parity_byte;
                for (size_t bit = 0u; bit < 8u; ++bit) {
                    target_bits[target_count++] = byte_index * 8u + bit;
                }
            }
        }

        size_t carrier_bit = 0u;
        size_t target_index = 0u;
        size_t modified_pixels = 0u;
        for (size_t pixel = 0u; pixel < total_pixels && target_index < target_count; ++pixel) {
            // The current frame starts with RS-protected payload symbols;
            // the old length prefix and repeated ECC header are absent.
            if (carrier_bit == target_bits[target_index]) {
                if (reserved[pixel] == 1u) {
                    free(reserved);
                    die("ppm_transform: target payload bit is overwritten in canonical layout");
                }
                size_t rgb = pixel * 3u;
                int value = current.data[rgb];
                if (current.data[rgb + 1u] != value || current.data[rgb + 2u] != value ||
                    (value != 0 && value != 255)) {
                    free(reserved);
                    die("ppm_transform: former-header-slot corruption expects a black/white encoded page");
                }
                int flipped = value ? 0 : 255;
                current.data[rgb] = flipped;
                current.data[rgb + 1u] = flipped;
                current.data[rgb + 2u] = flipped;
                ++modified_pixels;
                ++target_index;
            }
            ++carrier_bit;
        }
        free(reserved);
        if (target_index != target_count) {
            die("ppm_transform: page does not contain all former-header-slot payload bits");
        }
        fprintf(stdout, "former-header-slot payload pixels modified: %zu\n", modified_pixels);
    }

    if (white_checker) {
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (((x + y) & 1) == 0) {
                    if (canonical_control_source_mask) {
                        double source_x = ((double)x + 0.5) * (double)ppm.width / (double)w - 0.5;
                        double source_y = ((double)y + 0.5) * (double)ppm.height / (double)h - 0.5;
                        int source_pixel_x = (int)floor(source_x + 0.5);
                        int source_pixel_y = (int)floor(source_y + 0.5);
                        if (source_pixel_x < 0) source_pixel_x = 0;
                        if (source_pixel_y < 0) source_pixel_y = 0;
                        if (source_pixel_x >= ppm.width) source_pixel_x = ppm.width - 1;
                        if (source_pixel_y >= ppm.height) source_pixel_y = ppm.height - 1;
                        if (canonical_control_source_mask[(size_t)source_pixel_y * (size_t)ppm.width +
                                                          (size_t)source_pixel_x]) {
                            continue;
                        }
                    if (canonical_payload_sample_mask[(size_t)y * (size_t)w + (size_t)x]) {
                        continue;
                    }
                    }
                    size_t index = ((size_t)y * (size_t)w + (size_t)x) * 3u;
                    current.data[index + 0u] = 255;
                    current.data[index + 1u] = 255;
                    current.data[index + 2u] = 255;
                }
            }
        }
    }

    write_ppm_p6(output, &comments, w, h, &current, 0);

    intvec_free(&current);
    free(canonical_control_source_mask);
    free(canonical_payload_sample_mask);
    free(canonical_source_pixels);
    strvec_free(&metadata);
    strvec_free(&comments);
    ppm_free(&ppm);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }
    const char* cmd = argv[1];
    if (strcmp(cmd, "transform") == 0) {
        cmd_transform(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "solid") == 0) {
        cmd_solid(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "noise") == 0) {
        cmd_noise(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "corrupt-footer-data-destroyed") == 0) {
        cmd_corrupt_footer_data_destroyed(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "corrupt-footer-valid-data-too-corrupt") == 0) {
        cmd_corrupt_footer_valid_data_too_corrupt(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "overlay-mask") == 0) {
        cmd_overlay_mask(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "overlay-check") == 0) {
        cmd_overlay_check(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "bytes-len") == 0) {
        cmd_bytes_len(argc, argv);
        return 0;
    }
    if (strcmp(cmd, "bytes") == 0) {
        const char* output = nullptr;
        int byte_count = -1;
        int seed = -1;
        for (int i = 2; i < argc; i++) {
            const char* arg = argv[i];
            auto require_value = [&](const char* flag) -> const char* {
                if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
                return argv[++i];
            };
            if (strcmp(arg, "--output") == 0) output = require_value("--output");
            else if (strcmp(arg, "--size") == 0) byte_count = parse_i32(require_value("--size"), "size");
            else if (strcmp(arg, "--seed") == 0) seed = parse_i32(require_value("--seed"), "seed");
            else die2("ppm_transform: unknown flag ", arg);
        }
        if (!output || byte_count < 0 || seed < 0) {
            die("ppm_transform: bytes requires --output, non-negative --size, and non-negative --seed");
        }
        uint32_t rng = (uint32_t)seed ^ 0xC0FFEEu;
        if (rng == 0u) rng = 0xA341316Cu;
        bool write_stdout = strcmp(output, "-") == 0;
        FILE* file = write_stdout ? stdout : fopen(output, "wb");
        if (!file) die2("ppm_transform: unable to open ", output);
        unsigned char buffer[4096];
        int remaining = byte_count;
        while (remaining > 0) {
            size_t count = remaining < (int)sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
            for (size_t i = 0; i < count; i++) buffer[i] = (unsigned char)rand_u8(&rng);
            if (fwrite(buffer, 1, count, file) != count) {
                if (!write_stdout) fclose(file);
                die2("ppm_transform: unable to write ", output);
            }
            remaining -= (int)count;
        }
        if (write_stdout) {
            if (fflush(file) != 0) die2("ppm_transform: unable to flush ", output);
        } else if (fclose(file) != 0) {
            die2("ppm_transform: unable to close ", output);
        }
        return 0;
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        usage();
        return 0;
    }
    die2("ppm_transform: unknown command ", cmd);
}

#undef main

namespace fs = std::filesystem;

namespace {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ProcessResult {
    int status = -1;
    std::string out;
    std::string err;
};

struct PythonRandom {
    std::array<uint32_t,624> state{};size_t index=624;
    explicit PythonRandom(uint32_t seed){state[0]=19650218u;for(size_t i=1;i<state.size();++i)state[i]=1812433253u*(state[i-1]^(state[i-1]>>30))+static_cast<uint32_t>(i);
        size_t i=1,j=0;for(size_t k=state.size();k;--k){state[i]=(state[i]^((state[i-1]^(state[i-1]>>30))*1664525u))+seed+static_cast<uint32_t>(j);++i;++j;if(i>=state.size()){state[0]=state.back();i=1;}if(j>=1)j=0;}
        for(size_t k=state.size()-1;k;--k){state[i]=(state[i]^((state[i-1]^(state[i-1]>>30))*1566083941u))-static_cast<uint32_t>(i);++i;if(i>=state.size()){state[0]=state.back();i=1;}}state[0]=0x80000000u;index=state.size();}
    uint32_t next(){static constexpr uint32_t upper=0x80000000u,lower=0x7fffffffu,matrix=0x9908b0dfu;if(index>=state.size()){for(size_t i=0;i<state.size();++i){uint32_t y=(state[i]&upper)|(state[(i+1)%state.size()]&lower);state[i]=state[(i+397)%state.size()]^(y>>1)^((y&1u)?matrix:0u);}index=0;}uint32_t y=state[index++];y^=y>>11;y^=(y<<7)&0x9d2c5680u;y^=(y<<15)&0xefc60000u;y^=y>>18;return y;}
    double random(){uint32_t a=next()>>5,b=next()>>6;return (a*67108864.0+b)/9007199254740992.0;}
};

struct TestCase {
    std::string id;
    std::string description;
    std::function<void(const fs::path&)> run;
};

static fs::path repo_root;
static fs::path output_root;
static fs::path makocode_bin;
static fs::path test_executable;
static unsigned master_seed = 0;
static std::mutex output_mutex;

static void require(bool condition, const std::string& message) {
    if (!condition) throw Failure(message);
}

static std::string read_file(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw Failure("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), {});
}

struct PpmImage {
    unsigned width = 0;
    unsigned height = 0;
    std::string pixels;
};

static PpmImage read_ppm(const fs::path& path) {
    std::string data=read_file(path);size_t pos=0;
    auto token=[&]() -> std::string {
        for(;;){while(pos<data.size()&&(data[pos]==' '||data[pos]=='\t'||data[pos]=='\r'||data[pos]=='\n'))++pos;
            if(pos<data.size()&&data[pos]=='#'){while(pos<data.size()&&data[pos]!='\n')++pos;continue;}break;}
        size_t begin=pos;while(pos<data.size()&&data[pos]!=' '&&data[pos]!='\t'&&data[pos]!='\r'&&data[pos]!='\n'&&data[pos]!='#')++pos;
        return data.substr(begin,pos-begin);
    };
    require(token()=="P6","expected binary P6 PPM: "+path.string());
    PpmImage image;std::string w=token(),h=token(),max=token();
    try{image.width=static_cast<unsigned>(std::stoul(w));image.height=static_cast<unsigned>(std::stoul(h));}
    catch(...){throw Failure("invalid PPM dimensions: "+path.string());}
    require(max=="255","expected PPM maxval 255: "+path.string());
    require(pos<data.size()&&(data[pos]==' '||data[pos]=='\t'||data[pos]=='\r'||data[pos]=='\n'),"missing PPM header separator");
    if(data[pos]=='\r'&&pos+1<data.size()&&data[pos+1]=='\n')pos+=2;else ++pos;
    image.pixels=data.substr(pos);
    require(image.pixels.size()==static_cast<size_t>(image.width)*image.height*3,"invalid PPM raster length: "+path.string());
    return image;
}

static void write_file(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output || !output.write(contents.data(), static_cast<std::streamsize>(contents.size())))
        throw Failure("cannot write " + path.string());
}

static ProcessResult run_process(const std::vector<std::string>& args,
                                 const fs::path& cwd,
                                 const fs::path& stdin_path,
                                 const fs::path& stdout_path,
                                 const fs::path& stderr_path,
                                 const std::map<std::string, std::string>& environment = {}) {
    require(!args.empty(), "empty process argv");
    fs::create_directories(stdout_path.parent_path());
    fs::create_directories(stderr_path.parent_path());
    pid_t child = fork();
    if (child < 0) throw Failure("fork failed: " + std::string(std::strerror(errno)));
    if (child == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(126);
        int out_fd = open(stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int err_fd = open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int in_fd = stdin_path.empty() ? open("/dev/null", O_RDONLY) : open(stdin_path.c_str(), O_RDONLY);
        if (out_fd < 0 || err_fd < 0 || in_fd < 0 || dup2(out_fd, STDOUT_FILENO) < 0 ||
            dup2(err_fd, STDERR_FILENO) < 0 || dup2(in_fd, STDIN_FILENO) < 0) _exit(126);
        close(out_fd); close(err_fd); close(in_fd);
        for (const auto& entry : environment) setenv(entry.first.c_str(), entry.second.c_str(), 1);
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw Failure("waitpid failed: " + std::string(std::strerror(errno)));
    }
    ProcessResult result;
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    result.out = read_file(stdout_path);
    result.err = read_file(stderr_path);
    return result;
}

static unsigned crc_cksum(const std::string& bytes) {
    uint32_t crc = 0;
    for (unsigned char byte : bytes) {
        crc ^= static_cast<uint32_t>(byte) << 24;
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    size_t length = bytes.size();
    while (length) {
        crc ^= static_cast<uint32_t>(length & 0xffu) << 24;
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
        length >>= 8;
    }
    return ~crc;
}

static std::string stable_seed_key(const std::string& key) {
    // Keep existing fixtures stable when their displayed case IDs are renumbered.
    std::string result = key;
    for (size_t i = 0; i + 5 <= result.size(); ++i) {
        if ((i && result[i - 1] != '/') || result[i] < '0' || result[i] > '9' ||
            result[i + 1] < '0' || result[i + 1] > '9' ||
            result[i + 2] < '0' || result[i + 2] > '9' ||
            result[i + 3] < '0' || result[i + 3] > '9' || result[i + 4] != '_') continue;
        unsigned number = static_cast<unsigned>((result[i] - '0') * 1000 +
                                                (result[i + 1] - '0') * 100 +
                                                (result[i + 2] - '0') * 10 +
                                                (result[i + 3] - '0'));
        unsigned original = number;
        if (number >= 5u && number <= 112u) original = number - 3u;
        else if (number >= 113u && number <= 127u) original = number - 4u;
        if (original != number) {
            result[i] = static_cast<char>('0' + original / 1000u);
            result[i + 1] = static_cast<char>('0' + (original / 100u) % 10u);
            result[i + 2] = static_cast<char>('0' + (original / 10u) % 10u);
            result[i + 3] = static_cast<char>('0' + original % 10u);
        }
        break;
    }
    return result;
}

static unsigned case_seed(const std::string& key) {
    return crc_cksum(std::to_string(master_seed) + ":" + stable_seed_key(key)) & 0x7fffffffu;
}

static fs::path make_work_dir(const std::string& label) {
    fs::path pattern = output_root / (label + "_work.XXXXXX");
    std::string mutable_pattern = pattern.string();
    std::vector<char> buffer(mutable_pattern.begin(), mutable_pattern.end());
    buffer.push_back('\0');
    char* made = mkdtemp(buffer.data());
    if (!made) throw Failure("mkdtemp failed: " + std::string(std::strerror(errno)));
    return fs::path(made);
}

static ProcessResult run_cli(const fs::path& work,
                             const std::vector<std::string>& args,
                             const std::string& tag,
                             const fs::path& stdin_file = {},
                             const std::map<std::string, std::string>& env = {}) {
    std::vector<std::string> command{makocode_bin.string()};
    command.insert(command.end(), args.begin(), args.end());
    ProcessResult result = run_process(command, work, stdin_file,
        work / (tag + ".stdout"), work / (tag + ".stderr"), env);
    return result;
}

static ProcessResult run_transform(const fs::path& work,
                                   const std::vector<std::string>& args,
                                   const std::string& tag,
                                   const std::map<std::string, std::string>& env = {}) {
    std::vector<std::string> command{test_executable.string(), "--ppm-transform"};
    command.insert(command.end(), args.begin(), args.end());
    return run_process(command, work, {}, work / (tag + ".stdout"), work / (tag + ".stderr"),env);
}

static void expect_success(const ProcessResult& result, const std::string& label) {
    if (result.status != 0) throw Failure(label + " failed (exit " + std::to_string(result.status) + "): " + result.err);
}

static void expect_failure(const ProcessResult& result, const std::string& label) {
    if (result.status == 0) throw Failure(label + " unexpectedly succeeded");
}

static std::vector<fs::path> matching_files(const fs::path& directory, const std::string& prefix, const std::string& suffix) {
    std::vector<fs::path> result;
    if (!fs::exists(directory)) return result;
    for (const auto& item : fs::directory_iterator(directory)) {
        if (!item.is_regular_file()) continue;
        const std::string name = item.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && (suffix.empty() || (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0))) result.push_back(item.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

static std::vector<unsigned char> decode_stdout_stream(const std::string& report, const std::string& label) {
    std::istringstream lines(report);
    std::string line;
    std::vector<std::string> all;
    while (std::getline(lines, line)) all.push_back(line);
    require(!all.empty() && all.front() == "MAKOCODE-DECODED", label + ": missing decoded header");
    size_t stream_index = all.size();
    bool file_entry = false;
    for (size_t i = 0; i < all.size(); ++i) if (all[i].rfind("stream ", 0) == 0) {
        require(stream_index == all.size(), label + ": multiple stream entries"); stream_index = i;
    }
    if (stream_index == all.size()) {
        for(size_t i=0;i<all.size();++i)if(all[i].rfind("file payload.bin ",0)==0){
            require(stream_index==all.size(),label+": multiple payload files");stream_index=i;file_entry=true;
        }
    }
    require(stream_index + 1 < all.size(), label + ": missing decoded payload entry");
    uint64_t expected_size = 0;
    try {
        size_t start=file_entry?std::string("file payload.bin ").size():std::string("stream ").size();
        expected_size = std::stoull(all[stream_index].substr(start));
    }
    catch (...) { throw Failure(label + ": invalid stream size"); }
    const std::string& encoded = all[stream_index + 1];
    std::vector<unsigned char> result;
    for (size_t i = 0; i < encoded.size();) {
        unsigned char ch = static_cast<unsigned char>(encoded[i]);
        if (ch == '\\') {
            require(i + 3 < encoded.size() && encoded[i + 1] == 'x', label + ": malformed byte escape");
            char hex[3] = {encoded[i + 2], encoded[i + 3], 0};
            char* end = nullptr;
            long value = std::strtol(hex, &end, 16);
            require(end && *end == 0, label + ": invalid hex escape");
            result.push_back(static_cast<unsigned char>(value)); i += 4;
        } else {
            require(ch >= 32 && ch <= 126, label + ": unexpected nonprintable byte");
            result.push_back(ch); ++i;
        }
    }
    require(result.size() == expected_size, label + ": stream size mismatch");
    require(std::find(all.begin(), all.end(), "end-archive") != all.end() && all.back() == "end", label + ": incomplete decoded report");
    return result;
}

static std::vector<unsigned char> expected_payload(const std::string& literal, size_t size, unsigned seed, const fs::path& work, const std::string& tag) {
    if (!literal.empty()) return std::vector<unsigned char>(literal.begin(), literal.end());
    fs::path path = work / (tag + ".bin");
    ProcessResult generated = run_transform(work, {"bytes", "--output", path.string(), "--size", std::to_string(size), "--seed", std::to_string(seed)}, tag + "_gen");
    expect_success(generated, "payload generation");
    std::string bytes = read_file(path);
    return std::vector<unsigned char>(bytes.begin(), bytes.end());
}

static void check_decoded(const std::string& report, const std::vector<unsigned char>& expected, double tolerance, const std::string& label) {
    std::vector<unsigned char> actual = decode_stdout_stream(report, label);
    size_t mismatch = 0;
    size_t common = std::min(actual.size(), expected.size());
    for (size_t i = 0; i < common; ++i) mismatch += actual[i] != expected[i];
    mismatch += actual.size() > expected.size() ? actual.size() - expected.size() : expected.size() - actual.size();
    size_t denominator = std::max<size_t>(1, std::max(actual.size(), expected.size()));
    double percent = 100.0 * static_cast<double>(mismatch) / static_cast<double>(denominator);
    require(percent <= tolerance, label + ": payload mismatch " + std::to_string(percent) + "% > " + std::to_string(tolerance) + "%");
}

static std::vector<std::string> ppm_paths(const fs::path& dir, const std::string& prefix) {
    return [&] { std::vector<std::string> out; for (const auto& p : matching_files(dir, prefix, ".ppm")) out.push_back(p.string()); return out; }();
}

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream input(line);
    std::string field;
    while (std::getline(input, field, ',')) fields.push_back(field);
    return fields;
}

static void check_fiducials(const fs::path& encoded_csv, const fs::path& decoded_csv, const std::string& label) {
    std::ifstream encoded(encoded_csv), decoded(decoded_csv);
    require(encoded.good() && decoded.good(), label + ": missing fiducial CSV");
    std::string line;
    std::getline(encoded, line); std::getline(decoded, line);
    size_t matched = 0, expected = 0;
    std::map<std::string, std::pair<double,double>> positions;
    while (std::getline(encoded, line)) {
        auto f = split_csv(line);
        if (f.size() < 5) continue;
        std::string key = f[0] + "," + f[1] + "," + f[2];
        positions[key] = {std::strtod(f[3].c_str(), nullptr), std::strtod(f[4].c_str(), nullptr)};
        ++expected;
    }
    while (std::getline(decoded, line)) {
        auto f = split_csv(line);
        if (f.size() < 7) continue;
        std::string key = f[0] + "," + f[1] + "," + f[2];
        auto found = positions.find(key);
        if (found == positions.end()) continue;
        double x = std::strtod(f[5].c_str(), nullptr), y = std::strtod(f[6].c_str(), nullptr);
        require(std::fabs(found->second.first - x) <= 4.0 && std::fabs(found->second.second - y) <= 4.0,
                label + ": fiducial position error exceeds 4 px");
        ++matched;
    }
    require(matched == expected, label + ": not all encoded fiducials were recovered");
}

static void compare_files(const fs::path& expected, const fs::path& actual, const std::string& label) {
    require(read_file(expected) == read_file(actual), label + ": file contents differ");
}

static std::vector<std::string> encode_file_args(const fs::path& input, const fs::path& out,
                                                const std::string& prefix,
                                                const std::vector<std::string>& extras = {}) {
    std::vector<std::string> args{"encode", "--input=" + input.filename().string(),
        "--output-dir=" + out.string(), "--prefix=" + prefix};
    args.insert(args.end(), extras.begin(), extras.end());
    return args;
}

static std::vector<std::string> decode_file_args(const std::vector<std::string>& pages,
                                                const fs::path& output,
                                                const std::vector<std::string>& extras = {}) {
    std::vector<std::string> args{"decode", "--output-dir=" + output.string()};
    args.insert(args.end(), extras.begin(), extras.end());
    args.insert(args.end(), pages.begin(), pages.end());
    return args;
}

static void generate_bytes(const fs::path& work, const fs::path& path, size_t size, unsigned seed, const std::string& tag) {
    ProcessResult result = run_transform(work, {"bytes", "--output", path.string(), "--size", std::to_string(size), "--seed", std::to_string(seed)}, tag);
    expect_success(result, tag);
}

static void decode_failure_case(const std::string& id, const std::string& kind) {
    fs::path work = make_work_dir(id);
    fs::path page = work / (kind + ".ppm");
    if (kind == "wrong_depth") write_file(page, "P6\n2 2\n42\n\n");
    else if (kind == "invalid_magic") write_file(page, "P6\n2 2\n255\n");
    else if (kind == "all_black" || kind == "all_white") {
        int color = kind == "all_black" ? 0 : 255;
        ProcessResult made = run_transform(work, {"solid", "--output", page.string(), "--width", "256", "--height", "256", "--r", std::to_string(color), "--g", std::to_string(color), "--b", std::to_string(color)}, "solid");
        expect_success(made, id + " image generation");
    } else if (kind == "random_noise") {
        ProcessResult made = run_transform(work, {"noise", "--output", page.string(), "--width", "256", "--height", "256", "--seed", "1337"}, "noise");
        expect_success(made, id + " noise generation");
    } else if (kind == "footer_data_destroyed" || kind == "footer_valid_data_too_corrupt") {
        size_t size = kind == "footer_data_destroyed" ? 4096 : 32768;
        unsigned seed = case_seed("payload/" + id + "/" + kind);
        fs::path input = work / "payload.bin"; generate_bytes(work, input, size, seed, "payload");
        std::string prefix = id + "_" + kind;
        unsigned crypto = case_seed("crypto/" + id + "/" + kind);
        auto encode = encode_file_args(input, work, prefix,
            kind == "footer_data_destroyed" ? std::vector<std::string>{"--page-width=260", "--page-height=260", "--debug"} : std::vector<std::string>{"--page-width=1000", "--page-height=1000", "--debug"});
        ProcessResult encoded = run_cli(work, encode, "encode", {}, {{"MAKOCODE_TEST_SEED", std::to_string(crypto)}});
        expect_success(encoded, id + " encode");
        auto pages = ppm_paths(work, prefix);
        require(pages.size() == 1, id + ": expected one PPM page");
        fs::path damaged = work / "damaged.ppm";
        std::vector<std::string> corrupt{"corrupt-footer-data-destroyed", "--input", pages.front(), "--output", damaged.string(), "--footer-height-px", "8"};
        if (kind == "footer_valid_data_too_corrupt") corrupt[0] = "corrupt-footer-valid-data-too-corrupt";
        ProcessResult changed = run_transform(work, corrupt, "corrupt"); expect_success(changed, "footer corruption");
        fs::path decoded = work / "decoded";
        ProcessResult result = run_cli(work, {"decode", damaged.string(), "--debug", "--output-dir=" + decoded.string()}, "decode");
        expect_failure(result, id + " decode");
        require(fs::exists(decoded / "decode_fiducials.csv"), id + ": decode did not emit fiducial CSV");
        if (kind == "footer_data_destroyed") require(result.out.find("decode: ECC could not repair the payload") != std::string::npos || result.err.find("decode: ECC could not repair the payload") != std::string::npos,
            id + ": expected ECC failure was not reported");
        check_fiducials(work / (prefix + "_encode_fiducials.csv"), decoded / "decode_fiducials.csv", id);
        return;
    }
    fs::path decoded = work / "decoded";
    ProcessResult result = run_cli(work, {"decode", page.string(), "--output-dir=" + decoded.string()}, "decode");
    expect_failure(result, id + " decode");
}

static void cli_output_dir_case() {
    const std::string id = "0032_cli_output_dir";
    fs::path work = make_work_dir(id);
    fs::path payload = work / "cli_payload.bin";
    write_file(payload, "encode-decode-cli test payload");
    auto encoded = run_cli(work, encode_file_args(payload, work, "cli_payload", {"--debug"}), "encode");
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, "cli_payload");
    require(!pages.empty() && fs::exists(work / "cli_payload_encode_fiducials.csv"), id + ": encode output missing");
    fs::path decoded = work / "decoded";
    auto result = run_cli(work, decode_file_args(pages, decoded, {"--debug"}), "decode");
    expect_success(result, id + " decode");
    check_fiducials(work / "cli_payload_encode_fiducials.csv", decoded / "decode_fiducials.csv", id);
    compare_files(payload, decoded / "cli_payload.bin", id);
}

static void palette_metadata_case(bool wrong_palette) {
    std::string id = wrong_palette ? "0041_palette_wrong_rejected" : "0040_palette_auto_discovery";
    fs::path work = make_work_dir(id);
    fs::path payload = work / "payload.bin";
    generate_bytes(work, payload, 16384, case_seed("payload/" + id + (wrong_palette ? "/wrong" : "/auto")), "payload");
    std::string prefix = id + "_page";
    auto encoded = run_cli(work, encode_file_args(payload, work, prefix, {"--palette", "White Cyan Magenta Yellow", "--debug"}), "encode",
                           {}, {{"MAKOCODE_TEST_SEED", std::to_string(case_seed("crypto/" + id + (wrong_palette ? "/wrong" : "/auto")))}});
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, prefix); require(!pages.empty(), id + ": no encoded PPM");
    fs::path decoded = work / "decoded";
    std::vector<std::string> args{"decode", pages.front(), "--output-dir=" + decoded.string(), "--debug"};
    if (wrong_palette) args.insert(args.end(), {"--palette", "White Black"});
    auto result = run_cli(work, args, "decode");
    if (wrong_palette) expect_failure(result, id + " decode"); else expect_success(result, id + " decode");
    require(fs::exists(decoded / "decode_fiducials.csv"), id + ": missing decode fiducial CSV");
    if (!wrong_palette) {
        check_fiducials(work / (prefix + "_encode_fiducials.csv"), decoded / "decode_fiducials.csv", id);
        compare_files(payload, decoded / "payload.bin", id);
    }
}

static void password_failure_case() {
    const std::string id = "0042_password_failures";
    fs::path work = make_work_dir(id), payload = work / "payload.bin";
    generate_bytes(work, payload, 32768, case_seed("payload/" + id), "payload");
    const std::string password = "suite-password", prefix = id + "_page";
    auto encoded = run_cli(work, encode_file_args(payload, work, prefix, {"--page-width=1000", "--page-height=1000", "--password=" + password,
        "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb", "--debug"}), "encode", {},
        {{"MAKOCODE_TEST_SEED", std::to_string(case_seed("crypto/" + id))}});
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, prefix); require(pages.size() == 1, id + ": expected one encrypted page");
    for (const auto& credentials : {std::vector<std::string>{}, std::vector<std::string>{"--password=bad-password"}}) {
        fs::path decoded = work / (credentials.empty() ? "no_password" : "wrong_password");
        std::vector<std::string> args{"decode", pages.front(), "--output-dir=" + decoded.string(), "--debug"};
        args.insert(args.end(), credentials.begin(), credentials.end());
        auto result = run_cli(work, args, "decode_" + decoded.filename().string());
        expect_failure(result, id + " decode");
        check_fiducials(work / (prefix + "_encode_fiducials.csv"), decoded / "decode_fiducials.csv", id);
    }
}

static void header_copy_corruption_case() {
    const std::string id = "0043_header_copy_corruption";
    fs::path work = make_work_dir(id), payload = work / "random.bin";
    generate_bytes(work, payload, 4096, case_seed("payload/" + id), "payload");
    const std::string prefix = id + "_page";
    auto encoded = run_cli(work, encode_file_args(payload, work, prefix, {"--ecc=fill", "--page-width=600", "--page-height=600", "--debug"}), "encode",
        {}, {{"MAKOCODE_TEST_SEED", std::to_string(case_seed("crypto/" + id))}});
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, prefix); require(!pages.empty(), id + ": no page output");
    for (int count=1; count<=3; ++count) {
        fs::path damaged=work/("corrupt_"+std::to_string(count)+".ppm"), decoded=work/("decoded_"+std::to_string(count));
        auto changed=run_transform(work,{"transform","--input",pages.front(),"--output",damaged.string(),"--corrupt-former-header-slots",std::to_string(count)},"transform");
        expect_success(changed,id+" transform");
        auto result=run_cli(work,{"decode",damaged.string(),"--output-dir="+decoded.string(),"--debug"},"decode_"+std::to_string(count));
        expect_success(result,id+" decode");
        check_fiducials(work/(prefix+"_encode_fiducials.csv"),decoded/"decode_fiducials.csv",id);
        compare_files(payload,decoded/"random.bin",id);
    }
}

static void decode_stdin_case() {
    const std::string id = "0060_decode_stdin";
    fs::path work=make_work_dir(id),payload=work/"payload.bin";
    std::string contents("\0stdin coverage payload\xff\n",26); write_file(payload,contents);
    const std::string prefix=id+"_stdin";
    auto encoded=run_cli(work,encode_file_args(payload,work,prefix,{"--debug"}),"encode"); expect_success(encoded,id+" encode");
    auto pages=ppm_paths(work,prefix);require(pages.size()==1,id+": expected one page");
    fs::path decoded=work/"decoded";
    auto result=run_cli(work,{"decode","--output-dir="+decoded.string(),"--debug"},"decode",pages.front());expect_success(result,id+" decode stdin");
    check_fiducials(work/(prefix+"_encode_fiducials.csv"),decoded/"decode_fiducials.csv",id);
    compare_files(payload,decoded/"payload.bin",id);
}

static void archive_tree_case() {
    const std::string id="0057_archive_tree_roundtrip";
    fs::path work=make_work_dir(id),source=work/"source",pages_dir=work/"pages",decoded=work/"decoded";
    fs::create_directories(source/"tree/empty");fs::create_directories(source/"tree/nested/deep_empty");
    fs::create_directories(source/"empty_only/nested/leaf_empty");fs::create_directories(pages_dir);
    write_file(source/"tree/root.txt","x");write_file(source/"tree/nested/leaf.txt","y");write_file(source/"tree/zero.bin","");
    auto encoded=run_cli(source,{"encode","--input=tree","--input=empty_only","--prefix=archive_tree","--debug","--output-dir="+pages_dir.string()},"encode");
    expect_success(encoded,id+" encode");
    auto pages=ppm_paths(pages_dir,"");require(!pages.empty(),id+": no pages");
    auto result=run_cli(work,decode_file_args(pages,decoded,{"--debug"}),"decode");expect_success(result,id+" decode");
    check_fiducials(pages_dir/"archive_tree_encode_fiducials.csv",decoded/"decode_fiducials.csv",id);
    for(const auto& name:{std::string("tree"),std::string("empty_only")}){
        fs::path a=source/name,b=decoded/name;require(fs::is_directory(b),id+": missing directory "+name);
        std::vector<std::string> aa,bb;
        for(const auto& e:fs::recursive_directory_iterator(a))aa.push_back(fs::relative(e.path(),a).string()+(e.is_directory()?"/":""));
        for(const auto& e:fs::recursive_directory_iterator(b))bb.push_back(fs::relative(e.path(),b).string()+(e.is_directory()?"/":""));
        std::sort(aa.begin(),aa.end());std::sort(bb.begin(),bb.end());require(aa==bb,id+": directory tree differs for "+name);
    }
    for(const auto& path:{"root.txt","nested/leaf.txt","zero.bin"})compare_files(source/"tree"/path,decoded/"tree"/path,id);
}

static void archive_symlink_case() {
    const std::string id="0064_archive_symlink_roundtrip";
    fs::path work=make_work_dir(id),source=work/"source",input=source/"input",pages_dir=work/"pages",decoded=work/"decoded";
    fs::create_directories(input);fs::create_directories(pages_dir);
    write_file(source/"program","target contents from above the encoded directory\n");
    fs::create_symlink("../program",input/"program");
    auto encoded=run_cli(source,{"encode","--input=input","--output-dir="+pages_dir.string()},"encode");expect_success(encoded,id+" encode");
    auto pages=ppm_paths(pages_dir,"");require(!pages.empty(),id+": encode emitted no pages");
    auto result=run_cli(work,decode_file_args(pages,decoded),"decode");expect_success(result,id+" decode");
    require(fs::is_regular_file(decoded/"input/program")&&!fs::is_symlink(decoded/"input/program"),id+": symlink was not restored as regular file");
    compare_files(source/"program",decoded/"input/program",id);
    require(fs::is_symlink(input/"program")&&fs::exists(source/"program"),id+": source symlink changed");
}

static void default_naming_case() {
    const std::string id="0067_default_encode_naming";
    fs::path work=make_work_dir(id),pages_dir=work/"pages",payload=work/"payload.bin";fs::create_directories(pages_dir);
    write_file(payload,std::string("\0default timestamp naming payload\xff\n",35));
    auto result=run_cli(work,{"encode","--input=payload.bin","--palette=Cyan Black","--output-dir="+pages_dir.string(),"--debug"},"encode");expect_success(result,id+" encode");
    auto pages=matching_files(pages_dir,"", ".ppm"),csvs=matching_files(pages_dir,"", "_encode_fiducials.csv");
    require(pages.size()==1&&csvs.size()==1,id+": expected one timestamped PPM and CSV");
    std::regex pattern("^[0-9]{8}T[0-9]{6}Z\\.ppm$");
    auto name=pages.front().filename().string();require(std::regex_match(name,pattern),id+": unexpected default PPM filename "+name);
    require(csvs.front().filename().string()==name.substr(0,name.size()-4)+"_encode_fiducials.csv",id+": timestamp CSV name does not match PPM");
}

static void fiducials_case() {
    const std::string id="0066_fiducials",prefix=id+"_fiducials";
    fs::path work=make_work_dir(id),pages_dir=work/"pages",decoded=work/"decoded",payload=work/"payload.bin";fs::create_directories(pages_dir);
    write_file(payload,std::string("\0fiducials custom grid payload\xff\n",31));
    auto encoded=run_cli(work,{"encode","--input=payload.bin","--page-width=240","--page-height=240","--fiducial-spacing=30","--prefix="+prefix,"--output-dir="+pages_dir.string(),"--debug"},"encode");expect_success(encoded,id+" encode");
    require(fs::exists(pages_dir/(prefix+"_encode_fiducials.csv")),id+": encode debug CSV missing");
    auto pages=ppm_paths(pages_dir,prefix);require(pages.size()==1,id+": expected one output page");
    auto image=read_ppm(pages.front());size_t white=0;
    for(unsigned y=29;y<31;++y)for(unsigned x=29;x<31;++x){size_t i=(static_cast<size_t>(y)*image.width+x)*3;white+=static_cast<unsigned char>(image.pixels[i])==255&&static_cast<unsigned char>(image.pixels[i+1])==255&&static_cast<unsigned char>(image.pixels[i+2])==255;}
    require(image.width>=31&&image.height>=31&&white==4,id+": first 2x2 grayscale marker is not white");
    auto result=run_cli(work,decode_file_args(pages,decoded),"decode");expect_success(result,id+" decode");compare_files(payload,decoded/"payload.bin",id);
}

static void ppm_transform_cli_case() {
    const std::string id="0070_ppm_transform_cli";fs::path work=make_work_dir(id);
    auto help=run_process({test_executable.string(),"--ppm-transform","--help"},work,{},work/"help.out",work/"help.err");
    require(help.status==0&&help.err.find("Usage:")!=std::string::npos,id+": helper help did not print usage");
    auto no_command=run_process({test_executable.string(),"--ppm-transform"},work,{},work/"none.out",work/"none.err");
    require(no_command.status==1&&no_command.err.find("Usage:")!=std::string::npos,id+": helper no-command path changed");
    auto missing=run_process({test_executable.string(),"--ppm-transform","bytes-len"},work,{},work/"missing.out",work/"missing.err");
    require(missing.status==1&&missing.err.find("bytes-len requires TEXT")!=std::string::npos,id+": bytes-len error changed");
    auto unknown=run_process({test_executable.string(),"--ppm-transform","unknown-command"},work,{},work/"unknown.out",work/"unknown.err");
    require(unknown.status==1&&unknown.err.find("unknown command unknown-command")!=std::string::npos,id+": unknown command error changed");
}

static void cli_help_case() {
    const std::string id="0058_cli_help";fs::path work=make_work_dir(id);
    struct HelpCheck{std::vector<std::string> args;const char* first;const char* second;};
    std::vector<HelpCheck> help={{{},"Usage:","makocode encode [options]"},{{"--help"},"Usage:","makocode encode [options]"},{{"-h"},"Usage:","makocode encode [options]"},{{"unknown-command"},"Usage:","makocode encode [options]"},{{"encode","--help"},"Usage: makocode encode","--input PATH"},{{"decode","--help"},"Usage: makocode decode","stdin"},{{"checkpoint","--help"},"makocode checkpoint","checkpoint restore"},{{"overlay","--help"},"Usage:","makocode encode [options]"},{{"minify","--help"},"Usage: makocode minify","Strips comments/whitespace"}};
    for(size_t i=0;i<help.size();++i){auto result=run_cli(work,help[i].args,"help_"+std::to_string(i));require(result.status==0,id+": help command failed");std::string all=result.out+result.err;require(all.find(help[i].first)!=std::string::npos&&all.find(help[i].second)!=std::string::npos,id+": missing expected help text");}
    for(const auto& pair:std::vector<std::pair<std::vector<std::string>,std::string>>{
        {{"encode","--ecc=254.01"},"--ecc ratio must be between 0 and 254"},
        {{"encode","--overlay-fraction=1.01"},"--overlay-fraction must be between 0.0 and 254/255"},
        {{"encode","--overlay-fraction=0.5"},"--overlay-fraction and overlay images must be used together"},
        {{"encode","--overlay-ppm=missing.ppm"},"each --overlay-ppm requires a following --overlay-pages"}}){
        auto result=run_cli(work,pair.first,"reject");expect_failure(result,id+" rejection");require(result.err.find(pair.second)!=std::string::npos||result.out.find(pair.second)!=std::string::npos,id+": rejection text mismatch");
    }
    auto encode_help=run_cli(work,{"encode","--help"},"encode_help");expect_success(encode_help,id+" encode help");
    require(encode_help.out.find("Omit to use the minimum ECC")!=std::string::npos&&encode_help.out.find("0 disables payload ECC")!=std::string::npos&&encode_help.out.find("255 total symbols")!=std::string::npos,id+": encode help contract changed");
}

static void decode_ecc_retry_case() {
    const std::string id="0071_decode_ecc_retry";fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded_dir=work/"encoded",output=work/"output";fs::create_directories(encoded_dir);
    generate_bytes(work,payload,8192,1337,"payload");
    auto encoded=run_cli(work,encode_file_args(payload,encoded_dir,id,{"--page-width=500","--page-height=500"}),"encode");expect_success(encoded,id+" encode");
    auto pages=ppm_paths(encoded_dir,id);require(pages.size()==1,id+": expected one page");
    fs::path damaged=work/"invalid_stream.ppm";
    auto changed=run_transform(work,{"corrupt-footer-valid-data-too-corrupt","--input",pages.front(),"--output",damaged.string(),"--border-keep","80","--seed","1337"},"corrupt");expect_success(changed,id+" corrupt");
    auto result=run_cli(work,{"decode",damaged.string(),"--output-dir="+output.string()},"decode");expect_failure(result,id+" decode");
    std::string log=result.out+result.err;const std::string retry="decode: ECC could not repair the payload; retrying without fiducial subgrid";const std::string terminal="decode: ECC could not repair the payload";
    size_t retry_count=0,terminal_count=0;std::istringstream lines(log);std::string line;while(std::getline(lines,line)){retry_count+=line==retry;terminal_count+=line==terminal;}
    require(retry_count==1&&terminal_count==1,id+": retry/terminal ECC messages were not each emitted once");
}

static void page_fit_case(){
    const std::string id="0065_page_fit";fs::path work=make_work_dir(id);
    unsigned seed=case_seed("page_fit/"+id);
    fs::path payload=work/"payload.bin";
    generate_bytes(work,payload,8192,seed,"payload");
    auto bytes=read_file(payload);
    for(const std::string& mode:{"both","height","width"}){
        fs::path pages_dir=work/(mode+"_pages");fs::create_directories(pages_dir);
        std::vector<std::string> args={"encode","--output-dir="+pages_dir.string(),"--prefix="+mode};
        if(mode=="height")args.push_back("--page-width=500");else if(mode=="width")args.push_back("--page-height=500");
        auto encoded=run_cli(work,args,"encode_"+mode,payload);expect_success(encoded,id+" "+mode+" encode");
        auto pages=ppm_paths(pages_dir,mode);require(pages.size()==1,id+" "+mode+": expected one page");
        auto image=read_ppm(pages.front());
        if(mode=="both")require(image.width<2480&&image.height<3508,id+": automatic dimensions did not fit");
        if(mode=="height")require(image.width==500&&image.height<3508,id+": height-only fitting dimensions incorrect");
        if(mode=="width")require(image.height==500&&image.width<2480,id+": width-only fitting dimensions incorrect");
        auto decoded=run_cli(work,{"decode","--decoded-stdout"},"decode_"+mode,pages.front());expect_success(decoded,id+" "+mode+" decode");
        std::vector<unsigned char> expected(bytes.begin(),bytes.end());check_decoded(decoded.out,expected,0,id+" "+mode);
    }
    fs::path tiny=work/"tiny.bin",tiny_pages=work/"tiny_pages";write_file(tiny,"makocode");fs::create_directories(tiny_pages);
    auto encoded=run_cli(work,{"encode","--page-width=160","--page-height=160","--output-dir="+tiny_pages.string(),"--prefix=tiny"},"tiny_encode",tiny);
    expect_success(encoded,id+" tiny encode");auto pages=ppm_paths(tiny_pages,"tiny");require(pages.size()==1,id+": small fixed page expected one page");
    auto image=read_ppm(pages.front());size_t white=0;
    for(size_t i=0;i<image.pixels.size();i+=3)white+=static_cast<unsigned char>(image.pixels[i])==255&&static_cast<unsigned char>(image.pixels[i+1])==255&&static_cast<unsigned char>(image.pixels[i+2])==255;
    require(static_cast<double>(white)/(image.width*image.height)>.60,id+": small page lacks large white margin");
    auto decoded=run_cli(work,{"decode","--decoded-stdout"},"tiny_decode",pages.front());expect_success(decoded,id+" tiny decode");
    check_decoded(decoded.out,std::vector<unsigned char>{'m','a','k','o','c','o','d','e'},0,id+" small-page margin");
}

static void prefix_ends_encoded_case(){
    const std::string id="0062_prefix_ends_encoded",prefix=id+"_payload_encoded";
    fs::path work=make_work_dir(id),pages_dir=work/"pages",decoded=work/"decoded",payload=work/"payload.bin";fs::create_directories(pages_dir);
    std::string data("\0prefix-encoded roundtrip payload\xff\n",35);write_file(payload,data);
    auto encoded=run_cli(work,{"encode","--input=payload.bin","--page-width=80","--page-height=80","--palette=Cyan Black","--prefix="+prefix,"--output-dir="+pages_dir.string(),"--debug"},"encode");expect_success(encoded,id+" encode");
    auto pages=ppm_paths(pages_dir,"");require(pages.size()==1&&fs::path(pages[0]).filename()==prefix+".ppm",id+": prefix filename changed");
    require(fs::exists(pages_dir/(prefix+"_encode_fiducials.csv")),id+": missing fiducial CSV");
    PpmImage image=read_ppm(pages.front());size_t cyan=0;
    for(unsigned y=15;y<17;++y)for(unsigned x=15;x<17;++x){size_t i=(static_cast<size_t>(y)*image.width+x)*3;cyan+=static_cast<unsigned char>(image.pixels[i])==0&&static_cast<unsigned char>(image.pixels[i+1])==255&&static_cast<unsigned char>(image.pixels[i+2])==255;}
    require(cyan==4,id+": top-left marker did not use cyan palette endpoint");
    auto result=run_cli(work,decode_file_args(pages,decoded,{"--debug"}),"decode");expect_success(result,id+" decode");
    check_fiducials(pages_dir/(prefix+"_encode_fiducials.csv"),decoded/"decode_fiducials.csv",id);compare_files(payload,decoded/"payload.bin",id);
}

static void metadata_pages_case(){
    const std::string id="0123_metadata_pages";fs::path work=make_work_dir(id);fs::path payload=work/"payload.bin";
    unsigned seed=case_seed("metadata_pages/payload");generate_bytes(work,payload,3000,seed,"payload");std::string payload_bytes=read_file(payload);
    struct Selection{const char* name;const char* selector;std::vector<int> pages;};
    const Selection selections[]={{"middle","middle",{2}},{"one","1",{1}},{"range","2-3",{2,3}},{"list","1,3",{1,3}},{"last","last",{3}}};
    for(const auto& selection:selections){
        fs::path out=work/(std::string(selection.name)+"_pages");fs::create_directories(out);
        auto encoded=run_cli(work,{"encode","--input=payload.bin","--output-dir="+out.string(),std::string("--prefix=")+selection.name,"--page-width=220","--page-height=220","--ecc=3.5",std::string("--metadata-pages=")+selection.selector},std::string("encode_")+selection.name,payload);
        expect_success(encoded,id+" encode "+selection.name);auto pages=ppm_paths(out,selection.name);require(pages.size()==3,id+": expected three serialized pages");
        auto decodeargs=std::vector<std::string>{"decode","--decoded-stdout"};decodeargs.insert(decodeargs.end(),pages.begin(),pages.end());
        auto decoded=run_cli(work,decodeargs,std::string("decode_")+selection.name);expect_success(decoded,id+" decode "+selection.name);check_decoded(decoded.out,std::vector<unsigned char>(payload_bytes.begin(),payload_bytes.end()),0,id+" "+selection.name);
        std::istringstream lines(decoded.out);std::string line;int current=0;std::vector<int> observed;
        while(std::getline(lines,line)){if(line.rfind("page ",0)==0)current=std::atoi(line.c_str()+5);if(line.rfind("metadata tile_schema ",0)==0)observed.push_back(current);}
        require(observed==selection.pages,id+": selected metadata tile pages differ");
    }
}

static void decoded_file_case(){
    const std::string id="0086_decoded_file";fs::path work=make_work_dir(id),input=work/"input",pages_dir=work/"pages",out=work/"out",stdin_out=work/"stdin_out";
    fs::create_directories(input/"empty");fs::create_directories(input/"sub");fs::create_directories(pages_dir);fs::create_directories(out);
    write_file(input/"sub"/"a b",std::string("hello\nback\\slash\0\xff",18));write_file(input/"blank","");
    auto enc=run_cli(work,{"encode","--input=input","--password=secret","--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb","--prefix=page","--output-dir=pages"},"encode",{},{{"MAKOCODE_TEST_SEED",std::to_string(case_seed("crypto/"+id))}});
    expect_success(enc,id+" encode");auto pages=ppm_paths(pages_dir,"page");require(!pages.empty(),id+": no encoded pages");
    auto decoded=run_cli(work,decode_file_args(pages,out,{"--decoded-file","--password=secret"}),"decoded_file");expect_success(decoded,id+" decode file");
    auto text_files=matching_files(out,"makocode_decoded_",".txt");require(text_files.size()==1,id+": expected one timestamped decoded file");
    std::regex name_re("^makocode_decoded_[0-9]{14}\\.txt$");require(std::regex_match(text_files[0].filename().string(),name_re),id+": invalid decoded filename");
    std::string report=read_file(text_files[0]);
    for(const std::string& required:{"MAKOCODE-DECODED","end-archive\nend","metadata encryption_version 1","metadata ecc_enabled 1","metadata tile_schema 0","metadata lzma_uncompressed_bytes ","directory input/empty","file input/blank 0","file input/sub/a\\x20b 18","hello\\x0Aback\\x5Cslash\\x00\\xFF"})
        require(report.find(required)!=std::string::npos,id+": decoded report missing "+required);
    for(const std::string& forbidden:{"metadata tile_magic ","metadata metadata_magic ","metadata metadata_bytes ","metadata metadata_crc8 ","metadata tile_header ","metadata tile_data ","metadata compression ","metadata lzma_properties "})
        require(report.find(forbidden)==std::string::npos,id+": decoded report exposed omitted metadata");
    for(const auto& field:std::vector<std::pair<std::string,size_t>>{{"encryption_salt",32},{"encryption_nonce",24},{"encryption_tag",32}}){
        std::string prefix="metadata "+field.first+" ";size_t pos=report.find(prefix);require(pos!=std::string::npos,id+": missing "+field.first);pos+=prefix.size();size_t end=report.find('\n',pos);std::string hex=report.substr(pos,end-pos);
        require(hex.size()==field.second,id+": wrong hex length for "+field.first);for(char ch:hex)require((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F'),id+": invalid hex metadata");
    }
    fs::create_directories(stdin_out);auto stdin_decode=run_cli(work,{"decode","--decoded-file","--password=secret","--output-dir="+stdin_out.string()},"decode_stdin",pages.front());
    expect_success(stdin_decode,id+" stdin decoded-file");auto stdin_reports=matching_files(stdin_out,"makocode_decoded_",".txt");require(stdin_reports.size()==1,id+": stdin decoded-file output missing");compare_files(text_files[0],stdin_reports[0],id+" stdin");
    auto stdout_decode=run_cli(work,{"decode","--decoded-stdout","--password=secret","--output-dir="+(work/"not_created").string(),pages.front()},"decode_stdout");
    expect_success(stdout_decode,id+" stdout");require(stdout_decode.out==report,id+": stdout output differs from decoded file");require(!fs::exists(work/"not_created"),id+": decoded-stdout created output directory");
    for(const std::string& flag:{"--decoded-file","--debug"}){
        auto conflict=run_cli(work,{"decode","--decoded-stdout",flag,pages.front()},"conflict");expect_failure(conflict,id+" conflicting mode");require(conflict.out.empty(),id+": conflicting mode wrote output");
    }
    fs::path plain=work/"plain.bin",plain_pages=work/"plain_pages",plain_out=work/"plain_out";std::vector<unsigned char> deterministic(3000);
    uint32_t state=42;for(auto& byte:deterministic){state^=state<<13;state^=state>>17;state^=state<<5;byte=static_cast<unsigned char>(state&255);}
    write_file(plain,std::string(deterministic.begin(),deterministic.end()));fs::create_directories(plain_pages);fs::create_directories(plain_out);
    auto plain_enc=run_cli(work,{"encode","--input=plain.bin","--page-width=160","--page-height=160","--prefix=page","--output-dir="+plain_pages.string()},"plain_encode");expect_success(plain_enc,id+" plain encode");
    auto plain_paths=ppm_paths(plain_pages,"page");require(plain_paths.size()>1,id+": expected multipage decoded-file case");
    auto plain_dec=run_cli(work,decode_file_args(plain_paths,plain_out,{"--decoded-file"}),"plain_decode");expect_success(plain_dec,id+" plain decoded-file");
    auto plain_reports=matching_files(plain_out,"makocode_decoded_",".txt");require(plain_reports.size()==1,id+": missing plain decoded file");std::string plain_report=read_file(plain_reports[0]);
    size_t page_lines=0,pos=0;while((pos=plain_report.find("\npage ",pos))!=std::string::npos){++page_lines;++pos;}require(page_lines==plain_paths.size(),id+": page metadata count does not match pages");
    require(plain_report.find("metadata ecc_enabled 1")!=std::string::npos&&plain_report.find("metadata encryption_")==std::string::npos,id+": plain metadata mismatch");
    std::vector<std::string> plain_stdout_args={"decode","--decoded-stdout"};plain_stdout_args.insert(plain_stdout_args.end(),plain_paths.begin(),plain_paths.end());
    auto plain_stdout=run_cli(work,plain_stdout_args,"plain_stdout");expect_success(plain_stdout,id+" plain stdout");
    require(plain_stdout.out==plain_report,id+": plain decoded-stdout differs from decoded-file output");
    // A bounded fixed-size page keeps this path deterministic; the output file records the complete archive.
    require(plain_report.find("file plain.bin 3000")!=std::string::npos,id+": plain archive entry missing");
}

static void checkpoint_restore_case(){
    const std::string id="0112_checkpoint_restore";fs::path work=make_work_dir(id);
    fs::create_directories(work/"base_a");fs::create_directories(work/"base_b");fs::create_directories(work/"current_a");fs::create_directories(work/"current_b");fs::create_directories(work/"pages");
    std::vector<unsigned char> a(256*20+173),b(5100);for(size_t i=0;i<a.size();++i)a[i]=static_cast<unsigned char>((i*29+i/7)&255);
    uint32_t state=0xabc123u;for(auto& v:b){state^=state<<13;state^=state>>17;state^=state<<5;v=static_cast<unsigned char>(state&255);}
    write_file(work/"base_a"/"old-a.bin",std::string(a.begin(),a.end()));write_file(work/"base_b"/"old-b.bin",std::string(b.begin(),b.end()));
    fs::copy_file(work/"base_a"/"old-a.bin",work/"current_a"/"alpha.bin");fs::copy_file(work/"base_b"/"old-b.bin",work/"current_b"/"beta.bin");
    fs::copy_file(work/"current_a"/"alpha.bin",work/"current_a"/"renamed.bin");write_file(work/"current_b"/"new.txt","new checkpoint content\n");
    auto delta=run_cli(work,{"checkpoint","create","--input","current_a","--input","current_b","--base","base_a","--base","base_b","--output","update.mcp","--chunk-size","256"},"checkpoint_delta");expect_success(delta,id+" delta checkpoint");
    auto full=run_cli(work,{"checkpoint","create","--input","current_a","--input","current_b","--output","full.mcp","--chunk-size","256"},"checkpoint_full");expect_success(full,id+" full checkpoint");
    require(fs::file_size(work/"update.mcp")<fs::file_size(work/"full.mcp"),id+": base chunks did not reduce bundle size");
    auto encoded=run_cli(work,{"encode","--input=update.mcp","--output-dir=pages","--prefix=checkpoint","--page-width=800","--page-height=800"},"encode");expect_success(encoded,id+" encode checkpoint");
    auto pages=ppm_paths(work/"pages","checkpoint");require(!pages.empty(),id+": no checkpoint pages");
    fs::path decoded=work/"decoded";auto decoded_result=run_cli(work,decode_file_args(pages,decoded),"decode");expect_success(decoded_result,id+" decode checkpoint");
    fs::path restored=work/"restored";auto restore=run_cli(work,{"checkpoint","restore","--input", (decoded/"update.mcp").string(),"--base",(work/"base_a").string(),"--base",(work/"base_b").string(),"--output",restored.string()},"restore");expect_success(restore,id+" restore");
    for(const auto& name:{"alpha.bin","renamed.bin"})compare_files(work/"current_a"/name,restored/"current_a"/name,id);
    for(const auto& name:{"beta.bin","new.txt"})compare_files(work/"current_b"/name,restored/"current_b"/name,id);
    fs::path partial=work/"partial";auto partial_result=run_cli(work,{"checkpoint","restore","--input",(decoded/"update.mcp").string(),"--base",(work/"base_a").string(),"--output",partial.string()},"partial_restore");
    expect_success(partial_result,id+" partial restore");require(fs::exists(partial/"current_b/beta.bin.partial")&&fs::exists(partial/"checkpoint_recovery.txt"),id+": missing base did not create partial file/recovery report");
    std::string partial_bytes=read_file(partial/"current_b/beta.bin.partial");require(std::none_of(partial_bytes.begin(),partial_bytes.end(),[](char ch){return ch!=0;}),id+": missing chunk ranges are not zero-filled");
    compare_files(work/"current_b/new.txt",partial/"current_b/new.txt",id);
}

static void hidden_volumes_case(){
    const std::string id="0110_hidden_volumes_e2e";fs::path work=make_work_dir(id),source=work/"source",hidden=source/"hidden_tree";
    fs::create_directories(hidden/"nested/empty");fs::create_directories(source/"pages");
    fs::path visible=source/"visible.bin",secret=hidden/"nested/secret.bin";
    // This large CLI matrix keeps fixed pseudo-random inputs so its coverage
    // stays focused on the option combinations; other roundtrip cases still
    // vary their payloads with the suite master seed.
    constexpr unsigned fixture_master_seed=255974958u;
    auto fixture_seed=[&](const std::string& key){return crc_cksum(std::to_string(fixture_master_seed)+":"+stable_seed_key(key))&0x7fffffffu;};
    generate_bytes(work,visible,16384,fixture_seed("payload/"+id+"/visible"),"visible");
    generate_bytes(work,secret,1024,fixture_seed("payload/"+id+"/hidden"),"secret");
    write_file(hidden/"root.txt","hidden archive root file\n");write_file(hidden/"empty.bin","");
    fs::path overlay=work/"overlay.ppm",bad_overlay=work/"overlay_wrong_size.ppm";
    expect_success(run_transform(work,{"overlay-mask","--output",overlay.string(),"--circle-color","0 0 0","--background-color","255 255 255","--width","1000","--height","1000"},"overlay"),id+" overlay");
    expect_success(run_transform(work,{"overlay-mask","--output",bad_overlay.string(),"--circle-color","0 0 0","--background-color","255 255 255","--width","800","--height","1000"},"bad_overlay"),id+" wrong overlay");
    auto random_for=[&](const std::string& key){std::ostringstream out;for(int i=0;i<7;++i){char chunk[16];std::snprintf(chunk,sizeof(chunk),"%08x",fixture_seed("crypto/"+id+"/"+key+"/"+std::to_string(i)));out<<chunk;}return out.str();};
    struct Success {const char* name;int pages;const char* visible_password;const char* hidden_password;bool hidden_capable;bool random_fill;std::vector<std::string> options;};
    const std::vector<Success> scenarios={
        {"visible_default",1,"","",false,false,{}},{"visible_hidden_zero",1,"","",false,false,{"--hidden=0","--ecc=0.5"}},
        {"visible_password_hidden_zero",1,"visible test password","",false,false,{"--hidden=0"}},
        {"ecc_fill_one_page",1,"","",false,true,{"--ecc=fill","--fill-pages=1"}},
        {"ecc_fill_two_pages",2,"","",false,true,{"--ecc=fill","--fill-pages=2"}},
        {"ecc_fill_current_page",1,"","",false,false,{"--ecc=fill"}},
        {"hidden_ratio_auto",1,"visible test password","hidden test password",true,false,{"--hidden=0.5"}},
        {"hidden_ratio_ecc",1,"visible test password","hidden test password",true,false,{"--hidden=2","--ecc=0.5"}},
        {"hidden_ratio_filler_only",1,"visible test password","",true,false,{"--hidden=0.5","--ecc=0.5"}},
        {"hidden_ratio_auto_overlay_filler",1,"visible test password","",true,false,{"--hidden=0.5","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"hidden_ratio_ecc_fill_one_page",1,"visible test password","hidden test password",true,false,{"--hidden=2","--ecc=fill"}},
        {"hidden_ratio_ecc_fill",2,"visible test password","hidden test password",true,false,{"--hidden=2","--ecc=fill","--fill-pages=2"}},
        {"hidden_fill_filler_only",1,"visible test password","",true,false,{"--hidden=fill"}},
        {"hidden_fill_archive_auto",1,"visible test password","hidden test password",true,false,{"--hidden","fill"}},
        {"hidden_fill_archive",1,"visible test password","hidden test password",true,false,{"--hidden=fill","--ecc=0.5","--fill-pages=1"}},
        {"hidden_fill_two_pages",2,"visible test password","hidden test password",true,false,{"--hidden=fill","--ecc=0.25","--fill-pages=2"}},
        {"hidden_fill_auto_two_pages",2,"visible test password","hidden test password",true,false,{"--hidden=fill","--fill-pages=2"}},
        {"hidden_fill_overlay",1,"visible test password","hidden test password",true,false,{"--hidden=fill","--ecc=0.5","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"hidden_fill_overlay_two_pages",2,"visible test password","hidden test password",true,false,{"--hidden=fill","--ecc=0.5","--fill-pages=2","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"hidden_ratio_overlay",1,"visible test password","hidden test password",true,false,{"--hidden=0.5","--ecc=0.5","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"visible_ratio_overlay",1,"","",false,false,{"--ecc=0.5","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"hidden_ratio_ecc_fill_overlay",2,"visible test password","hidden test password",true,false,{"--hidden=2","--ecc=fill","--fill-pages=2","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"ecc_fill_overlay",1,"","",false,true,{"--ecc=fill","--fill-pages=1","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}}
    };
    for(const auto& scenario:scenarios){
        const std::string name=scenario.name;fs::path pages_dir=work/(name+"-pages"),visible_out=work/(name+"-visible"),hidden_out=work/(name+"-hidden");
        fs::create_directories(pages_dir);fs::create_directories(visible_out);
        std::vector<std::string> args={"encode","--input=visible.bin","--page-width=1000","--page-height=1000","--output-dir="+pages_dir.string(),"--prefix="+name};
        std::string random_data=random_for(name);
        if(*scenario.visible_password)args.push_back(std::string("--password=")+scenario.visible_password);
        if(*scenario.visible_password||scenario.random_fill)args.push_back("--random-data="+random_data);
        if(*scenario.hidden_password){args.push_back("--hidden-input=hidden_tree");args.push_back(std::string("--hidden-password=")+scenario.hidden_password);}
        args.insert(args.end(),scenario.options.begin(),scenario.options.end());
        auto encoded=run_cli(source,args,name+"_encode",{},{{"MAKOCODE_TEST_SEED",std::to_string(case_seed("encoder/"+id+"/"+name))}});
        expect_success(encoded,id+" encode "+name);
        auto pages=ppm_paths(pages_dir,name);require(static_cast<int>(pages.size())==scenario.pages,id+" "+name+": page count mismatch");
        std::vector<std::string> visible_args=decode_file_args(pages,visible_out);
        if(*scenario.visible_password)visible_args.insert(visible_args.begin()+2,std::string("--password=")+scenario.visible_password);
        auto visible_decoded=run_cli(work,visible_args,name+"_visible_decode");expect_success(visible_decoded,id+" visible decode "+name);compare_files(visible,visible_out/"visible.bin",id+" visible "+name);
        if(*scenario.hidden_password){
            fs::create_directories(hidden_out);std::vector<std::string> hidden_args=decode_file_args(pages,hidden_out,{std::string("--password=")+scenario.hidden_password});
            auto hidden_decoded=run_cli(work,hidden_args,name+"_hidden_decode");expect_success(hidden_decoded,id+" hidden decode "+name);
            compare_files(hidden/"root.txt",hidden_out/"hidden_tree/root.txt",id+" hidden root "+name);
            compare_files(hidden/"empty.bin",hidden_out/"hidden_tree/empty.bin",id+" hidden empty "+name);
            compare_files(secret,hidden_out/"hidden_tree/nested/secret.bin",id+" hidden secret "+name);
            require(fs::is_directory(hidden_out/"hidden_tree/nested/empty")&&!fs::exists(visible_out/"hidden_tree")&&!fs::exists(hidden_out/"visible.bin"),id+" hidden tree separation "+name);
        }else if(scenario.hidden_capable){
            fs::create_directories(hidden_out);
            auto no_password=run_cli(work,decode_file_args(pages,hidden_out),"hidden_no_password");expect_failure(no_password,id+" no-password container "+name);
            auto wrong=run_cli(work,decode_file_args(pages,hidden_out,{"--password=not-the-hidden-password"}),"hidden_wrong_password");expect_failure(wrong,id+" wrong hidden password "+name);
            require(!fs::exists(hidden_out/"visible.bin")&&!fs::exists(hidden_out/"hidden_tree"),id+" filler-only output "+name);
        }
    }
    struct Reject {const char* name;const char* message;std::vector<std::string> args;};
    const std::string visible_password="visible test password",hidden_password="hidden test password";
    std::vector<Reject> rejects={
        {"hidden_fill_ecc_fill","--hidden fill and --ecc fill compete",{"--hidden=fill","--ecc=fill","--password="+visible_password}},
        {"ecc_disabled_visible","ECC cannot cover structural erasures",{"--ecc=0"}},
        {"ecc_disabled_hidden_ratio","ECC cannot cover structural erasures",{"--hidden=64","--ecc=0","--password="+visible_password}},
        {"ecc_disabled_hidden_fill","ECC cannot cover structural erasures",{"--hidden=fill","--ecc=0","--fill-pages=1","--password="+visible_password}},
        {"fill_pages_without_fill","--fill-pages requires --hidden fill or --ecc fill",{"--hidden=2","--ecc=0.5","--fill-pages=1","--password="+visible_password}},
        {"hidden_inputs_without_mode","hidden inputs and password require --hidden",{"--hidden-input=hidden_tree","--hidden-password="+hidden_password}},
        {"hidden_input_without_password","--hidden-input and --hidden-password must be supplied together",{"--hidden=0.5","--hidden-input=hidden_tree","--password="+visible_password}},
        {"hidden_password_without_input","--hidden-input and --hidden-password must be supplied together",{"--hidden=0.5","--password="+visible_password,"--hidden-password="+hidden_password}},
        {"hidden_options_with_zero","hidden inputs and password require --hidden",{"--hidden=0","--hidden-input=hidden_tree","--hidden-password="+hidden_password,"--password="+visible_password}},
        {"hidden_without_visible_password","--hidden requires --password and --random-data",{"--hidden=0.5"}},
        {"identical_passwords","visible and hidden passwords must differ",{"--hidden=0.5","--hidden-input=hidden_tree","--hidden-password="+visible_password,"--password="+visible_password}},
        {"hidden_capacity_too_small","hidden payload exceeds the --hidden capacity",{"--hidden=0.001","--hidden-input=hidden_tree","--hidden-password="+hidden_password,"--password="+visible_password}},
        {"hidden_ratio_negative","--hidden ratio must be between 0 and 64",{"--hidden=-1"}},
        {"hidden_ratio_too_large","--hidden ratio must be between 0 and 64",{"--hidden=65"}},
        {"duplicate_hidden_option","--hidden may be specified only once",{"--hidden=0.5","--hidden=2"}},
        {"fill_pages_zero","--fill-pages must be between 1 and 1000000",{"--ecc=fill","--fill-pages=0"}},
        {"duplicate_fill_pages","--fill-pages may be specified only once",{"--ecc=fill","--fill-pages=1","--fill-pages=2"}},
        {"fill_pages_with_numeric_modes","--fill-pages requires --hidden fill or --ecc fill",{"--hidden=0.5","--ecc=0.5","--fill-pages=1","--password="+visible_password}},
        {"overlay_fraction_without_ppm","--overlay-fraction and overlay images must be used together",{"--ecc=0.5","--overlay-fraction=0.1"}},
        {"overlay_ppm_without_fraction","--overlay-fraction and overlay images must be used together",{"--ecc=0.5","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"overlay_fraction_too_large","--overlay-fraction must be between 0.0 and 254/255",{"--ecc=0.5","--overlay-fraction=1.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"overlay_without_ecc","overlay requires ECC",{"--ecc=0","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"multipage_overlay_ecc_too_low","multi-page overlays require --ecc of at least 0.5",{"--hidden=fill","--hidden-input=hidden_tree","--hidden-password="+hidden_password,"--password="+visible_password,"--ecc=0.25","--fill-pages=2","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"overlay_wrong_dimensions","--overlay-ppm dimensions must match",{"--ecc=0.5","--overlay-fraction=0.1","--overlay-ppm="+bad_overlay.string(),"--overlay-pages=all"}},
        {"random_data_without_password_or_fill","--random-data requires --password or --fill-pages",{"--random-data=ffeeddccbbaa99887766554433221100123456789abcdef012345678"}},
        {"hidden_missing_value","--hidden requires a ratio or 'fill'",{"--hidden"}},
        {"hidden_ratio_not_numeric","--hidden ratio must be between 0 and 64",{"--hidden=secret"}},
        {"fill_pages_missing_value","--fill-pages requires a positive page count",{"--ecc=fill","--fill-pages"}},
        {"fill_pages_over_limit","--fill-pages must be between 1 and 1000000",{"--ecc=fill","--fill-pages=1000001"}}
    };
    for(const auto& reject:rejects){
        fs::path out=work/(std::string(reject.name)+"-rejected");fs::create_directories(out);
        std::vector<std::string> args={"encode","--input=visible.bin","--page-width=1000","--page-height=1000","--output-dir="+out.string(),std::string("--prefix=")+reject.name};
        bool password=false,random=false;for(const auto& a:reject.args){password|=a.rfind("--password=",0)==0;random|=a.rfind("--random-data=",0)==0;}
        if(password&&!random)args.push_back("--random-data="+random_for(reject.name));
        args.insert(args.end(),reject.args.begin(),reject.args.end());
        auto failed=run_cli(source,args,std::string(reject.name)+"_reject");expect_failure(failed,id+" reject "+reject.name);
        std::string output=failed.err+failed.out;require(output.find(reject.message)!=std::string::npos,id+" rejection reason mismatch for "+reject.name);
        require(matching_files(out,"",".ppm").empty(),id+" rejected encode emitted page "+reject.name);
    }
    std::vector<std::string> missing_random={"encode","--input=visible.bin","--page-width=1000","--page-height=1000","--password="+visible_password,"--output-dir="+(work/"missing_random").string()};
    auto missing=run_cli(source,missing_random,"missing_random");expect_failure(missing,id+" missing random data");
    require((missing.err+missing.out).find("--password requires --random-data")!=std::string::npos,id+": missing random error changed");
}

struct TileSummary{
    u64 page_index=0,page_count=0,block_count=0,selector_seed=0,selector_total=0;
    u16 parity=0,unknown_budget=0,overlay_budget=0;
    bool selector_present=false;
};

static TileSummary read_tile_summary(const fs::path& path,unsigned expected_width,unsigned expected_height){
    makocode::ByteBuffer input;
    require(read_entire_file(path.c_str(),input)&&input.data&&input.size>0,"cannot read tile PPM "+path.string());
    PpmParserState state;state.data=input.data;state.size=input.size;
    const char* token=nullptr;usize token_length=0;u32 width=0,height=0,max_value=0;
    auto next_u32=[&](u32& value){if(!ppm_next_token(state,&token,&token_length))return false;u64 parsed=0;
        if(!ascii_to_u64(token,token_length,&parsed)||parsed>0xffffffffull)return false;value=static_cast<u32>(parsed);return true;};
    require(ppm_next_token(state,&token,&token_length)&&ascii_equals_token(token,token_length,"P6")&&
        next_u32(width)&&next_u32(height)&&next_u32(max_value)&&
        width==expected_width&&height==expected_height&&max_value==255u&&ppm_consume_binary_separator(state),
        "invalid tile PPM header "+path.string());
    ImageMappingConfig mapping;makocode::ByteBuffer frame,erasures,selector_counts;
    u64 frame_bits=0,selector_seed=0;u16 unknown=0,overlay=0;PpmParserState summary_state;
    require(ppm_extract_frame_bits(input,mapping,frame,frame_bits,summary_state,false,(bool*)0,
        (makocode::ByteBuffer*)0,1u,&erasures,&selector_seed,&selector_counts,&unknown,&overlay,
        (bool*)0,(const AffineTransform*)0,false,(u64*)0,(const MetadataTile::Values*)0,
        (const u8*)0,0u,0u,0u,true,true,false,true),
        "cannot extract tile metadata "+path.string());
    const MetadataTile::Values& values=summary_state.encoded_tile;
    require(values.schema_version==MetadataTile::TILE_SCHEMA_VERSION&&values.overwrite_layout&&values.ecc_enabled&&
        values.ecc_block_count>0&&values.ecc_parity>0&&values.page_index>0&&values.page_index<=values.page_count,
        "invalid ECC tile fields "+path.string());
    TileSummary out;out.page_index=values.page_index;out.page_count=values.page_count;out.block_count=values.ecc_block_count;
    out.parity=values.ecc_parity;out.unknown_budget=unknown;out.overlay_budget=overlay;
    out.selector_present=values.overlay_selector_present;out.selector_seed=selector_seed;
    out.selector_total=values.ecc_block_count*overlay;
    if(overlay)require(values.overlay_selector_present,"overlay selector map missing");
    return out;
}

struct OverlayConfig{
    std::string id;
    size_t size=32768;
    unsigned width=1000,height=1000;
    std::string fraction="0.1",ecc,palette,ignore_colors,footer,font_size;
    std::string circle="0 0 0",background="255 255 255",circle_colors,allowed_colors;
    std::vector<std::string> encode_options;
    int expected_pages=0;
    bool skip_grayscale=false,without_metadata=false,without_fiducials=false;
};

static void overlay_case(const OverlayConfig& cfg){
    fs::path work=make_work_dir(cfg.id),payload=work/"payload.bin",mask=work/"overlay.ppm",pages_dir=work/"pages",decoded=work/"decoded";
    fs::create_directories(pages_dir);
    unsigned payload_seed=case_seed("payload/"+cfg.id);
    generate_bytes(work,payload,cfg.size,payload_seed,"payload");
    std::map<std::string,std::string> mask_env;
    if(!cfg.circle_colors.empty())mask_env["MAKO_OVERLAY_CIRCLE_COLORS"]=cfg.circle_colors;
    auto made=run_transform(work,{"overlay-mask","--output",mask.string(),"--circle-color",cfg.circle,
        "--background-color",cfg.background,"--width",std::to_string(cfg.width),"--height",std::to_string(cfg.height)},"mask",mask_env);
    expect_success(made,cfg.id+" overlay mask");
    std::vector<std::string> encode={"encode","--input=payload.bin","--overlay-fraction="+cfg.fraction,
        "--overlay-ppm="+mask.string(),"--overlay-pages=all","--page-width="+std::to_string(cfg.width),
        "--page-height="+std::to_string(cfg.height),"--output-dir="+pages_dir.string(),"--prefix="+cfg.id,"--debug"};
    if(!cfg.ecc.empty())encode.push_back("--ecc="+cfg.ecc);
    if(!cfg.palette.empty()){encode.push_back("--palette");encode.push_back(cfg.palette);}
    if(!cfg.ignore_colors.empty()){encode.push_back("--overlay-ignore-colors");encode.push_back(cfg.ignore_colors);}
    if(!cfg.footer.empty()){encode.push_back("--footer");encode.push_back(cfg.footer);}
    if(!cfg.font_size.empty()){encode.push_back("--font-size");encode.push_back(cfg.font_size);}
    if(cfg.without_metadata)encode.push_back("--no-metadata");
    if(cfg.without_fiducials)encode.push_back("--no-fiducials");
    encode.insert(encode.end(),cfg.encode_options.begin(),cfg.encode_options.end());
    auto encoded=run_cli(work,encode,"encode",payload,{{"MAKOCODE_TEST_SEED",std::to_string(case_seed("crypto/"+cfg.id))}});
    expect_success(encoded,cfg.id+" encode");
    std::string log=encoded.out+encoded.err;
    require(log.find("debug: overlay erasure budget per block=")!=std::string::npos,cfg.id+": overlay selector budget was not logged");
    auto unordered=ppm_paths(pages_dir,cfg.id);require(!unordered.empty(),cfg.id+": encode produced no PPM pages");if(cfg.expected_pages>0)require(static_cast<int>(unordered.size())==cfg.expected_pages,cfg.id+": page count mismatch");
    std::vector<std::pair<u64,std::string>> indexed;
    TileSummary baseline;
    if(cfg.without_metadata){require(unordered.size()==1,cfg.id+": expected one metadata-free page");indexed.push_back({1,unordered.front()});}
    else for(size_t i=0;i<unordered.size();++i){
        TileSummary summary=read_tile_summary(unordered[i],cfg.width,cfg.height);
        require(summary.page_count==unordered.size()&&summary.selector_present&&summary.overlay_budget>0&&
                summary.selector_total==summary.block_count*summary.overlay_budget,cfg.id+": invalid per-page selector reserve");
        if(i==0)baseline=summary;
        else require(summary.page_count==baseline.page_count&&summary.block_count==baseline.block_count&&
                summary.parity==baseline.parity&&summary.unknown_budget==baseline.unknown_budget&&
                summary.overlay_budget==baseline.overlay_budget&&summary.selector_seed==baseline.selector_seed,
                cfg.id+": selector metadata differs across pages");
        indexed.push_back({summary.page_index,unordered[i]});
    }
    std::sort(indexed.begin(),indexed.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<std::string> ordered;for(size_t i=0;i<indexed.size();++i){require(indexed[i].first==i+1,cfg.id+": serialized page index gap");ordered.push_back(indexed[i].second);}
    fs::path csv=pages_dir/(cfg.id+"_encode_fiducials.csv");
    require(fs::exists(csv),cfg.id+": missing encode fiducial report");
    std::vector<std::string> decode_extra={"--debug"};
    if(cfg.without_metadata){decode_extra.insert(decode_extra.end(),{"--no-metadata","--no-fiducials","--palette",cfg.palette});}
    std::vector<std::string> decode_args=decode_file_args(ordered,decoded,decode_extra);
    auto result=run_cli(work,decode_args,"decode");expect_success(result,cfg.id+" decode");
    require(fs::exists(decoded/"decode_fiducials.csv"),cfg.id+": missing decode fiducial report");
    check_fiducials(csv,decoded/"decode_fiducials.csv",cfg.id);
    compare_files(payload,decoded/"payload.bin",cfg.id);
    if(cfg.without_fiducials){
        require(read_file(csv)=="page,row,column,x,y,logical_x,logical_y\n",cfg.id+": --no-fiducials still emitted markers");
        PpmImage image=read_ppm(ordered.front());size_t matching=0;
        for(size_t i=0;i<image.pixels.size();i+=3)matching+=static_cast<unsigned char>(image.pixels[i])==128&&static_cast<unsigned char>(image.pixels[i+1])==64&&static_cast<unsigned char>(image.pixels[i+2])==0;
        require(matching>=1000,cfg.id+": overlay pixels were not applied");
    }
    if(!cfg.allowed_colors.empty()){
        std::set<std::array<unsigned char,3>> allowed;
        std::istringstream colors(cfg.allowed_colors);std::string entry;
        while(std::getline(colors,entry,';')){std::istringstream rgb(entry);unsigned r=0,g=0,b=0;rgb>>r>>g>>b;
            require(!rgb.fail()&&r<=255&&g<=255&&b<=255,cfg.id+": invalid expected overlay color");
            allowed.insert({static_cast<unsigned char>(r),static_cast<unsigned char>(g),static_cast<unsigned char>(b)});}
        for(const auto& page:ordered){PpmImage image=read_ppm(page);size_t changed=0;
            for(size_t i=0;i<image.pixels.size();i+=3){std::array<unsigned char,3> pixel={static_cast<unsigned char>(image.pixels[i]),static_cast<unsigned char>(image.pixels[i+1]),static_cast<unsigned char>(image.pixels[i+2])};
                require(allowed.count(pixel)!=0,cfg.id+": output pixel outside expected overlay palette");
                if(!cfg.skip_grayscale)require(pixel[0]==pixel[1]&&pixel[1]==pixel[2]&&(pixel[0]==0||pixel[0]==255),cfg.id+": output is not pure grayscale");
                ++changed;}
            require(changed>0,cfg.id+": empty overlay page");}
    }
}

static void write_gray_ppm(const fs::path& path,unsigned width,unsigned height,const std::vector<unsigned char>& gray){
    require(gray.size()==static_cast<size_t>(width)*height,"gray raster size mismatch");
    std::string data="P6\n"+std::to_string(width)+" "+std::to_string(height)+"\n255\n";
    data.reserve(data.size()+gray.size()*3);
    for(unsigned char v:gray){data.push_back(static_cast<char>(v));data.push_back(static_cast<char>(v));data.push_back(static_cast<char>(v));}
    write_file(path,data);
}

static void make_affine_scan(const fs::path& source_path,const fs::path& output_path,
                             unsigned width,unsigned height,double pitch,double ox,double oy,
                             unsigned low=60,unsigned span=174,bool blur=false,bool external_streaks=false){
    PpmImage source=read_ppm(source_path);
    require(source.width==80&&source.height==80,"scan source must be 80x80");
    std::vector<unsigned char> base(static_cast<size_t>(width)*height);
    const double subsamples[4]={0.125,0.375,0.625,0.875};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        unsigned total=0;
        for(double sy:subsamples)for(double sx:subsamples){
            int col=static_cast<int>(std::floor((x+sx-ox)/pitch));
            int row=static_cast<int>(std::floor((y+sy-oy)/pitch));
            if(col>=0&&col<80&&row>=0&&row<80){
                unsigned value=static_cast<unsigned char>(source.pixels[(static_cast<size_t>(row)*80+col)*3]);
                total+=low+static_cast<unsigned>(std::floor(static_cast<double>(span)*value/255.0+0.5));
            }else total+=234;
        }
        base[static_cast<size_t>(y)*width+x]=static_cast<unsigned char>(std::floor(total/16.0+0.5));
    }
    std::vector<unsigned char> result=base;
    if(blur&&width>2&&height>2){
        for(unsigned y=1;y+1<height;++y)for(unsigned x=1;x+1<width;++x){
            size_t i=static_cast<size_t>(y)*width+x;
            double v=.75*base[i]+.0625*(base[i-1]+base[i+1]+base[i-width]+base[i+width]);
            result[i]=static_cast<unsigned char>(std::floor(v+0.5));
        }
    }
    if(external_streaks){
        for(unsigned y=20;y<23;++y)result[static_cast<size_t>(y)*width+(width-1)]=60;
        for(unsigned x=20;x<23;++x)result[static_cast<size_t>(height-1)*width+x]=60;
    }
    write_gray_ppm(output_path,width,height,result);
}

static void make_oriented_scan(PpmImage source,const fs::path& output,double degrees,
                               bool flip_x,bool flip_y,double scale=4.0,unsigned coverage=4){
    const unsigned width=source.width,height=source.height;
    const double angle=degrees*3.14159265358979323846/180.0,cs=std::cos(angle),sn=std::sin(angle);
    const unsigned out_w=static_cast<unsigned>(std::ceil(scale*(std::fabs(cs)*width+std::fabs(sn)*height)))+16;
    const unsigned out_h=static_cast<unsigned>(std::ceil(scale*(std::fabs(sn)*width+std::fabs(cs)*height)))+16;
    const double cx=(out_w-1.0)*.5,cy=(out_h-1.0)*.5,sx=(width-1.0)*.5,sy=(height-1.0)*.5;
    std::vector<unsigned char> gray(static_cast<size_t>(out_w)*out_h);
    for(unsigned y=0;y<out_h;++y)for(unsigned x=0;x<out_w;++x){unsigned sum=0;
        for(unsigned iy=0;iy<coverage;++iy)for(unsigned ix=0;ix<coverage;++ix){
            double px=x+(ix+.5)/coverage-.5-cx,py=y+(iy+.5)/coverage-.5-cy;
            double mx=sx+(cs*px+sn*py)/scale*(flip_x?-1.0:1.0);
            double my=sy+(-sn*px+cs*py)/scale*(flip_y?-1.0:1.0);
            int col=static_cast<int>(std::floor(mx+.5)),row=static_cast<int>(std::floor(my+.5));
            sum+=col>=0&&row>=0&&col<static_cast<int>(width)&&row<static_cast<int>(height)
                ? static_cast<unsigned char>(source.pixels[(static_cast<size_t>(row)*width+col)*3]) : 255u;
        }
        gray[static_cast<size_t>(y)*out_w+x]=static_cast<unsigned char>(std::floor(static_cast<double>(sum)/(coverage*coverage)+.5));
    }
    write_gray_ppm(output,out_w,out_h,gray);
}

static MetadataTile::Values extract_tile_values(const fs::path& path){
    makocode::ByteBuffer input,frame,erasures,selectors;u64 bits=0,seed=0;u16 unknown=0,overlay=0;
    require(read_entire_file(path.c_str(),input)&&input.size>0,"cannot read metadata source "+path.string());
    ImageMappingConfig mapping;PpmParserState state;
    require(ppm_extract_frame_bits(input,mapping,frame,bits,state,false,(bool*)0,(makocode::ByteBuffer*)0,1u,
        &erasures,&seed,&selectors,&unknown,&overlay,(bool*)0,(const AffineTransform*)0,false,(u64*)0,
        (const MetadataTile::Values*)0,(const u8*)0,0u,0u,0u,true,true,false,true),
        "cannot extract metadata tile "+path.string());
    require(state.has_encoded_tile,"source lacks encoded metadata tile");
    return state.encoded_tile;
}

static void affine_cell_sampling_case(){
    const std::string id="0079_decode_affine_cell_sampling";fs::path work=make_work_dir(id),payload=work/"makocode.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);
    write_file(payload,"makocode");auto encoded=run_cli(work,encode_file_args(payload,pages,id,{"--page-width=80","--page-height=80"}),"encode");expect_success(encoded,id+" encode");
    auto source=ppm_paths(pages,id);require(source.size()==1,id+": expected source page");fs::path scaled=work/"scaled.ppm",scan=work/"fixture.ppm";
    expect_success(run_transform(work,{"transform","--input",source[0],"--output",scaled.string(),"--scale-x","3.2363636363636363","--scale-y","3.2363636363636363"},"scale"),id+" scale");
    PpmImage image=read_ppm(scaled);require(image.width==259&&image.height==259,id+": scaled dimensions changed");
    std::vector<unsigned char> gray(278u*278u,255u);for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x)gray[(static_cast<size_t>(y)+6)*278+x+7]=static_cast<unsigned char>(image.pixels[(static_cast<size_t>(y)*image.width+x)*3]);
    write_gray_ppm(scan,278,278,gray);auto result=run_cli(work,decode_file_args({scan.string()},decoded),"decode");expect_success(result,id+" decode");compare_files(payload,decoded/"makocode.bin",id);
}

static void decode_scale_318_case(){
    const std::string id="0072_decode_scale_318";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);
    write_file(payload,"0");auto encoded=run_cli(work,encode_file_args(payload,pages,id,{"--page-width=160","--page-height=160"}),"encode");expect_success(encoded,id+" encode");auto source=ppm_paths(pages,id);require(source.size()==1,id+": expected one page");
    fs::path scan=work/"scaled.ppm";expect_success(run_transform(work,{"transform","--input",source[0],"--output",scan.string(),"--scale-x","3.18","--scale-y","3.18"},"scale"),id+" scale");
    auto result=run_cli(work,decode_file_args({scan.string()},decoded),"decode");expect_success(result,id+" decode");compare_files(payload,decoded/"payload.bin",id);
}

static void small_scan_case(){
    const std::string id="0080_decode_small_scan";fs::path work=make_work_dir(id),payload=work/"makocode.bin",pages=work/"pages";fs::create_directories(pages);write_file(payload,"makocode");
    auto encoded=run_cli(work,encode_file_args(payload,pages,"small_scan",{"--ecc=fill","--page-width=80","--page-height=80"}),"encode");expect_success(encoded,id+" encode");auto source=ppm_paths(pages,"small_scan");require(source.size()==1,id+": expected one source page");
    for(bool five:{false,true}){const double pitch=five?2.477:2.239;fs::path scaled=work/(five?"5x_scaled.ppm":"4_5x_scaled.ppm");
        expect_success(run_transform(work,{"transform","--input",source[0],"--output",scaled.string(),"--scale-x",std::to_string(pitch),"--scale-y",std::to_string(pitch)},five?"scale5":"scale45"),id+" transform");
        PpmImage image=read_ppm(scaled);unsigned cw=five?212:193,ch=five?208:188,ox=five?4:2,oy=five?5:4;
        require(image.width==(five?198u:179u)&&image.height==(five?198u:179u),id+": scaled dimensions changed");
        std::vector<unsigned char> canvas(static_cast<size_t>(cw)*ch,static_cast<unsigned char>(five?255:230));
        for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){size_t src=(static_cast<size_t>(y)*image.width+x)*3,dst=static_cast<size_t>(y+oy)*cw+x+ox;
            unsigned char value=static_cast<unsigned char>(image.pixels[src]);canvas[dst]=five?value:static_cast<unsigned char>(30+std::floor(.8*value+.5));}
        if(!five){std::vector<unsigned char> hard(static_cast<size_t>(cw)*ch,230),soft=canvas;
            for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){size_t src=(static_cast<size_t>(y)*image.width+x)*3,dst=static_cast<size_t>(y+oy)*cw+x+ox;hard[dst]=static_cast<unsigned char>((static_cast<unsigned char>(image.pixels[src])+static_cast<unsigned char>(image.pixels[src+1])+static_cast<unsigned char>(image.pixels[src+2]))<384?40:230);}
            fs::path hard_path=work/"4_5x_hard_scan.ppm",soft_path=work/"4_5x_soft_scan.ppm";write_gray_ppm(hard_path,cw,ch,hard);write_gray_ppm(soft_path,cw,ch,soft);
            for(const auto& variant:std::vector<std::pair<std::string,fs::path>>{{"hard",hard_path},{"soft",soft_path}}){fs::path out=work/("out_"+variant.first);auto d=run_cli(work,decode_file_args({variant.second.string()},out,{"--debug"}),variant.first);expect_success(d,id+" "+variant.first);compare_files(payload,out/"makocode.bin",id+" "+variant.first);}
            std::ifstream affine(work/"out_soft/decode_affine.csv");std::string header,line;std::getline(affine,header);std::getline(affine,line);auto f=split_csv(line);require(f.size()>=10&&f[1]=="1"&&std::fabs(std::strtod(f[2].c_str(),nullptr)-2.239)<=.01&&std::fabs(std::strtod(f[8].c_str(),nullptr)-1.5)<=.75&&std::fabs(std::strtod(f[9].c_str(),nullptr)-3.5)<=.75&&std::fabs(std::strtod(f[3].c_str(),nullptr))<=.25,id+": affine estimate out of tolerance");
        }else{fs::path scan=work/"5x_scan.ppm",out=work/"out5";write_gray_ppm(scan,cw,ch,canvas);auto d=run_cli(work,decode_file_args({scan.string()},out),"decode5");expect_success(d,id+" 5x");compare_files(payload,out/"makocode.bin",id+" 5x");}
    }
}

static void current_metadata_case(){
    const std::string id="0087_decode_current_metadata";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);write_file(payload,"x");
    auto enc=run_cli(work,encode_file_args(payload,pages,"page",{"--page-width=64","--page-height=64"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"page");require(ppms.size()==1,id+": expected page");
    MetadataTile::Values tile=extract_tile_values(ppms.front());PpmImage image=read_ppm(ppms.front());
    const unsigned tile_x=(image.width-48)/2,tile_y=(image.height-48)/2;unsigned cell=tile.metadata_length_prefix_bits+128;
    unsigned x=tile_x+tile.metadata_square_x0+cell%tile.metadata_square_side,y=tile_y+tile.metadata_square_y0+cell/tile.metadata_square_side;
    size_t index=(static_cast<size_t>(y)*image.width+x)*3;require(index+2<image.pixels.size(),id+": target module outside page");
    unsigned char original=static_cast<unsigned char>(image.pixels[index]);require(original==0||original==255,id+": target module not black/white");
    for(int c=0;c<3;++c)image.pixels[index+c]=static_cast<char>(255-original);fs::path damaged=work/"damaged.ppm";write_file(damaged,"P6\n64 64\n255\n"+image.pixels);
    auto result=run_cli(work,decode_file_args({damaged.string()},decoded),"decode");expect_success(result,id+" damaged metadata decode");compare_files(payload,decoded/"payload.bin",id);
    PpmImage no_tile=read_ppm(ppms.front());for(unsigned yy=tile_y;yy<tile_y+48;++yy)for(unsigned xx=tile_x;xx<tile_x+48;++xx){size_t at=(static_cast<size_t>(yy)*no_tile.width+xx)*3;no_tile.pixels[at]=no_tile.pixels[at+1]=no_tile.pixels[at+2]=127;}
    fs::path absent=work/"no_bootstrap.ppm";write_file(absent,"P6\n64 64\n255\n"+no_tile.pixels);auto failed=run_cli(work,decode_file_args({absent.string()},work/"no_bootstrap_out"),"no_bootstrap");expect_failure(failed,id+" absent metadata");
    require(failed.out.find("metadata tile missing; aborting")!=std::string::npos||failed.err.find("metadata tile missing; aborting")!=std::string::npos||failed.out.find("invalid canonical metadata tile")!=std::string::npos||failed.err.find("invalid canonical metadata tile")!=std::string::npos,id+": absent-tile failure diagnostic missing");
}

static void metadata_free_case(bool large){
    const std::string id=large?"0122_decode_metadata_free_large":"0121_decode_metadata_free";fs::path work=make_work_dir(id);
    const std::vector<std::string> palettes={"White Black","White Cyan Magenta Yellow","White Cyan Magenta Yellow Black","White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black"};
    const unsigned base_seed=large?master_seed+130363u:master_seed;
    const unsigned iterations=large?1u:4u;
    for(unsigned i=0;i<iterations;++i){const unsigned seed=large?base_seed:base_seed+7919u*i+17u;const size_t size=large?1800u+(seed%501u):700u+(seed%151u);
        fs::path payload=work/("payload_"+std::to_string(i)+".bin"),encoded_dir=work/("encoded_"+std::to_string(i)),damaged=work/("damaged_"+std::to_string(i)+".ppm");fs::create_directories(encoded_dir);
        generate_bytes(work,payload,size,seed,"bytes_"+std::to_string(i));std::string prefix=large?"large_page":"palette_"+std::to_string(i);
        unsigned dims=large?500u:220u;auto enc=run_cli(work,{"encode","--output-dir="+encoded_dir.string(),"--prefix="+prefix,"--page-width="+std::to_string(dims),"--page-height="+std::to_string(dims),"--palette",large?"White Black":palettes[i],"--ecc=3.5"},"encode_"+std::to_string(i),payload);expect_success(enc,id+" encode");
        auto ppms=ppm_paths(encoded_dir,prefix);require(ppms.size()==1,id+": expected one-page fixture");PpmImage image=read_ppm(ppms.front());require(image.width==dims&&image.height==dims,id+": page dimensions changed");
        const int center=static_cast<int>(dims/2),radius=37;unsigned changed=0;for(int y=0;y<static_cast<int>(dims);++y)for(int x=0;x<static_cast<int>(dims);++x)if((x-center)*(x-center)+(y-center)*(y-center)<=radius*radius){size_t at=(static_cast<size_t>(y)*dims+x)*3;image.pixels[at]=image.pixels[at+1]=image.pixels[at+2]=0;++changed;}
        require(changed>4000,id+": metadata damage circle too small");write_file(damaged,"P6\n"+std::to_string(dims)+" "+std::to_string(dims)+"\n255\n"+image.pixels);
        auto decoded=run_cli(work,{"decode","--decoded-stdout",damaged.string()},"decode_"+std::to_string(i));expect_success(decoded,id+" decode");
        std::string expected=read_file(payload);std::vector<unsigned char> expected_bytes(expected.begin(),expected.end());check_decoded(decoded.out,expected_bytes,0.0,id);
    }
}

static void missing_pages_case(){
    const std::string id="0115_decode_missing_pages";fs::path work=make_work_dir(id);fs::path payload=work/"payload.bin",out=work/"out";unsigned seed=2110;generate_bytes(work,payload,32768,seed,"payload");
    auto enc=run_cli(work,{"encode","--output-dir="+work.string(),"--prefix=page","--page-width=400","--page-height=400","--ecc=0.8"},"encode",payload);expect_success(enc,id+" encode");auto pages=ppm_paths(work,"page");require(pages.size()==4,id+": expected four pages");
    for(const auto& item:std::vector<std::pair<std::string,std::vector<std::string>>>{{"middle",{pages[0],pages[2],pages[3]}},{"shuffled",{pages[3],pages[0],pages[2]}},{"first",{pages[1],pages[2],pages[3]}},{"last",{pages[0],pages[1],pages[2]}}}){
        std::vector<std::string> args={"decode","--decoded-stdout"};args.insert(args.end(),item.second.begin(),item.second.end());auto decoded=run_cli(work,args,item.first);
        expect_success(decoded,id+" "+item.first);std::string bytes=read_file(payload);check_decoded(decoded.out,std::vector<unsigned char>(bytes.begin(),bytes.end()),0.0,id+" "+item.first);}
    fs::path stdin_dir=work/"stdin_pages",stdin_payload=work/"stdin_payload.bin";fs::create_directories(stdin_dir);generate_bytes(work,stdin_payload,8192,2111,"stdin_payload");
    auto stdin_enc=run_cli(work,{"encode","--output-dir="+stdin_dir.string(),"--prefix=stdin","--page-width=400","--page-height=400","--ecc=2.0"},"stdin_encode",stdin_payload);expect_success(stdin_enc,id+" stdin encode");
    auto stdin_pages=ppm_paths(stdin_dir,"stdin");require(stdin_pages.size()==2,id+": expected two stdin fixture pages");auto first=run_cli(work,{"decode","--decoded-stdout"},"stdin_decode",stdin_pages.front());expect_success(first,id+" stdin decode");std::string expected=read_file(stdin_payload);check_decoded(first.out,std::vector<unsigned char>(expected.begin(),expected.end()),0.0,id+" stdin");
}

static void orientation_scan_case(){
    const std::string id="0117_decode_orientation_scan";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages";fs::create_directories(pages);write_file(payload,"orientation scan round-trip payload");
    auto enc=run_cli(work,encode_file_args(payload,pages,"source",{"--page-width=80","--page-height=80"}),"encode");expect_success(enc,id+" encode");auto source=ppm_paths(pages,"source");require(source.size()==1,id+": expected source page");PpmImage image=read_ppm(source.front());
    const std::array<std::array<double,3>,10> variants={{{0,0,0},{90,0,0},{180,0,0},{270,0,0},{0,1,0},{0,0,1},{91,0,0},{181,0,0},{271,0,0},{1,1,1}}};
    for(size_t i=0;i<variants.size();++i){fs::path scan=work/("scan_"+std::to_string(i)+".ppm"),out=work/("decoded_"+std::to_string(i));make_oriented_scan(image,scan,variants[i][0],variants[i][1]!=0,variants[i][2]!=0);
        auto decoded=run_cli(work,decode_file_args({scan.string()},out),"decode_"+std::to_string(i));expect_success(decoded,id+" orientation "+std::to_string(i));compare_files(payload,out/"payload.bin",id);}
}

static void scan_registration_noise_case(){
    const std::string id="0088_decode_scan_registration";fs::path work=make_work_dir(id),payload=work/"input.bin",pages=work/"pages";fs::create_directories(pages);write_file(payload,"registration regression");
    auto enc=run_cli(work,encode_file_args(payload,pages,"page",{"--ecc=fill","--page-width=80","--page-height=80"}),"encode");expect_success(enc,id+" encode");auto source=ppm_paths(pages,"page");require(source.size()==1,id+": expected one source page");
    const double pitch=2.75,ox=2.5,oy=2.0;fs::path scan=work/"scan.ppm";make_affine_scan(source.front(),scan,225,226,pitch,ox,oy,30,200,false,false);
    PpmImage img=read_ppm(scan);std::set<std::pair<int,int>> centers;for(int y=0;y<80;++y)for(int x=0;x<80;++x)centers.insert({static_cast<int>(std::floor((x+.5)*pitch+ox)),static_cast<int>(std::floor((y+.5)*pitch+oy))});
    PythonRandom rng(0);for(unsigned y=0;y<img.height;++y)for(unsigned x=0;x<img.width;++x){if(centers.count({static_cast<int>(x),static_cast<int>(y)})==0&&rng.random()<.012){size_t at=(static_cast<size_t>(y)*img.width+x)*3;img.pixels[at]=img.pixels[at+1]=img.pixels[at+2]=0;}}
    write_file(scan,"P6\n"+std::to_string(img.width)+" "+std::to_string(img.height)+"\n255\n"+img.pixels);
    auto control=run_cli(work,{"decode","--decoded-stdout",source.front()},"control"),actual=run_cli(work,{"decode","--decoded-stdout",scan.string()},"scan");expect_success(control,id+" control");expect_success(actual,id+" scan");require(control.out==actual.out,id+": stdout archive differs");
}

static void synthetic_scan_case(const std::string& id,bool margin_noise,bool off_center){
    fs::path work=make_work_dir(id),payload,source_dir=work/"encoded";fs::create_directories(source_dir);std::string payload_name;
    if(margin_noise){payload=work/"payload.bin";payload_name="payload.bin";write_file(payload,"scan-test");}
    else{payload=work/"tmp/makocode_probe_enc/makocode.bin";payload_name="tmp/makocode_probe_enc/makocode.bin";write_file(payload,"makocode");}
    auto enc=run_cli(work,{"encode","--input="+payload_name,"--page-width=80","--page-height=80","--output-dir="+source_dir.string(),"--prefix=synthetic"},"encode");expect_success(enc,id+" encode");auto pages=ppm_paths(source_dir,"synthetic");require(pages.size()==1,id+": expected one source page");
    const double pitch=2.24,ox=margin_noise?2.6:2.2,oy=3.5;fs::path soft=work/"soft.ppm",damaged=work/"damaged.ppm";make_affine_scan(pages[0],soft,193,188,pitch,ox,oy,30,200,false,false);PpmImage scan=read_ppm(soft);
    if(!margin_noise&&!off_center){for(auto point:std::array<std::pair<unsigned,unsigned>,6>{{{0,1},{0,2},{0,3},{1,0},{2,0},{3,0}}}){size_t at=(static_cast<size_t>(point.second)*scan.width+point.first)*3;scan.pixels[at]=scan.pixels[at+1]=scan.pixels[at+2]=80;}}
    if(margin_noise){for(unsigned y=scan.height-7;y<scan.height-4;++y)for(unsigned x=scan.width-5;x<scan.width-2;++x){size_t at=(static_cast<size_t>(y)*scan.width+x)*3;scan.pixels[at]=scan.pixels[at+1]=scan.pixels[at+2]=80;}}
    else if(off_center){std::set<std::pair<unsigned,unsigned>> centers;for(unsigned y=0;y<80;++y)for(unsigned x=0;x<80;++x){int cx=static_cast<int>(std::floor((x+.5)*pitch+ox)),cy=static_cast<int>(std::floor((y+.5)*pitch+oy));centers.insert({static_cast<unsigned>(cx),static_cast<unsigned>(cy)});}
        PythonRandom rng(0);for(unsigned y=0;y<scan.height;++y)for(unsigned x=0;x<scan.width;++x){if(!centers.count({x,y})&&rng.random()<.03){size_t at=(static_cast<size_t>(y)*scan.width+x)*3;scan.pixels[at]=scan.pixels[at+1]=scan.pixels[at+2]=0;}}}
    else for(auto point:std::array<std::pair<unsigned,unsigned>,3>{{{6,78},{6,79},{7,79}}}){size_t at=(static_cast<size_t>(point.second)*scan.width+point.first)*3;scan.pixels[at]=scan.pixels[at+1]=scan.pixels[at+2]=0;}
    write_file(damaged,"P6\n193 188\n255\n"+scan.pixels);
    PpmImage encoded=read_ppm(pages[0]);std::vector<unsigned char> recovered(80u*80u);for(unsigned y=0;y<80;++y)for(unsigned x=0;x<80;++x){unsigned sx=static_cast<unsigned>(std::floor((x+.5)*pitch+ox)),sy=static_cast<unsigned>(std::floor((y+.5)*pitch+oy));size_t at=(static_cast<size_t>(sy)*scan.width+sx)*3;bool dark=static_cast<unsigned char>(scan.pixels[at])<128;bool original=static_cast<unsigned char>(encoded.pixels[(static_cast<size_t>(y)*80+x)*3])<128;require(dark==original,id+": a generated cell center is corrupted");recovered[static_cast<size_t>(y)*80+x]=dark?0:255;}
    fs::path recovered_ppm=work/"recovered.ppm";write_gray_ppm(recovered_ppm,80,80,recovered);
    for(const auto& page:std::vector<std::pair<std::string,fs::path>>{{"recovered",recovered_ppm},{"soft",soft}}){fs::path out=work/(page.first+"_out");auto dec=run_cli(work,decode_file_args({page.second.string()},out),page.first);expect_success(dec,id+" "+page.first);compare_files(payload,out/payload_name,id+" "+page.first);}
    fs::path output=work/"damaged_out";std::vector<std::string> args={"decode","--output-dir="+output.string()};if(!off_center)args.push_back("--debug");args.push_back(damaged.string());auto dec=run_cli(work,args,"damaged_decode");expect_success(dec,id+" damaged scan");compare_files(payload,output/payload_name,id+" damaged scan");
}

static void lowres_rotated_scan_case(){
    const std::string id="0085_decode_lowres_rotated_scan";fs::path work=make_work_dir(id),payload=work/"makocode.bin",pages=work/"encoded";fs::create_directories(pages);write_file(payload,"makocode");
    auto enc=run_cli(work,encode_file_args(payload,pages,"source",{"--page-width=80","--page-height=80"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"source");require(ppms.size()==1,id+": expected source page");PpmImage src=read_ppm(ppms.front());
    const double scale=1.75,angle=-1.25*3.14159265358979323846/180.0,cs=std::cos(angle),sn=std::sin(angle),cx=39.5,cy=39.5;const double a00=scale*cs,a01=-scale*sn,a10=scale*sn,a11=scale*cs,tx=75-(a00*cx+a01*cy),ty=75-(a10*cx+a11*cy);
    std::vector<unsigned char> gray(150u*151u,230);const double subs[]={.125,.375,.625,.875};for(unsigned y=0;y<151;++y)for(unsigned x=0;x<150;++x){unsigned sum=0;for(double sy:subs)for(double sx:subs){double px=x+sx-.5,py=y+sy-.5,dx=px-tx,dy=py-ty;int col=static_cast<int>(std::floor((a11*dx-a01*dy)/(scale*scale))),row=static_cast<int>(std::floor((-a10*dx+a00*dy)/(scale*scale)));unsigned char value=col>=0&&row>=0&&col<80&&row<80?static_cast<unsigned char>(30+std::floor(.8*static_cast<unsigned char>(src.pixels[(static_cast<size_t>(row)*80+col)*3])+.5)):230;sum+=value;}
        gray[static_cast<size_t>(y)*150+x]=static_cast<unsigned char>(std::floor(sum/16.0+.5));}
    const std::array<std::array<unsigned,4>,8> marks={{{10,1,20,5},{20,1,37,4},{21,1,39,4},{22,1,41,4},{23,1,42,4},{24,1,44,4},{25,1,46,4},{26,1,48,4}}};
    std::set<std::pair<int,int>> true_samples;for(unsigned row=0;row<80;++row)for(unsigned col=0;col<80;++col)if(!(col>=16&&col<64&&row>=16&&row<64)){double px=a00*(col+.5)+a01*(row+.5)+tx,py=a10*(col+.5)+a11*(row+.5)+ty;true_samples.insert({static_cast<int>(std::floor(px+.5)),static_cast<int>(std::floor(py+.5))});}
    for(const auto& m:marks){require(!true_samples.count({static_cast<int>(m[2]),static_cast<int>(m[3])}),id+": damage overlaps true cell center");unsigned char original=static_cast<unsigned char>(src.pixels[(static_cast<size_t>(m[1])*80+m[0])*3]);gray[static_cast<size_t>(m[3])*150+m[2]]=original<128?234:30;}
    fs::path scan=work/"scan.ppm",out=work/"decoded";write_gray_ppm(scan,150,151,gray);auto dec=run_cli(work,decode_file_args({scan.string()},out),"decode");expect_success(dec,id+" decode");compare_files(payload,out/"makocode.bin",id);
}

static void short_length_header_case(){
    const std::string id="0084_decode_short_length_header";fs::path work=make_work_dir(id),payload=work/"makocode.bin",encoded=work/"encoded";fs::create_directories(encoded);write_file(payload,"makocode");
    auto enc=run_cli(work,encode_file_args(payload,encoded,"length_header",{"--page-width=240","--page-height=240"}),"encode");expect_success(enc,id+" encode");auto pages=ppm_paths(encoded,"length_header");require(pages.size()==1,id+": expected page");
    PpmImage original=read_ppm(pages.front());require(original.width==240&&original.height==240,id+": encoded dimensions changed");PpmImage damaged=original;size_t target=(4u*3u);unsigned char cell=static_cast<unsigned char>(damaged.pixels[target]);require(cell==0||cell==255,id+": first payload cell not binary");for(unsigned c=0;c<3;++c)damaged.pixels[target+c]=static_cast<char>(255-cell);fs::path damaged_source=work/"damaged_source.ppm";write_file(damaged_source,"P6\n240 240\n255\n"+damaged.pixels);
    const double scale=2.018181818;fs::path clean_scaled=work/"clean_scaled.ppm",bad_scaled=work/"bad_scaled.ppm";
    for(const auto& item:std::vector<std::pair<fs::path,fs::path>>{{pages[0],clean_scaled},{damaged_source,bad_scaled}})expect_success(run_transform(work,{"transform","--input",item.first.string(),"--output",item.second.string(),"--scale-x",std::to_string(scale),"--scale-y",std::to_string(scale)},"scale"),id+" scale");
    auto soften=[&](const fs::path& input,const fs::path& output){PpmImage img=read_ppm(input);require(img.width==484&&img.height==484,id+": scale render dimensions changed");for(size_t i=0;i<img.pixels.size();++i){unsigned char value=static_cast<unsigned char>(img.pixels[i]);img.pixels[i]=static_cast<char>(30+std::floor(.8*value+.5));}write_file(output,"P6\n484 484\n255\n"+img.pixels);};
    fs::path clean=work/"clean_scan.ppm",bad=work/"damaged_scan.ppm",alias=work/"alias_scan.ppm";soften(clean_scaled,clean);soften(bad_scaled,bad);PpmImage aliased=read_ppm(clean);
    const std::array<std::pair<unsigned,unsigned>,6> cells={{{0,3},{9,3},{2,5},{10,6},{0,8},{3,10}}};for(auto cell_xy:cells){double sx=scale*(cell_xy.first+.5)-.5,sy=scale*(cell_xy.second+.5)-.5;int x0=static_cast<int>(sx),y0=static_cast<int>(sy),nx=static_cast<int>(sx+.5),ny=static_cast<int>(sy+.5);size_t center=(static_cast<size_t>(ny)*484+nx)*3;unsigned char opposite=static_cast<unsigned char>(aliased.pixels[center])<128?234:30;
        for(int y:{y0,y0+1})for(int x:{x0,x0+1})if(x!=nx||y!=ny){size_t at=(static_cast<size_t>(y)*484+x)*3;aliased.pixels[at]=aliased.pixels[at+1]=aliased.pixels[at+2]=static_cast<char>(opposite);}}
    write_file(alias,"P6\n484 484\n255\n"+aliased.pixels);
    for(const auto& item:std::vector<std::pair<std::string,fs::path>>{{"clean",clean},{"damaged",bad},{"alias",alias}}){fs::path out=work/("decoded_"+item.first);auto dec=run_cli(work,decode_file_args({item.second.string()},out),"decode_"+item.first);expect_success(dec,id+" "+item.first);compare_files(payload,out/"makocode.bin",id+" "+item.first);}
}

static void erased_metadata_scan_case(const std::string& id,int kind){
    fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded=work/"encoded";fs::create_directories(encoded);std::string bytes=kind==0?"partially damaged marker scan payload":kind==1?"erased marker orientation scan payload":"erased metadata length row scan payload";write_file(payload,bytes);
    auto enc=run_cli(work,encode_file_args(payload,encoded,"source",{"--page-width=80","--page-height=80"}),"encode");expect_success(enc,id+" encode");auto pages=ppm_paths(encoded,"source");require(pages.size()==1,id+": expected one encoded page");MetadataTile::Values tile=extract_tile_values(pages[0]);
    PpmImage source=read_ppm(pages[0]);unsigned tile_x=(source.width-48)/2,tile_y=(source.height-48)/2;
    if(kind==0){std::vector<unsigned> dark;for(unsigned bit=0;bit<32;++bit){unsigned x=tile_x+MetadataTile::tag_marker_x0()+(bit&7),y=tile_y+tile.metadata_tag_y0+1+bit/8;size_t at=(static_cast<size_t>(y)*source.width+x)*3;if(static_cast<unsigned char>(source.pixels[at])<128)dark.push_back(bit);}
        require(dark.size()==20,id+": expected 20 dark marker modules");for(size_t i=0;i<16;++i){unsigned bit=dark[i],x=tile_x+MetadataTile::tag_marker_x0()+(bit&7),y=tile_y+tile.metadata_tag_y0+1+bit/8;size_t at=(static_cast<size_t>(y)*source.width+x)*3;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(255);}
        unsigned matches=0;for(unsigned bit=0;bit<32;++bit){unsigned x=tile_x+MetadataTile::tag_marker_x0()+(bit&7),y=tile_y+tile.metadata_tag_y0+1+bit/8;size_t at=(static_cast<size_t>(y)*source.width+x)*3;bool observed=static_cast<unsigned char>(source.pixels[at])<128,expected=MetadataTile::makocode_tag_bit(bit);matches+=observed==expected;}require(matches==16,id+": partial marker fixture does not leave half the bits matched");
    }else if(kind==1){for(unsigned row=0;row<4;++row)for(unsigned col=0;col<8;++col){size_t at=(static_cast<size_t>(tile_y+tile.metadata_tag_y0+1+row)*source.width+tile_x+20+col)*3;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(255);}}
    else{u64 metadata_cells=tile.metadata_length_prefix_bits+tile.metadata_symbol_count;unsigned value_bits=0;for(u64 n=metadata_cells;n;n>>=1)++value_bits;unsigned length_bits=1;for(unsigned remaining=value_bits-1;remaining>1;remaining>>=1)++length_bits;unsigned outer=2+length_bits+value_bits;unsigned x0=tile_x+(48-outer)/2,y=tile_y+tile.metadata_tag_y0+5;
        unsigned changed=0;for(unsigned col=0;col<outer;++col){size_t at=(static_cast<size_t>(y)*source.width+x0+col)*3;if(static_cast<unsigned char>(source.pixels[at])!=255)++changed;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(255);}require(changed>0,id+": length row fixture changed no cells");}
    const std::vector<std::array<double,3>> variants=kind==0?std::vector<std::array<double,3>>{{{0,0,0}}}:kind==1?std::vector<std::array<double,3>>{{{90,0,0},{180,0,0},{270,0,0},{0,1,0},{0,0,1}}}:std::vector<std::array<double,3>>{{{0,0,0},{90,0,0},{180,0,0},{270,0,0},{0,1,0},{0,0,1}}};
    for(size_t i=0;i<variants.size();++i){PpmImage variant=source;fs::path scan=work/("scan_"+std::to_string(i)+".ppm");write_file(work/"mutated_source.ppm","P6\n80 80\n255\n"+variant.pixels);variant=read_ppm(work/"mutated_source.ppm");make_oriented_scan(variant,scan,variants[i][0],variants[i][1]!=0,variants[i][2]!=0);
        fs::path out=work/("decoded_"+std::to_string(i));auto dec=run_cli(work,decode_file_args({scan.string()},out,{"--debug"}),"decode_"+std::to_string(i));expect_success(dec,id+" scan "+std::to_string(i));std::string log=dec.out+dec.err;
        const std::string diagnostic=kind==0?"partial MAKO/CODE marker recovered by metadata ECC":kind==1?"metadata ECC recovered from header length row":"metadata ECC recovered from guessed header size";require(log.find(diagnostic)!=std::string::npos,id+": expected recovery diagnostic missing");compare_files(payload,out/"payload.bin",id+" "+std::to_string(i));}
}

static void minify_literals_case(){
    const std::string id="0059_minify_literals";fs::path work=make_work_dir(id),fixture=work/"makocode.cpp";
    const std::string source=R"SRC(  #define MINIFY_DIRECTIVE_SWITCH 1
#if MINIFY_DIRECTIVE_SWITCH
#define MINIFY_DIRECTIVE_VALUE "preprocessor directive kept"
#else
#error preprocessor directive line was not preserved
#endif
static bool text_matches(const char* a,const char* b){while(*a&&*b&&*a==*b){++a;++b;}return *a=='\0'&&*b=='\0';}
static  const int before_directive = 0;
#define MINIFY_LATE_VALUE 7
static const int after_directive = MINIFY_LATE_VALUE;
int main(){/* MINIFY_BLOCK_COMMENT_SENTINEL */ const char* literal="literal: // /* */ quote: \" preserved"; // MINIFY_LINE_COMMENT_SENTINEL
const char* expected="literal: // /* */ quote: \" preserved";const char chars[]={'/','*','x','*','/','\0'};const char quote='\'';
if(!text_matches(literal,expected))return 1;if(chars[0]!='/'||chars[1]!='*'||chars[3]!='*'||chars[4]!='/')return 2;if(quote!='\'')return 3;
if(!text_matches(MINIFY_DIRECTIVE_VALUE,"preprocessor directive kept"))return 4;if(before_directive!=0||after_directive!=7)return 5;return 0;}
)SRC";
    write_file(fixture,source);auto minify=run_cli(work,{"minify","makocode.cpp","--width","240","--height","80"},"minify");expect_success(minify,id+" minify");fs::path emitted=work/"makocode_minified.cpp";require(fs::exists(emitted)&&fs::file_size(emitted)>0,id+": missing output");
    std::string compact=read_file(emitted);require(compact.find("MINIFY_BLOCK_COMMENT_SENTINEL")==std::string::npos&&compact.find("MINIFY_LINE_COMMENT_SENTINEL")==std::string::npos,id+": comments retained");require(compact.find("  ")==std::string::npos,id+": repeated whitespace retained");
    auto compile=run_process({"g++","-std=c++17","-Wall","-Wextra","-pedantic",emitted.string(),"-o",(work/"minified_test").string()},work,{},work/"compile.out",work/"compile.err");expect_success(compile,id+" compile");
    auto execute=run_process({(work/"minified_test").string()},work,{},work/"execute.out",work/"execute.err");expect_success(execute,id+" executable");
    fs::path empty=work/"empty";fs::create_directories(empty);write_file(empty/"makocode.cpp","");auto empty_minify=run_cli(empty,{"minify","makocode.cpp","--width","240","--height","80"},"empty_minify");expect_success(empty_minify,id+" empty source");require(fs::exists(empty/"makocode_minified.cpp")&&fs::file_size(empty/"makocode_minified.cpp")==0,id+": empty source output mismatch");
    fs::path whitespace=work/"whitespace";fs::create_directories(whitespace);write_file(whitespace/"makocode.cpp","int\fvalue;\vint\tother;\n");auto ws=run_cli(whitespace,{"minify","makocode.cpp","--width","240","--height","80"},"whitespace");expect_success(ws,id+" control whitespace");require(read_file(whitespace/"makocode_minified.cpp")=="int value; int other;",id+": control whitespace normalization changed");
    fs::path named=work/"named";fs::create_directories(named);write_file(named/"sample.part.cpp","int  named_value;\n");auto named_minify=run_cli(named,{"minify","sample.part.cpp","--width","240","--height","80","--output","result.cpp"},"named");expect_success(named_minify,id+" named source");require(read_file(named/"result.cpp")=="int named_value;"&&fs::exists(named/"result_0000.ppm"),id+": named source output or page prefix mismatch");
    for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"char_escape","'abc\\"},{"string_escape","\"abc\\"}}){fs::path dir=work/item.first;fs::create_directories(dir);write_file(dir/"makocode.cpp",item.second);auto run=run_cli(dir,{"minify","makocode.cpp","--width","240","--height","80"},item.first);expect_success(run,id+" trailing escape");compare_files(dir/"makocode.cpp",dir/"makocode_minified.cpp",id+" trailing escape");}
    auto invalid=run_cli(work,{"minify","makocode.cpp","unexpected"},"invalid");expect_failure(invalid,id+" invalid arguments");require(invalid.out.find("minify: only one input filename is accepted")!=std::string::npos||invalid.err.find("minify: only one input filename is accepted")!=std::string::npos,id+": invalid-argument diagnostic changed");
    auto missing_source=run_cli(work,{"minify","--width","240","--height","80"},"missing_source");expect_failure(missing_source,id+" missing source");require(missing_source.out.find("minify: an input filename is required")!=std::string::npos||missing_source.err.find("minify: an input filename is required")!=std::string::npos,id+": missing-source diagnostic changed");
    auto missing=run_cli(work,{"minify","makocode.cpp"},"missing_dimensions");expect_failure(missing,id+" missing dimensions");require(missing.out.find("minify: both --width and --height are required")!=std::string::npos||missing.err.find("minify: both --width and --height are required")!=std::string::npos,id+": missing-dimensions diagnostic changed");
    fs::path crlf=work/"crlf";fs::create_directories(crlf);std::string crlf_source;for(char c:source){if(c=='\n')crlf_source.push_back('\r');crlf_source.push_back(c);}write_file(crlf/"makocode.cpp",crlf_source);auto crlf_min=run_cli(crlf,{"minify","makocode.cpp","--width","240","--height","80"},"crlf");expect_success(crlf_min,id+" CRLF");std::string crlf_result=read_file(crlf/"makocode_minified.cpp");require(crlf_result.find('\r')==std::string::npos,id+": CRLF remains");
    auto crlf_compile=run_process({"g++","-std=c++17","-Wall","-Wextra","-pedantic",(crlf/"makocode_minified.cpp").string(),"-o",(work/"crlf_test").string()},work,{},work/"crlf_compile.out",work/"crlf_compile.err");expect_success(crlf_compile,id+" CRLF compile");
    fs::path write_fail=work/"write_failure";fs::create_directories(write_fail/"makocode_minified.cpp");write_file(write_fail/"makocode.cpp",source);auto write_error=run_cli(write_fail,{"minify","makocode.cpp","--width","240","--height","80"},"write_error");expect_failure(write_error,id+" output write failure");require(write_error.out.find("minify: failed to write makocode_minified.cpp")!=std::string::npos||write_error.err.find("minify: failed to write makocode_minified.cpp")!=std::string::npos,id+": output-write diagnostic changed");
    auto allocation_check=[&](const fs::path& dir,unsigned long at,const std::string& expected){auto fail=run_process({test_executable.string(),"--minify-allocation-failure",std::to_string(at)},dir,{},dir/"alloc.out",dir/"alloc.err");expect_success(fail,id+" allocation failure mode");require(fail.out.find(expected)!=std::string::npos||fail.err.find(expected)!=std::string::npos,id+": allocation diagnostic changed at "+std::to_string(at));};
    fs::path alloc=work/"alloc";fs::create_directories(alloc);write_file(alloc/"makocode.cpp","int value;\n");allocation_check(alloc,5,"minify: failed to compact whitespace");allocation_check(alloc,3,"minify: failed to read input file");allocation_check(alloc,4,"minify: failed to strip comments");
    fs::path literal_alloc=work/"literal_alloc";fs::create_directories(literal_alloc);write_file(literal_alloc/"makocode.cpp",std::string(64,'0')+"'x");allocation_check(literal_alloc,4,"minify: failed to strip comments");
    fs::path ws_alloc=work/"ws_alloc";fs::create_directories(ws_alloc);write_file(ws_alloc/"makocode.cpp",std::string(64,'0')+"\n");allocation_check(ws_alloc,6,"minify: failed to compact whitespace");
    fs::path no_source=work/"no_source";fs::create_directories(no_source);allocation_check(no_source,0,"minify: failed to read input file");
}

static void rs_erasures_case(){
    const u16 data_symbols=32,parity_symbols=12,codeword_length=data_symbols+parity_symbols;u8 generator[makocode::RS_POLY_CAPACITY];u16 generator_length=0;
    require(makocode::rs_build_generator(parity_symbols,generator,generator_length)&&generator_length==parity_symbols+1,"RS generator setup failed");
    std::array<u8,makocode::RS_FIELD_SIZE> original{};for(u16 i=0;i<data_symbols;++i)original[i]=static_cast<u8>((i*37u+11u)&255u);
    makocode::rs_compute_parity(generator,parity_symbols,original.data(),data_symbols,original.data()+data_symbols);u8 syndromes[makocode::RS_POLY_CAPACITY];bool all_zero=false;
    makocode::rs_compute_syndromes(original.data(),codeword_length,parity_symbols,syndromes,all_zero);require(all_zero,"RS generated codeword invalid");
    std::array<u8,makocode::RS_FIELD_SIZE> damaged{};u16 corrected=0;u16 at_limit[12];memcpy(damaged.data(),original.data(),sizeof(original));
    for(u16 i=0;i<parity_symbols;++i){at_limit[i]=static_cast<u16>((i*3u+1u)%codeword_length);damaged[at_limit[i]]^=static_cast<u8>(0x51u+i);}
    require(makocode::rs_decode_block(damaged.data(),data_symbols,parity_symbols,&corrected,at_limit,parity_symbols)&&corrected==parity_symbols&&memcmp(damaged.data(),original.data(),codeword_length)==0,"RS p erasures did not recover");
    u16 too_many[13];memcpy(damaged.data(),original.data(),sizeof(original));for(u16 i=0;i<13;++i){too_many[i]=static_cast<u16>((i*3u+2u)%codeword_length);damaged[too_many[i]]^=static_cast<u8>(0x71u+i);}
    require(!makocode::rs_decode_block(damaged.data(),data_symbols,parity_symbols,&corrected,too_many,13),"RS p+1 erasures unexpectedly decoded");
    const u16 mixed_erased[6]={1,8,15,22,29,36},mixed_errors[3]={3,10,17};memcpy(damaged.data(),original.data(),sizeof(original));
    for(u16 i=0;i<6;++i)damaged[mixed_erased[i]]^=static_cast<u8>(0x91u+i);for(u16 i=0;i<3;++i)damaged[mixed_errors[i]]^=static_cast<u8>(0x31u+i);
    require(makocode::rs_decode_block(damaged.data(),data_symbols,parity_symbols,&corrected,mixed_erased,6)&&corrected==9&&memcmp(damaged.data(),original.data(),codeword_length)==0,"RS s+2e=p mixed case did not recover");
    const u16 over_erased[5]={0,8,16,24,32},over_errors[4]={4,12,20,28};memcpy(damaged.data(),original.data(),sizeof(original));
    for(u16 i=0;i<5;++i)damaged[over_erased[i]]^=static_cast<u8>(0xA1u+i);for(u16 i=0;i<4;++i)damaged[over_errors[i]]^=static_cast<u8>(0xC1u+i);
    require(!makocode::rs_decode_block(damaged.data(),data_symbols,parity_symbols,&corrected,over_erased,5),"RS s+2e=p+1 unexpectedly decoded");
    u16 duplicate[2]={4,4};memcpy(damaged.data(),original.data(),sizeof(original));damaged[4]^=0x5a;require(!makocode::rs_decode_block(damaged.data(),data_symbols,parity_symbols,&corrected,duplicate,2),"RS duplicate erasure accepted");
    u16 out_of_range[1]={codeword_length};require(!makocode::rs_decode_block(original.data(),data_symbols,parity_symbols,&corrected,out_of_range,1),"RS out-of-range erasure accepted");
    const u16 ld=26,lp=229,le=69,unknown=80;std::array<u8,makocode::RS_FIELD_SIZE> large{},large_damaged{};u8 lg[makocode::RS_POLY_CAPACITY];u16 lgl=0,erased[le];
    require(makocode::rs_build_generator(lp,lg,lgl)&&lgl,"high-parity generator failed");for(u16 i=0;i<ld;++i)large[i]=static_cast<u8>((i*91u+43u)&255u);makocode::rs_compute_parity(lg,lp,large.data(),ld,large.data()+ld);
    memcpy(large_damaged.data(),large.data(),sizeof(large));for(u16 i=0;i<le;++i){erased[i]=i;large_damaged[i]^=static_cast<u8>(0x21u+i);}std::array<u8,makocode::RS_FIELD_SIZE> pure=large_damaged;
    require(makocode::rs_decode_block(pure.data(),ld,lp,&corrected,erased,le)&&memcmp(pure.data(),large.data(),255)==0,"high-parity e=69 failed");
    for(u16 i=0;i<unknown;++i)large_damaged[le+i]^=static_cast<u8>(i+1);require(makocode::rs_decode_block(large_damaged.data(),ld,lp,&corrected,erased,le)&&corrected==le+unknown&&memcmp(large_damaged.data(),large.data(),255)==0,"high-parity e=69,t=80 boundary failed");
}

static void metadata_tile_format_case(){
    const u64 expected[]={0,1,65535,65536,0x123456789ABull,U64_MAX_VALUE};for(u64 value:expected){MetadataTile::BitWriter writer;require(MetadataTile::write_compact_u64(writer,value),"compact u64 write failed");MetadataTile::BitReader reader;reader.reset(writer.data(),writer.bit_size());u64 actual=0;require(MetadataTile::read_compact_u64(reader,actual)&&actual==value&&!reader.failed&&reader.cursor==writer.bit_size(),"compact u64 roundtrip failed");}
    const u32 vals[]={0,1,2,127,128,253,254,65535};for(u32 p:vals)for(u32 e:vals){MetadataTile::BitWriter writer;require(MetadataTile::write_compact_u16(writer,p,true)&&MetadataTile::write_compact_u16(writer,e,true),"compact u16 write failed");MetadataTile::BitReader reader;reader.reset(writer.data(),writer.bit_size());u32 actual=0;require(MetadataTile::read_compact_u16(reader,actual,true)&&actual==p&&MetadataTile::read_compact_u16(reader,actual,true)&&actual==e&&!reader.failed&&reader.cursor==writer.bit_size(),"compact u16 roundtrip failed");if(p==0&&e==0)require(writer.bit_size()==6,"zero compact-u16 framing changed");}
    makocode::ByteBuffer data;require(data.ensure(1000),"metadata RS allocation failed");data.size=1000;for(usize i=0;i<data.size;++i)data.data[i]=static_cast<u8>(i*73u+19u);makocode::ByteBuffer word;require(MetadataTile::rs_encode_with_parity(data,232,word)&&word.size==11208,"metadata RS encode failed");
    makocode::ByteBuffer damaged;require(damaged.ensure(word.size),"metadata RS damage allocation failed");damaged.size=word.size;memcpy(damaged.data,word.data,word.size);usize offset=0;
    for(u32 block=0;block<44;++block){usize count=block==43?11:23;damaged.data[offset+count/2]^=static_cast<u8>(block+1);offset+=count+232;}
    u32 corrections=0;require(MetadataTile::rs_decode_with_parity(damaged,1000,232,corrections)&&corrections==44&&damaged.size==data.size&&memcmp(damaged.data,data.data,data.size)==0,"metadata multi-block RS correction failed");
    require(damaged.ensure(word.size),"metadata RS reset allocation failed");damaged.size=word.size;memcpy(damaged.data,word.data,word.size);for(u32 i=0;i<117;++i)damaged.data[i]^=static_cast<u8>(i+1);corrections=0;require(!MetadataTile::rs_decode_with_parity(damaged,1000,232,corrections),"metadata over-boundary RS errors unexpectedly recovered");
}

static void partial_ecc_recovery_case(){
    using namespace makocode;BitWriter archive;static const char first_path[]="00-first.txt",first_data[]="recovered before the damaged block\n",second_path[]="99-large.bin";const usize second_size=65536;ByteBuffer second;
    require(second.ensure(second_size)&&MetadataTile::write_compact_u64(archive,2)&&archive.write_bit(0)&&MetadataTile::write_compact_u64(archive,sizeof(first_path)-1),"partial archive init failed");
    for(usize i=0;i<sizeof(first_path)-1;++i)require(archive.write_bits(static_cast<u8>(first_path[i]),8),"partial first path failed");
    require(MetadataTile::write_compact_u64(archive,sizeof(first_data)-1),"partial first length failed");for(usize i=0;i<sizeof(first_data)-1;++i)require(archive.write_bits(static_cast<u8>(first_data[i]),8),"partial first data failed");
    require(archive.write_bit(0)&&MetadataTile::write_compact_u64(archive,sizeof(second_path)-1),"partial second entry failed");for(char c:second_path)if(c)require(archive.write_bits(static_cast<u8>(c),8),"partial second path failed");require(MetadataTile::write_compact_u64(archive,second_size),"partial second length failed");
    u32 random=0x9E3779B9u;for(usize i=0;i<second_size;++i){random^=random<<13;random^=random>>17;random^=random<<5;second.data[i]=static_cast<u8>(random);require(archive.write_bits(second.data[i],8),"partial second bytes failed");}second.size=second_size;require(archive.align_to_byte(),"partial archive align failed");
    ByteBuffer compressed;require(lzma_compress(archive.data(),archive.byte_size(),compressed,true),"partial compression failed");ByteBuffer properties;require(properties.ensure(compressed.size+LZMA_PROPS_SIZE),"partial properties allocation failed");memcpy(properties.data,LZMA_FIXED_PROPERTIES,LZMA_PROPS_SIZE);memcpy(properties.data+LZMA_PROPS_SIZE,compressed.data,compressed.size);properties.size=compressed.size+LZMA_PROPS_SIZE;byte_buffer_move(compressed,properties);
    BitWriter encoded;EccSummary summary;require(encode_payload_with_ecc(compressed,.05,encoded,summary),"partial ECC encode failed");usize header=ecc_header_span_for_bytes(ECC_HEADER_TOTAL_BYTES+encoded.byte_size());ByteBuffer stream;require(header==ECC_HEADER_TOTAL_BYTES&&stream.ensure(header+encoded.byte_size()),"partial stream allocation failed");stream.size=header+encoded.byte_size();
    require(build_ecc_header_bytes(stream.data,summary.block_data_symbols,summary.parity_symbols,summary.block_count,summary.original_bytes),"partial ECC header failed");memcpy(stream.data+header,encoded.data(),encoded.byte_size());usize block_total=summary.block_data_symbols+summary.parity_symbols,final_block=header+(summary.block_count-1)*block_total;
    for(u16 i=0;i<summary.parity_symbols;++i)stream.data[final_block+summary.block_data_symbols-summary.parity_symbols+i]^=static_cast<u8>(0xA5u+i*17u);require(fisher_yates_shuffle(stream.data+header,stream.size-header),"partial shuffle failed");
    DecoderContext decoder;require(decoder.parse(stream.data,static_cast<u64>(stream.size)*8,nullptr,0)&&decoder.has_payload&&decoder.partial_payload&&decoder.ecc_stats.unrecoverable_blocks>0,"partial ECC decoder failed to preserve a partial payload");
    struct Check{bool first=false,second=false;const u8* expected=nullptr;usize size=0;} check{false,false,second.data,second.size};
    auto visitor=[](u8 type,const char* path,usize path_size,const u8* data,usize size,u64,void* context)->bool{Check* c=static_cast<Check*>(context);const char p1[]="00-first.txt",d1[]="recovered before the damaged block\n",p2[]="99-large.bin";
        if(type==0&&path_size==sizeof(p1)-1&&memcmp(path,p1,path_size)==0)c->first=size==sizeof(d1)-1&&memcmp(data,d1,size)==0;else if(type==0&&path_size==sizeof(p2)-1&&memcmp(path,p2,path_size)==0)c->second=size==c->size&&memcmp(data,c->expected,size)==0;return true;};
    bool archive_ok=archive_parse(decoder.payload,visitor,&check,nullptr,true);require(archive_ok&&check.first&&!check.second,"partial archive outcome parse="+std::to_string(archive_ok)+" first="+std::to_string(check.first)+" second="+std::to_string(check.second));
}

static fs::path damage_palette_row(const fs::path& page,const fs::path& output,unsigned palette_count){
    PpmImage image=read_ppm(page);MetadataTile::Values tile=extract_tile_values(page);unsigned tile_x=(image.width-48)/2,tile_y=(image.height-48)/2;
    unsigned x0=MetadataTile::palette_tag_x0(palette_count),row=tile_y+tile.metadata_tag_y0;std::string original=image.pixels;
    for(unsigned x=0;x<palette_count+1;++x){size_t at=(static_cast<size_t>(row)*image.width+tile_x+x0+x)*3;image.pixels[at]=image.pixels[at+1]=image.pixels[at+2]=static_cast<char>(255);}
    require(image.pixels!=original,"palette row mutation changed no pixels");write_file(output,"P6\n"+std::to_string(image.width)+" "+std::to_string(image.height)+"\n255\n"+image.pixels);return output;
}

static void recover_palette_case(){
    const std::string id="0116_decode_recover_palette";fs::path work=make_work_dir(id),payload=work/"payload.bin";generate_bytes(work,payload,8192,48621,"payload");
    const std::vector<std::tuple<std::string,unsigned,std::string>> common={{"two_color",2,"White Black"},{"four_color",4,"White Cyan Magenta Yellow"},{"five_color",5,"White Cyan Magenta Yellow Black"},{"eight_color",8,"White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black"}};
    for(const auto& item:common){std::string label=std::get<0>(item),palette=std::get<2>(item);unsigned count=std::get<1>(item);fs::path encoded=work/(label+"_encoded"),decoded=work/(label+"_decoded");fs::create_directories(encoded);
        auto enc=run_cli(work,encode_file_args(payload,encoded,"page",{"--page-width=700","--page-height=700","--ecc=2.0","--palette",palette}),"encode_"+label);expect_success(enc,id+" encode "+label);auto pages=ppm_paths(encoded,"page");require(pages.size()==1,id+": expected one palette page");
        fs::path damaged=damage_palette_row(pages[0],work/(label+"_damaged.ppm"),count);auto dec=run_cli(work,decode_file_args({damaged.string()},decoded,{"--recover-palette","--recover-limit","4"}),"decode_"+label);expect_success(dec,id+" recover "+label);compare_files(payload,decoded/"payload.bin",id+" "+label);
        if(label=="two_color"){fs::path huge=work/"huge_limit";auto hd=run_cli(work,decode_file_args({damaged.string()},huge,{"--recover-palette","--recover-limit","100000000000000000000000"}),"huge_limit");expect_success(hd,id+" huge recover limit");compare_files(payload,huge/"payload.bin",id+" huge limit");}}
    const std::string odd="White #17D94B #E24A91 #2977C3 #D6B32A Black";fs::path odd_enc=work/"odd_encoded";fs::create_directories(odd_enc);auto enc=run_cli(work,encode_file_args(payload,odd_enc,"page",{"--page-width=700","--page-height=700","--ecc=2.0","--palette",odd}),"odd_encode");expect_success(enc,id+" odd encode");auto pages=ppm_paths(odd_enc,"page");require(pages.size()==1,id+": expected odd palette page");fs::path damaged=damage_palette_row(pages[0],work/"odd_damaged.ppm",6);
    auto limited=run_cli(work,decode_file_args({damaged.string()},work/"odd_limited",{"--recover-palette","--recover-limit","4"}),"odd_limited");expect_failure(limited,id+" odd palette bounded search");require((limited.err+limited.out).find("palette recovery tried 4 candidate(s)")!=std::string::npos,id+": missing bounded-search diagnostic");
    auto override=run_cli(work,decode_file_args({damaged.string()},work/"odd_override",{"--recover-palette","--recover-limit","1","--palette",odd}),"odd_override");expect_success(override,id+" odd palette override");compare_files(payload,work/"odd_override/payload.bin",id+" odd override");
}

static void cli_decoder_paths_case(){
    using namespace MetadataTile;const std::string id="0061_cli_decoder_paths";fs::path work=make_work_dir(id),inputs=work/"harness_inputs",archive_out=work/"archive_output";fs::create_directories(inputs);fs::create_directories(archive_out);
    write_file(inputs/"gray.pgm",std::string("P5\n3 1\n255\n",11)+std::string("\0\x7f\xff",3));write_file(inputs/"color.ppm",std::string("P6\n2 1\n255\n",11)+std::string("\x01\x02\x03\xfe\x64\0",6));write_file(inputs/"bad.ppm","P1\n1 1\n0\n");
    makocode::image::ImageBuffer loaded={};require(makocode::image::load_ppm_grayscale((inputs/"gray.pgm").c_str(),loaded,nullptr)==makocode::image::LoadSuccess&&loaded.width==3&&loaded.height==1&&loaded.pixels[0]==0&&loaded.pixels[1]==127&&loaded.pixels[2]==255,"P5 grayscale loading mismatch");
    require(makocode::image::load_ppm_grayscale((inputs/"color.ppm").c_str(),loaded,nullptr)==makocode::image::LoadSuccess&&loaded.width==2&&loaded.height==1&&loaded.pixels[0]==2&&loaded.pixels[1]==118,"P6 grayscale conversion mismatch");
    makocode::image::LoadDiagnostics diagnostics={};require(makocode::image::load_ppm_grayscale((inputs/"bad.ppm").c_str(),loaded,&diagnostics)==makocode::image::LoadUnsupportedFormat&&std::strcmp(diagnostics.message,"unsupported PPM format")==0,"unsupported PPM diagnostic mismatch");makocode::image::release(loaded);
    ArchiveBuildContext traversal;require(archive_init(traversal)&&archive_add_directory(traversal,"../traversal_escape")&&archive_finalize(traversal),"traversal archive setup failed");
    require(!unpack_archive_to_directory(traversal.buffer,archive_out.c_str()),"archive directory traversal accepted");
    struct stat outside={};require(stat((archive_out.string()+"/../traversal_escape").c_str(),&outside)!=0,"traversal path escaped output");
    makocode::image::ImageBuffer border={};border.width=border.height=7;border.pixels=static_cast<unsigned char*>(std::malloc(49));require(border.pixels!=nullptr,"border fixture allocation failed");std::memset(border.pixels,255,49);border.pixels[21]=0;border.pixels[24]=0;makocode::ByteBuffer mask;
    bool border_ok=makocode::remove_border_dirt(border,200,&mask,1);require(border_ok&&mask.size==49&&border.pixels[21]==255&&mask.data[21]==255&&border.pixels[24]==0&&mask.data[24]==0,"border dirt cleanup mismatch");mask.release();makocode::image::release(border);
    std::array<unsigned char,4096> source{};for(usize i=0;i<source.size();++i){usize n=i%1024;source[i]=static_cast<unsigned char>(n*73u+n/17u);}std::array<unsigned char,8192> compressed{};unsigned char properties[LZMA_PROPS_SIZE];size_t compressed_size=compressed.size(),properties_size=sizeof(properties);
    require(makocode::LzmaCompress(compressed.data(),&compressed_size,source.data(),source.size(),properties,&properties_size,0)==SZ_OK,"LZMA compression failed");std::array<unsigned char,4096> decoded{};size_t decoded_size=decoded.size();makocode::SizeT input_size=compressed_size;
    require(makocode::LzmaUncompress(decoded.data(),&decoded_size,compressed.data(),&input_size,properties,properties_size)==SZ_OK&&decoded_size==source.size()&&decoded==source,"LZMA roundtrip mismatch");
    const usize input_size_mf=1024u*1024u;std::vector<unsigned char> mf_input(input_size_mf);for(usize i=0;i<input_size_mf;++i){usize n=i%4096;mf_input[i]=static_cast<unsigned char>(n*29u+n/13u);}CSeqInStreamBuf stream={};stream.funcTable.Read=MyRead;stream.data=mf_input.data();stream.rem=mf_input.size();CMatchFinder finder={};MatchFinder_Construct(&finder);
    require(MatchFinder_Create(&finder,4096,32,16,16,&g_Alloc),"match finder create failed");finder.stream=&stream.funcTable;IMatchFinder vtable={};MatchFinder_CreateVTable(&finder,&vtable);vtable.Init(&finder);UInt32 initial=finder.pos;vtable.Skip(&finder,16);require(finder.pos==initial+16,"match finder skip did not advance");
    bool refilled=false,repeat=false;UInt32 distances[64];while(vtable.GetNumAvailableBytes(&finder)>0){UInt32 before=finder.streamPos,count=vtable.GetMatches(&finder,distances);if(count>=2&&distances[0]>=16)repeat=true;if(finder.streamPos>before)refilled=true;}
    require(refilled&&repeat&&stream.rem==0&&finder.streamEndWasReached,"match finder did not refill/drain");MatchFinder_Free(&finder,&g_Alloc);
    CMatchFinder normalized={};CLzRef hashes[4];normalized.pos=0xffffffffu;normalized.posLimit=0xfffffffeu;normalized.streamPos=0xffffffffu;normalized.historySize=4096;normalized.hash=hashes;normalized.hashSizeSum=2;normalized.numSons=2;UInt32 subtract=MatchFinder_GetSubValue(&normalized);hashes[0]=subtract-1;hashes[1]=subtract;hashes[2]=subtract+1;hashes[3]=0xffffffffu;MatchFinder_Normalize(&normalized);
    require(normalized.pos==0xffffffffu-subtract&&normalized.streamPos==0xffffffffu-subtract&&normalized.posLimit==0xfffffffeu-subtract&&hashes[0]==0&&hashes[1]==0&&hashes[2]==1&&hashes[3]==0xffffffffu-subtract,"match finder normalization mismatch");
    fs::path input_dir=work/"auto/input",ppm_dir=work/"auto/ppm",decoded_dir=work/"auto/decoded";fs::create_directories(input_dir);fs::create_directories(ppm_dir);fs::create_directories(decoded_dir);std::string repeated;
    for(unsigned i=0;i<256;++i){char line[100];std::snprintf(line,sizeof(line),"LZMA CLI decoder path regression payload; repetition=%03u\n",i);repeated+=line;}fs::path payload=input_dir/"payload.bin";write_file(payload,repeated);
    const std::string prefix="cli_decoder_paths_auto";auto enc=run_cli(input_dir,{"encode","--input=payload.bin","--page-width=500","--page-height=500","--output-dir="+ppm_dir.string(),"--prefix="+prefix,"--debug"},"cli_encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(ppm_dir,prefix);require(!ppms.empty()&&fs::exists(ppm_dir/(prefix+"_encode_fiducials.csv")),id+": encode fiducial report/pages missing");
    auto dec=run_cli(work,decode_file_args(ppms,decoded_dir,{"--debug"}),"cli_decode");expect_success(dec,id+" decode");compare_files(payload,decoded_dir/"payload.bin",id);check_fiducials(ppm_dir/(prefix+"_encode_fiducials.csv"),decoded_dir/"decode_fiducials.csv",id);
    fs::path pass_out=work/"decoded_with_ignored_password";auto pass=run_cli(work,decode_file_args(ppms,pass_out,{"--password=unused-password"}),"ignored_password");expect_success(pass,id+" ignored password");require((pass.out+pass.err).find("decode: warning: payload was not encrypted; password ignored")!=std::string::npos,id+": ignored-password warning missing");compare_files(payload,pass_out/"payload.bin",id+" ignored password");
}

static void footer_features_case(){
    const std::string id="0113_footer_features";fs::path work=make_work_dir(id),payload=work/"payload.bin";generate_bytes(work,payload,16384,732881,"payload");
    auto encode=[&](const fs::path& dir,const std::string& prefix,const std::vector<std::string>& extra,const fs::path& input=fs::path()){
        fs::create_directories(dir);std::vector<std::string> args={"encode","--output-dir="+dir.string(),"--prefix="+prefix};args.insert(args.end(),extra.begin(),extra.end());return run_cli(work,args,"encode_"+prefix,input.empty()?payload:input);};
    const std::string long_footer="a very long title that wraps across multiple footer lines without growing the page";fs::path wrapped=work/"wrapped";auto wrap=encode(wrapped,"wrap",{"--page-width=220","--page-height=180","--footer="+long_footer,"--ecc=fill"});expect_success(wrap,id+" wrapped footer");auto wrap_pages=ppm_paths(wrapped,"wrap");require(!wrap_pages.empty(),id+": wrapped footer emitted no pages");
    for(const auto& page:wrap_pages){PpmImage image=read_ppm(page);require(image.width==220&&image.height==180,id+": footer exceeded fixed page dimensions");}
    std::vector<std::string> args={"decode","--decoded-stdout"};args.insert(args.end(),wrap_pages.begin(),wrap_pages.end());auto wrap_dec=run_cli(work,args,"wrap_decode");expect_success(wrap_dec,id+" wrapped decode");std::string payload_bytes=read_file(payload);check_decoded(wrap_dec.out,std::vector<unsigned char>(payload_bytes.begin(),payload_bytes.end()),0,id+" wrapped footer");
    fs::path short_dir=work/"too_short";fs::create_directories(short_dir);fs::path short_payload=work/"one_byte.bin";write_file(short_payload,"x");auto too_short=encode(short_dir,"too_short",{"--page-width=220","--page-height=15","--footer=LIMIT","--font-size=2"},short_payload);expect_failure(too_short,id+" too-short page");require(ppm_paths(short_dir,"too_short").empty(),id+": too-short footer emitted PPM");
    fs::path selected=work/"selected";auto selected_enc=encode(selected,"selected",{"--page-width=180","--page-height=180","--footer=MARK","--footer-pages=1,middle","--ecc=fill"});expect_success(selected_enc,id+" selected footer pages");auto selected_pages=ppm_paths(selected,"selected");require(selected_pages.size()>=3,id+": selected footer fixture too short");
    size_t middle=(selected_pages.size()+1)/2;for(size_t p=0;p<selected_pages.size();++p){PpmImage image=read_ppm(selected_pages[p]);size_t dark=0;for(unsigned y=image.height-8;y<image.height;++y)for(unsigned x=0;x<image.width;++x){size_t at=(static_cast<size_t>(y)*image.width+x)*3;dark+=static_cast<unsigned char>(image.pixels[at])<128&&static_cast<unsigned char>(image.pixels[at+1])<128&&static_cast<unsigned char>(image.pixels[at+2])<128;}
        bool expected=p==0||p==middle-1;require(expected?dark>0:dark==0,id+": footer page selection mismatch at "+std::to_string(p+1));}
    args={"decode","--decoded-stdout"};args.insert(args.end(),selected_pages.begin(),selected_pages.end());auto selected_dec=run_cli(work,args,"selected_decode");expect_success(selected_dec,id+" selected decode");check_decoded(selected_dec.out,std::vector<unsigned char>(payload_bytes.begin(),payload_bytes.end()),0,id+" selected footer");
    fs::path mask=work/"footer_mask.ppm";std::vector<char> pixels(180u*180u*3u,static_cast<char>(255));pixels[pixels.size()-3]=static_cast<char>(255);pixels[pixels.size()-2]=0;pixels[pixels.size()-1]=static_cast<char>(255);write_file(mask,"P6\n180 180\n255\n"+std::string(pixels.begin(),pixels.end()));
    fs::path overlay=work/"overlay";auto over_enc=encode(overlay,"overlay",{"--page-width=180","--page-height=180","--footer=MARK","--overlay-fraction=0.1","--overlay-ppm="+mask.string(),"--overlay-pages=all","--ecc=fill"});expect_success(over_enc,id+" overlay footer");auto overlay_pages=ppm_paths(overlay,"overlay");require(!overlay_pages.empty(),id+": footer overlay emitted no page");PpmImage over=read_ppm(overlay_pages.front());size_t last=over.pixels.size()-3;require(static_cast<unsigned char>(over.pixels[last])==255&&static_cast<unsigned char>(over.pixels[last+1])==255&&static_cast<unsigned char>(over.pixels[last+2])==255,id+": overlay modified footer region");
    fs::path over_dir=work/"over_barcode";auto ob=encode(over_dir,"over",{"--page-width=500","--page-height=500","--footer=OVER","--footer-over-barcode","--ecc=fill"});expect_success(ob,id+" over-barcode footer encode");auto ob_pages=ppm_paths(over_dir,"over");require(!ob_pages.empty(),id+": over-barcode footer emitted no page");args={"decode","--decoded-stdout"};args.insert(args.end(),ob_pages.begin(),ob_pages.end());auto ob_stdout=run_cli(work,args,"over_stdout");expect_success(ob_stdout,id+" over-barcode footer decode");check_decoded(ob_stdout.out,std::vector<unsigned char>(payload_bytes.begin(),payload_bytes.end()),0,id+" over-barcode footer");
    auto ob_debug=run_cli(work,decode_file_args(ob_pages,work/"over_debug",{"--debug","--decoded-file"}),"over_debug");expect_success(ob_debug,id+" over-barcode debug decode");require(std::regex_search(ob_debug.out+ob_debug.err,std::regex("ECC detected [1-9][0-9]* symbol errors")),id+": over-barcode footer did not damage protected symbols");
    fs::path weak_dir=work/"weak";fs::path footer_payload=work/"footer.bin";write_file(footer_payload,"footer");auto weak=encode(weak_dir,"weak",{"--page-width=500","--page-height=500","--footer=OVER","--footer-over-barcode","--ecc=0.01"},footer_payload);expect_failure(weak,id+" insufficient ECC");std::string why=weak.out+weak.err;require(why.find("ECC cannot cover")!=std::string::npos||why.find("footer")!=std::string::npos||why.find("ECC")!=std::string::npos,id+": insufficient ECC diagnostic missing");
}

static void scan_registration_case(const std::string& id,double pitch,double ox,double oy,
                                   unsigned width,unsigned height,bool blur,bool streaks,
                                   bool low_contrast=false,bool ecc_fill=false,
                                   double expected_scale=0.0,double expected_tx=0.0,double expected_ty=0.0){
    fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded=work/"encoded",control=work/"control",decoded=work/"decoded";
    fs::create_directories(encoded);
    write_file(payload,"makocode");
    std::vector<std::string> enc=encode_file_args(payload,encoded,"source",{"--page-width=80","--page-height=80"});
    if(ecc_fill)enc.push_back("--ecc=fill");
    auto result=run_cli(work,enc,"encode");expect_success(result,id+" encode");
    auto pages=ppm_paths(encoded,"source");require(pages.size()==1,id+": expected one source page");
    fs::path scan=work/"scan.ppm";
    make_affine_scan(pages.front(),scan,width,height,pitch,ox,oy,
                     low_contrast?120:60,low_contrast?114:174,blur,streaks);
    auto control_result=run_cli(work,decode_file_args(pages,control),"control_decode");expect_success(control_result,id+" control decode");
    compare_files(payload,control/"payload.bin",id+" control");
    auto scan_result=run_cli(work,decode_file_args({scan.string()},decoded,expected_scale>0.0?std::vector<std::string>{"--debug"}:std::vector<std::string>{}),"scan_decode");expect_success(scan_result,id+" scan decode");
    compare_files(payload,decoded/"payload.bin",id+" scan");
    auto control_stdout=run_cli(work,{"decode","--decoded-stdout",pages.front()},"control_stdout");
    auto scan_stdout=run_cli(work,{"decode","--decoded-stdout",scan.string()},"scan_stdout");
    expect_success(control_stdout,id+" stdout control");expect_success(scan_stdout,id+" stdout scan");
    require(control_stdout.out==scan_stdout.out,id+": decoded stdout report changed");
    if(expected_scale>0.0){
        std::ifstream affine(decoded/"decode_affine.csv");std::string header,line;std::getline(affine,header);std::getline(affine,line);
        auto fields=split_csv(line);require(fields.size()>=10&&fields[1]=="1",id+": missing affine estimate");
        double scale=std::strtod(fields[2].c_str(),nullptr),rotation=std::strtod(fields[3].c_str(),nullptr);
        double tx=std::strtod(fields[8].c_str(),nullptr),ty=std::strtod(fields[9].c_str(),nullptr);
        require(std::fabs(scale-expected_scale)<=0.01&&std::fabs(tx-expected_tx)<=0.75&&std::fabs(ty-expected_ty)<=0.75&&std::fabs(rotation)<=0.25,id+": affine registration outside tolerance");
    }
}

static void canonical_tile_case(){
    const std::string id="0104_canonical_tile";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);
    std::vector<unsigned char> bytes(65536);std::mt19937 random(42);for(auto& b:bytes)b=static_cast<unsigned char>(random()&255u);write_file(payload,std::string(bytes.begin(),bytes.end()));
    for(const auto& dims:std::vector<std::pair<unsigned,unsigned>>{{23,23},{48,48},{49,48},{64,48}}){makocode::ByteBuffer mask;u64 reserved=0;require(compute_fiducial_reservation(dims.first,dims.second,dims.second,reserved,&mask,false,0,0,0),id+": canonical reservation failed");u64 overwritten=0;for(usize i=0;i<mask.size;++i){require(mask.data[i]==0||mask.data[i]==OVERWRITE_MASK_PIXEL,id+": unexpected reservation mask value");overwritten+=mask.data[i]==OVERWRITE_MASK_PIXEL;}auto p=MetadataTile::compute_center_tile_placement(dims.first,dims.second);require(p.valid&&p.x0==(dims.first>=48?(dims.first-48)/2:0)&&p.y0==(dims.second>=48?(dims.second-48)/2:0)&&p.offset_x==(dims.first<48?(48-dims.first)/2:0)&&p.offset_y==(dims.second<48?(48-dims.second)/2:0)&&overwritten==reserved&&overwritten<=static_cast<u64>(dims.first)*dims.second,id+": canonical small-page capacity layout mismatch");}
    auto enc=run_cli(work,encode_file_args(payload,pages,"page",{"--page-width=1000","--page-height=1000"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"page");require(ppms.size()==1,id+": expected a single canonical page");TileSummary summary=read_tile_summary(ppms.front(),1000,1000);require(summary.block_count>0&&summary.parity>0,id+": missing canonical ECC");
    auto dec=run_cli(work,decode_file_args(ppms,decoded),"decode");expect_success(dec,id+" decode");compare_files(payload,decoded/"payload.bin",id);
    fs::path overlay=work/"overlay.ppm",budget_dir=work/"budget_pages";expect_success(run_transform(work,{"overlay-mask","--output",overlay.string(),"--circle-color","0 0 0","--background-color","255 255 255","--width","1200","--height","1200"},"mask"),id+" mask");
    auto budget=run_cli(work,encode_file_args(payload,budget_dir,"budget",{"--page-width=1200","--page-height=1200","--overlay-fraction=0.10","--overlay-ppm="+overlay.string(),"--overlay-pages=all","--debug"}),"budget");expect_success(budget,id+" ECC overlay budget");auto budget_pages=ppm_paths(budget_dir,"budget");require(budget_pages.size()==1,id+": expected one overlay budget page");TileSummary bs=read_tile_summary(budget_pages[0],1200,1200);require(bs.overlay_budget>0&&bs.selector_present,id+": automatic ECC omitted selector budget");
    fs::path invalid=work/"no_overlay_attempt";auto rejected=run_cli(work,encode_file_args(payload,invalid,"bad",{"--ecc=0","--overlay-fraction=0.10","--overlay-ppm="+overlay.string(),"--overlay-pages=all","--page-width=1000","--page-height=1000"}),"overlay_without_ecc");expect_failure(rejected,id+" zero-ECC overlay");require((rejected.out+rejected.err).find("overlay requires ECC")!=std::string::npos,id+": zero-ECC overlay diagnostic changed");
    fs::path custom_dir=work/"custom_pages",custom_out=work/"custom_out";auto custom=run_cli(work,encode_file_args(payload,custom_dir,"custom",{"--page-width=1000","--page-height=1000","--fiducial-spacing=30"}),"custom_encode");expect_success(custom,id+" custom fiducials");auto custom_pages=ppm_paths(custom_dir,"custom");require(custom_pages.size()==1,id+": custom-fiducial page missing");read_tile_summary(custom_pages[0],1000,1000);auto custom_dec=run_cli(work,decode_file_args(custom_pages,custom_out),"custom_decode");expect_success(custom_dec,id+" custom decode");compare_files(payload,custom_out/"payload.bin",id+" custom");
    fs::path tiny=work/"tiny.txt",tiny_dir=work/"tiny_pages",tiny_out=work/"tiny_out";write_file(tiny,"small canonical roundtrip\n");auto tiny_enc=run_cli(work,encode_file_args(tiny,tiny_dir,"tiny",{"--page-width=80","--page-height=80"}),"tiny_encode");expect_success(tiny_enc,id+" tiny encode");auto tiny_pages=ppm_paths(tiny_dir,"tiny");require(tiny_pages.size()==1,id+": tiny page missing");read_tile_summary(tiny_pages[0],80,80);auto tiny_dec=run_cli(work,decode_file_args(tiny_pages,tiny_out),"tiny_decode");expect_success(tiny_dec,id+" tiny decode");compare_files(tiny,tiny_out/"tiny.txt",id+" tiny");
}

static void palette_base5_capacity_case(){
    const std::string id="0106_palette_base5_capacity";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);unsigned seed=case_seed("payload/"+id);generate_bytes(work,payload,34000,seed,"payload");const std::string palette="White Cyan Magenta Yellow Black";
    auto enc=run_cli(work,encode_file_args(payload,pages,id+"_random_payload_encoded",{"--page-width=360","--page-height=360","--palette",palette}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,id+"_random_payload_encoded");require(ppms.size()==1,id+": base-5 expected one page");auto normal=run_cli(work,decode_file_args(ppms,decoded,{"--palette",palette}),"decode_payload");expect_success(normal,id+" payload decode");compare_files(payload,decoded/"payload.bin",id);
    fs::path metadata=work/"metadata";auto dec=run_cli(work,decode_file_args(ppms,metadata,{"--decoded-file","--palette",palette}),"decode_metadata");expect_success(dec,id+" metadata decode");
    PpmImage image=read_ppm(ppms[0]);MetadataTile::Values tile=extract_tile_values(ppms[0]);int tx=static_cast<int>(image.width/2)-24+static_cast<int>(tile.metadata_square_x0),ty=static_cast<int>(image.height/2)-24+static_cast<int>(tile.metadata_tag_y0);size_t black_outside=0;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){size_t at=(static_cast<size_t>(y)*image.width+x)*3;bool in=x>=static_cast<unsigned>(tx)&&x<static_cast<unsigned>(tx+tile.metadata_square_side)&&y>=static_cast<unsigned>(ty)&&y<static_cast<unsigned>(ty+tile.metadata_square_side+6);if(image.pixels[at]==0&&image.pixels[at+1]==0&&image.pixels[at+2]==0&&!in)++black_outside;}
    require(black_outside>2500,id+": expected base-5 black symbols outside metadata tile");std::vector<fs::path> reports;for(const auto& entry:fs::directory_iterator(metadata))if(entry.is_regular_file()&&entry.path().extension()==".txt")reports.push_back(entry.path());require(reports.size()==1,id+": decoded metadata report count changed");std::istringstream lines(read_file(reports[0]));std::string line;u64 page_bits=0;unsigned matches=0;while(std::getline(lines,line))if(line.rfind("metadata page_bits ",0)==0){page_bits=std::strtoull(line.c_str()+19,nullptr,10);++matches;}require(matches==1&&page_bits>2ull*image.width*image.height,id+": base-5 payload did not exceed two bits per pixel");
}

static void canonical_overlay_multipage_case(){
    const std::string id="0105_canonical_overlay_multipage";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages_dir=work/"pages";generate_bytes(work,payload,4096,103,"payload");
    fs::path black=work/"overlay.ppm",cyan=work/"overlay_second.ppm";expect_success(run_transform(work,{"overlay-mask","--output",black.string(),"--circle-color","0 0 0","--background-color","255 255 255","--width","1000","--height","1000"},"mask"),id+" black mask");expect_success(run_transform(work,{"overlay-mask","--output",cyan.string(),"--circle-color","0 255 255","--background-color","255 255 255","--width","1000","--height","1000"},"cyan"),id+" cyan mask");
    std::vector<std::string> args={"encode","--input=payload.bin","--overlay-fraction=0.992156862745098","--overlay-ppm="+black.string(),"--overlay-pages=1-1,1","--overlay-ppm="+cyan.string(),"--overlay-pages=2-2","--page-width=1000","--page-height=1000","--prefix=page","--output-dir="+pages_dir.string(),"--debug"};auto enc=run_cli(work,args,"encode");expect_success(enc,id+" encode");std::string log=enc.out+enc.err;size_t occurrences=0,offset=0;while((offset=log.find("debug: overlay erasure budget per block=",offset))!=std::string::npos){++occurrences;++offset;}require(occurrences==2,id+": expected two page overlay reservations");
    auto pages=ppm_paths(pages_dir,"page");require(pages.size()>=2&&pages.size()<=16,id+": unexpected multipage output count");std::vector<std::pair<u64,std::string>> indexed;TileSummary baseline;
    for(size_t i=0;i<pages.size();++i){TileSummary t=read_tile_summary(pages[i],1000,1000);require(t.page_count==pages.size()&&t.selector_present&&t.overlay_budget>0,id+": invalid overlay tile metadata");if(i==0)baseline=t;else require(t.block_count==baseline.block_count&&t.parity==baseline.parity&&t.overlay_budget==baseline.overlay_budget&&t.selector_seed==baseline.selector_seed,id+": page selector settings disagree");indexed.push_back({t.page_index,pages[i]});}
    std::sort(indexed.begin(),indexed.end(),[](auto a,auto b){return a.first<b.first;});pages.clear();for(size_t i=0;i<indexed.size();++i){require(indexed[i].first==i+1,id+": page metadata index gap");pages.push_back(indexed[i].second);}
    fs::path reserved_dir=work/"mixed_reserved",plain_dir=work/"mixed_plain";auto reserved=run_cli(work,{"encode","--input=payload.bin","--overlay-fraction=0.992156862745098","--overlay-ppm="+black.string(),"--overlay-pages=all","--page-width=1000","--page-height=1000","--prefix=reserved","--output-dir="+reserved_dir.string()},"reserved_encode");expect_success(reserved,id+" reserved encode");auto plain=run_cli(work,{"encode","--input=payload.bin","--page-width=1000","--page-height=1000","--prefix=plain","--output-dir="+plain_dir.string()},"plain_encode");expect_success(plain,id+" plain encode");auto rp=ppm_paths(reserved_dir,"reserved"),pp=ppm_paths(plain_dir,"plain");require(!rp.empty()&&pp.size()==1,id+": mixed-selector fixture page count changed");auto conflict=run_cli(work,decode_file_args({rp[0],pp[0]},work/"mixed_decode"),"mixed_decode");expect_failure(conflict,id+" mixed selector decode");require((conflict.out+conflict.err).find("conflicting overlay selector settings between pages")!=std::string::npos,id+": mixed selector diagnostic changed");
    PpmImage corrupted=read_ppm(pages.front());unsigned x0=(corrupted.width-48)/2,y0=(corrupted.height-48)/2;for(unsigned y=y0;y<y0+48;++y)for(unsigned x=x0;x<x0+48;++x){size_t at=(static_cast<size_t>(y)*corrupted.width+x)*3;corrupted.pixels[at]=corrupted.pixels[at+1]=corrupted.pixels[at+2]=127;}
    bool corrupted_tile_decoded=false;MetadataTile::Values bad_values={};makocode::ByteBuffer bad_palette;const u8* rgb=reinterpret_cast<const u8*>(corrupted.pixels.data());
    for(u32 footer_rows=0;footer_rows<=32&&footer_rows<corrupted.height;++footer_rows){u32 data_height=corrupted.height-footer_rows;auto placement=MetadataTile::compute_center_tile_placement(corrupted.width,data_height);if(placement.valid&&MetadataTile::decode_tile(rgb,corrupted.width,corrupted.height,data_height,placement,bad_values,bad_palette)){corrupted_tile_decoded=true;break;}}
    require(!corrupted_tile_decoded,id+": corrupted centered selector tile decoded directly");
    auto dec=run_cli(work,decode_file_args(pages,work/"decoded"),"decode");expect_success(dec,id+" decode");compare_files(payload,work/"decoded/payload.bin",id);
}

struct BlobDecodeResult{bool parsed=false,partial=false;u64 unrecoverable=0,erased=0;};

static bool test_uniform_region_detector(){
    const u32 width=100,height=100;usize count=static_cast<usize>(width)*height;makocode::ByteBuffer symbols,ranks;if(!symbols.ensure(count)||!ranks.ensure(count*sizeof(u64)))return false;symbols.size=count;ranks.size=count*sizeof(u64);u64* rank=reinterpret_cast<u64*>(ranks.data);u32 state=0xA341316Cu;
    for(usize i=0;i<count;++i){state^=state<<13;state^=state>>17;state^=state<<5;symbols.data[i]=static_cast<u8>(state&1u);rank[i]=i;}MetadataTile::Placement placement={};makocode::ByteBuffer mask;
    if(!detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,placement,1,count,mask))return false;for(usize i=0;i<mask.size;++i)if(mask.data[i])return false;
    for(u32 y=20;y<80;++y)for(u32 x=20;x<80;++x)symbols.data[static_cast<usize>(y)*width+x]=1;makocode::ByteBuffer blob;if(!detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,placement,1,count,blob))return false;u64 marked=0;for(usize i=0;i<blob.size;++i)marked+=blob.data[i]!=0;return marked>=1500&&marked<5000;
}

static bool decode_blob_fixture(const fs::path& path,bool detect,BlobDecodeResult& result){
    result={};makocode::ByteBuffer ppm;if(!read_entire_file(path.c_str(),ppm))return false;ImageMappingConfig mapping;makocode::ByteBuffer frame,frame_erasures;u64 frame_bits=0;PpmParserState state;bool retry=false,rotation=false;
    if(!ppm_extract_frame_bits(ppm,mapping,frame,frame_bits,state,false,&retry,nullptr,1,&frame_erasures,nullptr,nullptr,nullptr,nullptr,&rotation,nullptr,false,nullptr,nullptr,nullptr,0,0,0,detect)||!state.has_encoded_tile||!metadata_schema_is_current(state.encoded_tile.schema_version))return false;
    if(rotation){for(usize i=0;i<frame.size;++i)frame.data[i]=rotate_right_u8(frame.data[i],static_cast<u8>((i%3)+1));for(usize i=0;i<frame_erasures.size;++i)frame_erasures.data[i]=rotate_right_u8(frame_erasures.data[i],static_cast<u8>((i%3)+1));}
    makocode::ByteBuffer payload_stream,payload_erasures;u64 payload_bits=0;if(!frame_bits_to_payload(frame.data,frame_bits,state,payload_stream,payload_bits,nullptr,frame_erasures.data,frame_bits,&payload_erasures))return false;for(usize i=0;i<payload_erasures.size;++i)result.erased+=payload_erasures.data[i]!=0;
    const MetadataTile::Values& tile=state.encoded_tile;if(!tile.ecc_enabled||!tile.ecc_block_data||!tile.ecc_parity||!tile.ecc_block_count||tile.ecc_block_count>U64_MAX_VALUE/(tile.ecc_block_data+tile.ecc_parity))return false;
    u64 protected_bytes=tile.ecc_block_count*static_cast<u64>(tile.ecc_block_data+tile.ecc_parity);if(protected_bytes>payload_stream.size||protected_bytes>static_cast<u64>(USIZE_MAX_VALUE)-makocode::ECC_HEADER_TOTAL_BYTES)return false;
    u8 header[makocode::ECC_HEADER_TOTAL_BYTES];if(!makocode::build_ecc_header_bytes(header,tile.ecc_block_data,tile.ecc_parity,tile.ecc_block_count,tile.ecc_original_bytes,tile.ecc_header_magic,tile.ecc_header_version,tile.ecc_header_flags,tile.ecc_header_reserved))return false;
    makocode::ByteBuffer decoder_stream;if(!decoder_stream.ensure(makocode::ECC_HEADER_TOTAL_BYTES+static_cast<usize>(protected_bytes)))return false;decoder_stream.size=makocode::ECC_HEADER_TOTAL_BYTES+static_cast<usize>(protected_bytes);memcpy(decoder_stream.data,header,makocode::ECC_HEADER_TOTAL_BYTES);memcpy(decoder_stream.data+makocode::ECC_HEADER_TOTAL_BYTES,payload_stream.data,static_cast<usize>(protected_bytes));
    makocode::ByteBuffer erasures;if(!erasures.ensure(decoder_stream.size))return false;erasures.size=decoder_stream.size;memset(erasures.data,0,makocode::ECC_HEADER_TOTAL_BYTES);for(usize i=0;i<static_cast<usize>(protected_bytes);++i)erasures.data[makocode::ECC_HEADER_TOTAL_BYTES+i]=i<payload_erasures.size?payload_erasures.data[i]:0;
    makocode::DecoderContext decoder;result.parsed=decoder.parse(decoder_stream.data,static_cast<u64>(decoder_stream.size)*8,nullptr,0,erasures.data,erasures.size,0,nullptr,0,0,0,true,true,makocode::LZMA_SIZE_FROM_HEADER,false);result.partial=decoder.partial_payload;result.unrecoverable=decoder.ecc_statistics().unrecoverable_blocks;return true;
}

static void blob_erasure_case(){
    const std::string id="0114_decode_blob_erasures";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages";fs::create_directories(pages);generate_bytes(work,payload,24576,1944,"payload");
    auto enc=run_cli(work,encode_file_args(payload,pages,"blob",{"--page-width=600","--page-height=600","--palette","White Black","--ecc=0.75"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"blob");require(!ppms.empty(),id+": source page missing");
    require(test_uniform_region_detector(),id+": uniform-region detector threshold checks failed");
    for(bool black:{true,false}){PpmImage source=read_ppm(ppms[0]);require(source.width==600&&source.height==600,id+": source dimensions changed");const int cx=460,cy=400,radius=120;size_t changed=0;for(int y=cy-radius;y<=cy+radius;++y)for(int x=cx-radius;x<=cx+radius;++x)if((x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius){size_t at=(static_cast<size_t>(y)*source.width+x)*3;unsigned char value=black?0:255;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(value);++changed;}require(changed>40000,id+": blot mutation too small");
        fs::path mutated=work/(black?"blob_black.ppm":"blob_white.ppm");write_file(mutated,"P6\n600 600\n255\n"+source.pixels);auto cli=run_cli(work,{"decode","--decoded-stdout",mutated.string()},black?"decode_black":"decode_white");expect_success(cli,id+" CLI decode");std::string expected=read_file(payload);check_decoded(cli.out,std::vector<unsigned char>(expected.begin(),expected.end()),0,id);
        BlobDecodeResult unknown,detected;require(decode_blob_fixture(mutated,false,unknown)&&decode_blob_fixture(mutated,true,detected),id+": direct fixture decode failed");
        require(unknown.unrecoverable>0,id+": blot did not exceed unknown-error capacity");require(detected.parsed&&!detected.partial&&detected.unrecoverable==0&&detected.erased>unknown.erased,id+": detected erasures did not recover ECC blocks");}
}

static void raw_fallback_case(){
    const std::string id="0069_decode_raw_fallback";fs::path work=make_work_dir(id),payload_path=work/"raw_payload.bin",ppm_path=work/"raw_payload.ppm";generate_bytes(work,payload_path,4096,5066,"raw_payload");makocode::ByteBuffer payload;require(read_entire_file(payload_path.c_str(),payload),id+": cannot read raw payload");
    makocode::EncoderContext encoder;encoder.config.ecc_redundancy=.5;encoder.config.overlay_erasure_budget=0;require(encoder.set_payload(payload.data,payload.size)&&encoder.build(),id+": raw fallback fixture encode failed");
    ImageMappingConfig mapping;const u32 width=500,height=500;PageFooterConfig footer_config;makocode::ByteBuffer frame;u64 frame_bits=0;FooterLayout footer;u32 data_height=height;u64 bits_per_page=0,page_count=0;
    require(build_frame_bits(encoder,mapping,frame,frame_bits)&&compute_page_layout(mapping,footer_config,frame_bits,width,height,footer,data_height,bits_per_page,page_count)&&page_count==1,id+": raw fallback fixture page layout failed");
    makocode::ByteBuffer page;require(encode_page_to_ppm(mapping,frame,frame_bits,0,width,height,1,bits_per_page,&encoder.ecc_info(),0.0,nullptr,0,footer,page),id+": raw fallback fixture page render failed");makocode::ByteBuffer fiducials;const u8 white[3]={255,255,255};require(apply_default_fiducial_grid(page,fiducials,data_height,white,nullptr,1),id+": raw fallback fiducials failed");write_file(ppm_path,std::string(reinterpret_cast<const char*>(fiducials.data),fiducials.size));
    fs::path disabled=work/"disabled",enabled=work/"enabled";fs::create_directories(disabled);fs::create_directories(enabled);auto off=run_cli(work,decode_file_args({ppm_path.string()},disabled),"disabled",{},{{"MAKOCODE_DECODE_RAW_FALLBACK","0"}});expect_failure(off,id+" disabled fallback");require(!fs::exists(disabled/"payload.bin"),id+": disabled fallback wrote a raw payload");
    auto on=run_cli(work,decode_file_args({ppm_path.string()},enabled),"enabled",{},{{"MAKOCODE_DECODE_RAW_FALLBACK","1"}});expect_success(on,id+" enabled fallback");compare_files(payload_path,enabled/"payload.bin",id);
}

static void unit_scale_scan_case(const std::string& id,bool edge_bleed,bool optical_blur){
    fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded=work/"encoded",decoded=work/"decoded",control=work/"control";
    fs::create_directories(encoded);write_file(payload,"makocode");
    std::vector<std::string> extras={"--page-width=80","--page-height=80"};if(optical_blur)extras.push_back("--ecc=fill");
    auto encoded_result=run_cli(work,encode_file_args(payload,encoded,"source",extras),"encode");expect_success(encoded_result,id+" encode");
    auto pages=ppm_paths(encoded,"source");require(pages.size()==1,id+": expected one source page");
    PpmImage src=read_ppm(pages.front());std::vector<unsigned char> canvas(85*85,234);
    unsigned ox=edge_bleed?2:3,oy=edge_bleed?3:4;
    for(unsigned y=0;y<80;++y)for(unsigned x=0;x<80;++x){
        unsigned value=30+static_cast<unsigned>(std::floor(.8*static_cast<unsigned char>(src.pixels[(static_cast<size_t>(y)*80+x)*3])+.5));
        canvas[static_cast<size_t>(y+oy)*85+x+ox]=static_cast<unsigned char>(value);
    }
    if(edge_bleed){for(unsigned y=20;y<23;++y)canvas[static_cast<size_t>(y)*85+82]=30;for(unsigned x=20;x<23;++x)canvas[83*85+x]=30;}
    if(optical_blur){
        std::vector<unsigned char> ink=canvas;
        for(size_t i=0;i<canvas.size();++i)ink[i]=static_cast<unsigned char>(80+static_cast<unsigned>(std::floor(canvas[i]*154.0/255.0+.5)));
        for(unsigned y=1;y<84;++y)for(unsigned x=1;x<84;++x){size_t i=static_cast<size_t>(y)*85+x;double v=.85*ink[i]+.0375*(ink[i-1]+ink[i+1]+ink[i-85]+ink[i+85]);canvas[i]=static_cast<unsigned char>(std::floor(v+.5));}
    }
    std::vector<unsigned char> rgb;rgb.reserve(canvas.size()*3);for(auto v:canvas){rgb.push_back(v);rgb.push_back(v);rgb.push_back(v);}
    fs::path scan=work/"scan.ppm";write_file(scan,"P6\n85 85\n255\n"+std::string(rgb.begin(),rgb.end()));
    auto control_result=run_cli(work,decode_file_args(pages,control),"control");expect_success(control_result,id+" control");
    auto decoded_result=run_cli(work,decode_file_args({scan.string()},decoded),"decode");expect_success(decoded_result,id+" decode");
    compare_files(payload,control/"payload.bin",id+" control");compare_files(payload,decoded/"payload.bin",id);
    auto expected=run_cli(work,{"decode","--decoded-stdout",pages.front()},"expected");
    auto actual=run_cli(work,{"decode","--decoded-stdout",scan.string()},"actual");
    expect_success(expected,id+" control stdout");expect_success(actual,id+" scan stdout");require(expected.out==actual.out,id+": decoded stdout changed");
}

struct RoundTrip {
    std::string id;
    size_t size = 0;
    std::string literal;
    bool has_payload_seed_override = false; // Stable random bytes for geometry-sensitive fixtures.
    unsigned payload_seed_override = 0;
    std::vector<std::string> encode_args;
    std::vector<std::string> decode_args;
    std::vector<std::string> transform_args;
    bool require_multiple_pages = false;
    bool tiny_dimensions_check = false;
    bool white_checker = false;
};

static void run_roundtrip(const RoundTrip& test) {
    fs::path work = make_work_dir(test.id);
    const std::string prefix = test.id + "_random_payload_encoded";
    unsigned payload_seed = test.has_payload_seed_override
        ? test.payload_seed_override
        : case_seed("payload/" + test.id);
    unsigned crypto_seed = case_seed("crypto/" + test.id);
    std::vector<unsigned char> expected = expected_payload(test.literal, test.size, payload_seed, work, "payload");
    fs::path payload = work / "payload.bin";
    write_file(payload, std::string(expected.begin(), expected.end()));

    std::vector<std::string> encode{"encode", "--output-dir=" + work.string(), "--prefix=" + prefix};
    encode.insert(encode.end(), test.encode_args.begin(), test.encode_args.end());
    std::map<std::string, std::string> env{{"MAKOCODE_TEST_SEED", std::to_string(crypto_seed)}};
    ProcessResult encoded = run_cli(work, encode, "encode", payload, env);
    expect_success(encoded, test.id + " encode");
    auto pages = ppm_paths(work, prefix);
    require(!pages.empty(), test.id + ": encode produced no PPM pages");
    if (test.require_multiple_pages) require(pages.size() > 1, test.id + ": expected multiple pages");
    if (test.id == "0001_makocode_2_colors" ||
        test.id == "0002_makocode_4_colors" ||
        test.id == "0003_makocode_5_colors" ||
        test.id == "0004_makocode_8_colors") {
        static const char* expected_marker_rows[] = {
            "11010001", "10111111", "00111111", "01000101"};
        require(pages.size() == 1u, test.id + ": marker fixture expected one page");
        MetadataTile::Values tile = extract_tile_values(pages.front());
        require(tile.palette_count >= 2u, test.id + ": marker palette missing endpoints");
        makocode::ByteBuffer tile_bits;
        require(MetadataTile::build_tile_bits(tile, tile_bits),
                test.id + ": could not rebuild metadata tile");
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 8u; ++col) {
                u32 bit = row * 8u + col;
                bool one = expected_marker_rows[row][col] == '1';
                usize cell = (usize)(tile.metadata_tag_y0 + 1u + row) *
                                 MetadataTile::TILE_SIDE +
                             MetadataTile::tag_marker_x0() + col;
                u8 expected_palette_index = one ? (u8)(tile.palette_count - 1u) : 0u;
                require(tile_bits.data[cell] == expected_palette_index,
                        test.id + ": marker pixel " + std::to_string(bit) +
                            " does not match its binary endpoint index");
            }
        }
    }
    if (test.tiny_dimensions_check) {
        std::string header = read_file(pages.front());
        std::istringstream input(header);
        std::string magic; unsigned width = 0, height = 0, maximum = 0;
        input >> magic >> width >> height >> maximum;
        require(magic == "P6" && maximum == 255 && width < 48 && height < 48, test.id + ": expected clamped sub-48 page");
    }
    std::vector<std::string> decode_base{"decode", "--decoded-stdout"};
    decode_base.insert(decode_base.end(), test.decode_args.begin(), test.decode_args.end());
    if (pages.size() == 1) {
        ProcessResult result = run_cli(work, decode_base, "decode", pages.front(), env);
        expect_success(result, test.id + " decode");
        check_decoded(result.out, expected, 0.0, test.id);
    } else {
        std::vector<std::string> decode = decode_base;
        decode.insert(decode.end(), pages.begin(), pages.end());
        ProcessResult result = run_cli(work, decode, "decode", {}, env);
        expect_success(result, test.id + " decode");
        check_decoded(result.out, expected, 0.0, test.id);
    }
    if (!test.transform_args.empty()) {
        std::vector<std::string> transformed_pages;
        for (size_t i = 0; i < pages.size(); ++i) {
            fs::path transformed = work / ("transformed_" + std::to_string(i + 1) + ".ppm");
            std::vector<std::string> command{"transform", "--input", pages[i], "--output", transformed.string()};
            command.insert(command.end(), test.transform_args.begin(), test.transform_args.end());
            if (test.white_checker) command.insert(command.end(), {"--white-checker", "--preserve-canonical-control"});
            ProcessResult transformed_result = run_transform(work, command, "transform_" + std::to_string(i));
            expect_success(transformed_result, test.id + " transform");
            transformed_pages.push_back(transformed.string());
        }
        std::vector<std::string> transformed_decode = decode_base;
        ProcessResult result;
        if(transformed_pages.size()==1)result=run_cli(work,transformed_decode,"transformed_decode",transformed_pages.front(),env);
        else {transformed_decode.insert(transformed_decode.end(),transformed_pages.begin(),transformed_pages.end());result=run_cli(work,transformed_decode,"transformed_decode",{},env);}
        expect_success(result, test.id + " transformed decode");
        check_decoded(result.out, expected, 5.0, test.id + " transformed");
    }
}

static std::vector<TestCase> build_cases() {
    std::vector<TestCase> cases;
    auto rt = [&](const char* id, const char* description, RoundTrip test) {
        test.id = id;
        cases.push_back({id, description, [test](const fs::path&) { run_roundtrip(test); }});
    };
    RoundTrip r;
    r.size=1; r.literal="0"; r.tiny_dimensions_check=true; rt("0000_tiny_0", "1 byte payload literal '0' encoded from stdin", r); r={};
    const char* makocode_ids[]={"0001_makocode_2_colors","0002_makocode_4_colors","0003_makocode_5_colors","0004_makocode_8_colors"};
    const char* makocode_palettes[]={"White Black","White Cyan Magenta Yellow","White Cyan Magenta Yellow Black","White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black"};
    const char* makocode_descriptions[]={"makocode encoded with 2 colors","makocode encoded with 4 colors","makocode encoded with 5 colors","makocode encoded with 8 colors"};
    for(size_t i=0;i<4;++i){r.size=8;r.literal="makocode";r.encode_args={"--palette",makocode_palettes[i]};r.decode_args={"--palette",makocode_palettes[i]};rt(makocode_ids[i],makocode_descriptions[i],r);r={};}
    r.size=8192; rt("0005_baseline_8k_auto", "8 KiB payload with automatically selected ECC", r); r={};
    r.size=8192; rt("0006_baseline_8k_auto_seeded", "8 KiB payload with automatically selected ECC", r); r={};
    r.size=131072; r.encode_args={"--page-width=700", "--page-height=700"}; r.require_multiple_pages=true; rt("0007_baseline_multi_page_auto", "128 KiB multi-page payload", r); r={};
    r.size=12000; r.encode_args={"--ecc=fill"}; rt("0008_ecc_fill_moderate", "ECC fill moderate payload", r); r={};
    r.size=1024; r.encode_args={"--page-width=1400", "--page-height=1800", "--ecc=fill"}; rt("0009_ecc_fill_sparse_large_page", "ECC fill sparse large page", r); r={};
    const char* palettes[] = {"White Black", "White Cyan Magenta", "White Cyan Magenta Yellow", "White Cyan Magenta Yellow Black"};
    const char* ids[] = {"0010_palette_white_black", "0011_palette_cmy", "0012_palette_cmyk", "0013_palette_cmykw"};
    const char* desc[] = {"Two-color palette stress", "Three-color palette stress", "Four-color palette stress", "Five-color palette stress"};
    for (size_t i=0;i<4;++i) { r.size=8192; r.encode_args={"--palette",palettes[i]}; r.decode_args={"--palette",palettes[i]}; rt(ids[i],desc[i],r); r={}; }
    r.size=8192; r.encode_args={"--page-width=240","--page-height=240","--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.decode_args={"--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; rt("0014_palette_hex_octet","Expanded hex octet palette",r); r={};
    r.size=8192; r.encode_args={"--palette","White Cyan Magenta Yellow Black"}; r.decode_args={"--palette","White Cyan Magenta Yellow Black"}; rt("0015_large_canvas_cmykw","Large canvas CMYKW palette",r); r={};
    r.size=256; r.encode_args={"--page-width=200","--page-height=64"}; rt("0016_fiducial_minimum_ecc","Minimum ECC with fiducial reservation",r); r={};
    r.size=65536; r.encode_args={"--footer", " !\"#$%&\\'()*+,-./0123456789:;<=>?@[\\]^_`{|}~ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy | random.bin | Page %page/%totalpages | 100\\% done", "--font-size", "2"}; rt("0017_footer_metadata","Footer metadata rendering stress",r); r={};
    r.size=102400; rt("0018_gray_100k_single","100 KiB grayscale single page baseline",r); r={};
    r.size=102400; r.encode_args={"--page-width=1100","--page-height=1100"}; r.transform_args={"--scale-x","2.5","--scale-y","2.5","--seed","0"}; rt("0019_gray_100k_scaled","100 KiB scaled 2.5x",r); r={};
    r.size=102400; r.encode_args={"--page-width=1100","--page-height=1100"}; r.transform_args={"--scale-x","2.6","--scale-y","2.4","--seed","0"}; rt("0020_gray_100k_stretch_h26_v24","Grayscale horizontal 2.6 / vertical 2.4",r); r={};
    r.size=102400; r.encode_args={"--page-width=1100","--page-height=1100"}; r.transform_args={"--scale-x","2.4","--scale-y","2.6","--seed","0"}; rt("0021_gray_100k_stretch_h24_v26","Grayscale horizontal 2.4 / vertical 2.6",r); r={};
    r.size=65536; r.encode_args={"--page-width=900","--page-height=900"}; r.transform_args={"--scale-x","3.03","--scale-y","3.03","--seed","0"}; rt("0022_gray_scale_303_blur","Grayscale upsample 3.03x",r); r={};
    r.size=65536; r.encode_args={"--page-width=900","--page-height=900"}; r.transform_args={"--scale-x","3","--scale-y","3","--rotate","1","--seed","1337"}; rt("0023_gray_rotate_seeded","Grayscale rotation with deterministic seed",r); r={};
    r.size=65536; r.encode_args={"--page-width=900","--page-height=900","--ecc=fill"}; r.transform_args={"--scale-x","3","--scale-y","3","--skew-x","5","--skew-y","-5","--seed","1337"}; rt("0024_gray_skew_xy_seeded","Grayscale XY skew",r); r={};
    r.size=65536; r.has_payload_seed_override=true; r.payload_seed_override=1254688731u; r.encode_args={"--page-width=900","--page-height=900"}; r.transform_args={"--scale-x","3","--scale-y","3","--skew-x","-5","--skew-y","5","--seed","7331"}; rt("0025_gray_skew_xy_reverse_seeded","Grayscale reverse XY skew",r); r={};
    r.size=65536; r.encode_args={"--page-width=900","--page-height=900","--ecc=fill"}; r.transform_args={"--scale-x","3","--scale-y","3","--rotate","1","--skew-x","5","--skew-y","-5","--seed","1337"}; rt("0026_gray_rotate_skew_seeded","Grayscale rotation and skew",r); r={};
    r.size=32768; r.encode_args={"--password=suite-password","--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"}; r.decode_args={"--password=suite-password"}; rt("0027_password_ecc","Password-protected payload with ECC",r); r={};
    r.size=262144; r.encode_args={"--page-width=520","--page-height=520"}; r.require_multiple_pages=true; rt("0028_ecc_multi_page_massive","262 KiB ECC multi-page stress",r); r={};
    r.size=65536; r.has_payload_seed_override=true; r.payload_seed_override=703681913u; r.encode_args={"--page-width=620","--page-height=620","--ecc=0.41"}; r.transform_args={"--scale-x","2.10","--scale-y","2.10","--rotate","1","--seed","7331"}; r.require_multiple_pages=true; rt("0029_multi_page_distorted","Multi-page distorted payload",r); r={};
    r.size=16384; r.encode_args={"--palette","White Cyan Magenta Yellow Black"}; r.decode_args={"--palette","White Cyan Magenta Yellow Black"}; rt("0030_palette_base5_custom","Custom palette/base-5 mode",r); r={};
    r.size=32768; r.encode_args={"--page-width=640","--page-height=640"}; r.transform_args={"--border-thickness","12","--border-density","0.15","--seed","8675309"}; rt("0031_border_dirt_dense","Dense border-noise stress",r); r={};
    cases.push_back({"0032_cli_output_dir","CLI respects explicit output directory",[](const fs::path&) { cli_output_dir_case(); }});
    const char* failure_kinds[]={"wrong_depth","invalid_magic","all_black","all_white","random_noise","footer_data_destroyed","footer_valid_data_too_corrupt"};
    const char* failure_ids[]={"0033_decode_wrong_depth","0034_decode_invalid_magic","0035_decode_all_black","0036_decode_all_white","0037_decode_random_noise","0038_decode_footer_data_destroyed","0039_decode_footer_valid_data_too_corrupt"};
    const char* failure_desc[]={"Decoder rejects malformed PPM (wrong maxval)","Decoder rejects malformed PPM (bad magic)","Decoder rejects all-black image","Decoder rejects all-white image","Decoder rejects random noise","Decoder rejects footer with destroyed data","Decoder rejects ECC-overwhelmed payload"};
    for(size_t i=0;i<7;++i){std::string id=failure_ids[i],kind=failure_kinds[i];cases.push_back({failure_ids[i],failure_desc[i],[id,kind](const fs::path&){decode_failure_case(id,kind);}});}
    cases.push_back({"0040_palette_auto_discovery","Decoder auto-discovers palette metadata",[](const fs::path&){palette_metadata_case(false);}});
    cases.push_back({"0041_palette_wrong_rejected","Decoder rejects forced wrong palette",[](const fs::path&){palette_metadata_case(true);}});
    cases.push_back({"0042_password_failures","Decoder enforces password requirement",[](const fs::path&){password_failure_case();}});
    cases.push_back({"0043_header_copy_corruption","Payload ECC repairs former in-band header damage",[](const fs::path&){header_copy_corruption_case();}});
    cases.push_back({"0057_archive_tree_roundtrip","Nested and empty archive directories round-trip",[](const fs::path&){archive_tree_case();}});
    cases.push_back({"0058_cli_help","CLI usage and command help smoke test",[](const fs::path&){cli_help_case();}});
    cases.push_back({"0059_minify_literals","Minify preserves source literals and directives",[](const fs::path&){minify_literals_case();}});
    cases.push_back({"0061_cli_decoder_paths","Decoder image paths, LZMA, and match finder behavior",[](const fs::path&){cli_decoder_paths_case();}});
    cases.push_back({"0113_footer_features","Footer wrapping, page selection, over-barcode ECC, and page bounds",[](const fs::path&){footer_features_case();}});
    cases.push_back({"0103_rs_erasures","RS erasure decoder enforces mixed correction boundaries",[](const fs::path&){rs_erasures_case();}});
    cases.push_back({"0104_canonical_tile","Canonical center tile reserves ECC and page capacity",[](const fs::path&){canonical_tile_case();}});
    cases.push_back({"0105_canonical_overlay_multipage","Canonical overlay selectors merge across multipage streams",[](const fs::path&){canonical_overlay_multipage_case();}});
    cases.push_back({"0106_palette_base5_capacity","Base-5 palette exceeds two bits per pixel",[](const fs::path&){palette_base5_capacity_case();}});
    cases.push_back({"0114_decode_blob_erasures","Solid-color blot positions become ECC erasures",[](const fs::path&){blob_erasure_case();}});
    cases.push_back({"0107_metadata_tile_format","Variable metadata fields and multi-block tile Reed-Solomon framing",[](const fs::path&){metadata_tile_format_case();}});
    cases.push_back({"0111_partial_ecc_recovery","Partial ECC archive preserves earlier recoverable files",[](const fs::path&){partial_ecc_recovery_case();}});
    cases.push_back({"0116_decode_recover_palette","Damaged palette rows recover from ordered color candidates",[](const fs::path&){recover_palette_case();}});
    cases.push_back({"0060_decode_stdin","Decoder reads one PPM page from stdin",[](const fs::path&){decode_stdin_case();}});
    cases.push_back({"0064_archive_symlink_roundtrip","Directory symlink target decodes as a regular file",[](const fs::path&){archive_symlink_case();}});
    cases.push_back({"0066_fiducials","Custom fiducial dimensions happy path",[](const fs::path&){fiducials_case();}});
    cases.push_back({"0067_default_encode_naming","Default encode output uses a timestamped filename",[](const fs::path&){default_naming_case();}});
    cases.push_back({"0070_ppm_transform_cli","ppm_transform help and fatal error paths",[](const fs::path&){ppm_transform_cli_case();}});
    cases.push_back({"0071_decode_ecc_retry","Canonical ECC failure retries without fiducial subgrid",[](const fs::path&){decode_ecc_retry_case();}});
    cases.push_back({"0069_decode_raw_fallback","Raw decode fallback is gated and writes recovered bytes",[](const fs::path&){raw_fallback_case();}});
    cases.push_back({"0062_prefix_ends_encoded","Prefix ending in _encoded keeps its filename and fiducials",[](const fs::path&){prefix_ends_encoded_case();}});
    cases.push_back({"0065_page_fit","Automatic page width and height fit independently",[](const fs::path&){page_fit_case();}});
    cases.push_back({"0123_metadata_pages","Metadata tiles appear only on selected pages",[](const fs::path&){metadata_pages_case();}});
    cases.push_back({"0086_decoded_file","Decoded file retains metadata and archive bytes",[](const fs::path&){decoded_file_case();}});
    cases.push_back({"0072_decode_scale_318","Decode a 160x160 page rendered at 3.18 pixels per module",[](const fs::path&){decode_scale_318_case();}});
    cases.push_back({"0079_decode_affine_cell_sampling","Affine retry sampling preserves translated cell coordinates",[](const fs::path&){affine_cell_sampling_case();}});
    cases.push_back({"0080_decode_small_scan","Decodes grayscale affine bias in a 193x188 scan",[](const fs::path&){small_scan_case();}});
    cases.push_back({"0081_decode_synthetic_scan_damage","Generated blurred 4.5x scan remains decodable",[](const fs::path&){synthetic_scan_case("0081_decode_synthetic_scan_damage",false,false);}});
    cases.push_back({"0082_decode_scan_margin_noise","Generated scan margin noise still decodes",[](const fs::path&){synthetic_scan_case("0082_decode_scan_margin_noise",true,false);}});
    cases.push_back({"0083_decode_scan_off_center_noise","Recoverable scan with off-center specks remains decodable",[](const fs::path&){synthetic_scan_case("0083_decode_scan_off_center_noise",false,true);}});
    cases.push_back({"0084_decode_short_length_header","Center metadata length survives damaged payload bits and aliasing",[](const fs::path&){short_length_header_case();}});
    cases.push_back({"0085_decode_lowres_rotated_scan","Generated rotated scan below two pixels per module round-trips",[](const fs::path&){lowres_rotated_scan_case();}});
    cases.push_back({"0118_decode_partial_tag_scan","ECC recovers a scan with half the marker erased",[](const fs::path&){erased_metadata_scan_case("0118_decode_partial_tag_scan",0);}});
    cases.push_back({"0119_decode_erased_tag_orientation_scan","ECC recovers erased markers across orientations",[](const fs::path&){erased_metadata_scan_case("0119_decode_erased_tag_orientation_scan",1);}});
    cases.push_back({"0120_decode_erased_length_row_scan","ECC recovers a scan with metadata size erased",[](const fs::path&){erased_metadata_scan_case("0120_decode_erased_length_row_scan",2);}});
    cases.push_back({"0087_decode_current_metadata","Damaged canonical metadata module is RS-repaired",[](const fs::path&){current_metadata_case();}});
    cases.push_back({"0088_decode_scan_registration","Offset noisy scan preserves decoded report",[](const fs::path&){scan_registration_noise_case();}});
    cases.push_back({"0115_decode_missing_pages","Metadata-indexed missing pages recover payload",[](const fs::path&){missing_pages_case();}});
    cases.push_back({"0117_decode_orientation_scan","Rotated and mirrored print scans round-trip",[](const fs::path&){orientation_scan_case();}});
    cases.push_back({"0121_decode_metadata_free","Center metadata damage recovers under standard palettes",[](const fs::path&){metadata_free_case(false);}});
    cases.push_back({"0122_decode_metadata_free_large","Large page recovers without center metadata",[](const fs::path&){metadata_free_case(true);}});
    cases.push_back({"0112_checkpoint_restore","Checkpoint deduplication, page round-trip, and partial recovery",[](const fs::path&){checkpoint_restore_case();}});
    cases.push_back({"0110_hidden_volumes_e2e","Hidden capacity, filler, ECC, overlay, and CLI validation matrix",[](const fs::path&){hidden_volumes_case();}});
    cases.push_back({"0089_decode_unit_scale_translation","Unit-scale padded scan translation",[](const fs::path&){unit_scale_scan_case("0089_decode_unit_scale_translation",false,false);}});
    cases.push_back({"0090_decode_unit_scale_edge_bleed","Unit-scale ink bleed keeps decoded geometry",[](const fs::path&){unit_scale_scan_case("0090_decode_unit_scale_edge_bleed",true,false);}});
    cases.push_back({"0091_decode_unit_scale_optical_blur","Unit-scale optical blur",[](const fs::path&){unit_scale_scan_case("0091_decode_unit_scale_optical_blur",false,true);}});
    cases.push_back({"0093_decode_lowres_optical_blur","Canonical 1.5x optical scan",[](const fs::path&){scan_registration_case("0093_decode_lowres_optical_blur",1.5,2.0,2.5,125,125,true,true);}});
    cases.push_back({"0095_decode_quarter_scale_registration","Canonical 1.25x optical registration",[](const fs::path&){scan_registration_case("0095_decode_quarter_scale_registration",1.25,2.25,3.25,106,107,true,true,false,true,1.25,2.25,3.25);}});
    cases.push_back({"0097_decode_midscale_low_contrast","Canonical 2.5x low contrast scan",[](const fs::path&){scan_registration_case("0097_decode_midscale_low_contrast",2.5,0.0,0.0,200,200,false,false,true);}});
    cases.push_back({"0099_decode_scale_relative_registration","Canonical 5.8x scale-relative registration",[](const fs::path&){scan_registration_case("0099_decode_scale_relative_registration",5.8,0.0,0.0,464,464,false,false,true);}});
    cases.push_back({"0101_decode_integer_scan_dimensions","Canonical padded integer scan dimensions",[](const fs::path&){scan_registration_case("0101_decode_integer_scan_dimensions",2.0,.25,.75,161,161,false,false,true);}});
    r.size=4096; r.encode_args={"--page-width=600","--page-height=600","--palette","White Black","--ecc=fill"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--ink-blot-radius","180","--ink-blot-color","black","--seed","0"}; rt("0044_palette_white_black_blot_black","Black blot recovery on two-color page",r); r={};
    r.size=4096; r.encode_args={"--page-width=600","--page-height=600","--palette","White Black","--ecc=fill"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--ink-blot-radius","180","--ink-blot-color","white","--seed","0"}; rt("0045_palette_white_black_blot_white","White blot recovery on two-color page",r); r={};
    r.size=8192; r.encode_args={"--page-width=300","--page-height=300","--palette","White Black"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--paper-color","FFF3D6","--paper-alpha","0.105","--paper-splotch-alpha","0.15","--paper-splotch-shade","0.06","--paper-splotch-px","96","--seed","424242"}; rt("0054_paper_offwhite_bw","Off-white paper tint BW",r); r={};
    r.size=8192; r.encode_args={"--page-width=320","--page-height=320","--palette","White Cyan Magenta Yellow"}; r.decode_args={"--palette","White Cyan Magenta Yellow"}; r.transform_args={"--paper-color","FFF3D6","--paper-alpha","0.105","--paper-splotch-alpha","0.15","--paper-splotch-shade","0.06","--paper-splotch-px","96","--seed","424242"}; rt("0055_paper_offwhite_cmyk","Off-white paper tint CMYK",r); r={};
    r.size=8192; r.encode_args={"--page-width=360","--page-height=360","--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.decode_args={"--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.transform_args={"--paper-color","FFF3D6","--paper-alpha","0.105","--paper-splotch-alpha","0.15","--paper-splotch-shade","0.06","--paper-splotch-px","96","--seed","424242"}; rt("0056_paper_offwhite_cmykwrbg","Off-white paper tint eight-color",r); r={};
    r.size=4096; r.encode_args={"--page-width=640","--page-height=400","--footer","Archive title","--font-size","5"}; rt("0108_footer_geometry","Tall footer geometry roundtrip",r); r={};
    r.size=4096; r.encode_args={"--page-width=620","--page-height=620","--ecc=fill"}; r.transform_args={"--scale-x","2.071","--scale-y","2.071","--seed","2071"}; rt("0073_decode_scale_2071","Fractional affine scan scale 2.071x",r); r={};
    r.size=4096; r.has_payload_seed_override=true; r.payload_seed_override=2118103236u; r.encode_args={"--page-width=620","--page-height=620"}; r.transform_args={"--scale-x","2.701","--scale-y","2.701","--seed","2701"}; rt("0074_decode_scale_2701","Fractional affine scan scale 2.701x",r); r={};
    r.size=4096; r.has_payload_seed_override=true; r.payload_seed_override=282487847u; r.encode_args={"--page-width=620","--page-height=620"}; r.transform_args={"--scale-x","4.701","--scale-y","4.701","--seed","4701"}; rt("0075_decode_scale_4701","Fractional affine scan scale 4.701x",r); r={};
    r.size=8; r.literal="makocode"; r.encode_args={"--page-width=160","--page-height=160","--palette","White Black"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--scale-x","2.701","--scale-y","2.701","--seed","2701"}; r.white_checker=true; rt("0076_tiny_palette_2_white_checker","Two-color tiny page with white checker overwrite",r); r={};
    r.size=8; r.literal="makocode"; r.encode_args={"--page-width=160","--page-height=160","--ecc=fill","--palette","White Cyan Magenta Yellow"}; r.decode_args={"--palette","White Cyan Magenta Yellow"}; r.transform_args={"--scale-x","2.701","--scale-y","2.701","--seed","2701"}; r.white_checker=true; rt("0077_tiny_palette_4_white_checker","Four-color tiny page with white checker overwrite",r); r={};
    r.size=8; r.literal="makocode"; r.encode_args={"--page-width=480","--page-height=480","--ecc=fill","--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.decode_args={"--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.transform_args={"--scale-x","2.701","--scale-y","2.701","--seed","2701"}; r.white_checker=true; rt("0078_tiny_palette_8_white_checker","Eight-color tiny page with diagonal white damage",r); r={};

    auto overlay=[&](OverlayConfig cfg){cases.push_back({cfg.id,"Overlay CLI merge and decode checks",[cfg](const fs::path&){overlay_case(cfg);}});};
    OverlayConfig oc;oc.id="0046_overlay_e2e";oc.fraction="0.992156862745098";overlay(oc);
    oc=OverlayConfig{};oc.id="0047_overlay_palette_cmyy";oc.fraction="0.992156862745098";oc.palette="White Cyan Magenta Yellow";oc.circle="255 255 0";oc.skip_grayscale=true;oc.allowed_colors="0 0 0;255 255 255;0 255 255;255 0 255;255 255 0";overlay(oc);
    oc=OverlayConfig{};oc.id="0048_overlay_bw_base_color_cmwy";oc.fraction="0.992156862745098";oc.palette="White Black";oc.circle_colors="0 255 255;255 0 255;255 255 0;255 255 255";oc.background="0 0 0";oc.encode_options={"--fiducial-spacing=24"};oc.skip_grayscale=true;oc.allowed_colors="0 0 0;255 255 255;0 255 255;255 0 255;255 255 0";overlay(oc);
    oc=OverlayConfig{};oc.id="0049_overlay_cmy_base_bw";oc.fraction="0.992156862745098";oc.palette="White Cyan Magenta Yellow";oc.skip_grayscale=true;oc.allowed_colors="0 0 0;255 255 255;0 255 255;255 0 255;255 255 0";overlay(oc);
    oc=OverlayConfig{};oc.id="0050_overlay_e2e_ignore_colors";oc.fraction="0.992156862745098";oc.ignore_colors="White";overlay(oc);
    for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"0051_overlay_bw_ecc_ratio_025","0.25"},{"0052_overlay_bw_ecc_ratio_050","0.5"}}){
        oc=OverlayConfig{};oc.id=item.first;oc.fraction="0.1";oc.ecc=item.second;oc.width=1840;oc.height=1840;oc.palette="White Black";oc.circle="0 0 0";oc.background="255 255 255";oc.encode_options={"--fiducial-spacing=60"};oc.skip_grayscale=true;oc.allowed_colors="0 0 0;255 255 255";overlay(oc);}
    oc=OverlayConfig{};oc.id="0053_overlay_ecc_ratio_gray_base_color";oc.fraction="0.1";oc.ecc="0.25";oc.circle="255 0 0";oc.background="0 255 0";oc.skip_grayscale=true;overlay(oc);
    oc=OverlayConfig{};oc.id="0063_overlay_fill";oc.fraction="0.992156862745098";oc.ecc="fill";overlay(oc);
    oc=OverlayConfig{};oc.id="0068_overlay_fill_mixed";oc.fraction="0.992156862745098";oc.palette="White Cyan Magenta Yellow";oc.circle="255 0 0";oc.background="0 255 0";oc.ignore_colors="#FF0000";oc.skip_grayscale=true;overlay(oc);
    oc=OverlayConfig{};oc.id="0109_overlay_footer_geometry";oc.size=4096;oc.width=640;oc.height=400;oc.footer="Archive title";oc.font_size="5";overlay(oc);
    const std::vector<std::pair<std::string,std::string>> no_metadata={{"0124_overlay_no_metadata_2","White Black"},{"0125_overlay_no_metadata_4","White Cyan Magenta Yellow"},{"0126_overlay_no_metadata_5","White Cyan Magenta Yellow Black"},{"0127_overlay_no_metadata_8","White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black"}};
    for(const auto& item:no_metadata){oc=OverlayConfig{};oc.id=item.first;oc.size=24;oc.fraction="0.992156862745098";oc.palette=item.second;oc.circle="128 64 0";oc.without_metadata=true;oc.without_fiducials=true;oc.encode_options={"--metadata-pages=all","--no-metadata","--fiducial-spacing=24","--no-fiducials"};overlay(oc);}
    return cases;
}

static unsigned parse_master_seed() {
    const char* env = std::getenv("MAKO_TEST_SEED");
    if (env && *env) {
        char* end = nullptr; unsigned long value = std::strtoul(env, &end, 10);
        require(end && *end == 0 && value <= 2147483647ul, "MAKO_TEST_SEED must be a non-negative decimal integer <= 2147483647");
        return static_cast<unsigned>(value);
    }
    int fd = open("/dev/urandom", O_RDONLY);
    unsigned value = 0;
    if (fd >= 0) { (void)read(fd, &value, sizeof(value)); close(fd); }
    return value & 0x7fffffffu;
}

static int jobs_from_makeflags() {
    const char* flags = std::getenv("MAKEFLAGS");
    if (!flags || !*flags) return 1;
    std::istringstream input(flags); std::string token;
    int jobs = 1;
    while (input >> token) {
        if (token == "-j" || token == "--jobs") jobs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        else if (token.rfind("-j", 0) == 0 && token.size() > 2) jobs = std::max(1, std::atoi(token.c_str() + 2));
        else if (token.rfind("--jobs=", 0) == 0) jobs = std::max(1, std::atoi(token.c_str() + 7));
    }
    return jobs;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--ppm-transform") == 0) return ppm_transform_cli_main(argc - 1, argv + 1);
    if (argc >= 2 && std::strcmp(argv[1], "--minify-allocation-failure") == 0) {
        minify_allocation_count=0;minify_allocation_failure_at=argc>2?std::strtoul(argv[2],nullptr,10):3ul;
        char source_path[]="makocode.cpp",width_option[]="--width",width_value[]="240",height_option[]="--height",height_value[]="80";
        char* args[]={source_path,width_option,width_value,height_option,height_value};
        return command_minify(5,args)==1?0:1;
    }
    test_executable = fs::canonical(fs::path(argv[0]));
    repo_root = test_executable.parent_path();
    while (!repo_root.empty() && !fs::exists(repo_root / "src" / "makocode.cpp")) {
        fs::path parent = repo_root.parent_path();
        if (parent == repo_root) break;
        repo_root = parent;
    }
    if (!fs::exists(repo_root / "src" / "makocode.cpp")) {
        std::cerr << "Unable to locate repository root from " << test_executable << '\n';
        return 2;
    }
    std::vector<TestCase> cases = build_cases();
    if (argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& test : cases) std::cout << test.id << '\n';
        return 0;
    }
    if (!(argc == 2 && std::strcmp(argv[1], "--all") == 0) &&
        !(argc == 3 && std::strcmp(argv[1], "--case") == 0)) {
        std::cerr << "Usage: makocode_test --list | --case ID | --all\n";
        return 2;
    }
    bool stdout_color = isatty(STDOUT_FILENO) != 0;
    bool stderr_color = isatty(STDERR_FILENO) != 0;
    const char* test_label = stdout_color ? "\033[1;34m[TEST]\033[0m " : "[TEST] ";
    const char* pass_label = stdout_color ? "\033[32m[PASS]\033[0m " : "[PASS] ";
    const char* fail_label = stderr_color ? "\033[31m[FAIL]\033[0m " : "[FAIL] ";
    output_root = repo_root / "test_output";
    makocode_bin = test_executable.parent_path() / "makocode";
    fs::create_directories(output_root / "test_logs");
    master_seed = parse_master_seed();
    {
        std::ofstream seed_file(output_root / "test_logs/test_seed.txt", std::ios::trunc);
        seed_file << master_seed << '\n';
    }
    std::cout << test_label << "Test seed: " << master_seed << "\n";
    if (argc == 3 && std::strcmp(argv[1], "--case") == 0) {
        auto found = std::find_if(cases.begin(), cases.end(), [&](const TestCase& test) { return test.id == std::string(argv[2]); });
        if (found == cases.end()) { std::cerr << "unknown test case: " << argv[2] << '\n'; return 2; }
        try { found->run(output_root); std::cout << pass_label << found->id << '\n'; return 0; }
        catch (const std::exception& error) { std::cerr << fail_label << found->id << ": " << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && std::strcmp(argv[1], "--all") == 0) {
        std::atomic<size_t> next{0}; std::atomic<int> failed{0};
        const int jobs = std::min<int>(jobs_from_makeflags(), static_cast<int>(cases.size()));
        std::vector<std::thread> workers;
        for (int worker = 0; worker < jobs; ++worker) workers.emplace_back([&] {
            for (;;) {
                size_t index = next.fetch_add(1);
                if (index >= cases.size()) break;
                const auto& test = cases[index];
                try {
                    test.run(output_root);
                    std::lock_guard<std::mutex> lock(output_mutex); std::cout << pass_label << test.id << "\n";
                } catch (const std::exception& error) {
                    failed.fetch_add(1);
                    std::lock_guard<std::mutex> lock(output_mutex); std::cerr << fail_label << test.id << ": " << error.what() << "\n";
                }
            }
        });
        for (auto& worker : workers) worker.join();
        std::cout << test_label << "Test cases: " << cases.size() << ", failures: " << failed.load() << '\n';
        return failed.load() ? 1 : 0;
    }
    return 2;
}
