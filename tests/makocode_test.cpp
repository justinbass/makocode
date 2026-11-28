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

#if defined(__GLIBC__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
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
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <iterator>
#include <tuple>
#include <vector>
#include <poll.h>

static unsigned long minify_allocation_count=0;
static unsigned long minify_allocation_failure_at=0;
static unsigned long minify_allocation_failure_size=0;
static unsigned long minify_4096_allocation_count=0;
static bool record_platform_allocation_sizes=false;
static std::vector<size_t> platform_allocation_sizes;
static size_t platform_allocation_size_limit=0u;
static unsigned long platform_allocation_limit_rejection_count=0ul;
static unsigned long watch_platform_allocation_at=0;
static void* watch_platform_free_pointer=nullptr;
static unsigned long watch_platform_free_count=0;
static thread_local size_t test_allocation_fill_size=0u;
static thread_local unsigned char test_allocation_fill_value=0u;
extern "C" void* makocode_test_malloc(size_t size);
extern "C" void makocode_test_free(void* ptr);

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char** environ;

#define main makocode_embedded_cli_main
#include "../src/makocode_common.cpp"
#include "../src/makocode_encode.cpp"
#include "../src/makocode_decode.cpp"
#include "../src/makocode_minify.cpp"
#include "../src/makocode_checkpoint.cpp"
#include "../src/makocode.cpp"
#undef main

extern "C" void* makocode_test_malloc(size_t size) {
    if(size==4096ul)++minify_4096_allocation_count;
    unsigned long allocation_index=0;
    if(minify_allocation_failure_at||record_platform_allocation_sizes){
        allocation_index=++minify_allocation_count;
        if(record_platform_allocation_sizes)platform_allocation_sizes.push_back(size);
    }
    if(minify_allocation_failure_at&&allocation_index==minify_allocation_failure_at){minify_allocation_failure_size=size;return nullptr;}
    if(platform_allocation_size_limit&&size>platform_allocation_size_limit){++platform_allocation_limit_rejection_count;return nullptr;}
    void* ptr=std::malloc(static_cast<size_t>(size));
    if(ptr&&test_allocation_fill_size==size&&test_allocation_fill_value){std::memset(ptr,test_allocation_fill_value,size);}
    if(ptr&&watch_platform_allocation_at&&allocation_index==watch_platform_allocation_at){
        watch_platform_free_pointer=ptr;
    }
    return ptr;
}

extern "C" void makocode_test_free(void* ptr) {
    if(ptr&&ptr==watch_platform_free_pointer){
        ++watch_platform_free_count;
        watch_platform_free_pointer=nullptr;
    }
    std::free(ptr);
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
    int center_clear_radius = 0;

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
        else if (strcmp(arg, "--center-clear-radius") == 0) center_clear_radius = parse_i32(require_value("--center-clear-radius"), "center clear radius");
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
    long long center_clear_radius_sq = (long long)center_clear_radius * center_clear_radius;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dx = x - cx;
            int dy = y - cy;
            long long distance_sq = (long long)dx * dx + (long long)dy * dy;
            int inside = distance_sq <= radius_sq && distance_sq > center_clear_radius_sq;
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
    explicit Failure(const char* message) : std::runtime_error(message) {}
    explicit Failure(const std::string& message) : std::runtime_error(message) {}
    ~Failure() override {}
};

struct ProcessResult {
    int status = -1;
    bool timed_out = false;
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

static fs::path& repo_root() {
    static fs::path value;
    return value;
}

static fs::path& output_root() {
    static fs::path value;
    return value;
}

static fs::path& makocode_bin() {
    static fs::path value;
    return value;
}

static fs::path& test_executable() {
    static fs::path value;
    return value;
}
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
                                 const std::map<std::string, std::string>& environment = {},
                                 unsigned long timeout_milliseconds = 0ul) {
    require(!args.empty(), "empty process argv");
    fs::create_directories(stdout_path.parent_path());
    fs::create_directories(stderr_path.parent_path());
    std::vector<char*> argv;
    argv.reserve(args.size() + 1u);
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> environment_storage;
    std::set<std::string> emitted_overrides;
    for (char** current = environ; current && *current; ++current) {
        std::string inherited(*current);
        size_t separator = inherited.find('=');
        std::string key = inherited.substr(0u, separator);
        auto override_value = environment.find(key);
        if (override_value != environment.end()) {
            environment_storage.push_back(key + "=" + override_value->second);
            emitted_overrides.insert(key);
        } else {
            environment_storage.push_back(inherited);
        }
    }
    for (const auto& entry : environment) {
        if (!emitted_overrides.count(entry.first)) {
            environment_storage.push_back(entry.first + "=" + entry.second);
        }
    }
    std::vector<char*> child_environment;
    child_environment.reserve(environment_storage.size() + 1u);
    for (std::string& entry : environment_storage) {
        child_environment.push_back(const_cast<char*>(entry.c_str()));
    }
    child_environment.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    int spawn_error = posix_spawn_file_actions_init(&actions);
    if (spawn_error != 0) {
        throw Failure("spawn file actions init failed: " +
                      std::string(std::strerror(spawn_error)));
    }
#if defined(__APPLE__)
#if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000
    spawn_error = posix_spawn_file_actions_addchdir(&actions, cwd.c_str());
#else
    spawn_error = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#endif
#elif defined(__GLIBC__)
    spawn_error = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
#else
    spawn_error = ENOTSUP;
#endif
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, stdout_path.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addopen(
            &actions, STDERR_FILENO, stderr_path.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC, 0666);
    }
    const fs::path child_stdin = stdin_path.empty() ? fs::path("/dev/null") : stdin_path;
    if (spawn_error == 0) {
        spawn_error = posix_spawn_file_actions_addopen(
            &actions, STDIN_FILENO, child_stdin.c_str(), O_RDONLY, 0);
    }
    pid_t child = -1;
    if (spawn_error == 0) {
        spawn_error = posix_spawnp(&child, argv[0], &actions, nullptr,
                                   argv.data(), child_environment.data());
    }
    int destroy_error = posix_spawn_file_actions_destroy(&actions);
    if (spawn_error == 0 && destroy_error != 0) spawn_error = destroy_error;
#if !defined(__APPLE__) && !defined(__GLIBC__)
    if (spawn_error == ENOTSUP) {
        const char* cwd_path = cwd.c_str();
        const char* stdout_file = stdout_path.c_str();
        const char* stderr_file = stderr_path.c_str();
        const char* stdin_file = child_stdin.c_str();
        child = fork();
        if (child < 0) {
            throw Failure("fork failed: " + std::string(std::strerror(errno)));
        }
        if (child == 0) {
            if (chdir(cwd_path) != 0) _exit(126);
            int out_fd = open(stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            int err_fd = open(stderr_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
            int in_fd = open(stdin_file, O_RDONLY);
            if (out_fd < 0 || err_fd < 0 || in_fd < 0 ||
                dup2(out_fd, STDOUT_FILENO) < 0 ||
                dup2(err_fd, STDERR_FILENO) < 0 ||
                dup2(in_fd, STDIN_FILENO) < 0) _exit(126);
            close(out_fd);
            close(err_fd);
            close(in_fd);
            execve(argv[0], argv.data(), child_environment.data());
            _exit(errno == ENOENT ? 127 : 126);
        }
        spawn_error = 0;
    }
#endif
    if (spawn_error != 0) {
        throw Failure("posix_spawn failed: " + std::string(std::strerror(spawn_error)));
    }
    int status = 0;
    bool timed_out = false;
    if (timeout_milliseconds == 0ul) {
        while (waitpid(child, &status, 0) < 0) {
            if (errno == EINTR) continue;
            throw Failure("waitpid failed: " + std::string(std::strerror(errno)));
        }
    } else {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeout_milliseconds);
        for (;;) {
            pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) break;
            if (waited < 0) {
                if (errno == EINTR) continue;
                throw Failure("waitpid failed: " + std::string(std::strerror(errno)));
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                kill(child, SIGKILL);
                while (waitpid(child, &status, 0) < 0) {
                    if (errno == EINTR) continue;
                    throw Failure("waitpid failed: " + std::string(std::strerror(errno)));
                }
                timed_out = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    ProcessResult result;
    result.status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    result.timed_out = timed_out;
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

static unsigned case_seed(const std::string& key, unsigned seed = master_seed) {
    return crc_cksum(std::to_string(seed) + ":" + stable_seed_key(key)) & 0x7fffffffu;
}

static fs::path make_work_dir(const std::string& label) {
    fs::path pattern = output_root() / (label + "_work.XXXXXX");
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
                             const std::map<std::string, std::string>& env = {},
                             unsigned long timeout_milliseconds = 0ul) {
    std::vector<std::string> command{makocode_bin().string()};
    command.insert(command.end(), args.begin(), args.end());
    ProcessResult result = run_process(command, work, stdin_file,
        work / (tag + ".stdout"), work / (tag + ".stderr"), env,
        timeout_milliseconds);
    return result;
}

static ProcessResult run_cli_with_password_prompts(
        const fs::path& work,
        const std::vector<std::string>& args,
        const std::vector<std::pair<std::string, std::string>>& prompts,
        bool cancel_at_first_prompt = false,
        bool* terminal_restored = nullptr) {
    if (terminal_restored) *terminal_restored = false;
    int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        if (master >= 0) ::close(master);
        throw Failure("unable to initialize password prompt pseudo-terminal");
    }
    char* slave_name = ::ptsname(master);
    if (!slave_name) {
        ::close(master);
        throw Failure("unable to find password prompt terminal");
    }
    std::vector<std::string> command{makocode_bin().string()};
    command.insert(command.end(), args.begin(), args.end());
    std::vector<char*> argv;
    argv.reserve(command.size() + 1u);
    for (std::string& item : command) argv.push_back(item.data());
    argv.push_back(nullptr);

    pid_t child = ::fork();
    if (child < 0) {
        ::close(master);
        throw Failure("unable to fork password prompt test process");
    }
    if (child == 0) {
        if (::setsid() < 0) _exit(126);
        int slave = ::open(slave_name, O_RDWR);
        if (slave < 0 || ::ioctl(slave, TIOCSCTTY, 0) != 0 ||
            ::dup2(slave, STDIN_FILENO) < 0 ||
            ::dup2(slave, STDOUT_FILENO) < 0 ||
            ::dup2(slave, STDERR_FILENO) < 0 || ::chdir(work.c_str()) != 0) {
            _exit(126);
        }
        if (slave > STDERR_FILENO) ::close(slave);
        ::close(master);
        ::execv(makocode_bin().c_str(), argv.data());
        _exit(127);
    }

    ProcessResult result;
    auto read_more = [&]() -> bool {
        struct pollfd descriptor;
        descriptor.fd = master;
        descriptor.events = POLLIN | POLLHUP;
        descriptor.revents = 0;
        int ready;
        do {
            ready = ::poll(&descriptor, 1u, 30000);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0) return false;
        char buffer[4096];
        ssize_t count = ::read(master, buffer, sizeof(buffer));
        if (count <= 0) return false;
        result.out.append(buffer, (size_t)count);
        return true;
    };
    bool interaction_ok = true;
    size_t prompt_search_offset = 0u;
    for (const auto& item : prompts) {
        while (result.out.find(item.first, prompt_search_offset) == std::string::npos) {
            if (!read_more()) {
                interaction_ok = false;
                break;
            }
        }
        if (!interaction_ok) break;
        prompt_search_offset = result.out.size();
        if (cancel_at_first_prompt) {
            if (::kill(child, SIGINT) != 0) interaction_ok = false;
            cancel_at_first_prompt = false;
            break;
        }
        std::string response = item.second + "\n";
        size_t written = 0u;
        while (written < response.size()) {
            size_t remaining = response.size() - written;
            size_t chunk = remaining < 128u ? remaining : 128u;
            ssize_t count = ::write(master, response.data() + written, chunk);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) {
                interaction_ok = false;
                break;
            }
            written += (size_t)count;
        }
        if (!interaction_ok) break;
    }
    while (read_more()) {}
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        ::close(master);
        throw Failure("unable to wait for password prompt test process");
    }
    if (terminal_restored) {
        struct termios current_terminal;
        if (::tcgetattr(master, &current_terminal) == 0) {
            const tcflag_t prompt_flags = (tcflag_t)(ECHO | ICANON);
            *terminal_restored = (current_terminal.c_lflag & prompt_flags) == prompt_flags;
        }
    }
    ::close(master);
    result.status = WIFEXITED(status) ? WEXITSTATUS(status)
                                      : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    if (!interaction_ok) {
        throw Failure("password prompt did not appear or accept input: " + result.out);
    }
    return result;
}

static ProcessResult run_transform(const fs::path& work,
                                   const std::vector<std::string>& args,
                                   const std::string& tag,
                                   const std::map<std::string, std::string>& env = {}) {
    std::vector<std::string> command{test_executable().string(), "--ppm-transform"};
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
            kind == "footer_data_destroyed" ? std::vector<std::string>{"--page-width=500", "--page-height=500"} : std::vector<std::string>{"--page-width=1000", "--page-height=1000"});
        ProcessResult encoded = run_cli(work, encode, "encode", {}, {{"MAKOCODE_TEST_SEED", std::to_string(crypto)}});
        expect_success(encoded, id + " encode");
        auto pages = ppm_paths(work, prefix);
        require(pages.size() == 1, id + ": expected one PPM page");
        fs::path damaged = work / "damaged.ppm";
        std::vector<std::string> corrupt{"corrupt-footer-data-destroyed", "--input", pages.front(), "--output", damaged.string(), "--footer-height-px", "8"};
        if (kind == "footer_valid_data_too_corrupt") corrupt[0] = "corrupt-footer-valid-data-too-corrupt";
        ProcessResult changed = run_transform(work, corrupt, "corrupt"); expect_success(changed, "footer corruption");
        fs::path decoded = work / "decoded";
        ProcessResult result = run_cli(work, {"decode", damaged.string(), "--output-dir=" + decoded.string()}, "decode");
        expect_failure(result, id + " decode");
        if (kind == "footer_data_destroyed") require(result.out.find("decode: ECC could not repair the payload") != std::string::npos || result.err.find("decode: ECC could not repair the payload") != std::string::npos,
            id + ": expected ECC failure was not reported");

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
    auto encoded = run_cli(work, encode_file_args(payload, work, "cli_payload", {}), "encode");
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, "cli_payload");
    require(!pages.empty(), id + ": encode output missing");
    fs::path decoded = work / "decoded";
    auto result = run_cli(work, decode_file_args(pages, decoded, {}), "decode");
    expect_success(result, id + " decode");

    compare_files(payload, decoded / "cli_payload.bin", id);
}

static void palette_metadata_case(bool wrong_palette) {
    std::string id = wrong_palette ? "0041_palette_wrong_rejected" : "0040_palette_auto_discovery";
    fs::path work = make_work_dir(id);
    fs::path payload = work / "payload.bin";
    generate_bytes(work, payload, 16384, case_seed("payload/" + id + (wrong_palette ? "/wrong" : "/auto")), "payload");
    std::string prefix = id + "_page";
    auto encoded = run_cli(work, encode_file_args(payload, work, prefix, {"--palette", "White Cyan Magenta Yellow"}), "encode",
                           {}, {{"MAKOCODE_TEST_SEED", std::to_string(case_seed("crypto/" + id + (wrong_palette ? "/wrong" : "/auto")))}});
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, prefix); require(!pages.empty(), id + ": no encoded PPM");
    fs::path decoded = work / "decoded";
    std::vector<std::string> args{"decode", pages.front(), "--output-dir=" + decoded.string()};
    if (wrong_palette) args.insert(args.end(), {"--palette", "White Black"});
    auto result = run_cli(work, args, "decode");
    if (wrong_palette) expect_failure(result, id + " decode"); else expect_success(result, id + " decode");
    if (!wrong_palette) {

        compare_files(payload, decoded / "payload.bin", id);
    }
}

static void password_failure_case() {
    const std::string id = "0042_password_failures";
    fs::path work = make_work_dir(id), payload = work / "payload.bin";
    generate_bytes(work, payload, 32768, case_seed("payload/" + id), "payload");
    const std::string password = "suite-password", prefix = id + "_page";
    auto encoded = run_cli(work, encode_file_args(payload, work, prefix, {"--page-width=1000", "--page-height=1000", "--password-cli=" + password,
        "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"}), "encode", {},
        {{"MAKOCODE_TEST_SEED", std::to_string(case_seed("crypto/" + id))}});
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, prefix); require(pages.size() == 1, id + ": expected one encrypted page");
    for (const auto& credentials : {std::vector<std::string>{}, std::vector<std::string>{"--password-cli=bad-password"}}) {
        fs::path decoded = work / (credentials.empty() ? "no_password" : "wrong_password");
        std::vector<std::string> args{"decode", pages.front(), "--output-dir=" + decoded.string()};
        args.insert(args.end(), credentials.begin(), credentials.end());
        auto result = run_cli(work, args, "decode_" + decoded.filename().string());
        expect_failure(result, id + " decode");

    }
}

static void chacha20_poly1305_vector_case() {
    auto from_hex = [](const std::string& value) {
        auto digit = [](char ch) -> unsigned {
            if (ch >= '0' && ch <= '9') return static_cast<unsigned>(ch - '0');
            if (ch >= 'a' && ch <= 'f') return static_cast<unsigned>(ch - 'a' + 10);
            if (ch >= 'A' && ch <= 'F') return static_cast<unsigned>(ch - 'A' + 10);
            throw Failure("invalid hexadecimal test vector");
        };
        require((value.size() & 1u) == 0u, "odd hexadecimal test vector");
        std::string bytes(value.size() / 2u, '\0');
        for (size_t i = 0u; i < bytes.size(); ++i) {
            bytes[i] = static_cast<char>((digit(value[i * 2u]) << 4u) |
                                         digit(value[i * 2u + 1u]));
        }
        return bytes;
    };
    auto to_hex = [](const u8* data, size_t length) {
        static const char digits[] = "0123456789abcdef";
        std::string value;
        value.reserve(length * 2u);
        for (size_t i = 0u; i < length; ++i) {
            value.push_back(digits[data[i] >> 4u]);
            value.push_back(digits[data[i] & 15u]);
        }
        return value;
    };
    auto check_vector = [&](const std::string& key_hex,
                            const std::string& nonce_hex,
                            const std::string& aad_hex,
                            const std::string& plaintext_hex,
                            const std::string& expected_hex,
                            const std::string& label) {
        std::string key = from_hex(key_hex);
        std::string nonce = from_hex(nonce_hex);
        std::string aad = from_hex(aad_hex);
        std::string plaintext = from_hex(plaintext_hex);
        std::string expected = from_hex(expected_hex);
        require(key.size() == 32u && nonce.size() == 12u && expected.size() >= 16u,
                label + ": invalid vector dimensions");
        makocode::ByteBuffer ciphertext;
        u8 tag[16];
        require(MetadataTile::chacha20_poly1305_encrypt(
                    (const u8*)key.data(), (const u8*)nonce.data(),
                    (const u8*)plaintext.data(), plaintext.size(),
                    (const u8*)aad.data(), aad.size(), ciphertext, tag),
                label + ": encryption failed");
        bool matches = ciphertext.size + sizeof(tag) == expected.size() &&
                       (!ciphertext.size ||
                        memcmp(ciphertext.data, expected.data(), ciphertext.size) == 0) &&
                       memcmp(tag, expected.data() + ciphertext.size, sizeof(tag)) == 0;
        require(matches,
                label + ": RFC 8439 ciphertext or tag mismatch, got " +
                    to_hex(ciphertext.data, ciphertext.size) + to_hex(tag, sizeof(tag)));
        makocode::ByteBuffer decoded;
        bool auth_failed = false;
        require(MetadataTile::chacha20_poly1305_decrypt(
                    (const u8*)key.data(), (const u8*)nonce.data(),
                    ciphertext.data, ciphertext.size, (const u8*)aad.data(),
                    aad.size(), decoded, tag, &auth_failed) &&
                decoded.size == plaintext.size() &&
                (!plaintext.size() ||
                 memcmp(decoded.data, plaintext.data(), plaintext.size()) == 0),
                label + ": decryption failed");
        tag[0] ^= 1u;
        require(!MetadataTile::chacha20_poly1305_decrypt(
                    (const u8*)key.data(), (const u8*)nonce.data(),
                    ciphertext.data, ciphertext.size, (const u8*)aad.data(),
                    aad.size(), decoded, tag, &auth_failed) && auth_failed,
                label + ": altered tag was accepted");
    };

    const std::string plaintext =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one "
        "tip for the future, sunscreen would be it.";
    std::string plaintext_hex;
    static const char hex_digits[] = "0123456789abcdef";
    for (unsigned char byte : plaintext) {
        plaintext_hex.push_back(hex_digits[byte >> 4u]);
        plaintext_hex.push_back(hex_digits[byte & 15u]);
    }
    check_vector(
        "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f",
        "070000004041424344454647", "50515253c0c1c2c3c4c5c6c7",
        plaintext_hex,
        "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691",
        "RFC 8439 partial blocks");
    check_vector("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                 "000102030405060708090a0b", "000102030405060708090a0b0c0d0e0f",
                 "101112131415161718191a1b1c1d1e1f",
                 "99ea1a133d02b357af9a25e88400107c5ff71fb29cccbf62f39f168aa2f6e9cb",
                 "independent aligned vector");
    check_vector("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                 "000102030405060708090a0b", "000102030405060708090a0b0c0d0e0f",
                 "", "36744a8792bab9b41cce2ab3a75d45b7",
                 "independent empty-message vector");
}

static void secure_random_padding_case() {
    makocode::ByteBuffer first;
    makocode::ByteBuffer second;
    require(makocode::stealth_fill_random(first, 4096u) &&
            makocode::stealth_fill_random(second, 4096u),
            "OS-backed padding randomness failed");
    require(first.size == second.size && first.size == 4096u &&
            memcmp(first.data, second.data, first.size) != 0,
            "padding repeated across independent random fills");
}

static void interactive_password_case() {
    const std::string id = "0138_interactive_password";
    fs::path work = make_work_dir(id);
    fs::path visible = work / "visible.bin";
    fs::path hidden = work / "hidden.bin";
    fs::path pages_dir = work / "pages";
    fs::path visible_out = work / "visible_out";
    fs::path hidden_out = work / "hidden_out";
    write_file(visible, "visible test content");
    write_file(hidden, "hidden test content");
    fs::create_directories(pages_dir);
    const std::string visible_password = "visible-prompt-secret";
    const std::string hidden_password = "hidden-prompt-secret";
    bool terminal_restored = false;
    ProcessResult cancelled = run_cli_with_password_prompts(
        work,
        {"encode", "--input=visible.bin", "--prefix=cancelled", "--password",
         "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"},
        {{"Password: ", ""}}, true, &terminal_restored);
    require(cancelled.status != 0 &&
            cancelled.out.find("password input failed") != std::string::npos,
            id + ": cancelling password prompt did not abort password entry");
    require(terminal_restored,
            id + ": password prompt cancellation did not restore terminal settings");

    bool eof_terminal_restored = false;
    ProcessResult eof_password = run_cli_with_password_prompts(
        work,
        {"encode", "--input=visible.bin", "--prefix=eof", "--password",
         "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"},
        {{"Password: ", std::string(1u, '\x04')}}, false, &eof_terminal_restored);
    expect_failure(eof_password, id + " password EOF");
    require(eof_password.out.find("password input failed") != std::string::npos &&
            eof_terminal_restored,
            id + ": terminal EOF did not abort password entry and restore terminal settings");

    ProcessResult overlong = run_cli_with_password_prompts(
        work,
        {"encode", "--input=visible.bin", "--prefix=overlong", "--password",
         "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"},
        {{"Password: ", std::string(4096u, 'x')}});
    expect_failure(overlong, id + " overlong password");
    require(overlong.out.find("password input failed") != std::string::npos &&
            overlong.out.find(std::string(16u, 'x')) == std::string::npos,
            id + ": overlong input was accepted or echoed");

    ProcessResult encoded = run_cli_with_password_prompts(
        work,
        {"encode", "--input=visible.bin", "--hidden-input=hidden.bin",
         "--password", "--hidden-password", "--hidden=64",
         "--page-width=1000", "--page-height=1000", "--output-dir=pages",
         "--prefix=prompted",
         "--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"},
        {{"Password: ", visible_password},
         {"Confirm password: ", visible_password},
         {"Hidden password: ", hidden_password},
         {"Confirm password: ", hidden_password}});
    if (encoded.status != 0) {
        throw Failure(id + " encode failed: " + encoded.out);
    }
    require(encoded.out.find(visible_password) == std::string::npos &&
            encoded.out.find(hidden_password) == std::string::npos,
            id + ": terminal echoed a password");

    auto pages = ppm_paths(pages_dir, "prompted");
    require(!pages.empty(), id + ": no encoded pages");
    ProcessResult visible_decoded = run_cli_with_password_prompts(
        work, {"decode", "--output-dir=" + visible_out.string(), "--password",
               pages.front()},
        {{"Password: ", visible_password.substr(0u, visible_password.size() - 1u) +
                              "x\x7f" + visible_password.substr(visible_password.size() - 1u)}});
    expect_success(visible_decoded, id + " visible decode");
    require(visible_decoded.out.find(visible_password) == std::string::npos,
            id + ": terminal echoed the decode password");
    compare_files(visible, visible_out / "visible.bin", id + " visible");

    fs::path hidden_prompt_out = work / "hidden_prompt_out";
    std::string kill_then_visible = "discard";
    kill_then_visible.push_back('\x15');
    kill_then_visible += visible_password;
    ProcessResult hidden_prompt_decoded = run_cli_with_password_prompts(
        work, {"decode", "--output-dir=" + hidden_prompt_out.string(), "--password",
               pages.front()}, {{"Password: ", kill_then_visible}});
    expect_success(hidden_prompt_decoded, id + " password line kill");
    compare_files(visible, hidden_prompt_out / "visible.bin", id + " password line kill");

    ProcessResult hidden_decoded = run_cli(
        work, {"decode", "--output-dir=" + hidden_out.string(),
               "--password-cli", hidden_password, pages.front()},
        "hidden_decode");
    expect_success(hidden_decoded, id + " hidden decode");
    compare_files(hidden, hidden_out / "hidden.bin", id + " hidden");
}

static void header_copy_corruption_case() {
    const std::string id = "0043_header_copy_corruption";
    fs::path work = make_work_dir(id), payload = work / "random.bin";
    generate_bytes(work, payload, 4096, case_seed("payload/" + id), "payload");
    const std::string prefix = id + "_page";
    auto encoded = run_cli(work, encode_file_args(payload, work, prefix, {"--ecc=fill", "--page-width=600", "--page-height=600"}), "encode",
        {}, {{"MAKOCODE_TEST_SEED", std::to_string(case_seed("crypto/" + id))}});
    expect_success(encoded, id + " encode");
    auto pages = ppm_paths(work, prefix); require(!pages.empty(), id + ": no page output");
    for (int count=1; count<=3; ++count) {
        fs::path damaged=work/("corrupt_"+std::to_string(count)+".ppm"), decoded=work/("decoded_"+std::to_string(count));
        auto changed=run_transform(work,{"transform","--input",pages.front(),"--output",damaged.string(),"--corrupt-former-header-slots",std::to_string(count)},"transform");
        expect_success(changed,id+" transform");
        auto result=run_cli(work,{"decode",damaged.string(),"--output-dir="+decoded.string()},"decode_"+std::to_string(count));
        expect_success(result,id+" decode");

        compare_files(payload,decoded/"random.bin",id);
    }
}

static void decode_stdin_case() {
    const std::string id = "0060_decode_stdin";
    fs::path work=make_work_dir(id),payload=work/"payload.bin";
    std::string contents("\0stdin coverage payload\xff\n",26); write_file(payload,contents);
    const std::string prefix=id+"_stdin";
    auto encoded=run_cli(work,encode_file_args(payload,work,prefix,{}),"encode"); expect_success(encoded,id+" encode");
    auto pages=ppm_paths(work,prefix);require(pages.size()==1,id+": expected one page");
    fs::path decoded=work/"decoded";
    auto result=run_cli(work,{"decode","--output-dir="+decoded.string()},"decode",pages.front());expect_success(result,id+" decode stdin");

    compare_files(payload,decoded/"payload.bin",id);
}

static void archive_tree_case() {
    const std::string id="0057_archive_tree_roundtrip";
    fs::path work=make_work_dir(id),source=work/"source",pages_dir=work/"pages",decoded=work/"decoded";
    fs::create_directories(source/"tree/empty");fs::create_directories(source/"tree/nested/deep_empty");
    fs::create_directories(source/"empty_only/nested/leaf_empty");fs::create_directories(pages_dir);
    write_file(source/"tree/root.txt","x");write_file(source/"tree/nested/leaf.txt","y");write_file(source/"tree/zero.bin","");
    auto encoded=run_cli(source,{"encode","--input=tree","--input=empty_only","--prefix=archive_tree","--output-dir="+pages_dir.string()},"encode");
    expect_success(encoded,id+" encode");
    auto pages=ppm_paths(pages_dir,"");require(!pages.empty(),id+": no pages");
    auto result=run_cli(work,decode_file_args(pages,decoded,{}),"decode");expect_success(result,id+" decode");

    for(const auto& name:{std::string("tree"),std::string("empty_only")}){
        fs::path a=source/name,b=decoded/name;require(fs::is_directory(b),id+": missing directory "+name);
        std::vector<std::string> aa,bb;
        for(const auto& e:fs::recursive_directory_iterator(a))aa.push_back(fs::relative(e.path(),a).string()+(e.is_directory()?"/":""));
        for(const auto& e:fs::recursive_directory_iterator(b))bb.push_back(fs::relative(e.path(),b).string()+(e.is_directory()?"/":""));
        std::sort(aa.begin(),aa.end());std::sort(bb.begin(),bb.end());require(aa==bb,id+": directory tree differs for "+name);
    }
    for(const auto& path:{"root.txt","nested/leaf.txt","zero.bin"})compare_files(source/"tree"/path,decoded/"tree"/path,id);
    auto output_permissions=fs::status(decoded/"tree/root.txt").permissions();
    require((output_permissions&(fs::perms::group_all|fs::perms::others_all))==fs::perms::none,
            id+": restored files must not be readable by other users");
    auto directory_permissions=fs::status(decoded/"tree").permissions();
    require((directory_permissions&(fs::perms::group_all|fs::perms::others_all))==fs::perms::none,
            id+": restored directories must not be accessible by other users");
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
    auto result=run_cli(work,{"encode","--input=payload.bin","--palette=Cyan Black","--output-dir="+pages_dir.string()},"encode");expect_success(result,id+" encode");
    auto pages=matching_files(pages_dir,"", ".ppm");
    require(pages.size()==1,id+": expected one timestamped PPM");
    std::regex pattern("^[0-9]{8}T[0-9]{6}Z\\.ppm$");
    auto name=pages.front().filename().string();require(std::regex_match(name,pattern),id+": unexpected default PPM filename "+name);
}

static void fiducials_case() {
    const std::string id="0066_fiducials",prefix=id+"_fiducials";
    fs::path work=make_work_dir(id),pages_dir=work/"pages",decoded=work/"decoded",payload=work/"payload.bin";fs::create_directories(pages_dir);
    write_file(payload,std::string("\0fiducials custom grid payload\xff\n",31));
    auto encoded=run_cli(work,{"encode","--input=payload.bin","--page-width=240","--page-height=240","--fiducial-spacing=30","--prefix="+prefix,"--output-dir="+pages_dir.string()},"encode");expect_success(encoded,id+" encode");
    auto pages=ppm_paths(pages_dir,prefix);require(pages.size()==1,id+": expected one output page");
    auto image=read_ppm(pages.front());size_t white=0;
    for(unsigned y=29;y<31;++y)for(unsigned x=29;x<31;++x){size_t i=(static_cast<size_t>(y)*image.width+x)*3;white+=static_cast<unsigned char>(image.pixels[i])==255&&static_cast<unsigned char>(image.pixels[i+1])==255&&static_cast<unsigned char>(image.pixels[i+2])==255;}
    require(image.width>=31&&image.height>=31&&white==4,id+": first 2x2 grayscale marker is not white");
    auto result=run_cli(work,decode_file_args(pages,decoded),"decode");expect_success(result,id+" decode");compare_files(payload,decoded/"payload.bin",id);
}

static void ppm_transform_cli_case() {
    const std::string id="0070_ppm_transform_cli";fs::path work=make_work_dir(id);
    auto help=run_process({test_executable().string(),"--ppm-transform","--help"},work,{},work/"help.out",work/"help.err");
    require(help.status==0&&help.err.find("Usage:")!=std::string::npos,id+": helper help did not print usage");
    auto no_command=run_process({test_executable().string(),"--ppm-transform"},work,{},work/"none.out",work/"none.err");
    require(no_command.status==1&&no_command.err.find("Usage:")!=std::string::npos,id+": helper no-command path changed");
    auto missing=run_process({test_executable().string(),"--ppm-transform","bytes-len"},work,{},work/"missing.out",work/"missing.err");
    require(missing.status==1&&missing.err.find("bytes-len requires TEXT")!=std::string::npos,id+": bytes-len error changed");
    auto unknown=run_process({test_executable().string(),"--ppm-transform","unknown-command"},work,{},work/"unknown.out",work/"unknown.err");
    require(unknown.status==1&&unknown.err.find("unknown command unknown-command")!=std::string::npos,id+": unknown command error changed");
}

static void cli_help_case() {
    const std::string id="0058_cli_help";fs::path work=make_work_dir(id);
    struct HelpCheck {
        std::vector<std::string> args;
        const char* first;
        const char* second;

        HelpCheck(std::vector<std::string> args_value,
                  const char* first_value,
                  const char* second_value)
            : args(args_value), first(first_value), second(second_value) {}
        HelpCheck(const HelpCheck& other)
            : args(other.args), first(other.first), second(other.second) {}
        ~HelpCheck() {}
    };
    std::vector<HelpCheck> help={{{},"Usage:","makocode encode [options]"},{{"--help"},"Usage:","makocode encode [options]"},{{"-h"},"Usage:","makocode encode [options]"},{{"encode","--help"},"Usage: makocode encode","--input PATH"},{{"decode","--help"},"Usage: makocode decode","stdin"},{{"checkpoint","--help"},"makocode checkpoint","checkpoint restore"},{{"overlay","--help"},"Usage:","makocode encode [options]"},{{"minify","--help"},"Usage: makocode minify","Strips comments/whitespace"}};
    for(size_t i=0;i<help.size();++i){auto result=run_cli(work,help[i].args,"help_"+std::to_string(i));require(result.status==0,id+": help command failed");std::string all=result.out+result.err;require(all.find(help[i].first)!=std::string::npos&&all.find(help[i].second)!=std::string::npos,id+": missing expected help text");require(all.find("--debug")==std::string::npos,id+": removed option remains in help text");}
    auto unknown_command=run_cli(work,{"unknown-command"},"unknown_command");
    require(unknown_command.status==2&&unknown_command.err.find("makocode: unknown command: unknown-command")!=std::string::npos,
            id+": unknown commands must report an error and return status 2");
    auto removed_decode_debug=run_cli(work,{"decode","--debug"},"removed_decode_debug");
    expect_failure(removed_decode_debug,id+" removed decode debug option");
    require(!fs::exists(work/"decode_fiducials.csv")&&!fs::exists(work/"decode_affine.csv"),
            id+": removed decode debug reports were created");
    for(const auto& pair:std::vector<std::pair<std::vector<std::string>,std::string>>{
        {{"encode","--debug"},"unknown option"},
        {{"encode","--ecc=1023.01"},"--ecc ratio must be between 0 and 1023"},
        {{"encode","--overlay-fraction=1.01"},"--overlay-fraction must be between 0.0 and less than 1.0"},
        {{"encode","--overlay-fraction=0.5"},"--overlay-fraction and overlay images must be used together"},
        {{"encode","--overlay-ppm=missing.ppm"},"each --overlay-ppm requires a following --overlay-pages"}}){
        auto result=run_cli(work,pair.first,"reject");expect_failure(result,id+" rejection");require(result.err.find(pair.second)!=std::string::npos||result.out.find(pair.second)!=std::string::npos,id+": rejection text mismatch");
    }
    auto encode_help=run_cli(work,{"encode","--help"},"encode_help");expect_success(encode_help,id+" encode help");
    require(encode_help.out.find("Omit to use the minimum ECC")!=std::string::npos&&encode_help.out.find("0 disables payload parity")!=std::string::npos&&encode_help.out.find("max 1024 symbols")!=std::string::npos,id+": encode help contract changed");
}

static void decode_ecc_retry_case() {
    const std::string id="0071_decode_ecc_retry";fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded_dir=work/"encoded",output=work/"output";fs::create_directories(encoded_dir);
    generate_bytes(work,payload,8192,1337,"payload");
    auto encoded=run_cli(work,encode_file_args(payload,encoded_dir,id,{"--page-width=500","--page-height=500"}),"encode");expect_success(encoded,id+" encode");
    auto pages=ppm_paths(encoded_dir,id);require(pages.size()==1,id+": expected one page");
    fs::path damaged=work/"invalid_stream.ppm";
    auto changed=run_transform(work,{"corrupt-footer-valid-data-too-corrupt","--input",pages.front(),"--output",damaged.string(),"--border-keep","80","--seed","1337"},"corrupt");expect_success(changed,id+" corrupt");
    auto result=run_cli(work,{"decode",damaged.string(),"--output-dir="+output.string()},"decode");expect_failure(result,id+" decode");
    std::string log=result.out+result.err;const std::string retry="decode: parse failure; retrying without fiducial subgrid";const std::string terminal="decode: ECC could not repair the payload";
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
    u64 total_pixels=(u64)image.width*image.height;
    require(white*100u>=total_pixels*42u&&white*100u<=total_pixels*58u,
            id+": unused fixed-page symbols are not balanced palette noise");
    auto decoded=run_cli(work,{"decode","--decoded-stdout"},"tiny_decode",pages.front());expect_success(decoded,id+" tiny decode");
    check_decoded(decoded.out,std::vector<unsigned char>{'m','a','k','o','c','o','d','e'},0,id+" small-page margin");
}

static void prefix_ends_encoded_case(){
    const std::string id="0062_prefix_ends_encoded",prefix=id+"_payload_encoded";
    fs::path work=make_work_dir(id),pages_dir=work/"pages",decoded=work/"decoded",payload=work/"payload.bin";fs::create_directories(pages_dir);
    std::string data("\0prefix-encoded roundtrip payload\xff\n",35);write_file(payload,data);
    auto encoded=run_cli(work,{"encode","--input=payload.bin","--page-width=96","--page-height=96","--palette=Cyan Black","--fiducial-spacing=32","--prefix="+prefix,"--output-dir="+pages_dir.string()},"encode");expect_success(encoded,id+" encode");
    auto pages=ppm_paths(pages_dir,"");require(pages.size()==1&&fs::path(pages[0]).filename()==prefix+".ppm",id+": prefix filename changed");
    PpmImage image=read_ppm(pages.front());size_t cyan=0;
    for(unsigned y=15;y<17;++y)for(unsigned x=15;x<17;++x){size_t i=(static_cast<size_t>(y)*image.width+x)*3;cyan+=static_cast<unsigned char>(image.pixels[i])==0&&static_cast<unsigned char>(image.pixels[i+1])==255&&static_cast<unsigned char>(image.pixels[i+2])==255;}
    require(cyan==4,id+": top-left marker did not use cyan palette endpoint");
    auto result=run_cli(work,decode_file_args(pages,decoded,{}),"decode");expect_success(result,id+" decode");
    compare_files(payload,decoded/"payload.bin",id);
}

static void metadata_pages_case(){
    const std::string id="0123_metadata_pages";fs::path work=make_work_dir(id);fs::path payload=work/"payload.bin";
    unsigned seed=case_seed("metadata_pages/payload");generate_bytes(work,payload,3000,seed,"payload");std::string payload_bytes=read_file(payload);
    struct Selection {
        const char* name;
        const char* selector;
        std::vector<int> pages;
        ~Selection() {}
    };
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
    auto enc=run_cli(work,{"encode","--input=input","--password-cli=secret","--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb","--prefix=page","--output-dir=pages"},"encode",{},{{"MAKOCODE_TEST_SEED",std::to_string(case_seed("crypto/"+id))}});
    expect_success(enc,id+" encode");auto pages=ppm_paths(pages_dir,"page");require(!pages.empty(),id+": no encoded pages");
    auto decoded=run_cli(work,decode_file_args(pages,out,{"--decoded-file","--password-cli=secret"}),"decoded_file");expect_success(decoded,id+" decode file");
    fs::path output_not_directory=work/"output_not_directory";write_file(output_not_directory,"not a directory");
    auto output_path_error=run_cli(work,decode_file_args(pages,output_not_directory,{"--decoded-file","--password-cli=secret"}),"decoded_file_output_error");
    expect_failure(output_path_error,id+" output path is a file");require(output_path_error.err.find("decode: failed to write decoded file")!=std::string::npos,id+": decoded-file output-path diagnostic changed");
    auto text_files=matching_files(out,"makocode_decoded_",".txt");require(text_files.size()==1,id+": expected one timestamped decoded file");
    std::regex name_re("^makocode_decoded_[0-9]{14}\\.txt$");require(std::regex_match(text_files[0].filename().string(),name_re),id+": invalid decoded filename");
    std::string report=read_file(text_files[0]);
    for(const std::string& required:{"MAKOCODE-DECODED","end-archive\nend","metadata ecc_enabled 1","metadata encryption_salt ","metadata encryption_nonce ","metadata encryption_tag ","metadata tile_schema 0","metadata lzma_uncompressed_bytes ","directory input/empty","file input/blank 0","file input/sub/a\\x20b 18","hello\\x0Aback\\x5Cslash\\x00\\xFF"})
        require(report.find(required)!=std::string::npos,id+": decoded report missing "+required);
    for(const std::string& forbidden:{"metadata tile_magic ","metadata metadata_magic ","metadata metadata_bytes ","metadata metadata_crc8 ","metadata tile_header ","metadata tile_data ","metadata compression ","metadata lzma_properties ","metadata encryption_magic ","metadata encryption_version ","metadata encryption_flags ","metadata encryption_kdf ","metadata encryption_iterations ","metadata encryption_plaintext_bytes "})
        require(report.find(forbidden)==std::string::npos,id+": decoded report exposed omitted metadata");
    for(const auto& field:std::vector<std::pair<std::string,size_t>>{{"encryption_salt",32},{"encryption_nonce",24},{"encryption_tag",32}}){
        std::string prefix="metadata "+field.first+" ";size_t pos=report.find(prefix);require(pos!=std::string::npos,id+": missing "+field.first);pos+=prefix.size();size_t end=report.find('\n',pos);std::string hex=report.substr(pos,end-pos);
        require(hex.size()==field.second,id+": wrong hex length for "+field.first);for(char ch:hex)require((ch>='0'&&ch<='9')||(ch>='A'&&ch<='F'),id+": invalid hex metadata");
    }
    fs::create_directories(stdin_out);auto stdin_decode=run_cli(work,{"decode","--decoded-file","--password-cli=secret","--output-dir="+stdin_out.string()},"decode_stdin",pages.front());
    expect_success(stdin_decode,id+" stdin decoded-file");auto stdin_reports=matching_files(stdin_out,"makocode_decoded_",".txt");require(stdin_reports.size()==1,id+": stdin decoded-file output missing");compare_files(text_files[0],stdin_reports[0],id+" stdin");
    auto stdout_decode=run_cli(work,{"decode","--decoded-stdout","--password-cli=secret","--output-dir="+(work/"not_created").string(),pages.front()},"decode_stdout");
    expect_success(stdout_decode,id+" stdout");require(stdout_decode.out==report,id+": stdout output differs from decoded file");require(!fs::exists(work/"not_created"),id+": decoded-stdout created output directory");
    for(const std::string& flag:{"--decoded-file"}){
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
    fs::create_directories(work/"open_failure_tree");
    auto open_directory_failure=run_process({test_executable().string(),"--checkpoint-open-directory-failure","open_failure_tree","open_failure.mcp"},work,{},work/"open_directory_failure.out",work/"open_directory_failure.err");
    expect_success(open_directory_failure,id+" directory-open fault injection");require(read_file(work/"open_directory_failure.out").find("CHECKPOINT_STATUS:1")!=std::string::npos&&read_file(work/"open_directory_failure.err").find("failed to open directory open_failure_tree")!=std::string::npos,id+": failed directory-open diagnostic changed");
    fs::create_directories(work/"input_cycle");fs::create_directory_symlink("loop",work/"input_cycle/loop");
    auto input_cycle=run_cli(work,{"checkpoint","create","--input","input_cycle","--output","input_cycle.mcp"},"input_cycle");
    expect_failure(input_cycle,id+" input directory symlink cycle");require(input_cycle.err.find("failed to build snapshot")!=std::string::npos,id+": input scan failure was not propagated");
    fs::create_directories(work/"base_cycle");fs::create_directory_symlink("loop",work/"base_cycle/loop");
    fs::create_directories(work/"unsupported_tree");
    fs::create_symlink("/dev/null",work/"unsupported_tree/device");
    auto unsupported_entry=run_cli(work,{"checkpoint","create","--input","unsupported_tree","--output","unsupported.mcp"},"unsupported_entry");
    expect_failure(unsupported_entry,id+" unsupported filesystem entry");require(unsupported_entry.err.find("unsupported entry type for unsupported_tree/device")!=std::string::npos,id+": unsupported-entry diagnostic changed");
    fs::path empty_directory_input=work/"empty_directory";fs::create_directories(empty_directory_input);
    auto empty_directory_bundle=run_cli(work,{"checkpoint","create","--input","empty_directory","--output","empty_directory.mcp"},"empty_directory_bundle");expect_success(empty_directory_bundle,id+" empty directory checkpoint");
    auto base_cycle=run_cli(work,{"checkpoint","restore","--input","empty_directory.mcp","--base","base_cycle","--output","base_cycle_out"},"base_cycle");
    expect_failure(base_cycle,id+" base directory symlink cycle");require(base_cycle.err.find("failed to scan base")!=std::string::npos,id+": base scan failure was not propagated");
    fs::path empty_directory_output=work/"empty_directory_output";
    auto empty_directory_restore=run_cli(work,{"checkpoint","restore","--input","empty_directory.mcp","--output","empty_directory_output"},"empty_directory_restore");expect_success(empty_directory_restore,id+" empty directory restore");require(fs::is_directory(empty_directory_output/"empty_directory"),id+": empty directory entry was not restored");
    fs::path empty_directory_collision=work/"empty_directory_collision";fs::create_directories(empty_directory_collision);write_file(empty_directory_collision/"empty_directory","occupied");
    auto failed_empty_directory_restore=run_cli(work,{"checkpoint","restore","--input","empty_directory.mcp","--output","empty_directory_collision"},"empty_directory_collision_restore");expect_failure(failed_empty_directory_restore,id+" directory entry collision");
    std::vector<unsigned char> a(256*20+173),b(5100);for(size_t i=0;i<a.size();++i)a[i]=static_cast<unsigned char>((i*29+i/7)&255);
    uint32_t state=0xabc123u;for(auto& v:b){state^=state<<13;state^=state>>17;state^=state<<5;v=static_cast<unsigned char>(state&255);}
    write_file(work/"base_a"/"old-a.bin",std::string(a.begin(),a.end()));write_file(work/"base_b"/"old-b.bin",std::string(b.begin(),b.end()));
    fs::copy_file(work/"base_a"/"old-a.bin",work/"current_a"/"alpha.bin");fs::copy_file(work/"base_b"/"old-b.bin",work/"current_b"/"beta.bin");
    fs::copy_file(work/"current_a"/"alpha.bin",work/"current_a"/"renamed.bin");write_file(work/"current_b"/"new.txt","new checkpoint content\n");
    auto duplicate_directory=run_cli(work,{"checkpoint","create","--input","current_a","--input","current_a","--output","duplicate_directory.mcp"},"duplicate_directory");
    expect_failure(duplicate_directory,id+" duplicate directory input");require(duplicate_directory.err.find("invalid or duplicate path current_a\n")!=std::string::npos,id+": duplicate directory path was accepted");
    auto delta=run_cli(work,{"checkpoint","create","--input=current_a","--input=current_b","--base=base_a","--base=base_b","--output=update.mcp","--chunk-size=256"},"checkpoint_delta");expect_success(delta,id+" delta checkpoint");
    auto full=run_cli(work,{"checkpoint","create","--input=current_a","--input=current_b","--output=full.mcp","--chunk-size=256"},"checkpoint_full");expect_success(full,id+" full checkpoint");
    require(fs::file_size(work/"update.mcp")<fs::file_size(work/"full.mcp"),id+": base chunks did not reduce bundle size");
    fs::path cache_base=work/"cache_base",cache_input=work/"cache_input";fs::create_directories(cache_base);fs::create_directories(cache_input);std::string cache_source(32768,'\0');uint32_t cache_state=0x83d2a17u;for(char& value:cache_source){cache_state^=cache_state<<13;cache_state^=cache_state>>17;cache_state^=cache_state<<5;value=static_cast<char>(cache_state&255u);}write_file(cache_base/"source.bin",cache_source);write_file(cache_input/"source.bin",cache_source);
    auto cache_bundle=run_cli(work,{"checkpoint","create","--input","cache_input","--base","cache_base","--output","cache_update.mcp","--chunk-size","256"},"cache_bundle");expect_success(cache_bundle,id+" repeated base source bundle");
    auto cache_restore=run_process({test_executable().string(),"--checkpoint-restore-allocation-failure","100000","cache_update.mcp","cache_base","cache_output"},work,{},work/"cache_restore.out",work/"cache_restore.err");expect_success(cache_restore,id+" cached base source restore");
    std::string cache_restore_output=read_file(work/"cache_restore.out");usize read_count_offset=cache_restore_output.find("READ_4096_COUNT:");require(cache_restore_output.find("CHECKPOINT_STATUS:0 ")!=std::string::npos&&read_count_offset!=std::string::npos,id+": cached base source restore status changed");unsigned long source_read_count=std::strtoul(cache_restore_output.c_str()+read_count_offset+16u,nullptr,10);require(source_read_count<=4u,id+": repeated base chunks reread the source file instead of using the cache");compare_files(cache_input/"source.bin",work/"cache_output/cache_input/source.bin",id);
    fs::path stale_base=work/"stale_base";fs::create_directories(stale_base);std::string stale_source(32768,'\0');uint32_t stale_state=0x4b1d23u;for(char& value:stale_source){stale_state^=stale_state<<13;stale_state^=stale_state>>17;stale_state^=stale_state<<5;value=static_cast<char>(stale_state&255u);}write_file(stale_base/"source.bin",stale_source);write_file(work/"source.bin","replacement");usize stale_chunk_offset=0u,stale_chunk_end=0u;while(stale_chunk_offset<stale_source.size()){stale_chunk_end=checkpoint_chunk_end((const u8*)stale_source.data(),stale_chunk_offset,stale_source.size(),256u);if(stale_chunk_offset>8192u)break;stale_chunk_offset=stale_chunk_end;}require(stale_chunk_offset>8192u&&stale_chunk_end>stale_chunk_offset,id+": no later base chunk was found");usize stale_chunk_length=stale_chunk_end-stale_chunk_offset;write_file(work/"later.bin",stale_source.substr(stale_chunk_offset,stale_chunk_length));
    auto stale_bundle=run_cli(work,{"checkpoint","create","--input","source.bin","--input","later.bin","--base",stale_base.string(),"--output","stale_source.mcp","--chunk-size","256"},"stale_source_bundle");expect_success(stale_bundle,id+" stale-source bundle");
    auto stale_restore=run_cli(work,{"checkpoint","restore","--input","stale_source.mcp","--base",stale_base.string(),"--output",stale_base.string()},"stale_source_restore");expect_success(stale_restore,id+" changed-base source recovery");
    require(read_file(stale_base/"source.bin")=="replacement",id+": earlier entry was not restored over the base source");
    require(fs::exists(stale_base/"later.bin.partial")&&read_file(stale_base/"later.bin.partial")==std::string(stale_chunk_length,'\0'),id+": changed base source range was not recovered as zero-filled partial data");
    require(read_file(stale_base/"checkpoint_recovery.txt").find("missing 6c617465722e62696e offset=0 length="+std::to_string(stale_chunk_length))!=std::string::npos,id+": changed base source range was not reported");
    auto missing_snapshot=run_cli(work,{"checkpoint","create","--input","missing_input","--output","missing_snapshot.mcp"},"missing_snapshot");
    expect_failure(missing_snapshot,id+" missing snapshot input");require(missing_snapshot.err.find("failed to build snapshot")!=std::string::npos,id+": snapshot-build diagnostic changed");
    auto missing_create_base=run_cli(work,{"checkpoint","create","--input","current_a","--base","missing_base","--output","missing_base.mcp"},"missing_create_base");
    expect_failure(missing_create_base,id+" missing create base");require(missing_create_base.err.find("failed to scan base data")!=std::string::npos,id+": create-base scan diagnostic changed");
    auto invalid_chunk_size=run_cli(work,{"checkpoint","create","--input","current_a","--output","invalid_chunk_size.mcp","--chunk-size","0"},"invalid_chunk_size");
    expect_failure(invalid_chunk_size,id+" invalid chunk size");require(invalid_chunk_size.err.find("invalid --chunk-size")!=std::string::npos,id+": invalid-chunk-size diagnostic changed");
    auto missing_checkpoint_output=run_cli(work,{"checkpoint","create","--input","current_a"},"missing_checkpoint_output");
    expect_failure(missing_checkpoint_output,id+" missing checkpoint output");require(missing_checkpoint_output.err.find("required input or output is missing")!=std::string::npos,id+": missing checkpoint output diagnostic changed");
    auto missing_restore_output=run_cli(work,{"checkpoint","restore","--input","update.mcp"},"missing_restore_output");
    expect_failure(missing_restore_output,id+" missing restore output");require(missing_restore_output.err.find("required input or output is missing")!=std::string::npos,id+": missing restore output diagnostic changed");
    auto missing_option_value=run_cli(work,{"checkpoint","create","--input"},"missing_option_value");
    expect_failure(missing_option_value,id+" missing option value");require(missing_option_value.err.find("option requires a value")!=std::string::npos,id+": missing-option-value diagnostic changed");
    auto duplicate_output=run_cli(work,{"checkpoint","create","--input","current_a","--output","first.mcp","--output","second.mcp"},"duplicate_output");
    expect_failure(duplicate_output,id+" duplicate output");require(duplicate_output.err.find("--output may be supplied once")!=std::string::npos,id+": duplicate-output diagnostic changed");
    auto unknown_checkpoint_option=run_cli(work,{"checkpoint","restore","--unknown"},"unknown_checkpoint_option");
    expect_failure(unknown_checkpoint_option,id+" unknown option");require(unknown_checkpoint_option.err.find("unknown option --unknown")!=std::string::npos,id+": unknown-checkpoint-option diagnostic changed");
    auto unknown_checkpoint_command=run_cli(work,{"checkpoint","invalid"},"unknown_checkpoint_command");
    expect_failure(unknown_checkpoint_command,id+" unknown command");require(unknown_checkpoint_command.err.find("expected create or restore")!=std::string::npos,id+": unknown-checkpoint-command diagnostic changed");
    fs::create_directories(work/"bundle_output_is_directory");
    auto bundle_write_error=run_cli(work,{"checkpoint","create","--input","current_a","--output","bundle_output_is_directory","--chunk-size","256"},"bundle_write_error");
    expect_failure(bundle_write_error,id+" bundle write failure");require(bundle_write_error.err.find("failed to write bundle")!=std::string::npos,id+": bundle-write diagnostic changed");
    auto allocation_failure=run_process({test_executable().string(),"--checkpoint-allocation-failure"},work,{},work/"checkpoint_alloc.out",work/"checkpoint_alloc.err");
    expect_success(allocation_failure,id+" checkpoint allocation failure mode");require(read_file(work/"checkpoint_alloc.err").find("failed to initialize bundle")!=std::string::npos,id+": checkpoint initialization allocation diagnostic changed");
    write_file(work/"allocation_input.bin",std::string(16,'a'));
    auto duplicate_file=run_cli(work,{"checkpoint","create","--input","allocation_input.bin","--input","allocation_input.bin","--output","duplicate_file.mcp"},"duplicate_file");
    expect_failure(duplicate_file,id+" duplicate file input");require(duplicate_file.err.find("invalid or duplicate path")!=std::string::npos,id+": duplicate file path was accepted");
    auto read_allocation_failure=run_process({test_executable().string(),"--checkpoint-create-allocation-failure","4","allocation_input.bin","read_allocation_failure.mcp"},work,{},work/"read_allocation_failure.out",work/"read_allocation_failure.err");
    expect_success(read_allocation_failure,id+" checkpoint input allocation failure mode");
    require(read_file(work/"read_allocation_failure.out").find("CHECKPOINT_STATUS:1 ALLOCATION_COUNT:4 FAILURE_SIZE:4096")!=std::string::npos&&read_file(work/"read_allocation_failure.err").find("failed to build snapshot")!=std::string::npos,id+": file-read allocation failure was not propagated");
    auto allocation_bundle=run_cli(work,{"checkpoint","create","--input","allocation_input.bin","--output","allocation_bundle.mcp","--chunk-size","256"},"allocation_bundle");expect_success(allocation_bundle,id+" allocation checkpoint");
    fs::path destination_allocation_failure=work/"destination_allocation_failure";fs::create_directories(destination_allocation_failure);
    auto destination_alloc_fail=run_process({test_executable().string(),"--checkpoint-restore-allocation-failure","7","allocation_bundle.mcp","destination_allocation_failure"},work,{},work/"destination_alloc.out",work/"destination_alloc.err");
    expect_success(destination_alloc_fail,id+" restore destination allocation failure mode");
    require(read_file(work/"destination_alloc.out").find("CHECKPOINT_STATUS:1 ALLOCATION_COUNT:7 FAILURE_SIZE:64")!=std::string::npos&&read_file(work/"destination_alloc.err").empty(),id+": restore destination-path allocation failure was not returned before writing");
    require(!fs::exists(destination_allocation_failure/"allocation_input.bin"),id+": restore wrote a file after destination-path allocation failed");
    fs::path output_buffer_allocation_failure=work/"output_buffer_allocation_failure";fs::create_directories(output_buffer_allocation_failure);
    auto output_buffer_alloc_fail=run_process({test_executable().string(),"--checkpoint-restore-allocation-failure","6","allocation_bundle.mcp","output_buffer_allocation_failure"},work,{},work/"output_buffer_alloc.out",work/"output_buffer_alloc.err");
    expect_success(output_buffer_alloc_fail,id+" restore output-buffer allocation failure mode");
    require(read_file(work/"output_buffer_alloc.out").find("CHECKPOINT_STATUS:1 ALLOCATION_COUNT:6 FAILURE_SIZE:64")!=std::string::npos&&read_file(work/"output_buffer_alloc.err").empty(),id+": failed output-buffer allocation was not returned");
    require(!fs::exists(output_buffer_allocation_failure/"allocation_input.bin"),id+": restore wrote a file after output-buffer allocation failed");
    auto missing_bundle=run_cli(work,{"checkpoint","restore","--input","missing.mcp","--output","missing_bundle_out"},"missing_bundle");
    expect_failure(missing_bundle,id+" missing bundle");require(missing_bundle.err.find("failed to read bundle")!=std::string::npos,id+": missing-bundle read diagnostic changed");
    std::string raw_bundle=read_file(work/"update.mcp");raw_bundle.resize(8u);write_file(work/"truncated_header.mcp",raw_bundle);
    fs::path unmapped=work/"unmapped";
    auto preserved=run_cli(work,{"checkpoint","restore","--input","truncated_header.mcp","--output",unmapped.string()},"preserve_unmapped");expect_success(preserved,id+" preserve unmapped bundle");
    compare_files(work/"truncated_header.mcp",unmapped/"checkpoint_unmapped.partial",id+" unmapped bundle");
    require(fs::exists(unmapped/"checkpoint_recovery.txt"),id+": missing unmapped-bundle recovery report");
    require(read_file(unmapped/"checkpoint_recovery.txt")=="No complete snapshot records were recoverable; raw bundle bytes are preserved in checkpoint_unmapped.partial.\n",id+": unmapped-bundle recovery report changed");
    auto missing_base=run_cli(work,{"checkpoint","restore","--input","update.mcp","--base","missing_base","--output","missing_base_out"},"missing_base_restore");
    expect_failure(missing_base,id+" missing base directory");require(missing_base.err.find("failed to scan base")!=std::string::npos,id+": missing base scan diagnostic changed");
    write_file(work/"output_is_file","output path conflict");
    auto output_not_directory=run_cli(work,{"checkpoint","restore","--input","update.mcp","--output","output_is_file"},"output_not_directory");
    expect_failure(output_not_directory,id+" output path is a file");require(output_not_directory.err.find("failed to create output directory")!=std::string::npos,id+": output-directory diagnostic changed");
    auto encoded=run_cli(work,{"encode","--input=update.mcp","--output-dir=pages","--prefix=checkpoint","--page-width=800","--page-height=800"},"encode");expect_success(encoded,id+" encode checkpoint");
    auto pages=ppm_paths(work/"pages","checkpoint");require(!pages.empty(),id+": no checkpoint pages");
    fs::path decoded=work/"decoded";auto decoded_result=run_cli(work,decode_file_args(pages,decoded),"decode");expect_success(decoded_result,id+" decode checkpoint");
    fs::path restored=work/"restored";auto restore=run_cli(work,{"checkpoint","restore","--input", (decoded/"update.mcp").string(),"--base",(work/"base_a").string(),"--base",(work/"base_b").string(),"--output",restored.string()},"restore");expect_success(restore,id+" restore");
    require(restore.out.find("checkpoint: restored snapshot")!=std::string::npos,id+": successful restore diagnostic changed");
    for(const auto& name:{"alpha.bin","renamed.bin"})compare_files(work/"current_a"/name,restored/"current_a"/name,id);
    for(const auto& name:{"beta.bin","new.txt"})compare_files(work/"current_b"/name,restored/"current_b"/name,id);
    std::string valid_bundle=read_file(work/"full.mcp");
    usize bundle_cursor=sizeof(CHECKPOINT_MAGIC)+16u,first_entry_type_offset=bundle_cursor,path_offset=0u,file_size_offset=0u,chunk_count_offset=0u,first_ref_offset=0u;
    std::string first_file_path;
    u64 bundle_entries=read_le_u64((const u8*)valid_bundle.data()+sizeof(CHECKPOINT_MAGIC)+8u);
    for(u64 entry_index=0u;entry_index<bundle_entries&&bundle_cursor<valid_bundle.size();++entry_index){
        u8 entry_type=(u8)valid_bundle[bundle_cursor++];u64 entry_path_length=read_le_u64((const u8*)valid_bundle.data()+bundle_cursor);bundle_cursor+=8u;
        path_offset=bundle_cursor;bundle_cursor+=(usize)entry_path_length;
        if(entry_type==0u){first_file_path.assign(valid_bundle.data()+path_offset,(usize)entry_path_length);file_size_offset=bundle_cursor;chunk_count_offset=file_size_offset+8u;first_ref_offset=chunk_count_offset+8u;break;}
    }
    require(!first_file_path.empty()&&first_ref_offset+41u<=valid_bundle.size(),id+": full checkpoint file record is missing");
    usize first_data_offset=first_ref_offset+41u;u64 first_ref_length=read_le_u64((const u8*)valid_bundle.data()+first_ref_offset+32u);
    require((u8)valid_bundle[first_ref_offset+40u]==1u&&first_data_offset+first_ref_length<=valid_bundle.size(),id+": expected embedded first checkpoint chunk");
    std::string unsafe_path_bundle=valid_bundle;require(first_file_path.size()>3u,id+": first checkpoint path is too short for traversal mutation");memcpy(unsafe_path_bundle.data()+path_offset,"../",3u);
    write_file(work/"unsafe_path.mcp",unsafe_path_bundle);
    auto unsafe_path_restore=run_cli(work,{"checkpoint","restore","--input","unsafe_path.mcp","--output","unsafe_path_out"},"unsafe_path_restore");expect_success(unsafe_path_restore,id+" unsafe bundle path recovery");
    require(read_file(work/"unsafe_path_out/checkpoint_recovery.txt").find("bundle manifest truncated; later entries may be unavailable\n")!=std::string::npos,id+": unsafe bundle path did not stop manifest recovery");
    require(!fs::exists(work/"rent_a/alpha.bin"),id+": unsafe bundle path escaped the restore destination");
    std::string invalid_entry_bundle=valid_bundle;invalid_entry_bundle[first_entry_type_offset]=2;
    write_file(work/"invalid_entry.mcp",invalid_entry_bundle);
    auto invalid_entry_restore=run_cli(work,{"checkpoint","restore","--input","invalid_entry.mcp","--output","invalid_entry_out"},"invalid_entry_restore");expect_success(invalid_entry_restore,id+" invalid entry recovery");
    compare_files(work/"invalid_entry.mcp",work/"invalid_entry_out/checkpoint_unmapped.partial",id);
    std::string invalid_settings_bundle=valid_bundle;write_le_u64((u8*)invalid_settings_bundle.data()+sizeof(CHECKPOINT_MAGIC),0u);
    write_file(work/"invalid_settings.mcp",invalid_settings_bundle);
    auto invalid_settings_restore=run_cli(work,{"checkpoint","restore","--input","invalid_settings.mcp","--output","invalid_settings_out"},"invalid_settings_restore");expect_success(invalid_settings_restore,id+" invalid settings recovery");
    compare_files(work/"invalid_settings.mcp",work/"invalid_settings_out/checkpoint_unmapped.partial",id);
    std::vector<std::pair<std::string,std::string>> damaged_bundles;
    damaged_bundles.push_back({"manifest_file_size",valid_bundle.substr(0u,file_size_offset+4u)});
    damaged_bundles.push_back({"manifest_chunk_count",valid_bundle.substr(0u,chunk_count_offset+4u)});
    damaged_bundles.push_back({"chunk_record",valid_bundle.substr(0u,first_ref_offset+40u)});
    std::string zero_length_bundle=valid_bundle;write_le_u64((u8*)zero_length_bundle.data()+first_ref_offset+32u,0u);damaged_bundles.push_back({"zero_chunk",zero_length_bundle});
    std::string invalid_kind_bundle=valid_bundle;invalid_kind_bundle[first_ref_offset+40u]=3;damaged_bundles.push_back({"invalid_kind",invalid_kind_bundle});
    std::string truncated_chunk_bundle=valid_bundle;truncated_chunk_bundle.resize(first_data_offset+(usize)(first_ref_length/2u));damaged_bundles.push_back({"embedded_chunk",truncated_chunk_bundle});
    std::string invalid_chunk_id_bundle=valid_bundle;invalid_chunk_id_bundle[first_data_offset]^=1;damaged_bundles.push_back({"embedded_chunk_id",invalid_chunk_id_bundle});
    std::string manifest_tail_bundle=valid_bundle;write_le_u64((u8*)manifest_tail_bundle.data()+file_size_offset,read_le_u64((const u8*)manifest_tail_bundle.data()+file_size_offset)+1u);damaged_bundles.push_back({"manifest_tail",manifest_tail_bundle});
    std::string overflow_bundle=valid_bundle;u64 huge_ref=(u64)USIZE_MAX_VALUE/2u+1u;
    write_le_u64((u8*)overflow_bundle.data()+chunk_count_offset,2u);write_le_u64((u8*)overflow_bundle.data()+first_ref_offset+32u,huge_ref);overflow_bundle[first_ref_offset+40u]=0;
    usize second_ref_offset=first_data_offset;overflow_bundle.insert(second_ref_offset,41u,'\0');memcpy(overflow_bundle.data()+second_ref_offset,valid_bundle.data()+first_ref_offset,32u);write_le_u64((u8*)overflow_bundle.data()+second_ref_offset+32u,huge_ref);overflow_bundle[second_ref_offset+40u]=0;
    damaged_bundles.push_back({"overflow_chunks",overflow_bundle});
    if (USIZE_MAX_VALUE < U64_MAX_VALUE) {
        std::string usize_overflow_bundle=valid_bundle;u64 near_usize_max=(u64)USIZE_MAX_VALUE-15u;
        write_le_u64((u8*)usize_overflow_bundle.data()+chunk_count_offset,2u);write_le_u64((u8*)usize_overflow_bundle.data()+first_ref_offset+32u,near_usize_max);usize_overflow_bundle[first_ref_offset+40u]=0;
        usize_overflow_bundle.insert(first_data_offset,41u,'\0');memcpy(usize_overflow_bundle.data()+first_data_offset,valid_bundle.data()+first_ref_offset,32u);write_le_u64((u8*)usize_overflow_bundle.data()+first_data_offset+32u,32u);usize_overflow_bundle[first_data_offset+40u]=0;
        damaged_bundles.push_back({"usize_overflow",usize_overflow_bundle});
    }
    for(const auto& damaged_bundle:damaged_bundles){
        fs::path malformed_input=work/(damaged_bundle.first+".mcp"),malformed_output=work/(damaged_bundle.first+"_out");write_file(malformed_input,damaged_bundle.second);
        auto malformed_restore=run_cli(work,{"checkpoint","restore","--input",malformed_input.string(),"--output",malformed_output.string()},damaged_bundle.first+"_restore");expect_success(malformed_restore,id+" "+damaged_bundle.first+" recovery");
        require(fs::exists(malformed_output/(first_file_path+".partial"))&&fs::exists(malformed_output/"checkpoint_recovery.txt"),id+": "+damaged_bundle.first+" did not produce partial recovery");
    }
    require(read_file(work/"manifest_file_size_out/checkpoint_recovery.txt").find("bundle manifest truncated; later entries may be unavailable\n")!=std::string::npos,id+": truncated bundle manifest was not reported");
    std::string invalid_chunk_recovery=read_file(work/"embedded_chunk_id_out"/(first_file_path+".partial"));require(invalid_chunk_recovery.size()>=(usize)first_ref_length&&std::all_of(invalid_chunk_recovery.begin(),invalid_chunk_recovery.begin()+(usize)first_ref_length,[](char ch){return ch==0;}),id+": embedded chunk with invalid identifier was restored");
    std::string tail_report=read_file(work/"manifest_tail_out/checkpoint_recovery.txt");require(tail_report.find(" length=1 manifest-tail\n")!=std::string::npos,id+": manifest tail length was not reported");
    static const char hex_digits[]="0123456789abcdef";std::string expected_tail_line="missing ";for(unsigned char ch:first_file_path){expected_tail_line+=hex_digits[ch>>4u];expected_tail_line+=hex_digits[ch&15u];}expected_tail_line+=" offset=";expected_tail_line+=std::to_string(read_le_u64((const u8*)valid_bundle.data()+file_size_offset));expected_tail_line+=" length=1 manifest-tail\n";
    require(tail_report.find(expected_tail_line)!=std::string::npos,id+": manifest-tail report path or offset changed");
    std::string expected_missing_chunk_line="missing ";for(unsigned char ch:first_file_path){expected_missing_chunk_line+=hex_digits[ch>>4u];expected_missing_chunk_line+=hex_digits[ch&15u];}expected_missing_chunk_line+=" offset=0 length=";expected_missing_chunk_line+=std::to_string(first_ref_length);expected_missing_chunk_line+=" chunk=";for(usize i=0u;i<32u;++i){u8 ch=(u8)valid_bundle[first_ref_offset+i];expected_missing_chunk_line+=hex_digits[ch>>4u];expected_missing_chunk_line+=hex_digits[ch&15u];}expected_missing_chunk_line+='\n';
    std::string embedded_report=read_file(work/"embedded_chunk_out/checkpoint_recovery.txt");require(embedded_report.find(expected_missing_chunk_line)!=std::string::npos,id+": missing chunk report path, range, or identifier changed");
    fs::path write_failure=work/"write_failure";fs::create_directories(write_failure/"current_a/alpha.bin");
    auto failed_write=run_cli(work,{"checkpoint","restore","--input",(decoded/"update.mcp").string(),"--base",(work/"base_a").string(),"--base",(work/"base_b").string(),"--output",write_failure.string()},"failed_write_restore");
    expect_failure(failed_write,id+" restored file write failure");require(failed_write.err.find("failed to write restored file")!=std::string::npos,id+": restored-file write diagnostic changed");
    fs::path parent_failure=work/"parent_failure";fs::create_directories(parent_failure);write_file(parent_failure/"blocked_a","not a directory");
    std::string blocked_parent_bundle=valid_bundle;std::string blocked_parent_path="blocked_a"+first_file_path.substr(first_file_path.find('/'));require(blocked_parent_path.size()==first_file_path.size(),id+": blocked-parent path length changed");memcpy(blocked_parent_bundle.data()+path_offset,blocked_parent_path.data(),blocked_parent_path.size());write_file(work/"blocked_parent.mcp",blocked_parent_bundle);
    auto failed_parent=run_cli(work,{"checkpoint","restore","--input",(work/"blocked_parent.mcp").string(),"--output",parent_failure.string()},"failed_parent_restore");
    expect_failure(failed_parent,id+" restored parent-directory creation failure");require(failed_parent.err.find("failed to write restored file")!=std::string::npos,id+": parent-directory failure diagnostic changed");
    fs::path report_failure=work/"report_failure";fs::create_directories(report_failure/"checkpoint_recovery.txt");
    auto failed_report=run_cli(work,{"checkpoint","restore","--input",(work/"manifest_tail.mcp").string(),"--output",report_failure.string()},"failed_report_restore");
    require(failed_report.err.find("checkpoint: restored available chunks")==std::string::npos,id+": reported partial recovery after recovery-report write failure");
    expect_failure(failed_report,id+" recovery-report write failure");
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
    struct Success {
        const char* name;
        int pages;
        const char* visible_password;
        const char* hidden_password;
        bool hidden_capable;
        bool random_fill;
        std::vector<std::string> options;
        bool marker_free = false;

        Success(const char* name_value, int pages_value,
                const char* visible_password_value,
                const char* hidden_password_value, bool hidden_capable_value,
                bool random_fill_value, std::vector<std::string> options_value,
                bool marker_free_value = false)
            : name(name_value), pages(pages_value),
              visible_password(visible_password_value),
              hidden_password(hidden_password_value),
              hidden_capable(hidden_capable_value),
              random_fill(random_fill_value), options(options_value),
              marker_free(marker_free_value) {}
        Success(const Success& other)
            : name(other.name), pages(other.pages),
              visible_password(other.visible_password),
              hidden_password(other.hidden_password),
              hidden_capable(other.hidden_capable),
              random_fill(other.random_fill), options(other.options),
              marker_free(other.marker_free) {}
        ~Success() {}
    };
    const std::vector<Success> scenarios={
        {"visible_default",1,"","",false,false,{}},{"visible_hidden_zero",1,"","",false,false,{"--hidden=0","--ecc=0.5"}},
        {"visible_password_hidden_zero",1,"visible test password","",false,false,{"--hidden=0"}},
        {"ecc_fill_one_page",1,"","",false,false,{"--ecc=fill","--fill-pages=1"}},
        {"ecc_fill_two_pages",2,"","",false,true,{"--ecc=fill","--fill-pages=2"}},
        {"ecc_fill_current_page",1,"","",false,false,{"--ecc=fill"}},
        {"hidden_ratio_auto",1,"visible test password","hidden test password",true,false,{"--hidden=0.5"}},
        {"hidden_ratio_marker_free",1,"visible test password","hidden test password",true,false,{"--hidden=0.5","--palette","White Black","--no-metadata","--no-fiducials"},true},
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
        if(*scenario.visible_password)args.push_back(std::string("--password-cli=")+scenario.visible_password);
        if(*scenario.visible_password||scenario.random_fill)args.push_back("--random-data="+random_data);
        if(*scenario.hidden_password){args.push_back("--hidden-input=hidden_tree");args.push_back(std::string("--hidden-password-cli=")+scenario.hidden_password);}
        args.insert(args.end(),scenario.options.begin(),scenario.options.end());
        auto encoded=run_cli(source,args,name+"_encode",{},{{"MAKOCODE_TEST_SEED",std::to_string(case_seed("encoder/"+id+"/"+name))}});
        expect_success(encoded,id+" encode "+name);
        auto pages=ppm_paths(pages_dir,name);require(static_cast<int>(pages.size())==scenario.pages,id+" "+name+": page count mismatch");
        std::vector<std::string> visible_decode_options;
        if(scenario.marker_free)visible_decode_options={"--no-metadata","--no-fiducials","--page-width=1000","--page-height=1000","--palette","White Black"};
        if(*scenario.visible_password)visible_decode_options.push_back(std::string("--password-cli=")+scenario.visible_password);
        std::vector<std::string> visible_args=decode_file_args(pages,visible_out,visible_decode_options);
        auto visible_decoded=run_cli(work,visible_args,name+"_visible_decode");expect_success(visible_decoded,id+" visible decode "+name);compare_files(visible,visible_out/"visible.bin",id+" visible "+name);
        if(*scenario.hidden_password){
            fs::create_directories(hidden_out);std::vector<std::string> hidden_decode_options;
            if(scenario.marker_free)hidden_decode_options={"--no-metadata","--no-fiducials","--page-width=1000","--page-height=1000","--palette","White Black"};
            hidden_decode_options.push_back(std::string("--password-cli=")+scenario.hidden_password);
            std::vector<std::string> hidden_args=decode_file_args(pages,hidden_out,hidden_decode_options);
            auto hidden_decoded=run_cli(work,hidden_args,name+"_hidden_decode");expect_success(hidden_decoded,id+" hidden decode "+name);
            compare_files(hidden/"root.txt",hidden_out/"hidden_tree/root.txt",id+" hidden root "+name);
            compare_files(hidden/"empty.bin",hidden_out/"hidden_tree/empty.bin",id+" hidden empty "+name);
            compare_files(secret,hidden_out/"hidden_tree/nested/secret.bin",id+" hidden secret "+name);
            require(fs::is_directory(hidden_out/"hidden_tree/nested/empty")&&!fs::exists(visible_out/"hidden_tree")&&!fs::exists(hidden_out/"visible.bin"),id+" hidden tree separation "+name);
        }else if(scenario.hidden_capable){
            fs::create_directories(hidden_out);
            auto no_password=run_cli(work,decode_file_args(pages,hidden_out),"hidden_no_password");expect_failure(no_password,id+" no-password container "+name);
            auto wrong=run_cli(work,decode_file_args(pages,hidden_out,{"--password-cli=not-the-hidden-password"}),"hidden_wrong_password");expect_failure(wrong,id+" wrong hidden password "+name);
            require(!fs::exists(hidden_out/"visible.bin")&&!fs::exists(hidden_out/"hidden_tree"),id+" filler-only output "+name);
        }
    }
    struct Reject {
        const char* name;
        const char* message;
        std::vector<std::string> args;

        Reject(const char* name_value, const char* message_value,
               std::vector<std::string> args_value)
            : name(name_value), message(message_value), args(args_value) {}
        Reject(const Reject& other)
            : name(other.name), message(other.message), args(other.args) {}
        ~Reject() {}
    };
    const std::string visible_password="visible test password",hidden_password="hidden test password";
    std::vector<Reject> rejects={
        {"hidden_fill_ecc_fill","--hidden fill and --ecc fill compete",{"--hidden=fill","--ecc=fill","--password-cli="+visible_password}},
        {"ecc_disabled_visible","ECC cannot cover structural erasures",{"--ecc=0"}},
        {"ecc_disabled_hidden_ratio","ECC cannot cover structural erasures",{"--hidden=64","--ecc=0","--password-cli="+visible_password}},
        {"ecc_disabled_hidden_fill","ECC cannot cover structural erasures",{"--hidden=fill","--ecc=0","--fill-pages=1","--password-cli="+visible_password}},
        {"fill_pages_without_fill","--fill-pages requires --hidden fill or --ecc fill",{"--hidden=2","--ecc=0.5","--fill-pages=1","--password-cli="+visible_password}},
        {"hidden_inputs_without_mode","hidden inputs and password require --hidden",{"--hidden-input=hidden_tree","--hidden-password-cli="+hidden_password}},
        {"hidden_input_without_password","--hidden-input and --hidden-password must be supplied together",{"--hidden=0.5","--hidden-input=hidden_tree","--password-cli="+visible_password}},
        {"hidden_password_without_input","--hidden-input and --hidden-password must be supplied together",{"--hidden=0.5","--password-cli="+visible_password,"--hidden-password-cli="+hidden_password}},
        {"hidden_options_with_zero","hidden inputs and password require --hidden",{"--hidden=0","--hidden-input=hidden_tree","--hidden-password-cli="+hidden_password,"--password-cli="+visible_password}},
        {"hidden_without_visible_password","--hidden requires --password and --random-data",{"--hidden=0.5"}},
        {"identical_passwords","visible and hidden passwords must differ",{"--hidden=0.5","--hidden-input=hidden_tree","--hidden-password-cli="+visible_password,"--password-cli="+visible_password}},
        {"hidden_capacity_too_small","hidden payload exceeds the --hidden capacity",{"--hidden=0.001","--hidden-input=hidden_tree","--hidden-password-cli="+hidden_password,"--password-cli="+visible_password}},
        {"hidden_ratio_negative","--hidden ratio must be between 0 and 64",{"--hidden=-1"}},
        {"hidden_ratio_too_large","--hidden ratio must be between 0 and 64",{"--hidden=65"}},
        {"duplicate_hidden_option","--hidden may be specified only once",{"--hidden=0.5","--hidden=2"}},
        {"fill_pages_zero","--fill-pages must be between 1 and 1000000",{"--ecc=fill","--fill-pages=0"}},
        {"duplicate_fill_pages","--fill-pages may be specified only once",{"--ecc=fill","--fill-pages=1","--fill-pages=2"}},
        {"fill_pages_with_numeric_modes","--fill-pages requires --hidden fill or --ecc fill",{"--hidden=0.5","--ecc=0.5","--fill-pages=1","--password-cli="+visible_password}},
        {"overlay_fraction_without_ppm","--overlay-fraction and overlay images must be used together",{"--ecc=0.5","--overlay-fraction=0.1"}},
        {"removed_steganography_option","encode: unknown option: --steganography",{"--steganography"}},
        {"overlay_ppm_without_fraction","--overlay-fraction and overlay images must be used together",{"--ecc=0.5","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"overlay_fraction_too_large","--overlay-fraction must be between 0.0 and less than 1.0",{"--ecc=0.5","--overlay-fraction=1.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"overlay_without_ecc","overlay requires ECC",{"--ecc=0","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"multipage_overlay_ecc_too_low","multi-page overlays require --ecc of at least 0.5",{"--hidden=fill","--hidden-input=hidden_tree","--hidden-password-cli="+hidden_password,"--password-cli="+visible_password,"--ecc=0.25","--fill-pages=2","--overlay-fraction=0.1","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}},
        {"overlay_wrong_dimensions","--overlay-ppm dimensions must match",{"--ecc=0.5","--overlay-fraction=0.1","--overlay-ppm="+bad_overlay.string(),"--overlay-pages=all"}},
        {"random_data_without_password_or_fill","--random-data requires --password or --fill-pages",{"--random-data=ffeeddccbbaa99887766554433221100123456789abcdef012345678"}},
        {"removed_password_argument_syntax","use --password-cli to provide a password argument",{"--password=must-not-appear"}},
        {"removed_hidden_password_argument_syntax","use --hidden-password-cli to provide a password argument",{"--hidden-password=must-not-appear"}},
        {"hidden_missing_value","--hidden requires a ratio or 'fill'",{"--hidden"}},
        {"hidden_ratio_not_numeric","--hidden ratio must be between 0 and 64",{"--hidden=secret"}},
        {"fill_pages_missing_value","--fill-pages requires a positive page count",{"--ecc=fill","--fill-pages"}},
        {"fill_pages_over_limit","--fill-pages must be between 1 and 1000000",{"--ecc=fill","--fill-pages=1000001"}}
    };
    for(const auto& reject:rejects){
        fs::path out=work/(std::string(reject.name)+"-rejected");fs::create_directories(out);
        std::vector<std::string> args={"encode","--input=visible.bin","--page-width=1000","--page-height=1000","--output-dir="+out.string(),std::string("--prefix=")+reject.name};
        bool password=false,random=false;for(const auto& a:reject.args){password|=a.rfind("--password-cli=",0)==0;random|=a.rfind("--random-data=",0)==0;}
        if(password&&!random)args.push_back("--random-data="+random_for(reject.name));
        args.insert(args.end(),reject.args.begin(),reject.args.end());
        auto failed=run_cli(source,args,std::string(reject.name)+"_reject");expect_failure(failed,id+" reject "+reject.name);
        std::string output=failed.err+failed.out;require(output.find(reject.message)!=std::string::npos,id+" rejection reason mismatch for "+reject.name);
        if (std::string(reject.name).find("removed_") == 0u) {
            require(output.find("must-not-appear") == std::string::npos,
                    id + " printed a rejected password argument");
        }
        require(matching_files(out,"",".ppm").empty(),id+" rejected encode emitted page "+reject.name);
    }
    std::vector<std::string> missing_random={"encode","--input=visible.bin","--page-width=1000","--page-height=1000","--password-cli="+visible_password,"--output-dir="+(work/"missing_random").string()};
    auto missing=run_cli(source,missing_random,"missing_random");expect_failure(missing,id+" missing random data");
    require((missing.err+missing.out).find("--password requires --random-data")!=std::string::npos,id+": missing random error changed");
}

static void stealth_container_lengths_case(){
    const std::string id="0129_stealth_container_lengths";
    const u8 pbkdf_salt[]={'s','a','l','t'};
    const u8 pbkdf_expected[32]={
        0x12u,0x0fu,0xb6u,0xcfu,0xfcu,0xf8u,0xb3u,0x2cu,
        0x43u,0xe7u,0x22u,0x52u,0x56u,0xc4u,0xf8u,0x37u,
        0xa8u,0x65u,0x48u,0xc9u,0x2cu,0xccu,0x35u,0x48u,
        0x08u,0x05u,0x98u,0x7cu,0xb7u,0x0bu,0xe1u,0x7bu};
    u8 pbkdf_output[32];
    require(MetadataTile::pbkdf2_hmac_sha256((const u8*)"password",8u,pbkdf_salt,
                                             sizeof(pbkdf_salt),1u,pbkdf_output,
                                             sizeof(pbkdf_output))&&
            memcmp(pbkdf_output,pbkdf_expected,sizeof(pbkdf_output))==0,
            id+": cached HMAC seed changed PBKDF2 output");
    const char* visible_password="visible length password";
    const char* hidden_password="hidden length password";
    const char* visible_text="visible archive bytes";
    const char* hidden_text="hidden archive bytes";
    const u8 random_data[makocode::ENCRYPTION_SALT_BYTES+
                         makocode::ENCRYPTION_NONCE_BYTES]={
        0x11u,0x22u,0x33u,0x44u,0x55u,0x66u,0x77u,0x88u,
        0x99u,0xAAu,0xBBu,0xCCu,0xDDu,0xEEu,0xF0u,0x10u,
        0x21u,0x32u,0x43u,0x54u,0x65u,0x76u,0x87u,0x98u,
        0xA9u,0xBAu,0xCBu,0xDCu};
    const u8* salt=random_data;
    const u8* nonce_seed=random_data+makocode::ENCRYPTION_SALT_BYTES;
    u8 visible_key[32],hidden_key[32];
    require(makocode::stealth_derive_base_key(visible_password,strlen(visible_password),salt,visible_key),
            id+": visible key derivation failed");
    require(makocode::stealth_derive_base_key(hidden_password,strlen(hidden_password),salt,hidden_key),
            id+": hidden key derivation failed");
    makocode::ByteBuffer visible_compressed,hidden_compressed,visible_body,hidden_body;
    require(makocode::lzma_compress(reinterpret_cast<const u8*>(visible_text),strlen(visible_text),visible_compressed,true),
            id+": visible compression failed");
    require(makocode::lzma_compress(reinterpret_cast<const u8*>(hidden_text),strlen(hidden_text),hidden_compressed,true),
            id+": hidden compression failed");
    require(makocode::stealth_encrypt_blob(visible_compressed.data,visible_compressed.size,visible_key,
            nonce_seed,random_data,sizeof(random_data),"visible body",visible_body),
            id+": visible body encryption failed");
    require(makocode::stealth_encrypt_blob(hidden_compressed.data,hidden_compressed.size,hidden_key,
            nonce_seed,random_data,sizeof(random_data),"hidden body",hidden_body),
            id+": hidden body encryption failed");
    const usize body_offset=MetadataTile::STEALTH_CONTAINER_HEADER_BYTES+visible_body.size;
    const usize hidden_capacity=hidden_body.size+37u;
    const usize container_length=body_offset+hidden_capacity+29u;
    makocode::ByteBuffer container;
    require(container.ensure(container_length),id+": carrier allocation failed");
    container.size=container_length;
    memset(container.data,0xA5u,container.size);
    memcpy(container.data,random_data,sizeof(random_data));
    u8 visible_descriptor_plain[MetadataTile::STEALTH_VISIBLE_DESCRIPTOR_PLAINTEXT_BYTES];
    write_le_u64(visible_descriptor_plain,(u64)visible_body.size);
    write_le_u64(visible_descriptor_plain+8u,(u64)container_length);
    makocode::ByteBuffer visible_descriptor;
    require(makocode::stealth_encrypt_blob(visible_descriptor_plain,sizeof(visible_descriptor_plain),visible_key,
            nonce_seed,random_data,sizeof(random_data),"visible descriptor",visible_descriptor),
            id+": visible descriptor encryption failed");
    u8 hidden_descriptor_plain[MetadataTile::STEALTH_HIDDEN_DESCRIPTOR_PLAINTEXT_BYTES];
    write_le_u64(hidden_descriptor_plain,(u64)body_offset);
    write_le_u64(hidden_descriptor_plain+8u,(u64)hidden_body.size);
    write_le_u64(hidden_descriptor_plain+16u,(u64)hidden_capacity);
    write_le_u64(hidden_descriptor_plain+24u,(u64)container_length);
    makocode::ByteBuffer hidden_descriptor;
    require(makocode::stealth_encrypt_blob(hidden_descriptor_plain,sizeof(hidden_descriptor_plain),hidden_key,
            nonce_seed,random_data,sizeof(random_data),"hidden descriptor",hidden_descriptor),
            id+": hidden descriptor encryption failed");
    require(visible_descriptor.size==MetadataTile::STEALTH_VISIBLE_DESCRIPTOR_BYTES&&
            hidden_descriptor.size==MetadataTile::STEALTH_HIDDEN_DESCRIPTOR_BYTES,
            id+": descriptor sizes changed");
    memcpy(container.data+MetadataTile::STEALTH_CONTAINER_PREFIX_BYTES,
           visible_descriptor.data,visible_descriptor.size);
    memcpy(container.data+MetadataTile::STEALTH_CONTAINER_PREFIX_BYTES+
               MetadataTile::STEALTH_VISIBLE_DESCRIPTOR_BYTES,
           hidden_descriptor.data,hidden_descriptor.size);
    memcpy(container.data+MetadataTile::STEALTH_CONTAINER_HEADER_BYTES,
           visible_body.data,visible_body.size);
    memcpy(container.data+body_offset,hidden_body.data,hidden_body.size);
    require(read_le_u32(container.data)!=0x4D454E43u,
            id+": old clear encryption marker remains");
    require(memcmp(container.data+MetadataTile::STEALTH_CONTAINER_PREFIX_BYTES,
                   visible_descriptor_plain,sizeof(visible_descriptor_plain))!=0,
            id+": visible descriptor is not encrypted");

    for(unsigned password_index=0u;password_index<2u;++password_index){
        const char* password=password_index==0u?visible_password:hidden_password;
        const char* expected=password_index==0u?visible_text:hidden_text;
        const usize expected_body_end=password_index==0u
            ? MetadataTile::STEALTH_CONTAINER_HEADER_BYTES+visible_body.size
            : body_offset+hidden_body.size;
        bool is_container=false;
        usize recovered_container_length=0u,recovered_body_end=0u;
        makocode::ByteBuffer archive;
        require(makocode::decode_stealth_container(container.data,container.size,password,strlen(password),
                archive,is_container,&recovered_container_length,&recovered_body_end),
                id+": protected archive failed to open");
        require(is_container&&recovered_container_length==container_length&&
                recovered_body_end==expected_body_end,
                id+": encrypted stream or body length was recovered incorrectly");
        require(archive.size==strlen(expected)&&memcmp(archive.data,expected,archive.size)==0,
                id+": decrypted archive changed");
    }
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
        1u,&erasures,&selector_seed,&selector_counts,&unknown,&overlay,
        (bool*)0,false,(u64*)0,(const MetadataTile::Values*)0,
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
    unsigned center_clear_radius=0;
    std::vector<std::string> encode_options;
    int expected_pages=0;
    bool skip_grayscale=false,without_metadata=false,without_fiducials=false,encrypted=false,scatter=false;
};

static void overlay_case(const OverlayConfig& cfg){
    fs::path work=make_work_dir(cfg.id),payload=work/"payload.bin",mask=work/"overlay.ppm",pages_dir=work/"pages",decoded=work/"decoded";
    fs::create_directories(pages_dir);
    unsigned payload_seed=case_seed("payload/"+cfg.id);
    generate_bytes(work,payload,cfg.size,payload_seed,"payload");
    std::map<std::string,std::string> mask_env;
    if(!cfg.circle_colors.empty())mask_env["MAKO_OVERLAY_CIRCLE_COLORS"]=cfg.circle_colors;
    std::vector<std::string> mask_args={"overlay-mask","--output",mask.string(),"--circle-color",cfg.circle,
        "--background-color",cfg.background,"--width",std::to_string(cfg.width),"--height",std::to_string(cfg.height)};
    if(cfg.center_clear_radius){mask_args.push_back("--center-clear-radius");mask_args.push_back(std::to_string(cfg.center_clear_radius));}
    auto made=run_transform(work,mask_args,"mask",mask_env);
    expect_success(made,cfg.id+" overlay mask");
    std::vector<std::string> encode={"encode","--input=payload.bin","--overlay-fraction="+cfg.fraction,
        "--overlay-ppm="+mask.string(),"--overlay-pages=all","--page-width="+std::to_string(cfg.width),
        "--page-height="+std::to_string(cfg.height),"--output-dir="+pages_dir.string(),"--prefix="+cfg.id};
    if(!cfg.ecc.empty())encode.push_back("--ecc="+cfg.ecc);
    if(!cfg.palette.empty()){encode.push_back("--palette");encode.push_back(cfg.palette);}
    if(!cfg.ignore_colors.empty()){encode.push_back("--overlay-ignore-colors");encode.push_back(cfg.ignore_colors);}
    if(!cfg.footer.empty()){encode.push_back("--footer");encode.push_back(cfg.footer);}
    if(!cfg.font_size.empty()){encode.push_back("--font-size");encode.push_back(cfg.font_size);}
    if(cfg.scatter)encode.push_back("--scatter");
    if(cfg.without_metadata)encode.push_back("--no-metadata");
    if(cfg.without_fiducials)encode.push_back("--no-fiducials");
    if(cfg.without_metadata){
        encode.push_back("--password-cli=scatter-suite-password");
        encode.push_back("--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb");
    }
    encode.insert(encode.end(),cfg.encode_options.begin(),cfg.encode_options.end());
    auto encoded=run_cli(work,encode,"encode",payload,{{"MAKOCODE_TEST_SEED",std::to_string(case_seed("crypto/"+cfg.id))}});
    expect_success(encoded,cfg.id+" encode");

    auto unordered=ppm_paths(pages_dir,cfg.id);require(!unordered.empty(),cfg.id+": encode produced no PPM pages");if(cfg.expected_pages>0)require(static_cast<int>(unordered.size())==cfg.expected_pages,cfg.id+": page count mismatch");
    if(cfg.without_metadata){
        PpmImage overlay_image=read_ppm(mask),encoded_image=read_ppm(unordered.front());
        require(overlay_image.width==encoded_image.width&&overlay_image.height==encoded_image.height,
                cfg.id+": overlay comparison dimensions differ");
        size_t matching_pixels=0,total_pixels=static_cast<size_t>(encoded_image.width)*encoded_image.height;
        unsigned maximum_horizontal_run=0u;
        unsigned maximum_vertical_run=0u;
        std::vector<unsigned> vertical_runs(encoded_image.width,0u);
        for(size_t i=0;i<total_pixels*3u;i+=3u)
            if(overlay_image.pixels[i]==encoded_image.pixels[i]&&
               overlay_image.pixels[i+1u]==encoded_image.pixels[i+1u]&&
               overlay_image.pixels[i+2u]==encoded_image.pixels[i+2u])++matching_pixels;
        require(matching_pixels*1000u>total_pixels*998u,
                cfg.id+": overlay covers only "+std::to_string(matching_pixels)+"/"+
                    std::to_string(total_pixels)+" page pixels");
        size_t center_matches=0u,center_pixels=0u;
        unsigned center_x=encoded_image.width/2u,center_y=encoded_image.height/2u;
        unsigned center_radius=MetadataTile::TILE_SIDE/2u;
        for(unsigned y=0u;y<encoded_image.height;++y){
            unsigned horizontal_run=0u;
            for(unsigned x=0u;x<encoded_image.width;++x){
                size_t pixel=(static_cast<size_t>(y)*encoded_image.width+x)*3u;
                bool matches=overlay_image.pixels[pixel]==encoded_image.pixels[pixel]&&
                    overlay_image.pixels[pixel+1u]==encoded_image.pixels[pixel+1u]&&
                    overlay_image.pixels[pixel+2u]==encoded_image.pixels[pixel+2u];
                if(matches){
                    horizontal_run=0u;
                    vertical_runs[x]=0u;
                }else{
                    ++horizontal_run;
                    ++vertical_runs[x];
                    maximum_horizontal_run=std::max(maximum_horizontal_run,horizontal_run);
                    maximum_vertical_run=std::max(maximum_vertical_run,vertical_runs[x]);
                }
                if(x+center_radius>=center_x&&x<center_x+center_radius&&
                   y+center_radius>=center_y&&y<center_y+center_radius){
                    ++center_pixels;
                    if(matches)++center_matches;
                }
            }
        }
        require(center_matches*1000u>center_pixels*998u,
                cfg.id+": overlay does not cover the center area ("+
                    std::to_string(center_matches)+"/"+std::to_string(center_pixels)+")");
        require(maximum_horizontal_run<=8u&&maximum_vertical_run<=8u,
                cfg.id+": non-overlay data has a horizontal or vertical run longer than 8 pixels");
    }
    std::vector<std::pair<u64,std::string>> indexed;
    TileSummary baseline;
    if(cfg.without_metadata){require(unordered.size()==1,cfg.id+": expected one metadata-free page");indexed.push_back({1,unordered.front()});}
    else for(size_t i=0;i<unordered.size();++i){
        TileSummary summary=read_tile_summary(unordered[i],cfg.width,cfg.height);
        require(summary.page_count==unordered.size()&&summary.selector_present&&summary.overlay_budget>0&&
                summary.selector_total==summary.block_count*summary.overlay_budget,
                cfg.id+": invalid per-page selector reserve (pages="+std::to_string(summary.page_count)+
                    ", selector="+std::to_string(summary.selector_present)+
                    ", overlay="+std::to_string(summary.overlay_budget)+
                    ", blocks="+std::to_string(summary.block_count)+")");
        if(i==0)baseline=summary;
        else require(summary.page_count==baseline.page_count&&summary.block_count==baseline.block_count&&
                summary.parity==baseline.parity&&summary.unknown_budget==baseline.unknown_budget&&
                summary.overlay_budget==baseline.overlay_budget&&summary.selector_seed==baseline.selector_seed,
                cfg.id+": selector metadata differs across pages");
        indexed.push_back({summary.page_index,unordered[i]});
    }
    std::sort(indexed.begin(),indexed.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<std::string> ordered;for(size_t i=0;i<indexed.size();++i){require(indexed[i].first==i+1,cfg.id+": serialized page index gap");ordered.push_back(indexed[i].second);}
    std::vector<std::string> decode_extra={};
    if(cfg.without_metadata){decode_extra.insert(decode_extra.end(),{"--no-metadata","--no-fiducials","--page-width="+std::to_string(cfg.width),"--page-height="+std::to_string(cfg.height),"--palette",cfg.palette});}
    if(cfg.encrypted)decode_extra.push_back("--password-cli=scatter-suite-password");
    std::vector<std::string> decode_args=decode_file_args(ordered,decoded,decode_extra);
    auto result=run_cli(work,decode_args,"decode");expect_success(result,cfg.id+" decode");

    compare_files(payload,decoded/"payload.bin",cfg.id);
    if(cfg.encrypted){
        fs::path wrong_password_output=work/"wrong_password";
        fs::create_directories(wrong_password_output);
        std::vector<std::string> wrong_password_options={"--no-metadata","--no-fiducials",
            "--page-width="+std::to_string(cfg.width),"--page-height="+std::to_string(cfg.height),
            "--palette",cfg.palette,"--password-cli=incorrect-scatter-password"};
        auto wrong_password=run_cli(work,decode_file_args(ordered,wrong_password_output,
            wrong_password_options),"wrong_password");
        expect_failure(wrong_password,cfg.id+" wrong password");
    }
    if(cfg.without_metadata){
        fs::path wrong_grid_output=work/"wrong_grid";
        fs::create_directories(wrong_grid_output);
        std::vector<std::string> wrong_grid_options={"--no-metadata","--no-fiducials",
            "--page-width="+std::to_string(cfg.width+1u),"--page-height="+std::to_string(cfg.height),
            "--palette",cfg.palette};
        if(cfg.encrypted)wrong_grid_options.push_back("--password-cli=scatter-suite-password");
        auto wrong_grid=run_cli(work,decode_file_args(ordered,wrong_grid_output,wrong_grid_options),"wrong_grid");
        expect_failure(wrong_grid,cfg.id+" wrong grid dimensions");
    }
    if(cfg.without_fiducials){
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
    require(ppm_extract_frame_bits(input,mapping,frame,bits,state,false,(bool*)0,1u,
        &erasures,&seed,&selectors,&unknown,&overlay,(bool*)0,false,(u64*)0,
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
    write_gray_ppm(scan,278,278,gray);
    auto result=run_cli(work,decode_file_args({scan.string()},decoded),"decode");expect_success(result,id+" decode");compare_files(payload,decoded/"makocode.bin",id);
}

static void decode_scale_318_case(){
    const std::string id="0072_decode_scale_318";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);
    write_file(payload,"0");auto encoded=run_cli(work,encode_file_args(payload,pages,id,{"--page-width=160","--page-height=160"}),"encode");expect_success(encoded,id+" encode");auto source=ppm_paths(pages,id);require(source.size()==1,id+": expected one page");
    fs::path scan=work/"scaled.ppm";expect_success(run_transform(work,{"transform","--input",source[0],"--output",scan.string(),"--scale-x","3.18","--scale-y","3.18"},"scale"),id+" scale");
    auto result=run_cli(work,decode_file_args({scan.string()},decoded),"decode");expect_success(result,id+" decode");compare_files(payload,decoded/"payload.bin",id);
}

static void small_scan_case(){
    const std::string id="0080_decode_small_scan";fs::path work=make_work_dir(id),payload=work/"makocode.bin",pages=work/"pages";fs::create_directories(pages);write_file(payload,"makocode");
    auto encoded=run_cli(work,encode_file_args(payload,pages,"small_scan",{"--ecc=0.1","--page-width=80","--page-height=80"}),"encode");expect_success(encoded,id+" encode");auto source=ppm_paths(pages,"small_scan");require(source.size()==1,id+": expected one source page");
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
            for(const auto& variant:std::vector<std::pair<std::string,fs::path>>{{"hard",hard_path},{"soft",soft_path}}){fs::path out=work/("out_"+variant.first);auto d=run_cli(work,decode_file_args({variant.second.string()},out,{}),variant.first);expect_success(d,id+" "+variant.first);compare_files(payload,out/"makocode.bin",id+" "+variant.first);}
        }else{fs::path scan=work/"5x_scan.ppm",out=work/"out5";write_gray_ppm(scan,cw,ch,canvas);auto d=run_cli(work,decode_file_args({scan.string()},out),"decode5");expect_success(d,id+" 5x");compare_files(payload,out/"makocode.bin",id+" 5x");}
    }
}

static void current_metadata_case(){
    const std::string id="0087_decode_current_metadata";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);write_file(payload,"x");
    auto enc=run_cli(work,encode_file_args(payload,pages,"page",{"--page-width=80","--page-height=80","--palette","White Black"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"page");require(ppms.size()==1,id+": expected page");
    MetadataTile::Values tile=extract_tile_values(ppms.front());PpmImage image=read_ppm(ppms.front());
    const unsigned tile_x=(image.width-48)/2,tile_y=(image.height-48)/2;unsigned cell=tile.metadata_length_prefix_bits+128;
    makocode::ByteBuffer tile_bits,active_mask;
    require(MetadataTile::build_tile_bits(tile,tile_bits,&active_mask),id+": could not rebuild active metadata mask");
    require(cell>=tile.metadata_length_prefix_bits&&
                cell<tile.metadata_length_prefix_bits+tile.metadata_symbol_count,
            id+": damage target is outside the metadata codeword");
    unsigned damaged_local_x=tile.metadata_square_x0+cell%tile.metadata_square_side;
    unsigned damaged_local_y=tile.metadata_square_y0+cell/tile.metadata_square_side;
    require(active_mask.data[(size_t)damaged_local_y*48u+damaged_local_x]!=0u,
            id+": damage target is not an active metadata symbol");
    unsigned ring_dark=0,ring_light=0,one_cell_dark=0,one_cell_light=0,two_cell_dark=0;
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<48;++x){
        if(active_mask.data[static_cast<size_t>(y)*48u+x])continue;
        bool one_cell_border=false,two_cell_border=false;
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
            int nx=(int)x+dx,ny=(int)y+dy;
            if(nx<0||ny<0||nx>=48||ny>=48||!active_mask.data[(size_t)ny*48u+(size_t)nx])continue;
            int distance=std::max(std::abs(dx),std::abs(dy));
            if(distance==1)one_cell_border=true;
            else if(distance==2)two_cell_border=true;
        }
        size_t at=(static_cast<size_t>(tile_y+y)*image.width+tile_x+x)*3u;
        double luminance=MetadataTile::rgb_luminance_u8(static_cast<unsigned char>(image.pixels[at]),static_cast<unsigned char>(image.pixels[at+1]),static_cast<unsigned char>(image.pixels[at+2]));
        if(one_cell_border){
            require(luminance<1.0||luminance>=254.0,
                    id+": metadata one-cell border contains a non-palette color at "+std::to_string(x)+","+std::to_string(y));
            if(luminance<1.0)++one_cell_dark;else ++one_cell_light;
        }else if(two_cell_border&&luminance<128.0)++two_cell_dark;
        if(luminance<128.0)++ring_dark;else ++ring_light;
    }
    require(one_cell_dark>5u&&one_cell_light>5u&&ring_dark>10u&&ring_light>100u&&two_cell_dark>0u,
            id+": metadata border or surrounding palette noise is not balanced");
    for(u32 bit=0u;bit<32u;++bit){
        unsigned x=tile_x+MetadataTile::tag_marker_x0()+(bit&7u);
        unsigned y=tile_y+tile.metadata_tag_y0+1u+bit/8u;
        size_t at=(static_cast<size_t>(y)*image.width+x)*3u;
        static const char expected_marker_bits[]="11010001101111110011111101000101";
        bool marker_bit=expected_marker_bits[bit]=='1';
        unsigned char expected=marker_bit?0u:255u;
        require(static_cast<unsigned char>(image.pixels[at])==expected&&
                    static_cast<unsigned char>(image.pixels[at+1u])==expected&&
                    static_cast<unsigned char>(image.pixels[at+2u])==expected,
                id+": MAKO/CODE marker does not use the required fixed color");
    }
    PpmImage damaged_image=image;
    for(size_t pixel=0;pixel<damaged_image.pixels.size();pixel+=3u){
        if(static_cast<unsigned char>(damaged_image.pixels[pixel])==0u&&
           static_cast<unsigned char>(damaged_image.pixels[pixel+1u])==0u&&
           static_cast<unsigned char>(damaged_image.pixels[pixel+2u])==0u){
            damaged_image.pixels[pixel]=damaged_image.pixels[pixel+1u]=damaged_image.pixels[pixel+2u]=static_cast<char>(100);
        }
    }
    unsigned x=tile_x+tile.metadata_square_x0+cell%tile.metadata_square_side,y=tile_y+tile.metadata_square_y0+cell/tile.metadata_square_side;
    size_t index=(static_cast<size_t>(y)*damaged_image.width+x)*3;require(index+2<damaged_image.pixels.size(),id+": target module outside page");
    unsigned char original=static_cast<unsigned char>(damaged_image.pixels[index]);require(original==100||original==255,id+": target module not a scanned palette color");
    unsigned char flipped=original==100?255:100;for(int c=0;c<3;++c)damaged_image.pixels[index+c]=static_cast<char>(flipped);fs::path damaged=work/"damaged.ppm";write_file(damaged,"P6\n80 80\n255\n"+damaged_image.pixels);
    auto result=run_cli(work,decode_file_args({damaged.string()},decoded),"decode");expect_success(result,id+" damaged metadata decode");compare_files(payload,decoded/"payload.bin",id);
    PpmImage no_tile=read_ppm(ppms.front());for(unsigned yy=tile_y;yy<tile_y+48;++yy)for(unsigned xx=tile_x;xx<tile_x+48;++xx){size_t at=(static_cast<size_t>(yy)*no_tile.width+xx)*3;no_tile.pixels[at]=no_tile.pixels[at+1]=no_tile.pixels[at+2]=127;}
    fs::path absent=work/"no_bootstrap.ppm";write_file(absent,"P6\n80 80\n255\n"+no_tile.pixels);auto failed=run_cli(work,decode_file_args({absent.string()},work/"no_bootstrap_out"),"no_bootstrap");expect_failure(failed,id+" absent metadata");
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
        unsigned dims=large?500u:220u;auto enc=run_cli(work,{"encode","--output-dir="+encoded_dir.string(),"--prefix="+prefix,"--page-width="+std::to_string(dims),"--page-height="+std::to_string(dims),"--no-fiducials","--no-metadata","--palette",large?"White Black":palettes[i],"--ecc=3.5"},"encode_"+std::to_string(i),payload);expect_success(enc,id+" encode");
        auto ppms=ppm_paths(encoded_dir,prefix);require(ppms.size()==1,id+": expected one-page fixture");PpmImage image=read_ppm(ppms.front());require(image.width==dims&&image.height==dims,id+": page dimensions changed");
        const int center=static_cast<int>(dims/2),radius=37;unsigned changed=0;for(int y=0;y<static_cast<int>(dims);++y)for(int x=0;x<static_cast<int>(dims);++x)if((x-center)*(x-center)+(y-center)*(y-center)<=radius*radius){size_t at=(static_cast<size_t>(y)*dims+x)*3;image.pixels[at]=image.pixels[at+1]=image.pixels[at+2]=0;++changed;}
        require(changed>4000,id+": metadata damage circle too small");write_file(damaged,"P6\n"+std::to_string(dims)+" "+std::to_string(dims)+"\n255\n"+image.pixels);
        std::vector<std::string> decode_args={"decode","--decoded-stdout","--no-metadata","--no-fiducials","--page-width="+std::to_string(dims),"--page-height="+std::to_string(dims),"--palette",large?"White Black":palettes[i],damaged.string()};
        auto decoded=run_cli(work,decode_args,"decode_"+std::to_string(i));expect_success(decoded,id+" decode");
        std::string expected=read_file(payload);std::vector<unsigned char> expected_bytes(expected.begin(),expected.end());check_decoded(decoded.out,expected_bytes,0.0,id);
        if(!large&&i==0u){
            auto password_args=decode_args;password_args.insert(password_args.end()-1,"--password-cli=ignored");auto password_decoded=run_cli(work,password_args,"password_ignored_decode");
            expect_success(password_decoded,id+" plaintext password decode");
            check_decoded(password_decoded.out,expected_bytes,0.0,id+" plaintext password");
        }
    }
}

static void missing_pages_case(){
    const std::string id="0115_decode_missing_pages";fs::path work=make_work_dir(id);fs::path payload=work/"payload.bin",out=work/"out";unsigned seed=2110;generate_bytes(work,payload,32768,seed,"payload");
    auto enc=run_cli(work,{"encode","--output-dir="+work.string(),"--prefix=page","--page-width=400","--page-height=400","--ecc=0.8"},"encode",payload);expect_success(enc,id+" encode");auto pages=ppm_paths(work,"page");require(pages.size()==4,id+": expected four pages");
    for(const auto& item:std::vector<std::pair<std::string,std::vector<std::string>>>{{"middle",{pages[0],pages[2],pages[3]}},{"shuffled",{pages[3],pages[0],pages[2]}},{"first",{pages[1],pages[2],pages[3]}},{"last",{pages[0],pages[1],pages[2]}}}){
        std::vector<std::string> args={"decode","--decoded-stdout"};args.insert(args.end(),item.second.begin(),item.second.end());auto decoded=run_cli(work,args,item.first);
        expect_success(decoded,id+" "+item.first);std::string bytes=read_file(payload);check_decoded(decoded.out,std::vector<unsigned char>(bytes.begin(),bytes.end()),0.0,id+" "+item.first);
        if(item.first=="first"){
            std::vector<std::string> ignored_password={"decode","--decoded-stdout","--password-cli=unused-password"};ignored_password.insert(ignored_password.end(),item.second.begin(),item.second.end());
            auto password_decode=run_cli(work,ignored_password,"first_page_missing_ignored_password");expect_success(password_decode,id+" first page missing with ignored password");
            require((password_decode.out+password_decode.err).find("decode: warning: payload was not encrypted; password ignored")!=std::string::npos,id+": missing-page ignored-password warning missing");
            check_decoded(password_decode.out,std::vector<unsigned char>(bytes.begin(),bytes.end()),0.0,id+" ignored password");
        }
    }
    fs::path stdin_dir=work/"stdin_pages",stdin_payload=work/"stdin_payload.bin";fs::create_directories(stdin_dir);generate_bytes(work,stdin_payload,8192,2111,"stdin_payload");
    auto stdin_enc=run_cli(work,{"encode","--output-dir="+stdin_dir.string(),"--prefix=stdin","--page-width=400","--page-height=400","--ecc=2.0"},"stdin_encode",stdin_payload);expect_success(stdin_enc,id+" stdin encode");
    auto stdin_pages=ppm_paths(stdin_dir,"stdin");require(stdin_pages.size()==2,id+": expected two stdin fixture pages");auto first=run_cli(work,{"decode","--decoded-stdout"},"stdin_decode",stdin_pages.front());expect_success(first,id+" stdin decode");std::string expected=read_file(stdin_payload);check_decoded(first.out,std::vector<unsigned char>(expected.begin(),expected.end()),0.0,id+" stdin");
}

static void orientation_scan_case(){
    const std::string id="0117_decode_orientation_scan";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages";fs::create_directories(pages);write_file(payload,"orientation scan round-trip payload");
    const std::map<std::string,std::string> env{{"MAKOCODE_TEST_SEED","123456789"}};
    auto enc=run_cli(work,encode_file_args(payload,pages,"source",{"--page-width=80","--page-height=80","--ecc=0.85"}),"encode",{},env);expect_success(enc,id+" encode");auto source=ppm_paths(pages,"source");require(source.size()==1,id+": expected source page");PpmImage image=read_ppm(source.front());
    const std::array<std::array<double,3>,10> variants={{{0,0,0},{90,0,0},{180,0,0},{270,0,0},{0,1,0},{0,0,1},{91,0,0},{181,0,0},{271,0,0},{1,1,1}}};
    for(size_t i=0;i<variants.size();++i){fs::path scan=work/("scan_"+std::to_string(i)+".ppm"),out=work/("decoded_"+std::to_string(i));make_oriented_scan(image,scan,variants[i][0],variants[i][1]!=0,variants[i][2]!=0);
        auto decoded=run_cli(work,decode_file_args({scan.string()},out),"decode_"+std::to_string(i),{},env);expect_success(decoded,id+" orientation "+std::to_string(i));compare_files(payload,out/"payload.bin",id);}
}

static void scan_registration_noise_case(){
    const std::string id="0088_decode_scan_registration";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages";fs::create_directories(pages);write_file(payload,"registration regression");
    auto enc=run_cli(work,encode_file_args(payload,pages,"page",{"--ecc=0.02","--page-width=80","--page-height=80"}),"encode");expect_success(enc,id+" encode");auto source=ppm_paths(pages,"page");require(source.size()==1,id+": expected one source page");
    const double pitch=2.75,ox=2.5,oy=2.0;fs::path scan=work/"scan.ppm";make_affine_scan(source.front(),scan,225,226,pitch,ox,oy,30,200,false,false);
    PpmImage img=read_ppm(scan);std::set<std::pair<int,int>> centers;for(int y=0;y<80;++y)for(int x=0;x<80;++x)centers.insert({static_cast<int>(std::floor((x+.5)*pitch+ox)),static_cast<int>(std::floor((y+.5)*pitch+oy))});
    PythonRandom rng(0);for(unsigned y=0;y<img.height;++y)for(unsigned x=0;x<img.width;++x){if(centers.count({static_cast<int>(x),static_cast<int>(y)})==0&&rng.random()<.012){size_t at=(static_cast<size_t>(y)*img.width+x)*3;img.pixels[at]=img.pixels[at+1]=img.pixels[at+2]=0;}}
    write_file(scan,"P6\n"+std::to_string(img.width)+" "+std::to_string(img.height)+"\n255\n"+img.pixels);
    auto control=run_cli(work,{"decode","--decoded-stdout",source.front()},"control"),actual=run_cli(work,{"decode","--decoded-stdout",scan.string()},"scan");expect_success(control,id+" control");expect_success(actual,id+" scan");std::string expected=read_file(payload);std::vector<unsigned char> expected_bytes(expected.begin(),expected.end());check_decoded(actual.out,expected_bytes,0.0,id+" scan");
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
    fs::path output=work/"damaged_out";std::vector<std::string> args={"decode","--output-dir="+output.string()};args.push_back(damaged.string());auto dec=run_cli(work,args,"damaged_decode");expect_success(dec,id+" damaged scan");compare_files(payload,output/payload_name,id+" damaged scan");
}

static void lowres_rotated_scan_case(){
    const std::string id="0085_decode_lowres_rotated_scan";fs::path work=make_work_dir(id),payload=work/"makocode.bin",pages=work/"encoded";fs::create_directories(pages);write_file(payload,"makocode");
    auto enc=run_cli(work,encode_file_args(payload,pages,"source",{"--page-width=80","--page-height=80","--ecc=fill"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"source");require(ppms.size()==1,id+": expected source page");PpmImage src=read_ppm(ppms.front());
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
    auto enc=run_cli(work,encode_file_args(payload,encoded,"length_header",{"--page-width=240","--page-height=240","--ecc=fill"}),"encode");expect_success(enc,id+" encode");auto pages=ppm_paths(encoded,"length_header");require(pages.size()==1,id+": expected page");
    PpmImage original=read_ppm(pages.front());require(original.width==240&&original.height==240,id+": encoded dimensions changed");PpmImage damaged=original;size_t target=(4u*3u);unsigned char cell=static_cast<unsigned char>(damaged.pixels[target]);require(cell==0||cell==255,id+": first payload cell not binary");for(unsigned c=0;c<3;++c)damaged.pixels[target+c]=static_cast<char>(255-cell);fs::path damaged_source=work/"damaged_source.ppm";write_file(damaged_source,"P6\n240 240\n255\n"+damaged.pixels);
    const double scale=2.018181818;fs::path clean_scaled=work/"clean_scaled.ppm",bad_scaled=work/"bad_scaled.ppm";
    for(const auto& item:std::vector<std::pair<fs::path,fs::path>>{{pages[0],clean_scaled},{damaged_source,bad_scaled}})expect_success(run_transform(work,{"transform","--input",item.first.string(),"--output",item.second.string(),"--scale-x",std::to_string(scale),"--scale-y",std::to_string(scale)},"scale"),id+" scale");
    auto soften=[&](const fs::path& input,const fs::path& output){PpmImage img=read_ppm(input);require(img.width==484&&img.height==484,id+": scale render dimensions changed");for(size_t i=0;i<img.pixels.size();++i){unsigned char value=static_cast<unsigned char>(img.pixels[i]);img.pixels[i]=static_cast<char>(30+std::floor(.8*value+.5));}write_file(output,"P6\n484 484\n255\n"+img.pixels);};
    fs::path clean=work/"clean_scan.ppm",bad=work/"damaged_scan.ppm",alias=work/"alias_scan.ppm",center=work/"center_scan.ppm";soften(clean_scaled,clean);soften(bad_scaled,bad);PpmImage aliased=read_ppm(clean);PpmImage center_damaged=aliased;
    double center_sx=scale*(4.5)-.5,center_sy=scale*.5-.5;int center_x=static_cast<int>(center_sx+.5),center_y=static_cast<int>(center_sy+.5);size_t center_at=(static_cast<size_t>(center_y)*484u+static_cast<unsigned>(center_x))*3u;unsigned char opposite_center=static_cast<unsigned char>(center_damaged.pixels[center_at])<128?234:30;for(unsigned c=0;c<3u;++c)center_damaged.pixels[center_at+c]=static_cast<char>(opposite_center);write_file(center,"P6\n484 484\n255\n"+center_damaged.pixels);
    const std::array<std::pair<unsigned,unsigned>,6> cells={{{0,3},{9,3},{2,5},{10,6},{0,8},{3,10}}};for(auto cell_xy:cells){double sx=scale*(cell_xy.first+.5)-.5,sy=scale*(cell_xy.second+.5)-.5;int x0=static_cast<int>(sx),y0=static_cast<int>(sy),nx=static_cast<int>(sx+.5),ny=static_cast<int>(sy+.5);size_t center=(static_cast<size_t>(ny)*484+nx)*3;unsigned char opposite=static_cast<unsigned char>(aliased.pixels[center])<128?234:30;
        for(int y:{y0,y0+1})for(int x:{x0,x0+1})if(x!=nx||y!=ny){size_t at=(static_cast<size_t>(y)*484+x)*3;aliased.pixels[at]=aliased.pixels[at+1]=aliased.pixels[at+2]=static_cast<char>(opposite);}}
    write_file(alias,"P6\n484 484\n255\n"+aliased.pixels);
    for(const auto& item:std::vector<std::pair<std::string,fs::path>>{{"clean",clean},{"damaged",bad},{"alias",alias},{"center",center}}){fs::path out=work/("decoded_"+item.first);auto dec=run_cli(work,decode_file_args({item.second.string()},out),"decode_"+item.first);expect_success(dec,id+" "+item.first);compare_files(payload,out/"makocode.bin",id+" "+item.first);}
}

static void palette_q2_center_speck_case(){
    const std::string id="0132_palette_q2_center_specks";const unsigned width=180u,height=180u;const double scale=2.5;fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded=work/"encoded",overlay=work/"overlay.ppm",scan=work/"specked_scan.ppm",decoded=work/"decoded";fs::create_directories(encoded);generate_bytes(work,payload,128u,613947u,"payload");write_file(overlay,"P6\n180 180\n255\n"+std::string((usize)width*height*3u,'\0'));
    auto enc=run_cli(work,encode_file_args(payload,encoded,"page",{"--page-width="+std::to_string(width),"--page-height="+std::to_string(height),"--ecc=0.02","--overlay-fraction=0.012","--overlay-ppm="+overlay.string(),"--overlay-pages=all","--palette","Black White"}),"encode");expect_success(enc,id+" encode");auto pages=ppm_paths(encoded,"page");require(pages.size()==1u,id+": expected one page");PpmImage original=read_ppm(pages.front());require(original.width==width&&original.height==height,id+": encoded grid changed");
    makocode::PaletteSymbolProfile q2_profile;require(makocode::palette_symbol_profile(2u,q2_profile)&&q2_profile.field_order==256u&&q2_profile.block_length==255u,id+": q2 did not use the full GF(2^8) profile");MetadataTile::Values rank_values=extract_tile_values(pages.front());u32 block_length=(u32)rank_values.ecc_block_data+rank_values.ecc_parity;u64 bootstrap_symbols=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS;u64 source_symbol_count=makocode::palette_symbol_count_for_bytes(q2_profile,rank_values.ecc_original_bytes);u32 actual_last_block_length=(u32)(source_symbol_count-(rank_values.ecc_block_count-1u)*rank_values.ecc_block_data)+rank_values.ecc_parity;u64 expected_headed_bits=(bootstrap_symbols+source_symbol_count+rank_values.ecc_block_count*(u64)rank_values.ecc_parity)*32u;require(rank_values.ecc_parity==8u&&block_length==(u32)source_symbol_count+rank_values.ecc_parity&&block_length<q2_profile.block_length&&rank_values.ecc_block_data==source_symbol_count&&rank_values.unknown_parity_budget==2u&&rank_values.overlay_erasure_budget==3u&&rank_values.overlay_selector_present&&rank_values.ecc_block_count==1u&&rank_values.page_bits==expected_headed_bits,id+": overlay fixture did not use a shortened q2 block with P=8, O=3, E=2");u8 selector_mask[makocode::PALETTE_SYMBOL_MAX_BLOCK_LENGTH]={};require(makocode::palette_symbol_selector_mask(rank_values.overlay_selector_seed,0u,block_length,rank_values.overlay_erasure_budget,selector_mask),id+": selector mask failed");MetadataTile::Placement placement=MetadataTile::compute_center_tile_placement(width,height);makocode::ByteBuffer tile_bits,tile_active_mask;u8 guard_mask[MetadataTile::TILE_SIDE*MetadataTile::TILE_SIDE]={};require(MetadataTile::build_tile_bits(rank_values,tile_bits,&tile_active_mask)&&tile_active_mask.size==sizeof(guard_mask)&&MetadataTile::build_active_guard_mask(tile_active_mask.data,tile_active_mask.size,guard_mask,sizeof(guard_mask)),id+": metadata reservation mask failed");
    makocode::ByteBuffer reserved,rank_map;u64 payload_pixels=0u,reserved_pixels=0u;usize pixel_count=(usize)width*height;u32 fiducial_columns=0u,fiducial_rows=0u;compute_centered_fiducial_dimensions(width,height,24u,fiducial_columns,fiducial_rows);require(compute_fiducial_marker_mask(width,height,height,reserved_pixels,&reserved,true,fiducial_columns,fiducial_rows,24u),id+": fiducial reservation mask failed");for(unsigned y=0u;y<MetadataTile::TILE_SIDE;++y)for(unsigned x=0u;x<MetadataTile::TILE_SIDE;++x){if(!guard_mask[(usize)y*MetadataTile::TILE_SIDE+x])continue;u32 px=placement.x0+x-placement.offset_x,py=placement.y0+y-placement.offset_y;reserved.data[(usize)py*width+px]=1u;}require(MetadataTile::build_payload_rank_map(width,height,placement,rank_values,reserved.data,reserved.size,rank_map,payload_pixels,false,makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED),id+": payload rank map failed");
    std::vector<usize> selected_pixels;std::vector<u32> selected_positions;const u64* ranks=(const u64*)rank_map.data;for(usize pixel=0u;pixel<pixel_count&&selected_pixels.size()<5u;++pixel){u64 rank=ranks[pixel];if(rank==U64_MAX_VALUE||(rank&7u)!=0u)continue;u64 symbol=rank/8u;if(symbol<bootstrap_symbols)continue;u64 interleaved_offset=symbol-bootstrap_symbols;if(interleaved_offset>=actual_last_block_length*rank_values.ecc_block_count||interleaved_offset%rank_values.ecc_block_count!=0u)continue;u64 position=interleaved_offset/rank_values.ecc_block_count;if(position>=block_length||selector_mask[position])continue;usize at=pixel*3u;if((u8)original.pixels[at]==255u&&(u8)original.pixels[at+1u]==255u&&(u8)original.pixels[at+2u]==255u){selected_pixels.push_back(pixel);selected_positions.push_back((u32)position);}}require(selected_pixels.size()==5u&&std::set<u32>(selected_positions.begin(),selected_positions.end()).size()==5u,id+": did not find five non-selector white dots in distinct symbols of one deinterleaved RS block");
    PpmImage image;image.width=450u;image.height=450u;image.pixels.resize((usize)image.width*image.height*3u);for(unsigned y=0u;y<image.height;++y)for(unsigned x=0u;x<image.width;++x){unsigned source_x=(unsigned)((u64)x*2u/5u),source_y=(unsigned)((u64)y*2u/5u);usize source_at=((usize)source_y*width+source_x)*3u,dest_at=((usize)y*image.width+x)*3u;memcpy(image.pixels.data()+dest_at,original.pixels.data()+source_at,3u);}
    for(usize pixel:selected_pixels){unsigned x=(unsigned)(pixel%width),y=(unsigned)(pixel/width);unsigned scan_x=(unsigned)floor(scale*((double)x+.5)),scan_y=(unsigned)floor(scale*((double)y+.5));usize at=((usize)scan_y*image.width+scan_x)*3u;require((u8)image.pixels[at]==255u,id+": chosen center was not white after scaling");image.pixels[at]=image.pixels[at+1u]=image.pixels[at+2u]=0;}
    write_file(scan,"P6\n450 450\n255\n"+image.pixels);auto dec=run_cli(work,decode_file_args({scan},decoded,{"--palette","Black White"}),"decode");expect_success(dec,id+": five center specks plus O=3 exceeded the parity budget; decoding requires all five center disagreements to be erased");std::smatch corrected_match;std::string decode_log=dec.out+dec.err;u64 expected_corrections=(u64)rank_values.overlay_erasure_budget*rank_values.ecc_block_count+5u;require(std::regex_search(decode_log,corrected_match,std::regex("ECC detected ([0-9]+) symbol errors"))&&std::stoull(corrected_match[1].str())==expected_corrections,id+": corrected-symbol count does not include all five center-speck erasures");compare_files(payload,decoded/"payload.bin",id+" erasure recovery");
}

static void erased_metadata_scan_case(const std::string& id,int kind){
    fs::path work=make_work_dir(id),payload=work/"payload.bin",encoded=work/"encoded";fs::create_directories(encoded);std::string bytes=kind==0?"partially damaged marker scan payload":kind==1?"erased marker orientation scan payload":"erased metadata length row scan payload";write_file(payload,bytes);
    auto enc=run_cli(work,encode_file_args(payload,encoded,"source",{"--page-width=80","--page-height=80"}),"encode");expect_success(enc,id+" encode");auto pages=ppm_paths(encoded,"source");require(pages.size()==1,id+": expected one encoded page");MetadataTile::Values tile=extract_tile_values(pages[0]);
    PpmImage source=read_ppm(pages[0]);unsigned tile_x=(source.width-48)/2,tile_y=(source.height-48)/2;
    if(kind==0){
        require(tile.palette_count>=2u,id+": marker palette missing");
        std::vector<unsigned> one_bits;
        static const char expected_marker_bits[]="11010001101111110011111101000101";
        for(unsigned bit=0;bit<32;++bit)if(expected_marker_bits[bit]=='1')one_bits.push_back(bit);
        require(one_bits.size()==20u,id+": marker code weight changed");
        for(size_t i=0;i<16u;++i){unsigned bit=one_bits[i],x=tile_x+MetadataTile::tag_marker_x0()+(bit&7),y=tile_y+tile.metadata_tag_y0+1+bit/8;size_t at=(static_cast<size_t>(y)*source.width+x)*3;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(255);}
        unsigned matches=0;for(unsigned bit=0;bit<32;++bit){unsigned x=tile_x+MetadataTile::tag_marker_x0()+(bit&7),y=tile_y+tile.metadata_tag_y0+1+bit/8;size_t at=(static_cast<size_t>(y)*source.width+x)*3;unsigned char pixel=static_cast<unsigned char>(source.pixels[at]);bool observed=pixel==0u&&static_cast<unsigned char>(source.pixels[at+1])==0u&&static_cast<unsigned char>(source.pixels[at+2])==0u,expected=expected_marker_bits[bit]=='1';matches+=observed==expected;}require(matches==16u,id+": partial marker fixture does not leave half the bits matched");
        MetadataTile::AffineParams exact_grid;exact_grid.pitch_pixels=1.0;exact_grid.pitch_x_pixels=1.0;exact_grid.pitch_y_pixels=1.0;exact_grid.angle_rad=0.0;exact_grid.center_x=((double)source.width-1.0)*0.5;exact_grid.center_y=((double)source.height-1.0)*0.5;
        MetadataTile::Values partial_marker_values;makocode::ByteBuffer partial_marker_palette;double partial_marker_border=1.0,partial_marker_alignment=0.0;
        require(MetadataTile::decode_tile_affine((const u8*)source.pixels.data(),source.width,source.height,source.height,exact_grid,partial_marker_values,partial_marker_palette,&partial_marker_border,false,&partial_marker_alignment,false,16u,false),id+": metadata ECC rejected a tile with exactly half its marker bits damaged");
        require((u32)partial_marker_values.encoded_tag==MetadataTile::ASCII_LOW_NIBBLE_MARKER&&partial_marker_values.page_bits==tile.page_bits&&partial_marker_values.payload_bytes==tile.payload_bytes&&partial_marker_values.page_width_pixels==source.width,id+": partial-marker metadata ECC decoded incorrect page fields");
    }else if(kind==1){for(unsigned row=0;row<4;++row)for(unsigned col=0;col<8;++col){size_t at=(static_cast<size_t>(tile_y+tile.metadata_tag_y0+1+row)*source.width+tile_x+20+col)*3;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(255);}}
    else{u64 metadata_cells=tile.metadata_length_prefix_bits+tile.metadata_symbol_count;unsigned value_bits=0;for(u64 n=metadata_cells;n;n>>=1)++value_bits;unsigned length_bits=1;for(unsigned remaining=value_bits-1;remaining>1;remaining>>=1)++length_bits;unsigned outer=2+length_bits+value_bits;unsigned x0=tile_x+(48-outer)/2,y=tile_y+tile.metadata_tag_y0+5;
        unsigned changed=0;for(unsigned col=0;col<outer;++col){size_t at=(static_cast<size_t>(y)*source.width+x0+col)*3;if(static_cast<unsigned char>(source.pixels[at])!=255)++changed;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(255);}require(changed>0,id+": length row fixture changed no cells");}
    const std::vector<std::array<double,3>> variants=kind==0?std::vector<std::array<double,3>>{{{0,0,0}}}:kind==1?std::vector<std::array<double,3>>{{{90,0,0},{180,0,0},{270,0,0},{0,1,0},{0,0,1}}}:std::vector<std::array<double,3>>{{{0,0,0},{90,0,0},{180,0,0},{270,0,0},{0,1,0},{0,0,1}}};
    for(size_t i=0;i<variants.size();++i){PpmImage variant=source;fs::path scan=work/("scan_"+std::to_string(i)+".ppm");write_file(work/"mutated_source.ppm","P6\n80 80\n255\n"+variant.pixels);variant=read_ppm(work/"mutated_source.ppm");make_oriented_scan(variant,scan,variants[i][0],variants[i][1]!=0,variants[i][2]!=0);
        fs::path out=work/("decoded_"+std::to_string(i));auto dec=run_cli(work,decode_file_args({scan.string()},out,{}),"decode_"+std::to_string(i),{}, {},kind==1?5000ul:0ul);require(!dec.timed_out,id+" orientation decode exceeded the 5 second fast-path limit");expect_success(dec,id+" scan "+std::to_string(i));compare_files(payload,out/"payload.bin",id+" "+std::to_string(i));}
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
    fs::path ppm_write_fail=work/"ppm_write_failure";fs::create_directories(ppm_write_fail/"page_0000.ppm");write_file(ppm_write_fail/"source.cpp","int value;\n");
    auto ppm_write_error=run_cli(ppm_write_fail,{"minify","source.cpp","--width","240","--height","80","--output","page.cpp"},"ppm_write_error");
    expect_failure(ppm_write_error,id+" PPM output write failure");require(ppm_write_error.err.find("minify: failed to write PPM output")!=std::string::npos,id+": PPM-output write diagnostic changed");
    fs::path unsupported_ppm=work/"unsupported_ppm";fs::create_directories(unsupported_ppm);write_file(unsupported_ppm/"source.cpp",std::string("int value;\n",11)+"\x01");
    auto unsupported_ppm_result=run_cli(unsupported_ppm,{"minify","source.cpp","--width","240","--height","80"},"unsupported_ppm");
    expect_failure(unsupported_ppm_result,id+" unsupported PPM character");require(unsupported_ppm_result.err.find("PPM output supports printable ASCII and LF only")!=std::string::npos,id+": unsupported PPM character diagnostic changed");
    auto small_page=run_cli(unsupported_ppm,{"minify","source.cpp","--width","5","--height","8"},"small_page");
    expect_failure(small_page,id+" undersized PPM page");require(small_page.err.find("page dimensions must fit at least one 5x8 character cell")!=std::string::npos,id+": undersized-page diagnostic changed");
    fs::path oversized_ppm=work/"oversized_ppm";fs::create_directories(oversized_ppm);write_file(oversized_ppm/"source.cpp","int value;\n");
    auto oversized_page=run_cli(oversized_ppm,{"minify","source.cpp","--width","3000000000","--height","3000000000"},"oversized_page");
    expect_failure(oversized_page,id+" oversized PPM");require(oversized_page.err.find("minify: rendered PPM is too large")!=std::string::npos,id+": oversized-PPM diagnostic changed");
    auto allocation_check=[&](const fs::path& dir,unsigned long at,const std::string& expected){auto fail=run_process({test_executable().string(),"--minify-allocation-failure",std::to_string(at)},dir,{},dir/"alloc.out",dir/"alloc.err");expect_success(fail,id+" allocation failure mode");require(fail.out.find(expected)!=std::string::npos||fail.err.find(expected)!=std::string::npos,id+": allocation diagnostic changed at "+std::to_string(at));};
    fs::path alloc=work/"alloc";fs::create_directories(alloc);write_file(alloc/"makocode.cpp","int value;\n");allocation_check(alloc,5,"minify: failed to compact whitespace");allocation_check(alloc,3,"minify: failed to read input file");allocation_check(alloc,4,"minify: failed to strip comments");allocation_check(alloc,6,"minify: failed to allocate PPM scanline");
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
    u64 quotient = 0u;
    u64 remainder = 0u;
    require(multiply_divide_u64(100u, 1u, 10u, quotient, remainder) &&
                quotient == 10u && remainder == 0u,
            "exact hidden-ratio capacity changed for 100 bytes at 0.1");
    require(multiply_divide_u64(101u, 1u, 10u, quotient, remainder) &&
                quotient == 10u && remainder == 1u,
            "exact hidden-ratio remainder was lost");
    require(multiply_divide_u64(U64_MAX_VALUE, U64_MAX_VALUE,
                                U64_MAX_VALUE, quotient, remainder) &&
                quotient == U64_MAX_VALUE && remainder == 0u,
            "wide multiply-divide failed at the u64 boundary");
    require(!multiply_divide_u64(U64_MAX_VALUE, 2u, 1u,
                                 quotient, remainder),
            "wide multiply-divide accepted an overflowing quotient");
    u8 pcg_key[32];
    for (u32 i = 0u; i < sizeof(pcg_key); ++i) pcg_key[i] = (u8)i;
    makocode::Pcg64Generator pcg;
    pcg.seed(0u, pcg_key);
    const u64 expected_pcg[] = {
        0x6ce0951c055e28ebull, 0x8e0e8a7d1213c156ull,
        0x62cc7147e90f621bull, 0xdd223c84d65bde5cull
    };
    for (u32 i = 0u; i < sizeof(expected_pcg) / sizeof(expected_pcg[0]); ++i) {
        require(pcg.next() == expected_pcg[i],
                "PCG64 stream changed across fixed-width builds");
    }
    makocode::PaletteSymbolProfile exact_ratio_profile;
    u32 exact_ratio_parity = 0u;
    require(makocode::palette_symbol_profile(2u, exact_ratio_profile) &&
                makocode::palette_symbol_parity_for_redundancy(
                    exact_ratio_profile, 1u, 10u, exact_ratio_parity) &&
                exact_ratio_parity == 24u,
            "exact fractional ECC parity changed");
    MetadataTile::Values rank_values;rank_values.metadata_square_side=21u;rank_values.metadata_square_x0=13u;rank_values.metadata_square_y0=20u;rank_values.metadata_tag_y0=8u;rank_values.metadata_length_prefix_bits=1u;rank_values.metadata_symbol_count=1u;
    MetadataTile::Placement rank_placement=MetadataTile::compute_center_tile_placement(80u,80u);makocode::ByteBuffer rank_mask;require(rank_mask.ensure(80u*80u),"metadata rank mask allocation failed");rank_mask.size=80u*80u;memset(rank_mask.data,0,rank_mask.size);
    u8 active_metadata_cells[MetadataTile::TILE_SIDE*MetadataTile::TILE_SIDE]={};u8 guarded_metadata_cells[MetadataTile::TILE_SIDE*MetadataTile::TILE_SIDE]={};u32 marker_local_x=MetadataTile::tag_marker_x0(),marker_local_y=rank_values.metadata_tag_y0+1u;active_metadata_cells[(usize)marker_local_y*MetadataTile::TILE_SIDE+marker_local_x]=1u;active_metadata_cells[(usize)rank_values.metadata_square_y0*MetadataTile::TILE_SIDE+rank_values.metadata_square_x0]=1u;active_metadata_cells[(usize)rank_values.metadata_square_y0*MetadataTile::TILE_SIDE+rank_values.metadata_square_x0+1u]=1u;
    require(MetadataTile::build_active_guard_mask(active_metadata_cells,sizeof(active_metadata_cells),guarded_metadata_cells,sizeof(guarded_metadata_cells)),"metadata guard mask construction failed");require(!MetadataTile::build_active_guard_mask(active_metadata_cells,sizeof(active_metadata_cells)-1u,guarded_metadata_cells,sizeof(guarded_metadata_cells)),"metadata guard mask accepted mismatched dimensions");
    i64 tile_page_x=(i64)rank_placement.x0-(i64)rank_placement.offset_x,tile_page_y=(i64)rank_placement.y0-(i64)rank_placement.offset_y;require(tile_page_x>=0&&tile_page_y>=0,"metadata tile placement is negative");for(u32 y=0u;y<MetadataTile::TILE_SIDE;++y)for(u32 x=0u;x<MetadataTile::TILE_SIDE;++x){if(!guarded_metadata_cells[(usize)y*MetadataTile::TILE_SIDE+x])continue;u32 page_x=(u32)tile_page_x+x,page_y=(u32)tile_page_y+y;rank_mask.data[(usize)page_y*80u+page_x]=1u;}
    u32 marker_x=(u32)tile_page_x+marker_local_x,marker_y=(u32)tile_page_y+marker_local_y;u32 metadata_x=(u32)tile_page_x+rank_values.metadata_square_x0,metadata_y=(u32)tile_page_y+rank_values.metadata_square_y0;
    makocode::ByteBuffer rank_map;u64 rank_pixels=0u;
    require(MetadataTile::build_payload_rank_map(80u,80u,rank_placement,rank_values,rank_mask.data,rank_mask.size,rank_map,rank_pixels,false,0u),"metadata ring rank map failed");
    const u64* ranks=(const u64*)rank_map.data;usize ring_pixel=(usize)rank_placement.y0*80u+rank_placement.x0;
    require(rank_pixels>0u&&ranks[ring_pixel]!=U64_MAX_VALUE,"inactive metadata area was not available to payload");require(ranks[(usize)marker_y*80u+marker_x]==U64_MAX_VALUE&&ranks[(usize)marker_y*80u+marker_x+1u]==U64_MAX_VALUE&&ranks[(usize)marker_y*80u+marker_x+2u]!=U64_MAX_VALUE&&ranks[(usize)metadata_y*80u+metadata_x]==U64_MAX_VALUE&&ranks[(usize)metadata_y*80u+metadata_x+1u]==U64_MAX_VALUE,"active metadata cells or their one-cell guard were not reserved from payload");
    MetadataTile::PayloadRankMapCache rank_cache;makocode::ByteBuffer cached_rank_map,reference_rank_map;u64 cached_pixels=0u,reference_pixels=0u;rank_values.palette_count=4u;
    for(u64 page_index=1u;page_index<=2u;++page_index){rank_values.page_index=page_index;require(MetadataTile::build_payload_rank_map(80u,80u,rank_placement,rank_values,rank_mask.data,rank_mask.size,reference_rank_map,reference_pixels,false,0x123456789ABCDEF0ull),"uncached page rank map failed");require(MetadataTile::build_payload_rank_map(80u,80u,rank_placement,rank_values,rank_mask.data,rank_mask.size,cached_rank_map,cached_pixels,false,0x123456789ABCDEF0ull,&rank_cache),"cached page rank map failed");require(cached_pixels==reference_pixels&&cached_rank_map.size==reference_rank_map.size&&memcmp(cached_rank_map.data,reference_rank_map.data,reference_rank_map.size)==0,"cached page rank map differs from uncached output");}
    MetadataTile::Placement shifted_placement=rank_placement;shifted_placement.x0+=1u;require(MetadataTile::build_payload_rank_map(80u,80u,shifted_placement,rank_values,rank_mask.data,rank_mask.size,reference_rank_map,reference_pixels,false,0x123456789ABCDEF0ull),"shifted uncached page rank map failed");require(MetadataTile::build_payload_rank_map(80u,80u,shifted_placement,rank_values,rank_mask.data,rank_mask.size,cached_rank_map,cached_pixels,false,0x123456789ABCDEF0ull,&rank_cache),"shifted cached page rank map failed");require(cached_pixels==reference_pixels&&cached_rank_map.size==reference_rank_map.size&&memcmp(cached_rank_map.data,reference_rank_map.data,reference_rank_map.size)==0,"placement change reused an incompatible rank map");
    MetadataTile::PayloadRankMapCache fresh_rank_cache;require(!MetadataTile::build_payload_rank_map(80u,80u,shifted_placement,rank_values,rank_mask.data,rank_mask.size,fresh_rank_cache.base_rank_map,cached_pixels,false,0x123456789ABCDEF0ull,&fresh_rank_cache),"fresh base-map alias was accepted");require(!MetadataTile::build_payload_rank_map(80u,80u,shifted_placement,rank_values,rank_mask.data,rank_mask.size,fresh_rank_cache.reserved_mask,cached_pixels,false,0x123456789ABCDEF0ull,&fresh_rank_cache),"fresh reservation-mask alias was accepted");
    MetadataTile::Placement invalid_placement=shifted_placement;invalid_placement.valid=false;require(!MetadataTile::build_payload_rank_map(80u,80u,invalid_placement,rank_values,rank_mask.data,rank_mask.size,cached_rank_map,cached_pixels,false,0x123456789ABCDEF0ull,&rank_cache)&&cached_pixels==0u,"invalid placement was accepted on a cache hit");require(!MetadataTile::build_payload_rank_map(80u,80u,shifted_placement,rank_values,rank_mask.data,rank_mask.size,rank_cache.base_rank_map,cached_pixels,false,0x123456789ABCDEF0ull,&rank_cache),"rank map output aliased and corrupted its cache");
    const u64 expected[]={0,1,65535,65536,0x123456789ABull,U64_MAX_VALUE};for(u64 value:expected){MetadataTile::BitWriter writer;require(MetadataTile::write_compact_u64(writer,value),"compact u64 write failed");MetadataTile::BitReader reader;reader.reset(writer.data(),writer.bit_size());u64 actual=0;require(MetadataTile::read_compact_u64(reader,actual)&&actual==value&&!reader.failed&&reader.cursor==writer.bit_size(),"compact u64 roundtrip failed");}
    const u32 vals[]={0,1,2,127,128,253,254,65535};for(u32 p:vals)for(u32 e:vals){MetadataTile::BitWriter writer;require(MetadataTile::write_compact_u16(writer,p,true)&&MetadataTile::write_compact_u16(writer,e,true),"compact u16 write failed");MetadataTile::BitReader reader;reader.reset(writer.data(),writer.bit_size());u32 actual=0;require(MetadataTile::read_compact_u16(reader,actual,true)&&actual==p&&MetadataTile::read_compact_u16(reader,actual,true)&&actual==e&&!reader.failed&&reader.cursor==writer.bit_size(),"compact u16 roundtrip failed");if(p==0&&e==0)require(writer.bit_size()==6,"zero compact-u16 framing changed");}
    makocode::ByteBuffer data;require(data.ensure(1000),"metadata RS allocation failed");data.size=1000;for(usize i=0;i<data.size;++i)data.data[i]=static_cast<u8>(i*73u+19u);makocode::ByteBuffer word;require(MetadataTile::rs_encode_with_parity(data,232,word)&&word.size==11208,"metadata RS encode failed");
    makocode::ByteBuffer damaged;require(damaged.ensure(word.size),"metadata RS damage allocation failed");damaged.size=word.size;memcpy(damaged.data,word.data,word.size);usize offset=0;
    for(u32 block=0;block<44;++block){usize count=block==43?11:23;damaged.data[offset+count/2]^=static_cast<u8>(block+1);offset+=count+232;}
    u32 corrections=0;require(MetadataTile::rs_decode_with_parity(damaged,1000,232,corrections)&&corrections==44&&damaged.size==data.size&&memcmp(damaged.data,data.data,data.size)==0,"metadata multi-block RS correction failed");
    require(damaged.ensure(word.size),"metadata RS reset allocation failed");damaged.size=word.size;memcpy(damaged.data,word.data,word.size);for(u32 i=0;i<117;++i)damaged.data[i]^=static_cast<u8>(i+1);corrections=0;require(!MetadataTile::rs_decode_with_parity(damaged,1000,232,corrections),"metadata over-boundary RS errors unexpectedly recovered");
}

static void partial_ecc_recovery_case(){
    using namespace makocode;
    BitWriter archive;
    static const char first_path[]="00-first.txt";
    static const char first_data[]="recovered before the damaged block\n";
    static const char second_path[]="99-large.bin";
    const usize second_size=65536u;
    ByteBuffer second;
    require(second.ensure(second_size)&&MetadataTile::write_compact_u64(archive,2)&&
            archive.write_bit(0)&&MetadataTile::write_compact_u64(archive,sizeof(first_path)-1),
            "partial archive init failed");
    for(usize i=0;i<sizeof(first_path)-1;++i)
        require(archive.write_bits(static_cast<u8>(first_path[i]),8),"partial first path failed");
    require(MetadataTile::write_compact_u64(archive,sizeof(first_data)-1),"partial first length failed");
    for(usize i=0;i<sizeof(first_data)-1;++i)
        require(archive.write_bits(static_cast<u8>(first_data[i]),8),"partial first data failed");
    require(archive.write_bit(0)&&MetadataTile::write_compact_u64(archive,sizeof(second_path)-1),
            "partial second entry failed");
    for(char c:second_path) if(c)
        require(archive.write_bits(static_cast<u8>(c),8),"partial second path failed");
    require(MetadataTile::write_compact_u64(archive,second_size),"partial second length failed");
    u32 random=0x9E3779B9u;
    for(usize i=0;i<second_size;++i){
        random^=random<<13;random^=random>>17;random^=random<<5;
        second.data[i]=static_cast<u8>(random);
        require(archive.write_bits(second.data[i],8),"partial second bytes failed");
    }
    second.size=second_size;
    require(archive.align_to_byte(),"partial archive align failed");

    ByteBuffer compressed;
    require(lzma_compress(archive.data(),archive.byte_size(),compressed,true),
            "partial compression failed");
    PaletteSymbolProfile profile;
    require(palette_symbol_profile(2u,profile),"partial symbol profile failed");
    ByteBuffer stream;
    EccSummary summary;
    require(palette_symbol_encode_stream(profile,compressed.data,compressed.size,.05,0u,0u,
                                         nullptr,0u,stream,summary),
            "partial field-symbol encode failed");
    require(summary.block_count>1u&&summary.parity_symbols>=2u,
            "partial field-symbol fixture did not span blocks");
    ByteBuffer damaged;
    require(damaged.ensure(stream.size),"partial field-symbol damage allocation failed");
    damaged.size=stream.size;
    memcpy(damaged.data,stream.data,stream.size);
    u64 final_block=summary.block_count-1u;
    u32 errors=(summary.parity_symbols/2u)+1u;
    for(u32 position=0u;position<errors;++position){
        u64 interleaved=static_cast<u64>(position)*summary.block_count+final_block;
        usize offset=(usize)(PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+interleaved)*4u;
        u32 value=read_le_u32(damaged.data+offset);
        write_le_u32(damaged.data+offset,(value+position+1u)%profile.field_order);
    }
    ByteBuffer no_erasures,partial_compressed;
    EccDecodeStats stats;
    bool protected_stream=false;
    require(palette_symbol_decode_stream(profile,damaged,no_erasures,nullptr,0u,
                                         partial_compressed,&stats,&protected_stream,
                                         nullptr,true),
            "partial field-symbol decoder did not return the recoverable prefix");
    require(!protected_stream&&stats.unrecoverable_blocks==1u&&
            stats.failed_block_index==final_block&&
            partial_compressed.size<compressed.size&&
            memcmp(partial_compressed.data,compressed.data,partial_compressed.size)==0,
            "partial field-symbol decoder returned the wrong recoverable prefix");
    ByteBuffer partial_archive;
    require(lzma_decompress(partial_compressed.data,partial_compressed.size*8u,
                            partial_archive,LZMA_SIZE_FROM_HEADER,false,nullptr,true),
            "partial field-symbol archive decompression failed");
    struct Check{bool first=false,second=false;const u8* expected=nullptr;usize size=0;}
        check{false,false,second.data,second.size};
    auto visitor=[](u8 type,const char* path,usize path_size,const u8* data,usize size,u64,void* context)->bool{
        Check* c=static_cast<Check*>(context);
        const char p1[]="00-first.txt",d1[]="recovered before the damaged block\n",p2[]="99-large.bin";
        if(type==0&&path_size==sizeof(p1)-1&&memcmp(path,p1,path_size)==0)
            c->first=size==sizeof(d1)-1&&memcmp(data,d1,size)==0;
        else if(type==0&&path_size==sizeof(p2)-1&&memcmp(path,p2,path_size)==0)
            c->second=size==c->size&&memcmp(data,c->expected,size)==0;
        return true;
    };
    bool archive_ok=archive_parse(partial_archive,visitor,&check,nullptr,true);
    require(archive_ok&&check.first&&!check.second,
            "partial archive outcome parse="+std::to_string(archive_ok)+
            " first="+std::to_string(check.first)+
            " second="+std::to_string(check.second));
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
    fs::path symlink_target=work/"symlink_target";
    fs::create_directories(symlink_target);
    fs::create_symlink(symlink_target,archive_out/"linked");
    ArchiveBuildContext symlink_escape;
    const u8 secret_contents[]={'n','o','t','-','o','u','t','s','i','d','e'};
    require(archive_init(symlink_escape)&&
                archive_add_file(symlink_escape,"linked/escape.txt",secret_contents,sizeof(secret_contents))&&
                archive_finalize(symlink_escape),
            "symlink archive setup failed");
    require(!unpack_archive_to_directory(symlink_escape.buffer,archive_out.c_str()),
            "archive extraction followed an output-directory symlink");
    require(!fs::exists(symlink_target/"escape.txt"),
            "archive extraction wrote through an output-directory symlink");
    fs::path linked_output=work/"linked_output";
    fs::create_symlink(symlink_target,linked_output);
    ArchiveBuildContext root_symlink_archive;
    require(archive_init(root_symlink_archive)&&
                archive_add_file(root_symlink_archive,"root.txt",secret_contents,sizeof(secret_contents))&&
                archive_finalize(root_symlink_archive),
            "root-symlink archive setup failed");
    require(!unpack_archive_to_directory(root_symlink_archive.buffer,linked_output.c_str()),
            "archive extraction accepted a symlink output root");
    require(!fs::exists(symlink_target/"root.txt"),
            "archive extraction wrote through a symlink output root");
    fs::path existing_target=symlink_target/"existing.txt";
    write_file(existing_target,"original contents");
    fs::create_symlink(existing_target,archive_out/"final.txt");
    ArchiveBuildContext final_symlink_archive;
    require(archive_init(final_symlink_archive)&&
                archive_add_file(final_symlink_archive,"final.txt",secret_contents,sizeof(secret_contents))&&
                archive_finalize(final_symlink_archive),
            "final-symlink archive setup failed");
    require(!unpack_archive_to_directory(final_symlink_archive.buffer,archive_out.c_str()),
            "archive extraction accepted a symlink final path");
    require(read_file(existing_target)=="original contents",
            "archive extraction overwrote a symlink target");
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
    const std::string prefix="cli_decoder_paths_auto";auto enc=run_cli(input_dir,{"encode","--input=payload.bin","--page-width=500","--page-height=500","--output-dir="+ppm_dir.string(),"--prefix="+prefix},"cli_encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(ppm_dir,prefix);require(!ppms.empty(),id+": encode emitted no PPM pages");
    auto dec=run_cli(work,decode_file_args(ppms,decoded_dir,{}),"cli_decode");expect_success(dec,id+" decode");compare_files(payload,decoded_dir/"payload.bin",id);
    fs::path pass_out=work/"decoded_with_ignored_password";auto pass=run_cli(work,decode_file_args(ppms,pass_out,{"--password-cli=unused-password"}),"ignored_password");expect_success(pass,id+" ignored password");require((pass.out+pass.err).find("decode: warning: payload was not encrypted; password ignored")!=std::string::npos,id+": ignored-password warning missing");compare_files(payload,pass_out/"payload.bin",id+" ignored password");
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
    if(ecc_fill)enc.push_back("--ecc=fill");else enc.push_back("--ecc=0.5");
    auto result=run_cli(work,enc,"encode");expect_success(result,id+" encode");
    auto pages=ppm_paths(encoded,"source");require(pages.size()==1,id+": expected one source page");
    fs::path scan=work/"scan.ppm";
    make_affine_scan(pages.front(),scan,width,height,pitch,ox,oy,
                     low_contrast?120:60,low_contrast?114:174,blur,streaks);
    auto control_result=run_cli(work,decode_file_args(pages,control),"control_decode");expect_success(control_result,id+" control decode");
    compare_files(payload,control/"payload.bin",id+" control");
    auto scan_result=run_cli(work,decode_file_args({scan.string()},decoded),"scan_decode");expect_success(scan_result,id+" scan decode");
    compare_files(payload,decoded/"payload.bin",id+" scan");
    auto control_stdout=run_cli(work,{"decode","--decoded-stdout",pages.front()},"control_stdout");
    auto scan_stdout=run_cli(work,{"decode","--decoded-stdout",scan.string()},"scan_stdout");
    expect_success(control_stdout,id+" stdout control");expect_success(scan_stdout,id+" stdout scan");
    std::string expected_report=read_file(payload);std::vector<unsigned char> expected_report_bytes(expected_report.begin(),expected_report.end());check_decoded(scan_stdout.out,expected_report_bytes,0.0,id+" scan stdout");
    if(expected_scale>0.0){
        makocode::ByteBuffer scan_input,frame_bits;u64 frame_bit_count=0;PpmParserState state;ImageMappingConfig mapping;
        require(read_entire_file(scan.c_str(),scan_input)&&
                    ppm_extract_frame_bits(scan_input,mapping,frame_bits,frame_bit_count,state,false),
                id+": could not inspect affine fit");
        const AffineTransform& affine=state.affine_transform;
        double scale=std::sqrt(affine.a00*affine.a00+affine.a10*affine.a10);
        double rotation=std::atan2(affine.a10,affine.a00)*(180.0/3.14159265358979323846);
        require(state.has_affine_transform&&std::fabs(scale-expected_scale)<=0.01&&
                    std::fabs(affine.tx-expected_tx)<=0.75&&std::fabs(affine.ty-expected_ty)<=0.75&&
                    std::fabs(rotation)<=0.25,
                id+": affine registration outside tolerance");
    }
}

static void canonical_tile_case(){
    const std::string id="0104_canonical_tile";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);
    std::vector<unsigned char> bytes(65536);std::mt19937 random(42);for(auto& b:bytes)b=static_cast<unsigned char>(random()&255u);write_file(payload,std::string(bytes.begin(),bytes.end()));
    for(const auto& dims:std::vector<std::pair<unsigned,unsigned>>{{23,23},{48,48},{49,48},{64,48}}){makocode::ByteBuffer mask;u64 reserved=0;require(compute_fiducial_reservation(dims.first,dims.second,dims.second,reserved,&mask,false,0,0,0),id+": canonical reservation failed");u64 overwritten=0;for(usize i=0;i<mask.size;++i){require(mask.data[i]==0||mask.data[i]==OVERWRITE_MASK_PIXEL,id+": unexpected reservation mask value");overwritten+=mask.data[i]==OVERWRITE_MASK_PIXEL;}auto p=MetadataTile::compute_center_tile_placement(dims.first,dims.second);require(p.valid&&p.x0==(dims.first>=48?(dims.first-48)/2:0)&&p.y0==(dims.second>=48?(dims.second-48)/2:0)&&p.offset_x==(dims.first<48?(48-dims.first)/2:0)&&p.offset_y==(dims.second<48?(48-dims.second)/2:0)&&overwritten==reserved&&overwritten<=static_cast<u64>(dims.first)*dims.second,id+": canonical small-page capacity layout mismatch");}
    auto enc=run_cli(work,encode_file_args(payload,pages,"page",{"--page-width=1000","--page-height=1000","--ecc=0.5"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"page");require(ppms.size()==1,id+": expected a single canonical page");TileSummary summary=read_tile_summary(ppms.front(),1000,1000);require(summary.block_count>0&&summary.parity>0,id+": missing canonical ECC");
    auto dec=run_cli(work,decode_file_args(ppms,decoded),"decode");expect_success(dec,id+" decode");compare_files(payload,decoded/"payload.bin",id);
    fs::path overlay=work/"overlay.ppm",budget_dir=work/"budget_pages";expect_success(run_transform(work,{"overlay-mask","--output",overlay.string(),"--circle-color","0 0 0","--background-color","255 255 255","--width","1700","--height","1700"},"mask"),id+" mask");
    auto budget=run_cli(work,encode_file_args(payload,budget_dir,"budget",{"--page-width=1700","--page-height=1700","--overlay-fraction=0.10","--overlay-ppm="+overlay.string(),"--overlay-pages=all"}),"budget");expect_success(budget,id+" ECC overlay budget");auto budget_pages=ppm_paths(budget_dir,"budget");require(budget_pages.size()==1,id+": expected one overlay budget page");TileSummary bs=read_tile_summary(budget_pages[0],1700,1700);require(bs.overlay_budget>0&&bs.selector_present,id+": automatic ECC omitted selector budget");
    fs::path invalid=work/"no_overlay_attempt";auto rejected=run_cli(work,encode_file_args(payload,invalid,"bad",{"--ecc=0","--overlay-fraction=0.10","--overlay-ppm="+overlay.string(),"--overlay-pages=all","--page-width=1000","--page-height=1000"}),"overlay_without_ecc");expect_failure(rejected,id+" zero-ECC overlay");require((rejected.out+rejected.err).find("overlay requires ECC")!=std::string::npos,id+": zero-ECC overlay diagnostic changed");
    fs::path custom_dir=work/"custom_pages",custom_out=work/"custom_out";auto custom=run_cli(work,encode_file_args(payload,custom_dir,"custom",{"--page-width=1700","--page-height=1700","--fiducial-spacing=30"}),"custom_encode");expect_success(custom,id+" custom fiducials");auto custom_pages=ppm_paths(custom_dir,"custom");require(custom_pages.size()==1,id+": custom-fiducial page missing");read_tile_summary(custom_pages[0],1700,1700);auto custom_dec=run_cli(work,decode_file_args(custom_pages,custom_out),"custom_decode");expect_success(custom_dec,id+" custom decode");compare_files(payload,custom_out/"payload.bin",id+" custom");
    fs::path tiny=work/"tiny.txt",tiny_dir=work/"tiny_pages",tiny_out=work/"tiny_out";write_file(tiny,"small canonical roundtrip\n");auto tiny_enc=run_cli(work,encode_file_args(tiny,tiny_dir,"tiny",{"--page-width=80","--page-height=80"}),"tiny_encode");expect_success(tiny_enc,id+" tiny encode");auto tiny_pages=ppm_paths(tiny_dir,"tiny");require(tiny_pages.size()==1,id+": tiny page missing");read_tile_summary(tiny_pages[0],80,80);auto tiny_dec=run_cli(work,decode_file_args(tiny_pages,tiny_out),"tiny_decode");expect_success(tiny_dec,id+" tiny decode");compare_files(tiny,tiny_out/"tiny.txt",id+" tiny");
}

static void palette_base5_capacity_case(){
    const std::string id="0106_palette_base5_capacity";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages",decoded=work/"decoded";fs::create_directories(pages);unsigned seed=case_seed("payload/"+id);generate_bytes(work,payload,34000,seed,"payload");const std::string palette="White Cyan Magenta Yellow Black";
    auto enc=run_cli(work,encode_file_args(payload,pages,id+"_random_payload_encoded",{"--page-width=360","--page-height=360","--palette",palette,"--ecc=0.01"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,id+"_random_payload_encoded");require(ppms.size()==1,id+": base-5 expected one page");auto normal=run_cli(work,decode_file_args(ppms,decoded,{"--palette",palette}),"decode_payload");expect_success(normal,id+" payload decode");compare_files(payload,decoded/"payload.bin",id);
    fs::path metadata=work/"metadata";auto dec=run_cli(work,decode_file_args(ppms,metadata,{"--decoded-file","--palette",palette}),"decode_metadata");expect_success(dec,id+" metadata decode");
    PpmImage image=read_ppm(ppms[0]);MetadataTile::Values tile=extract_tile_values(ppms[0]);int tx=static_cast<int>(image.width/2)-24+static_cast<int>(tile.metadata_square_x0),ty=static_cast<int>(image.height/2)-24+static_cast<int>(tile.metadata_tag_y0);size_t black_outside=0;
    for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x){size_t at=(static_cast<size_t>(y)*image.width+x)*3;bool in=x>=static_cast<unsigned>(tx)&&x<static_cast<unsigned>(tx+tile.metadata_square_side)&&y>=static_cast<unsigned>(ty)&&y<static_cast<unsigned>(ty+tile.metadata_square_side+6);if(image.pixels[at]==0&&image.pixels[at+1]==0&&image.pixels[at+2]==0&&!in)++black_outside;}
    require(black_outside>2500,id+": expected base-5 black symbols outside metadata tile");std::vector<fs::path> reports;for(const auto& entry:fs::directory_iterator(metadata))if(entry.is_regular_file()&&entry.path().extension()==".txt")reports.push_back(entry.path());require(reports.size()==1,id+": decoded metadata report count changed");std::istringstream lines(read_file(reports[0]));std::string line;u64 page_bits=0;unsigned matches=0;while(std::getline(lines,line))if(line.rfind("metadata page_bits ",0)==0){page_bits=std::strtoull(line.c_str()+19,nullptr,10);++matches;}require(matches==1&&page_bits>0&&tile.ecc_original_bytes*8ull>2ull*image.width*image.height,id+": base-5 payload did not exceed two source bits per pixel");
}

static void canonical_overlay_multipage_case(){
    const std::string id="0105_canonical_overlay_multipage";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages_dir=work/"pages";generate_bytes(work,payload,4096,103,"payload");
    fs::path black=work/"overlay.ppm",cyan=work/"overlay_second.ppm";expect_success(run_transform(work,{"overlay-mask","--output",black.string(),"--circle-color","0 0 0","--background-color","255 255 255","--width","1000","--height","1000"},"mask"),id+" black mask");expect_success(run_transform(work,{"overlay-mask","--output",cyan.string(),"--circle-color","0 255 255","--background-color","255 255 255","--width","1000","--height","1000"},"cyan"),id+" cyan mask");
    std::vector<std::string> args={"encode","--input=payload.bin","--overlay-fraction=0.992156862745098","--overlay-ppm="+black.string(),"--overlay-pages=1-1,1","--overlay-ppm="+cyan.string(),"--overlay-pages=2-2","--page-width=1000","--page-height=1000","--prefix=page","--output-dir="+pages_dir.string()};auto enc=run_cli(work,args,"encode");expect_success(enc,id+" encode");
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

struct BlobDecodeResult {
    bool parsed = false;
    bool partial = false;
    u64 unrecoverable = 0;
    u64 erased = 0;
    u32 failed_errors = 0;
    u32 failed_erasures = 0;

    BlobDecodeResult() {}
};

static bool test_uniform_region_detector(){
    const u32 width=100,height=100;usize count=static_cast<usize>(width)*height;makocode::ByteBuffer symbols,ranks;if(!symbols.ensure(count)||!ranks.ensure(count*sizeof(u64)))return false;symbols.size=count;ranks.size=count*sizeof(u64);u64* rank=reinterpret_cast<u64*>(ranks.data);u32 state=0xA341316Cu;
    for(usize i=0;i<count;++i){state^=state<<13;state^=state>>17;state^=state<<5;symbols.data[i]=static_cast<u8>(state&1u);rank[i]=i;}MetadataTile::Placement placement={};makocode::ByteBuffer mask;
    size_t mask_capacity=64u;while(mask_capacity<count)mask_capacity*=2u;test_allocation_fill_size=mask_capacity;test_allocation_fill_value=0xA5u;bool no_uniform_regions=detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,placement,1,count,mask);test_allocation_fill_size=0u;test_allocation_fill_value=0u;if(!no_uniform_regions)return false;for(usize i=0;i<mask.size;++i)if(mask.data[i])return false;
    for (u32 x = 0u; x < 32u; ++x) symbols.data[x] = 0u;
    makocode::ByteBuffer prefix_failure_mask;
    minify_allocation_count = 0ul;
    minify_allocation_failure_size = 0ul;
    minify_allocation_failure_at = 2ul;
    bool prefix_allocation_rejected = detect_uniform_payload_regions(
        symbols.data, width, height, rank, 0, 0, placement, 1u, 1u,
        count, prefix_failure_mask);
    unsigned long prefix_failure_size = minify_allocation_failure_size;
    minify_allocation_failure_at = 0ul;
    if (prefix_allocation_rejected || prefix_failure_size == 0ul ||
        prefix_failure_mask.data || prefix_failure_mask.size != 0u) return false;
    for(u32 y=20;y<80;++y)for(u32 x=20;x<80;++x)symbols.data[static_cast<usize>(y)*width+x]=1;makocode::ByteBuffer blob;if(!detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,placement,1,count,blob))return false;u64 marked=0;for(usize i=0;i<blob.size;++i)marked+=blob.data[i]!=0;if(marked<1500||marked>=5000)return false;
    makocode::ByteBuffer reservation_mask,masked_blob;if(!reservation_mask.ensure(count))return false;reservation_mask.size=count;memset(reservation_mask.data,0,count);usize reserved_index=static_cast<usize>(50u)*width+50u;reservation_mask.data[reserved_index]=1u;if(!blob.data[reserved_index])return false;if(!detect_uniform_payload_regions(symbols.data,width,height,rank,reservation_mask.data,reservation_mask.size,placement,1,count,masked_blob))return false;if(masked_blob.data[reserved_index])return false;
    for(usize i=0;i<count;++i){state^=state<<13;state^=state>>17;state^=state<<5;symbols.data[i]=static_cast<u8>(state&1u);}
    for(u32 y=40u;y<99u;++y)for(u32 x=28u;x<72u;++x)symbols.data[static_cast<usize>(y)*width+x]=1u;
    for(u32 x=0u;x<width;++x)symbols.data[99u*width+x]=0u;
    symbols.data[99u*width+50u]=1u;
    makocode::ByteBuffer edge_blob;
    if(!detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,placement,2u,1u,count,edge_blob,false,true,90u))return false;
    if(edge_blob.size!=count||edge_blob.data[99u*width+50u]==0u)return false;
    for(usize i=0u;i<count;++i)symbols.data[i]=0u;
    for(u32 y=40u;y<86u;++y)for(u32 x=28u;x<72u;++x)symbols.data[static_cast<usize>(y)*width+x]=1u;
    for(u32 y=86u;y<99u;++y)for(u32 x=45u;x<72u;++x)symbols.data[static_cast<usize>(y)*width+x]=1u;
    symbols.data[99u*width+28u]=1u;
    makocode::ByteBuffer clipped_box_blob;
    if(!detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,placement,2u,1u,count,clipped_box_blob,false,false,90u))return false;
    if(clipped_box_blob.size!=count||clipped_box_blob.data[99u*width+28u]==0u)return false;
    for(usize i=0;i<count;++i)symbols.data[i]=0u;
    MetadataTile::Placement metadata_placement;
    metadata_placement.valid=true;
    metadata_placement.x0=20u;
    metadata_placement.y0=40u;
    for(u32 y=20u;y<56u;++y)for(u32 x=20u;x<68u;++x)symbols.data[static_cast<usize>(y)*width+x]=1u;
    makocode::ByteBuffer metadata_blob;
    if(!detect_uniform_payload_regions(symbols.data,width,height,rank,0,0,metadata_placement,2u,1u,count,metadata_blob,false,true,90u))return false;
    return metadata_blob.size==count&&metadata_blob.data[30u*width+40u]==0u;
}

static bool decode_blob_fixture(const fs::path& path,bool detect,BlobDecodeResult& result){
    result={};makocode::ByteBuffer ppm;if(!read_entire_file(path.c_str(),ppm))return false;ImageMappingConfig mapping;const char* palette="White Black";image_mapping_set_palette_text(mapping,palette,strlen(palette),"test");if(!image_mapping_build_custom_palette(mapping,"test"))return false;makocode::ByteBuffer frame,frame_erasures;u64 frame_bits=0;PpmParserState state;bool retry=false,rotation=false;
    if(!ppm_extract_frame_bits(ppm,mapping,frame,frame_bits,state,false,&retry,1,&frame_erasures,nullptr,nullptr,nullptr,nullptr,&rotation,false,nullptr,nullptr,nullptr,0,0,0,detect,true)||!state.has_encoded_tile||!metadata_schema_is_current(state.encoded_tile.schema_version))return false;
    makocode::ByteBuffer payload_stream,payload_erasures;u64 payload_bits=0;if(!frame_bits_to_payload(frame.data,frame_bits,state,payload_stream,payload_bits,nullptr,frame_erasures.data,frame_bits,&payload_erasures))return false;for(usize i=0;i<payload_erasures.size;++i)result.erased+=payload_erasures.data[i]!=0;
    const MetadataTile::Values& tile=state.encoded_tile;makocode::PaletteSymbolProfile profile;
    if(!tile.ecc_enabled||!tile.ecc_block_data||!tile.ecc_parity||!tile.ecc_block_count||
       !makocode::palette_symbol_profile(tile.palette_count,profile))return false;
    makocode::PaletteSymbolHeaderRecovery header_recovery;
    header_recovery.available=true;
    header_recovery.byte_count=tile.ecc_original_bytes;
    header_recovery.block_count=tile.ecc_block_count;
    header_recovery.data_symbols_per_block=tile.ecc_block_data;
    header_recovery.parity_symbols=tile.ecc_parity;
    header_recovery.overlay_erasure_budget=tile.overlay_erasure_budget;
    header_recovery.unknown_error_budget=tile.unknown_parity_budget;
    u64 source_symbols=makocode::palette_symbol_count_for_bytes(profile,tile.ecc_original_bytes);
    if(source_symbols==U64_MAX_VALUE||
       (tile.ecc_parity&&tile.ecc_block_count>(U64_MAX_VALUE-source_symbols)/tile.ecc_parity))return false;
    u64 encoded_payload_symbols=source_symbols+tile.ecc_block_count*tile.ecc_parity;
    bool bootstrap_omitted=tile.page_bits/32u==encoded_payload_symbols;
    makocode::EccDecodeStats stats;makocode::ByteBuffer decoded;
    result.parsed=makocode::palette_symbol_decode_stream(profile,payload_stream,payload_erasures,
                                                         nullptr,0u,decoded,&stats,nullptr,
                                                         &header_recovery,false,bootstrap_omitted);
    result.partial=!result.parsed;result.unrecoverable=stats.unrecoverable_blocks;result.failed_errors=stats.failed_block_errors;result.failed_erasures=stats.failed_block_erasures;
    return true;
}

static void blob_erasure_case(){
    const std::string id="0114_decode_blob_erasures";fs::path work=make_work_dir(id),payload=work/"payload.bin",pages=work/"pages";fs::create_directories(pages);generate_bytes(work,payload,24576,1944,"payload");
    auto enc=run_cli(work,encode_file_args(payload,pages,"blob",{"--page-width=620","--page-height=620","--palette","White Black","--ecc=0.75"}),"encode");expect_success(enc,id+" encode");auto ppms=ppm_paths(pages,"blob");require(ppms.size()==1,id+": expected one complete source page");
    require(test_uniform_region_detector(),id+": uniform-region detector threshold checks failed");
    // q2's all-black eight-dot symbol is 255, the final element of GF(2^8).
    for(bool black:{false,true}){PpmImage source=read_ppm(ppms[0]);require(source.width==620&&source.height==620,id+": source dimensions changed");const std::array<std::array<int,3>,2> blots={{{460,400,158},{470,150,103}}};size_t changed=0;for(const auto& blot:blots){int cx=blot[0],cy=blot[1],radius=blot[2];for(int y=cy-radius;y<=cy+radius;++y)for(int x=cx-radius;x<=cx+radius;++x)if((x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius){size_t at=(static_cast<size_t>(y)*source.width+x)*3;unsigned char value=black?0:255;source.pixels[at]=source.pixels[at+1]=source.pixels[at+2]=static_cast<char>(value);++changed;}}require(changed>70000,id+": blot mutation too small");
        fs::path mutated=work/(black?"blob_black.ppm":"blob_white.ppm");write_file(mutated,"P6\n620 620\n255\n"+source.pixels);auto cli=run_cli(work,{"decode","--decoded-stdout",mutated.string()},black?"decode_black":"decode_white");expect_success(cli,id+" CLI decode");std::string expected=read_file(payload);check_decoded(cli.out,std::vector<unsigned char>(expected.begin(),expected.end()),0,id);
        BlobDecodeResult unknown,detected;require(decode_blob_fixture(mutated,false,unknown),id+": unknown-error direct fixture decode failed");require(decode_blob_fixture(mutated,true,detected),id+": erasure direct fixture decode failed");
        require(unknown.unrecoverable>0,id+": blot did not exceed unknown-error capacity now that every q2 pattern is a valid field symbol");require(detected.parsed&&!detected.partial&&detected.unrecoverable==0&&detected.erased>unknown.erased,id+": detected erasures did not recover ECC blocks (parsed="+std::to_string(detected.parsed)+", partial="+std::to_string(detected.partial)+", unknown unrecoverable="+std::to_string(unknown.unrecoverable)+", unknown failed e/s="+std::to_string(unknown.failed_errors)+"/"+std::to_string(unknown.failed_erasures)+", detected unrecoverable="+std::to_string(detected.unrecoverable)+", detected failed e/s="+std::to_string(detected.failed_errors)+"/"+std::to_string(detected.failed_erasures)+", detected erased="+std::to_string(detected.erased)+", unknown erased="+std::to_string(unknown.erased)+")");}
}

static void raw_fallback_case(){
    const std::string id="0069_decode_raw_fallback";fs::path work=make_work_dir(id),payload_path=work/"raw_payload.bin",ppm_path=work/"raw_payload.ppm";generate_bytes(work,payload_path,4096,5066,"raw_payload");makocode::ByteBuffer payload;require(read_entire_file(payload_path.c_str(),payload),id+": cannot read raw payload");
    makocode::EncoderContext encoder;encoder.config.palette_size=2;encoder.config.ecc_redundancy=.5;encoder.config.overlay_erasure_budget=0;require(encoder.set_payload(payload.data,payload.size)&&encoder.build(),id+": raw fallback fixture encode failed");
    ImageMappingConfig mapping;const u32 width=500,height=500;PageFooterConfig footer_config;makocode::ByteBuffer frame;u64 frame_bits=0;FooterLayout footer;u32 data_height=height;u64 bits_per_page=0,page_count=0;
    require(build_frame_bits(encoder,frame,frame_bits)&&compute_page_layout(mapping,footer_config,frame_bits,width,height,footer,data_height,bits_per_page,page_count,&encoder.ecc_info(),0.0,encoder.ecc_info().unknown_error_budget,encoder.ecc_info().overlay_erasure_budget)&&page_count==1,id+": raw fallback fixture page layout failed");
    makocode::ByteBuffer page;require(encode_page_to_ppm(mapping,frame,frame_bits,0,width,height,1,bits_per_page,&encoder.ecc_info(),0.0,nullptr,0,footer,page),id+": raw fallback fixture page render failed");makocode::ByteBuffer fiducials;const u8 white[3]={255,255,255};require(apply_default_fiducial_grid(page,fiducials,data_height,white),id+": raw fallback fiducials failed");write_file(ppm_path,std::string(reinterpret_cast<const char*>(fiducials.data),fiducials.size));
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
    expect_success(expected,id+" control stdout");expect_success(actual,id+" scan stdout");std::string expected_bytes=read_file(payload);std::vector<unsigned char> report_bytes(expected_bytes.begin(),expected_bytes.end());check_decoded(actual.out,report_bytes,0.0,id+" scan stdout");
}

struct RoundTrip {
    std::string id;
    size_t size = 0;
    std::string literal;
    bool has_master_seed_override = false;
    unsigned master_seed_override = 0;
    bool has_payload_seed_override = false; // Stable random bytes for geometry-sensitive fixtures.
    unsigned payload_seed_override = 0;
    std::vector<std::string> encode_args;
    std::vector<std::string> decode_args;
    std::vector<std::string> transform_args;
    bool require_multiple_pages = false;
    bool tiny_dimensions_check = false;
    bool marker_free_compact = false;
    bool default_ecc_check = false;
    bool scatter_metadata_check = false;
    bool white_checker = false;
};

static void run_roundtrip(const RoundTrip& test) {
    fs::path work = make_work_dir(test.id);
    const std::string prefix = test.id + "_random_payload_encoded";
    unsigned test_master_seed = test.has_master_seed_override
        ? test.master_seed_override
        : master_seed;
    unsigned payload_seed = test.has_payload_seed_override
        ? test.payload_seed_override
        : case_seed("payload/" + test.id, test_master_seed);
    unsigned crypto_seed = case_seed("crypto/" + test.id, test_master_seed);
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
    bool compact_metadata_marker_fixture =
        test.id == "0001_makocode_2_colors" ||
        test.id == "0002_makocode_4_colors" ||
        test.id == "0003_makocode_5_colors" ||
        test.id == "0004_makocode_8_colors";
    if (test.id == "0130_default_ecc_2_colors") {
        require(pages.size() == 1u, test.id + ": marker fixture expected one page");
        MetadataTile::Values tile = extract_tile_values(pages.front());
        require(tile.palette_count >= 2u, test.id + ": marker palette missing endpoints");
        PaletteColor expected_zero = {255u, 255u, 255u};
        PaletteColor expected_one = {0u, 0u, 0u};
        u32 marker_zero_index = tile.palette_count;
        u32 marker_one_index = tile.palette_count;
        for (u32 color = 0u; color < tile.palette_count; ++color) {
            const PaletteColor& actual = tile.palette[color];
            if (actual.r == expected_zero.r && actual.g == expected_zero.g &&
                actual.b == expected_zero.b) marker_zero_index = color;
            if (actual.r == expected_one.r && actual.g == expected_one.g &&
                actual.b == expected_one.b) marker_one_index = color;
        }
        require(marker_zero_index < tile.palette_count &&
                    marker_one_index < tile.palette_count,
                test.id + ": marker palette does not contain its required fixed colors");
        makocode::ByteBuffer tile_bits;
        require(MetadataTile::build_tile_bits(tile, tile_bits),
                test.id + ": could not rebuild metadata tile");
        PpmImage image = read_ppm(pages.front());
        static const char expected_marker_bits[] = "11010001101111110011111101000101";
        MetadataTile::Placement tile_placement =
            MetadataTile::compute_center_tile_placement(image.width, image.height);
        int tile_x = (int)tile_placement.x0 - (int)tile_placement.offset_x;
        int tile_y = (int)tile_placement.y0 - (int)tile_placement.offset_y;
        for (u32 bit = 0u; bit < 32u; ++bit) {
            bool one = expected_marker_bits[bit] == '1';
            u32 x = MetadataTile::tag_marker_x0() + (bit & 7u);
            u32 y = tile.metadata_tag_y0 + 1u + bit / 8u;
            usize cell = (usize)y * MetadataTile::TILE_SIDE + x;
            u8 expected_palette_index =
                (u8)(one ? marker_one_index : marker_zero_index);
            require(tile_bits.data[cell] == expected_palette_index,
                    test.id + ": marker bit " + std::to_string(bit) +
                        " is not stored at its required fixed color");
            int pixel_x = tile_x + (int)x;
            int pixel_y = tile_y + (int)y;
            require(pixel_x >= 0 && pixel_y >= 0 &&
                        pixel_x < (int)image.width && pixel_y < (int)image.height,
                    test.id + ": active metadata marker falls outside the compact page");
            usize pixel = ((usize)pixel_y * image.width + (usize)pixel_x) * 3u;
            const PaletteColor& expected = one ? expected_one : expected_zero;
            require((u8)image.pixels[pixel] == expected.r &&
                        (u8)image.pixels[pixel + 1u] == expected.g &&
                        (u8)image.pixels[pixel + 2u] == expected.b,
                    test.id + ": printed marker bit " + std::to_string(bit) +
                        " does not match its required fixed color");
        }
    }
    if (compact_metadata_marker_fixture) {
        MetadataTile::Values compact_tile = extract_tile_values(pages.front());
        u64 compact_block_length = (u64)compact_tile.ecc_block_data +
                                   compact_tile.ecc_parity;
        require(compact_tile.ecc_block_count > 0u && compact_block_length > 0u &&
                    compact_tile.ecc_block_count <= U64_MAX_VALUE / compact_block_length,
                test.id + ": metadata ECC block dimensions overflow");
        makocode::PaletteSymbolProfile compact_profile;
        require(makocode::palette_symbol_profile(compact_tile.palette_count, compact_profile),
                test.id + ": compact palette profile unavailable");
        u64 compact_source_symbols = makocode::palette_symbol_count_for_bytes(
            compact_profile, compact_tile.ecc_original_bytes);
        u64 compact_payload_symbols = compact_source_symbols +
            compact_tile.ecc_block_count * compact_tile.ecc_parity;
        require(
                    compact_payload_symbols <= U64_MAX_VALUE / 32u &&
                    compact_tile.page_bits == compact_payload_symbols * 32u,
                test.id + ": all-page metadata stream retained bootstrap or padded symbols");
        PpmImage image = read_ppm(pages.front());
        static const char expected_marker_bits[] =
            "11010001101111110011111101000101";
        static const PaletteColor white_black[] = {{255u, 255u, 255u}, {0u, 0u, 0u}};
        static const PaletteColor white_cmy[] = {
            {255u, 255u, 255u}, {0u, 255u, 255u},
            {255u, 0u, 255u}, {255u, 255u, 0u}};
        static const PaletteColor white_cmyk[] = {
            {255u, 255u, 255u}, {0u, 255u, 255u},
            {255u, 0u, 255u}, {255u, 255u, 0u}, {0u, 0u, 0u}};
        static const PaletteColor white_rgb_cmyk[] = {
            {255u, 255u, 255u}, {255u, 0u, 0u}, {0u, 255u, 0u},
            {0u, 0u, 255u}, {0u, 255u, 255u}, {255u, 0u, 255u},
            {255u, 255u, 0u}, {0u, 0u, 0u}};
        const PaletteColor* expected_palette = white_black;
        u32 palette_count = 2u;
        if (test.id == "0002_makocode_4_colors") {
            expected_palette = white_cmy;
            palette_count = 4u;
        } else if (test.id == "0003_makocode_5_colors") {
            expected_palette = white_cmyk;
            palette_count = 5u;
        } else if (test.id == "0004_makocode_8_colors") {
            expected_palette = white_rgb_cmyk;
            palette_count = 8u;
        }
        PaletteColor expected_one = palette_count == 4u
            ? PaletteColor{255u, 255u, 0u}
            : PaletteColor{0u, 0u, 0u};
        bool marker_found = false;
        u32 marker_y = 0u;
        for (u32 y = 0u; y + 4u <= image.height && !marker_found; ++y) {
            for (u32 x = 0u; x + 8u <= image.width && !marker_found; ++x) {
                bool matches = true;
                for (u32 bit = 0u; bit < 32u && matches; ++bit) {
                    usize pixel = ((usize)(y + bit / 8u) * image.width +
                                   x + (bit & 7u)) * 3u;
                    const PaletteColor& expected = expected_marker_bits[bit] == '1'
                        ? expected_one : expected_palette[0];
                    matches = (u8)image.pixels[pixel] == expected.r &&
                              (u8)image.pixels[pixel + 1u] == expected.g &&
                              (u8)image.pixels[pixel + 2u] == expected.b;
                }
                if (matches) {
                    marker_found = true;
                    marker_y = y;
                }
            }
        }
        require(marker_found,
                test.id + ": compact page is missing the MAKO/CODE marker");
        MetadataTile::Placement placement =
            MetadataTile::compute_center_tile_placement(image.width, image.height);
        int palette_x = (int)placement.x0 +
                        (int)MetadataTile::palette_tag_x0(palette_count) -
                        (int)placement.offset_x;
        int palette_y = (int)marker_y - 1;
        for (u32 color = 0u; color <= palette_count; ++color) {
            int x = palette_x + (int)color;
            require(palette_y >= 0 && x >= 0 && x < (int)image.width,
                    test.id + ": compact metadata palette row falls outside the page");
            usize pixel = ((usize)palette_y * image.width + (usize)x) * 3u;
            const PaletteColor& expected = color < palette_count
                ? expected_palette[color] : expected_palette[0];
            require((u8)image.pixels[pixel] == expected.r &&
                        (u8)image.pixels[pixel + 1u] == expected.g &&
                        (u8)image.pixels[pixel + 2u] == expected.b,
                    test.id + ": first metadata palette row is missing or damaged");
        }
    }
    if (test.tiny_dimensions_check) {
        PpmImage image = read_ppm(pages.front());
        u32 maximum_tiny_edge = test.id == "0000_tiny_0" ? 23u :
                                test.id == "0001_makocode_2_colors" ? 22u : 16u;
        if (test.id == "0002_makocode_4_colors") maximum_tiny_edge = 18u;
        else if (test.id == "0003_makocode_5_colors") maximum_tiny_edge = 17u;
        require(image.width >= 16u && image.width <= maximum_tiny_edge &&
                    image.height >= 16u && image.height <= maximum_tiny_edge,
                test.id + ": expected a roughly 20 by 20 pixel bootstrap page");
        if (test.id == "0002_makocode_4_colors" ||
            test.id == "0003_makocode_5_colors" ||
            test.id == "0004_makocode_8_colors") {
            require(image.width < 30u && image.height < 30u,
                    test.id + ": higher-color barcode is not smaller than q2");
        }
        if (test.id == "0000_tiny_0" ||
            test.id == "0001_makocode_2_colors") {
            u64 white = 0u;
            u64 black = 0u;
            for (usize pixel = 0u; pixel < image.pixels.size(); pixel += 3u) {
                if ((u8)image.pixels[pixel] == 255u &&
                    (u8)image.pixels[pixel + 1u] == 255u &&
                    (u8)image.pixels[pixel + 2u] == 255u) {
                    ++white;
                } else if ((u8)image.pixels[pixel] == 0u &&
                           (u8)image.pixels[pixel + 1u] == 0u &&
                           (u8)image.pixels[pixel + 2u] == 0u) {
                    ++black;
                }
            }
            u64 total = white + black;
            require(total == image.width * image.height &&
                        white * 100u >= total * 42u &&
                        white * 100u <= total * 58u,
                    test.id + ": binary barcode pixels are not near a 50/50 split");
        }
    }
    if (test.default_ecc_check) {
        MetadataTile::Values tile = extract_tile_values(pages.front());
        require(tile.ecc_parity >= 170u && tile.unknown_parity_budget >= 85u,
                test.id + ": default auto-ECC no longer reserves 85 unknown symbol errors");
    }
    if (test.scatter_metadata_check) {
        MetadataTile::Values tile = extract_tile_values(pages.front());
        require(tile.scatter_payload,
                test.id + ": metadata does not record the scattered payload layout");
    }
    std::vector<std::string> decode_base{"decode", "--decoded-stdout"};
    decode_base.insert(decode_base.end(), test.decode_args.begin(), test.decode_args.end());
    if (test.marker_free_compact) {
        PpmImage image = read_ppm(pages.front());
        decode_base.push_back("--page-width=" + std::to_string(image.width));
        decode_base.push_back("--page-height=" + std::to_string(image.height));
    }
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
        if (test.id == "0023_gray_rotate_seeded") {
            makocode::ByteBuffer rotated_input, rotated_frame;
            require(read_entire_file(transformed_pages.front().c_str(), rotated_input),
                    test.id + ": could not read rotated image for state inspection");
            MetadataTile::Values rotated_tile = extract_tile_values(pages.front());
            ImageMappingConfig rotated_mapping;
            PpmParserState rotated_state;
            u64 rotated_frame_bits = 0u;
            require(ppm_extract_frame_bits(
                        rotated_input, rotated_mapping, rotated_frame,
                        rotated_frame_bits, rotated_state, false, nullptr, 1u,
                        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
                        nullptr, &rotated_tile, nullptr, 0u, 0u, 0u, true,
                        false, false, false, nullptr),
                    test.id + ": direct rotated-frame extraction failed");
            require(rotated_state.has_rotation_degrees &&
                        fabs(rotated_state.rotation_degrees_value - 1.0) < 1e-12,
                    test.id + ": detected rotation was not snapped to exactly 1 degree: " +
                        std::to_string(rotated_state.rotation_degrees_value));
        }
    }
}

static void palette_symbol_codec_case(){
    const std::string id="0129_palette_symbol_codec";
    const u32 palette_sizes[]={2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u};
    const u64 expected_orders[]={256ull,6561ull,65536ull,390625ull,1679609ull,
        5764801ull,16777216ull,43046721ull,99999989ull,214358881ull,
        429981691ull,815730721ull,1475788999ull,2562890591ull,4294967296ull};
    for(usize profile_index=0u;profile_index<sizeof(palette_sizes)/sizeof(palette_sizes[0]);++profile_index){
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(palette_sizes[profile_index],profile),id+": profile unavailable");
        require(profile.field_order==expected_orders[profile_index],id+": field order mismatch");
        u32 expected_block_limit = (u32)(profile.field_order - 1u);
        if (expected_block_limit > makocode::PALETTE_SYMBOL_MAX_BLOCK_LENGTH) {
            expected_block_limit = makocode::PALETTE_SYMBOL_MAX_BLOCK_LENGTH;
        }
        require(profile.block_length==expected_block_limit,id+": RS block limit mismatch");
        if (palette_sizes[profile_index] == 2u) {
            u32 parity = 0u;
            require(makocode::palette_symbol_parity_for_redundancy(profile, 0.02, parity) &&
                        parity == 8u,
                    id + ": q2 parity did not round up to a full print-error correction group");
            const u32 short_length = 59u;
            const u32 short_data_count = 53u;
            makocode::ByteBuffer short_data, short_code, short_damaged,
                short_erasures, short_recovered;
            require(short_data.ensure(short_data_count * 4u) &&
                        short_erasures.ensure(short_length),
                    id + ": q2 shortened-boundary buffers allocation failed");
            short_data.size = short_data_count * 4u;
            for (u32 i = 0u; i < short_data_count; ++i) {
                write_le_u32(short_data.data + i * 4u,
                             (i * 3571u + 19u) % profile.field_order);
            }
            require(makocode::palette_rs_encode(profile, short_data, short_length,
                                                 short_data_count, short_code) &&
                        short_damaged.ensure(short_code.size),
                    id + ": q2 shortened-boundary encoding failed");
            short_damaged.size = short_code.size;
            memcpy(short_damaged.data, short_code.data, short_code.size);
            short_erasures.size = short_length;
            memset(short_erasures.data, 0, short_length);
            for (u32 i = 0u; i < 3u; ++i) {
                u32 position = 3u + i * 11u;
                write_le_u32(short_damaged.data + position * 4u,
                             (read_le_u32(short_damaged.data + position * 4u) +
                              37u + i) % profile.field_order);
            }
            require(makocode::palette_rs_decode(profile, short_damaged, short_length,
                                                 short_data_count, short_erasures,
                                                 short_recovered) &&
                        short_recovered.size == short_data.size &&
                        memcmp(short_recovered.data, short_data.data,
                               short_data.size) == 0,
                    id + ": shortened q2 RS failed at the three-error parity boundary");
            const u32 full_field_block_length = 255u;
            const u32 full_field_data_count = 85u;
            const u32 full_field_error_count =
                (full_field_block_length - full_field_data_count) / 2u;
            makocode::ByteBuffer full_field_data, full_field_code,
                full_field_damaged, full_field_erasures, full_field_recovered;
            require(full_field_data.ensure(full_field_data_count * 4u) &&
                        full_field_erasures.ensure(full_field_block_length),
                    id + ": full-field correction buffers allocation failed");
            full_field_data.size = full_field_data_count * 4u;
            for (u32 i = 0u; i < full_field_data_count; ++i) {
                write_le_u32(full_field_data.data + i * 4u,
                             (i * 3571u + 101u) % profile.field_order);
            }
            require(makocode::palette_rs_encode(profile, full_field_data,
                                                full_field_block_length,
                                                full_field_data_count,
                                                full_field_code) &&
                        full_field_damaged.ensure(full_field_code.size),
                    id + ": full-field correction encoding failed");
            full_field_damaged.size = full_field_code.size;
            memcpy(full_field_damaged.data, full_field_code.data,
                   full_field_code.size);
            full_field_erasures.size = full_field_block_length;
            memset(full_field_erasures.data, 0, full_field_erasures.size);
            for (u32 error = 0u; error < full_field_error_count; ++error) {
                u32 position = (error * 73u + 11u) % full_field_block_length;
                u32 value = read_le_u32(full_field_damaged.data + position * 4u);
                write_le_u32(full_field_damaged.data + position * 4u,
                             value ^ (1u << (error & 7u)) ^ (error + 1u));
            }
            require(makocode::palette_rs_decode(profile, full_field_damaged,
                                                full_field_block_length,
                                                full_field_data_count,
                                                full_field_erasures,
                                                full_field_recovered) &&
                        full_field_recovered.size == full_field_data.size &&
                        memcmp(full_field_recovered.data, full_field_data.data,
                               full_field_data.size) == 0,
                    id + ": GF(256) failed at 85 unknown errors in a 255-symbol block");
            const u32 erasure_retry_data_count = 170u;
            const u32 erasure_retry_count = 65u;
            const u32 erasure_retry_block_length = 250u;
            const u32 erasure_retry_errors = 8u;
            makocode::ByteBuffer retry_data, retry_code, retry_damaged,
                retry_erasures, retry_recovered;
            require(retry_data.ensure(erasure_retry_data_count * 4u) &&
                        retry_erasures.ensure(erasure_retry_block_length),
                    id + ": q2 erasure-retry buffers allocation failed");
            retry_data.size = erasure_retry_data_count * 4u;
            for (u32 i = 0u; i < erasure_retry_data_count; ++i) {
                write_le_u32(retry_data.data + i * 4u,
                             (i * 3571u + 73u) % profile.field_order);
            }
            require(makocode::palette_rs_encode(profile, retry_data,
                                                erasure_retry_block_length,
                                                erasure_retry_data_count,
                                                retry_code) &&
                        retry_damaged.ensure(retry_code.size),
                    id + ": q2 erasure-retry encoding failed");
            retry_damaged.size = retry_code.size;
            memcpy(retry_damaged.data, retry_code.data, retry_code.size);
            retry_erasures.size = erasure_retry_block_length;
            memset(retry_erasures.data, 0, retry_erasures.size);
            for (u32 position = 0u; position < erasure_retry_count; ++position) {
                retry_erasures.data[position] = 1u;
            }
            for (u32 error = 0u; error < erasure_retry_errors; ++error) {
                u32 position = 100u + error;
                u32 value = read_le_u32(retry_damaged.data + position * 4u);
                write_le_u32(retry_damaged.data + position * 4u,
                             (value + 31u + error) % profile.field_order);
            }
            require(makocode::palette_rs_decode(profile, retry_damaged,
                                                erasure_retry_block_length,
                                                erasure_retry_data_count,
                                                retry_erasures,
                                                retry_recovered) &&
                        retry_recovered.size == retry_data.size &&
                        memcmp(retry_recovered.data, retry_data.data,
                               retry_data.size) == 0,
                    id + ": q2 RS did not recover one valid low-confidence symbol");
            memcpy(retry_damaged.data, retry_code.data, retry_code.size);
            for (u32 position = 0u; position < erasure_retry_count; ++position) {
                u32 value = read_le_u32(retry_damaged.data + position * 4u);
                write_le_u32(retry_damaged.data + position * 4u,
                             (value + 1u) % profile.field_order);
            }
            for (u32 error = 0u; error < erasure_retry_errors; ++error) {
                u32 position = 100u + error;
                u32 value = read_le_u32(retry_damaged.data + position * 4u);
                write_le_u32(retry_damaged.data + position * 4u,
                             (value + 31u + error) % profile.field_order);
            }
            require(!makocode::palette_rs_decode(profile, retry_damaged,
                                                 erasure_retry_block_length,
                                                 erasure_retry_data_count,
                                                 retry_erasures,
                                                 retry_recovered),
                    id + ": q2 RS accepted an all-wrong one-over-budget erasure set");
        }
        const u32 values[]={0u,1u,(u32)(profile.field_order/2u),
                            (u32)(profile.field_order-1u)};
        for(u32 value:values){
            u8 dots[8]={};u32 decoded=0u;
            require(makocode::palette_symbol_to_dots(profile,value,dots),id+": symbol to dots failed");
            for(u32 dot:dots)require(dot<profile.palette_size,id+": digit outside palette");
            require(makocode::palette_dots_to_symbol(profile,dots,decoded)&&decoded==value,id+": dot mapping did not round trip");
        }
        u64 pattern_count=1u;for(u32 dot=0u;dot<8u;++dot)pattern_count*=profile.palette_size;
        if (profile.palette_digit_degree && profile.palette_size <= 4u) {
            for (u32 value = 0u; value < profile.field_order; ++value) {
                u8 dots[8] = {};
                u32 decoded = 0u;
                require(makocode::palette_symbol_to_dots(profile, value, dots) &&
                            makocode::palette_dots_to_symbol(profile, dots, decoded) &&
                            decoded == value,
                        id + ": full-pattern mapping rejected or changed a valid symbol");
            }
        }
        if(profile.field_order<pattern_count){
            for(u64 unused=profile.field_order;unused<pattern_count;++unused){
                u32 rejected=0u;u8 dots[8]={};u64 value=unused;
                for(u32 reverse=8u;reverse>0u;--reverse){dots[reverse-1u]=(u8)(value%profile.palette_size);value/=profile.palette_size;}
                require(!makocode::palette_dots_to_symbol(profile,dots,rejected),id+": unused pattern accepted");
            }
        }
        if (profile.palette_size == 2u || profile.palette_size == 4u ||
            profile.palette_size == 8u || profile.palette_size == 16u) {
            u32 bits_per_symbol = profile.palette_size == 2u ? 8u :
                                  profile.palette_size == 4u ? 16u :
                                  profile.palette_size == 8u ? 24u : 32u;
            require(makocode::palette_symbol_count_for_bytes(profile, 96u) ==
                        768u / bits_per_symbol,
                    id + ": power-of-two palette did not pack a 96-byte chunk exactly");
        }
        for(usize byte_count:std::vector<usize>{0u,1u,7u,63u,64u,65u,257u}){
            std::vector<u8> source(byte_count);
            for(usize i=0u;i<byte_count;++i)source[i]=(u8)((i*73u+profile_index*29u+11u)&255u);
            makocode::ByteBuffer symbols,decoded;
            require(makocode::palette_bytes_to_symbols(profile,source.data(),source.size(),symbols),id+": byte packing failed");
            require(makocode::palette_symbols_to_bytes(profile,symbols,source.size(),decoded),id+": byte unpacking failed");
            require(decoded.size==source.size()&&(!source.size()||memcmp(decoded.data,source.data(),source.size())==0),id+": whole-stream base conversion mismatch");
        }
        const u32 block_length=31u,data_symbols=15u;
        makocode::ByteBuffer data,code,damaged,erasures,recovered;
        require(data.ensure(data_symbols*4u),id+": RS input allocation failed");data.size=data_symbols*4u;
        for(u32 i=0u;i<data_symbols;++i)write_le_u32(data.data+i*4u,(i*7919u+17u)%profile.field_order);
        write_le_u32(data.data, (u32)(profile.field_order - 1u));
        require(makocode::palette_rs_encode(profile,data,block_length,data_symbols,code),id+": RS encoding failed");
        require(code.size==block_length*4u,id+": RS block size mismatch");
        if (profile.palette_digit_degree) {
            u32 vector_data[3] = {};
            u32 vector_expected[5] = {};
            switch (profile.palette_size) {
            case 2u:
                vector_data[0]=0x000000ADu; vector_data[1]=0x00000053u;
                vector_data[2]=0x000000E7u;
                vector_expected[0]=0x000000ADu; vector_expected[1]=0x00000053u;
                vector_expected[2]=0x000000E7u; vector_expected[3]=0x00000069u;
                vector_expected[4]=0x000000DDu;
                break;
            case 3u:
                vector_data[0]=0x00001234u; vector_data[1]=0x0000011Bu;
                vector_data[2]=0x000018ABu;
                vector_expected[0]=0x00001234u; vector_expected[1]=0x0000011Bu;
                vector_expected[2]=0x000018ABu; vector_expected[3]=0x00000A24u;
                vector_expected[4]=0x00000A4Fu;
                break;
            case 4u:
                vector_data[0]=0x00001234u; vector_data[1]=0x0000A55Au;
                vector_data[2]=0x0000FEDCu;
                vector_expected[0]=0x00001234u; vector_expected[1]=0x0000A55Au;
                vector_expected[2]=0x0000FEDCu; vector_expected[3]=0x0000FFF2u;
                vector_expected[4]=0x0000A474u;
                break;
            case 5u:
                vector_data[0]=0x00012345u; vector_data[1]=0x00023456u;
                vector_data[2]=0x00034567u;
                vector_expected[0]=0x00012345u; vector_expected[1]=0x00023456u;
                vector_expected[2]=0x00034567u; vector_expected[3]=0x00040231u;
                vector_expected[4]=0x0000312Du;
                break;
            case 7u:
                vector_data[0]=0x00123456u; vector_data[1]=0x00234567u;
                vector_data[2]=0x00345678u;
                vector_expected[0]=0x00123456u; vector_expected[1]=0x00234567u;
                vector_expected[2]=0x00345678u; vector_expected[3]=0x000A6CA5u;
                vector_expected[4]=0x00383664u;
                break;
            case 8u:
                vector_data[0]=0x00FEDCBAu; vector_data[1]=0x00812345u;
                vector_data[2]=0x00123456u;
                vector_expected[0]=0x00FEDCBAu; vector_expected[1]=0x00812345u;
                vector_expected[2]=0x00123456u; vector_expected[3]=0x009F0103u;
                vector_expected[4]=0x000C1610u;
                break;
            case 9u:
                vector_data[0]=0x01234567u; vector_data[1]=0x00ABCDEFu;
                vector_data[2]=0x00F1E2D3u;
                vector_expected[0]=0x01234567u; vector_expected[1]=0x00ABCDEFu;
                vector_expected[2]=0x00F1E2D3u; vector_expected[3]=0x01E1B1F9u;
                vector_expected[4]=0x00CE6643u;
                break;
            case 11u:
                vector_data[0]=0x01234567u; vector_data[1]=0x00543210u;
                vector_data[2]=0x00FEDCBAu;
                vector_expected[0]=0x01234567u; vector_expected[1]=0x00543210u;
                vector_expected[2]=0x00FEDCBAu; vector_expected[3]=0x00B3105Au;
                vector_expected[4]=0x009A2650u;
                break;
            case 13u:
                vector_data[0]=0x12345678u; vector_data[1]=0x23456789u;
                vector_data[2]=0x03456789u;
                vector_expected[0]=0x12345678u; vector_expected[1]=0x23456789u;
                vector_expected[2]=0x03456789u; vector_expected[3]=0x0C418D2Au;
                vector_expected[4]=0x14CC18D7u;
                break;
            case 16u:
                vector_data[0]=0x80000000u; vector_data[1]=0x12345678u;
                vector_data[2]=0xDEADBEEFu;
                vector_expected[0]=0x80000000u; vector_expected[1]=0x12345678u;
                vector_expected[2]=0xDEADBEEFu; vector_expected[3]=0xEFBB29EEu;
                vector_expected[4]=0x2322C179u;
                break;
            default:
                require(false, id + ": missing full-field known vector");
            }
            makocode::ByteBuffer known_data, known_code;
            require(known_data.ensure(sizeof(vector_data)),
                    id + ": known-vector data allocation failed");
            known_data.size = sizeof(vector_data);
            for (u32 i = 0u; i < 3u; ++i) {
                write_le_u32(known_data.data + i * 4u, vector_data[i]);
            }
            require(makocode::palette_rs_encode(profile, known_data, 5u, 3u, known_code) &&
                        known_code.size == sizeof(vector_expected),
                    id + ": known-vector encoding failed");
            for (u32 i = 0u; i < 5u; ++i) {
                require(read_le_u32(known_code.data + i * 4u) == vector_expected[i],
                        id + ": extension-field known vector mismatch");
            }
        }
        require(damaged.ensure(code.size)&&erasures.ensure(block_length),id+": RS damage allocation failed");
        memcpy(damaged.data,code.data,code.size);damaged.size=code.size;erasures.size=block_length;memset(erasures.data,0,block_length);
        write_le_u32(damaged.data+7u*4u,(read_le_u32(damaged.data+7u*4u)+123u)%profile.field_order);
        erasures.data[2u]=1u;erasures.data[20u]=1u;
        require(makocode::palette_rs_decode(profile,damaged,block_length,data_symbols,erasures,recovered),id+": RS did not recover within mixed error/erasure bound");
        require(recovered.size==data.size&&memcmp(recovered.data,data.data,data.size)==0,id+": RS corrected data mismatch");
        memcpy(damaged.data,code.data,code.size);memset(erasures.data,0,block_length);
        for(u32 position:{1u,6u,13u,28u})erasures.data[position]=1u;
        for(u32 position:{2u,5u,9u,16u,22u,29u})write_le_u32(damaged.data+position*4u,(read_le_u32(damaged.data+position*4u)+position+1u)%profile.field_order);
        require(makocode::palette_rs_decode(profile,damaged,block_length,data_symbols,erasures,recovered),id+": RS failed at mixed error/erasure boundary");
        require(recovered.size==data.size&&memcmp(recovered.data,data.data,data.size)==0,id+": mixed error/erasure boundary recovery mismatch");
        const u32 tight_data_symbols=23u;
        makocode::ByteBuffer tight_data,tight_code,tight_damaged,tight_erasures,tight_recovered;
        require(tight_data.ensure(tight_data_symbols*4u),id+": tight RS input allocation failed");tight_data.size=tight_data_symbols*4u;
        for(u32 i=0u;i<tight_data_symbols;++i)write_le_u32(tight_data.data+i*4u,(i*3571u+91u)%profile.field_order);
        require(makocode::palette_rs_encode(profile,tight_data,block_length,tight_data_symbols,tight_code),id+": tight RS encoding failed");
        require(tight_damaged.ensure(tight_code.size)&&tight_erasures.ensure(block_length),id+": tight RS damage allocation failed");
        tight_damaged.size=tight_code.size;memcpy(tight_damaged.data,tight_code.data,tight_code.size);tight_erasures.size=block_length;memset(tight_erasures.data,0,block_length);
        for(u32 i=0u;i<4u;++i)write_le_u32(tight_damaged.data+i*4u,(read_le_u32(tight_damaged.data+i*4u)+i+1u)%profile.field_order);
        require(makocode::palette_rs_decode(profile,tight_damaged,block_length,tight_data_symbols,tight_erasures,tight_recovered),id+": RS failed at error bound");
        require(tight_recovered.size==tight_data.size&&memcmp(tight_recovered.data,tight_data.data,tight_data.size)==0,id+": RS boundary recovery mismatch");
        write_le_u32(tight_damaged.data+4u*4u,(read_le_u32(tight_damaged.data+4u*4u)+19u)%profile.field_order);
        require(!makocode::palette_rs_decode(profile,tight_damaged,block_length,tight_data_symbols,tight_erasures,tight_recovered),id+": RS accepted beyond error bound");
        const u32 erasure_data_symbols=1u;
        makocode::ByteBuffer one_data,one_code,one_erasures,one_recovered;
        require(one_data.ensure(4u)&&one_erasures.ensure(block_length),id+": erasure-bound buffers allocation failed");
        one_data.size=4u;write_le_u32(one_data.data,profile.field_order-1u);
        require(makocode::palette_rs_encode(profile,one_data,block_length,erasure_data_symbols,one_code),id+": erasure-bound RS encoding failed");
        one_erasures.size=block_length;memset(one_erasures.data,1,block_length-1u);one_erasures.data[block_length-1u]=0u;
        require(makocode::palette_rs_decode(profile,one_code,block_length,erasure_data_symbols,one_erasures,one_recovered),id+": RS failed at full erasure budget");
        require(one_recovered.size==4u&&read_le_u32(one_recovered.data)==profile.field_order-1u,id+": full erasure recovery mismatch");
        one_erasures.data[block_length-1u]=1u;
        require(!makocode::palette_rs_decode(profile,one_code,block_length,erasure_data_symbols,one_erasures,one_recovered),id+": RS accepted beyond erasure budget");
        if (palette_sizes[profile_index] == 2u) {
            makocode::ByteBuffer boundary_data, boundary_code, boundary_damaged,
                boundary_erasure_map, boundary_recovered;
            const u32 boundary_data_symbols = 80u;
            require(boundary_data.ensure(boundary_data_symbols * 4u) &&
                    boundary_erasure_map.ensure(profile.block_length),
                    id + ": exact mixed-boundary buffers allocation failed");
            boundary_data.size = boundary_data_symbols * 4u;
            for (u32 i = 0u; i < boundary_data_symbols; ++i) {
                write_le_u32(boundary_data.data + i * 4u,
                             (i * 3571u + 37u) % profile.field_order);
            }
            require(makocode::palette_rs_encode(profile, boundary_data,
                                                profile.block_length,
                                                boundary_data_symbols,
                                                boundary_code),
                    id + ": exact mixed-boundary RS encoding failed");
            require(boundary_damaged.ensure(boundary_code.size),
                    id + ": exact mixed-boundary damage allocation failed");
            boundary_damaged.size = boundary_code.size;
            memcpy(boundary_damaged.data, boundary_code.data, boundary_code.size);
            boundary_erasure_map.size = profile.block_length;
            memset(boundary_erasure_map.data, 0, boundary_erasure_map.size);
            u32 erased = 0u;
            for (u32 i = 0u; i < profile.block_length && erased < 30u; ++i) {
                u32 position = (i * 73u + 17u) % profile.block_length;
                if (boundary_erasure_map.data[position]) continue;
                boundary_erasure_map.data[position] = 1u;
                ++erased;
            }
            u32 errors = 0u;
            for (u32 i = 0u; i < profile.block_length && errors < 70u; ++i) {
                u32 position = (i * 137u + 11u) % profile.block_length;
                if (boundary_erasure_map.data[position]) continue;
                u32 value = (read_le_u32(boundary_damaged.data + position * 4u) +
                             position + 1u) % profile.field_order;
                write_le_u32(boundary_damaged.data + position * 4u, value);
                ++errors;
            }
            require(erased == 30u && errors == 70u,
                    id + ": exact mixed-boundary fixture was incomplete");
            require(makocode::palette_rs_decode(profile, boundary_damaged,
                                                profile.block_length,
                                                boundary_data_symbols,
                                                boundary_erasure_map,
                                                boundary_recovered),
                    id + ": RS failed at 70-error/30-erasure parity boundary");
            require(boundary_recovered.size == boundary_data.size &&
                    memcmp(boundary_recovered.data, boundary_data.data,
                           boundary_data.size) == 0,
                    id + ": exact mixed-boundary recovery mismatch");
            const u32 bootstrap_length=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS,bootstrap_data_symbols=makocode::PALETTE_SYMBOL_BOOTSTRAP_DATA_SYMBOLS;
            makocode::ByteBuffer bootstrap_data,bootstrap_code,bootstrap_damaged,bootstrap_erasures,bootstrap_recovered;
            require(bootstrap_data.ensure(bootstrap_data_symbols*4u)&&bootstrap_erasures.ensure(bootstrap_length),id+": bootstrap RS buffers allocation failed");
            bootstrap_data.size=bootstrap_data_symbols*4u;
            for(u32 i=0u;i<bootstrap_data_symbols;++i)write_le_u32(bootstrap_data.data+i*4u,(i*3571u+19u)%profile.field_order);
            require(makocode::palette_rs_encode(profile,bootstrap_data,bootstrap_length,bootstrap_data_symbols,bootstrap_code),id+": bootstrap RS encoding failed");
            require(bootstrap_damaged.ensure(bootstrap_code.size),id+": bootstrap RS damage allocation failed");bootstrap_damaged.size=bootstrap_code.size;
            const u32 bootstrap_erasure_positions[]={0u,2u,5u,8u,11u,14u,17u,20u,23u,26u,29u,32u,35u,38u};
            memcpy(bootstrap_damaged.data,bootstrap_code.data,bootstrap_code.size);memset(bootstrap_erasures.data,0,bootstrap_length);bootstrap_erasures.size=bootstrap_length;
            for(u32 position:bootstrap_erasure_positions)bootstrap_erasures.data[position]=1u;
            write_le_u32(bootstrap_damaged.data+47u*4u,(read_le_u32(bootstrap_damaged.data+47u*4u)+23u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,bootstrap_damaged,bootstrap_length,bootstrap_data_symbols,bootstrap_erasures,bootstrap_recovered),id+": bootstrap RS failed at 1-error/14-erasure boundary");
            require(bootstrap_recovered.size==bootstrap_data.size&&memcmp(bootstrap_recovered.data,bootstrap_data.data,bootstrap_data.size)==0,id+": bootstrap 1-error/14-erasure recovery mismatch");
            memcpy(bootstrap_damaged.data,bootstrap_code.data,bootstrap_code.size);memset(bootstrap_erasures.data,0,bootstrap_length);
            for(u32 position:{0u,2u,5u,8u,11u,14u})bootstrap_erasures.data[position]=1u;
            for(u32 position:{17u,20u,23u,26u,29u})write_le_u32(bootstrap_damaged.data+position*4u,(read_le_u32(bootstrap_damaged.data+position*4u)+position+1u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,bootstrap_damaged,bootstrap_length,bootstrap_data_symbols,bootstrap_erasures,bootstrap_recovered),id+": bootstrap RS failed at 5-error/6-erasure boundary");
            require(bootstrap_recovered.size==bootstrap_data.size&&memcmp(bootstrap_recovered.data,bootstrap_data.data,bootstrap_data.size)==0,id+": bootstrap 5-error/6-erasure recovery mismatch");
            memcpy(bootstrap_damaged.data,bootstrap_code.data,bootstrap_code.size);memset(bootstrap_erasures.data,0,bootstrap_length);
            const u32 boundary_errors=6u,boundary_erasures=(bootstrap_length-bootstrap_data_symbols)-2u*boundary_errors;
            for(u32 position=0u;position<boundary_erasures;++position)bootstrap_erasures.data[position]=1u;
            for(u32 position=boundary_erasures;position<boundary_erasures+boundary_errors;++position)write_le_u32(bootstrap_damaged.data+position*4u,(read_le_u32(bootstrap_damaged.data+position*4u)+position+1u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,bootstrap_damaged,bootstrap_length,bootstrap_data_symbols,bootstrap_erasures,bootstrap_recovered),id+": bootstrap RS failed at mixed boundary");
            require(bootstrap_recovered.size==bootstrap_data.size&&memcmp(bootstrap_recovered.data,bootstrap_data.data,bootstrap_data.size)==0,id+": bootstrap mixed-boundary recovery mismatch");
            makocode::ByteBuffer full_data,full_code,full_erasures,full_recovered;
            require(full_data.ensure(4u)&&full_erasures.ensure(profile.block_length),id+": full-length RS buffers allocation failed");
            full_data.size=4u;write_le_u32(full_data.data,profile.field_order-1u);
            require(makocode::palette_rs_encode(profile,full_data,profile.block_length,1u,full_code),id+": full-length RS encoding failed");
            full_erasures.size=profile.block_length;memset(full_erasures.data,0,full_erasures.size);
            require(makocode::palette_rs_decode(profile,full_code,profile.block_length,1u,full_erasures,full_recovered),id+": full-length RS decoding failed");
            require(full_recovered.size==4u&&read_le_u32(full_recovered.data)==profile.field_order-1u,id+": full-length RS round trip mismatch");
            const u32 wide_data_symbols=142u;
            makocode::ByteBuffer wide_data,wide_code,wide_damaged,wide_erasures,wide_recovered;
            require(wide_data.ensure(wide_data_symbols*4u)&&wide_erasures.ensure(profile.block_length),id+": wide RS buffers allocation failed");
            wide_data.size=wide_data_symbols*4u;
            for(u32 i=0u;i<wide_data_symbols;++i)write_le_u32(wide_data.data+i*4u,(i*7919u+53u)%profile.field_order);
            require(makocode::palette_rs_encode(profile,wide_data,profile.block_length,wide_data_symbols,wide_code),id+": wide RS encoding failed");
            require(wide_damaged.ensure(wide_code.size),id+": wide RS damage allocation failed");
            wide_damaged.size=wide_code.size;memcpy(wide_damaged.data,wide_code.data,wide_code.size);memset(wide_erasures.data,0,profile.block_length);wide_erasures.size=profile.block_length;
            for(u32 position:{0u,17u,39u,88u,131u,177u,211u,249u})write_le_u32(wide_damaged.data+position*4u,(read_le_u32(wide_damaged.data+position*4u)+position+1u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,wide_damaged,profile.block_length,wide_data_symbols,wide_erasures,wide_recovered),id+": wide RS failed within unknown-error bound");
            require(wide_recovered.size==wide_data.size&&memcmp(wide_recovered.data,wide_data.data,wide_data.size)==0,id+": wide RS correction mismatch");
            memcpy(wide_damaged.data,wide_code.data,wide_code.size);memset(wide_erasures.data,0,profile.block_length);
            for(u32 position:{1u,6u,13u,28u,33u,48u})wide_erasures.data[position]=1u;
            for(u32 position:{2u,5u,9u,16u,22u})write_le_u32(wide_damaged.data+position*4u,(read_le_u32(wide_damaged.data+position*4u)+position+1u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,wide_damaged,profile.block_length,wide_data_symbols,wide_erasures,wide_recovered),id+": wide RS failed with mixed erasures and errors");
            require(wide_recovered.size==wide_data.size&&memcmp(wide_recovered.data,wide_data.data,wide_data.size)==0,id+": wide RS mixed correction mismatch");
            memcpy(wide_damaged.data,wide_code.data,wide_code.size);memset(wide_erasures.data,0,profile.block_length);
            for(u32 position:{3u,6u,8u,12u,19u,27u,38u,51u,67u,84u,103u,129u,161u,201u})wide_erasures.data[position]=1u;
            write_le_u32(wide_damaged.data+247u*4u,(read_le_u32(wide_damaged.data+247u*4u)+19u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,wide_damaged,profile.block_length,wide_data_symbols,wide_erasures,wide_recovered),id+": wide RS failed with many erasures and one error");
            require(wide_recovered.size==wide_data.size&&memcmp(wide_recovered.data,wide_data.data,wide_data.size)==0,id+": wide RS many-erasure correction mismatch");
            memcpy(wide_damaged.data,wide_code.data,wide_code.size);memset(wide_erasures.data,0,profile.block_length);
            for(u32 position=0u;position<50u;++position)wide_erasures.data[position]=1u;
            for(u32 position=50u;position<79u;++position)write_le_u32(wide_damaged.data+position*4u,(read_le_u32(wide_damaged.data+position*4u)+position+1u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,wide_damaged,profile.block_length,wide_data_symbols,wide_erasures,wide_recovered),id+": q2 RS failed at 29-error/50-erasure parity boundary");
            require(wide_recovered.size==wide_data.size&&memcmp(wide_recovered.data,wide_data.data,wide_data.size)==0,id+": q2 RS parity-boundary recovery mismatch");
            makocode::ByteBuffer high_data,high_code,high_damaged,high_erasures,high_recovered;
            require(high_data.ensure(125u*4u)&&high_erasures.ensure(profile.block_length),id+": high-error RS buffers allocation failed");
            high_data.size=125u*4u;for(u32 i=0u;i<125u;++i)write_le_u32(high_data.data+i*4u,(i*3571u+37u)%profile.field_order);
            require(makocode::palette_rs_encode(profile,high_data,profile.block_length,125u,high_code),id+": high-error RS encoding failed");
            require(high_damaged.ensure(high_code.size),id+": high-error RS damage allocation failed");
            high_damaged.size=high_code.size;memcpy(high_damaged.data,high_code.data,high_code.size);memset(high_erasures.data,0,profile.block_length);high_erasures.size=profile.block_length;
            for(u32 position=0u;position<20u;++position)high_erasures.data[position]=1u;
            for(u32 position=20u;position<71u;++position)write_le_u32(high_damaged.data+position*4u,(read_le_u32(high_damaged.data+position*4u)+position+1u)%profile.field_order);
            require(makocode::palette_rs_decode(profile,high_damaged,profile.block_length,125u,high_erasures,high_recovered),id+": q2 RS failed at 51-error/20-erasure parity bound");
            require(high_recovered.size==high_data.size&&memcmp(high_recovered.data,high_data.data,high_data.size)==0,id+": q2 RS high-bound recovery mismatch");
        }
    }
    {
        MetadataTile::Values tile;
        tile.metadata_square_side = 2u;
        tile.metadata_length_prefix_bits = 1u;
        tile.metadata_symbol_count = 1u;
        tile.palette_count = 5u;
        MetadataTile::Placement placement =
            MetadataTile::compute_center_tile_placement(96u, 96u);
        makocode::ByteBuffer sequential_ranks, shuffled_ranks, repeated_ranks,
                             alternate_ranks;
        u64 sequential_count = 0u, shuffled_count = 0u, repeated_count = 0u,
            alternate_count = 0u;
        require(MetadataTile::build_payload_rank_map(
                    96u, 96u, placement, tile, 0, 0u, sequential_ranks,
                    sequential_count, false, 0u),
                id + ": sequential symbol layout failed");
        require(MetadataTile::build_payload_rank_map(
                    96u, 96u, placement, tile, 0, 0u, shuffled_ranks,
                    shuffled_count, false,
                    makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED),
                id + ": spatially interleaved layout failed");
        require(MetadataTile::build_payload_rank_map(
                    96u, 96u, placement, tile, 0, 0u, repeated_ranks,
                    repeated_count, false,
                    makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED),
                id + ": repeat spatial interleaver failed");
        require(MetadataTile::build_payload_rank_map(
                    96u, 96u, placement, tile, 0, 0u, alternate_ranks,
                    alternate_count, false, 0x3141592653589793ull),
                id + ": alternate spatial interleaver failed");
        require(sequential_count == shuffled_count &&
                    shuffled_count == repeated_count &&
                    repeated_count == alternate_count &&
                    shuffled_ranks.size == repeated_ranks.size &&
                    memcmp(shuffled_ranks.data, repeated_ranks.data,
                           shuffled_ranks.size) == 0 &&
                    sequential_ranks.size == shuffled_ranks.size &&
                    memcmp(sequential_ranks.data, shuffled_ranks.data,
                           shuffled_ranks.size) == 0,
                id + ": spatial interleaver changed capacity or is not deterministic");
        const u64* sequential = (const u64*)sequential_ranks.data;
        const u64* shuffled = (const u64*)alternate_ranks.data;
        usize pixel_count = alternate_ranks.size / sizeof(u64);
        std::vector<u8> seen((usize)shuffled_count, 0u);
        bool layout_changed = false;
        u64 ranked_pixels = 0u;
        for (usize pixel = 0u; pixel < pixel_count; ++pixel) {
            if (shuffled[pixel] >= shuffled_count) continue;
            require(seen[(usize)shuffled[pixel]] == 0u,
                    id + ": spatial permutation duplicated a symbol dot rank");
            seen[(usize)shuffled[pixel]] = 1u;
            ++ranked_pixels;
            if (sequential[pixel] != shuffled[pixel]) layout_changed = true;
        }
        require(ranked_pixels == shuffled_count && layout_changed,
                id + ": interleaver did not permute the complete payload rank set");

        makocode::PaletteSymbolPasswordContext placement_key_a,placement_key_b;
        makocode::ByteBuffer password_ranks_a,password_ranks_b;
        u64 password_count_a=0u,password_count_b=0u;
        require(placement_key_a.derive("first-password",14u)&&
                    placement_key_b.derive("second-password",15u)&&
                    MetadataTile::build_payload_rank_map(
                        96u,96u,placement,tile,0,0u,password_ranks_a,
                        password_count_a,false,
                        makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED,0,false,
                        placement_key_a.shuffle_key())&&
                    MetadataTile::build_payload_rank_map(
                        96u,96u,placement,tile,0,0u,password_ranks_b,
                        password_count_b,false,
                        makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED,0,false,
                        placement_key_b.shuffle_key())&&
                    password_count_a==password_count_b&&
                    password_ranks_a.size==password_ranks_b.size,
                id+": password-keyed spatial placement failed");
        const u64* password_map_a=(const u64*)password_ranks_a.data;
        const u64* password_map_b=(const u64*)password_ranks_b.data;
        bool bootstrap_layout_changed=false;
        for(usize pixel=0u;pixel<password_ranks_a.size/sizeof(u64);++pixel){
            bool in_bootstrap_a=password_map_a[pixel]<
                (u64)makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS*8u;
            bool in_bootstrap_b=password_map_b[pixel]<
                (u64)makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS*8u;
            if(in_bootstrap_a!=in_bootstrap_b ||
               (in_bootstrap_a && password_map_a[pixel]!=password_map_b[pixel])){
                bootstrap_layout_changed=true;
                break;
            }
        }
        require(bootstrap_layout_changed,
                id+": bootstrap symbol placement did not depend on password");

        u8 frame_data[4] = {1u, 2u, 3u, 4u};
        u8 symbol_erasure[1] = {1u};
        PpmParserState no_metadata;
        makocode::ByteBuffer payload, erased;
        u64 payload_bits = 0u;
        require(frame_bits_to_payload(frame_data, 32u, no_metadata, payload,
                                      payload_bits, 0, symbol_erasure, 32u,
                                      &erased) &&
                    payload_bits == 32u && erased.size == 1u && erased.data[0] == 1u,
                id + ": per-field-symbol erasure map was not preserved");
    }
    makocode::PaletteSymbolProfile password_profile;
    require(makocode::palette_symbol_profile(6u,password_profile),id+": password profile unavailable");
    makocode::ByteBuffer secret_symbols,original_symbols,other_symbols;
    require(secret_symbols.ensure(16u)&&original_symbols.ensure(16u)&&other_symbols.ensure(16u),id+": mask buffers allocation failed");
    secret_symbols.size=original_symbols.size=other_symbols.size=16u;
    for(u32 i=0u;i<4u;++i){u32 value=(i*100003u+17u)%password_profile.field_order;write_le_u32(secret_symbols.data+i*4u,value);write_le_u32(original_symbols.data+i*4u,value);write_le_u32(other_symbols.data+i*4u,value);}
    makocode::PaletteSymbolPasswordContext shuffle_a,shuffle_b,shuffle_other;
    require(shuffle_a.derive("secret",6u)&&shuffle_b.derive("secret",6u)&&
            shuffle_other.derive("other",5u)&&
            memcmp(shuffle_a.shuffle_key(),shuffle_b.shuffle_key(),32u)==0&&
            memcmp(shuffle_a.shuffle_key(),shuffle_other.shuffle_key(),32u)!=0,
            id+": 256-bit shuffle key is not deterministic or password-specific");
    u8 selector_a[255u]={},selector_b[255u]={},selector_other[255u]={};
    require(makocode::palette_symbol_selector_mask(0u,0u,255u,64u,selector_a,
                                                   shuffle_a.shuffle_key())&&
            makocode::palette_symbol_selector_mask(0u,0u,255u,64u,selector_b,
                                                   shuffle_b.shuffle_key())&&
            makocode::palette_symbol_selector_mask(0u,0u,255u,64u,selector_other,
                                                   shuffle_other.shuffle_key())&&
            memcmp(selector_a,selector_b,sizeof(selector_a))==0&&
            memcmp(selector_a,selector_other,sizeof(selector_a))!=0,
            id+": password-keyed selector ordering is not stable and distinct");
    const char unicode_password[] = u8"pāss🔐";
    const char unicode_composed[] = u8"café";
    const char unicode_decomposed[] = u8"café";
    makocode::PaletteSymbolPasswordContext unicode_key,unicode_repeat,
        composed_key,decomposed_key;
    require(unicode_key.derive(unicode_password,sizeof(unicode_password)-1u)&&
            unicode_repeat.derive(unicode_password,sizeof(unicode_password)-1u)&&
            memcmp(unicode_key.shuffle_key(),unicode_repeat.shuffle_key(),32u)==0&&
            composed_key.derive(unicode_composed,sizeof(unicode_composed)-1u)&&
            decomposed_key.derive(unicode_decomposed,sizeof(unicode_decomposed)-1u)&&
            memcmp(composed_key.shuffle_key(),decomposed_key.shuffle_key(),32u)!=0,
            id+": UTF-8 password bytes were not handled deterministically and exactly");
    require(makocode::palette_symbol_password_mask(password_profile,secret_symbols,"secret",6u,false),id+": symbol mask failed");
    require(memcmp(secret_symbols.data,original_symbols.data,secret_symbols.size)!=0,id+": password mask left symbols unchanged");
    require(makocode::palette_symbol_password_mask(password_profile,secret_symbols,"secret",6u,true),id+": symbol unmask failed");
    require(memcmp(secret_symbols.data,original_symbols.data,secret_symbols.size)==0,id+": password unmask mismatch");
    makocode::ByteBuffer full_mask_symbols,range_mask_symbols;
    require(full_mask_symbols.ensure(37u*4u)&&range_mask_symbols.ensure(25u*4u),id+": range mask buffers allocation failed");
    full_mask_symbols.size=37u*4u;range_mask_symbols.size=25u*4u;
    for(u32 i=0u;i<37u;++i)write_le_u32(full_mask_symbols.data+i*4u,(i*17011u+29u)%password_profile.field_order);
    require(makocode::palette_symbol_password_mask(password_profile,full_mask_symbols,"secret",6u,false),id+": full range mask failed");
    memcpy(range_mask_symbols.data,full_mask_symbols.data+5u*4u,range_mask_symbols.size);
    require(makocode::palette_symbol_password_mask(password_profile,range_mask_symbols,"secret",6u,true,5u),id+": partial range unmask failed");
    for(u32 i=0u;i<25u;++i)require(read_le_u32(range_mask_symbols.data+i*4u)==((i+5u)*17011u+29u)%password_profile.field_order,id+": partial range mask offset mismatch");
    require(makocode::palette_symbol_password_mask(password_profile,other_symbols,"other",5u,false),id+": alternate password mask failed");
    require(memcmp(shuffle_a.shuffle_key(),shuffle_other.shuffle_key(),32u)!=0,
            id+": selector key does not depend on password");

    std::vector<u8> stream_source(333u);
    for(usize i=0u;i<stream_source.size();++i)stream_source[i]=(u8)((i*47u+19u)&255u);
    makocode::ByteBuffer stream,stream_erasures,stream_decoded;
    makocode::EccSummary stream_summary;
    require(makocode::palette_symbol_encode_stream(password_profile,stream_source.data(),stream_source.size(),
                                                   0.1,3u,2u,"secret",6u,stream,stream_summary),
            id+": protected stream encoding failed");
    usize stream_symbol_count=stream.size/4u;
    require(stream_summary.palette_symbol_stream&&stream_summary.overlay_erasure_budget==3u&&
            stream_summary.unknown_error_budget==2u,id+": stream summary mismatch");
    require(stream_erasures.ensure(stream_symbol_count),id+": stream erasure allocation failed");
    stream_erasures.size=stream_symbol_count;memset(stream_erasures.data,0,stream_symbol_count);
    for(u32 i=0u;i<3u;++i){usize index=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+17u+i*31u;u32 value=read_le_u32(stream.data+index*4u);write_le_u32(stream.data+index*4u,(value+101u+i)%password_profile.field_order);stream_erasures.data[index]=1u;}
    for(u32 i=0u;i<2u;++i){usize index=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+stream_summary.block_data_symbols-2u+i;u32 value=read_le_u32(stream.data+index*4u);write_le_u32(stream.data+index*4u,(value+211u+i)%password_profile.field_order);}
    bool stream_protected=false;
    require(makocode::palette_symbol_decode_stream(password_profile,stream,stream_erasures,"secret",6u,
                                                    stream_decoded,0,&stream_protected),
            id+": protected stream decoding failed within ECC bound");
    require(stream_protected&&stream_decoded.size==stream_source.size()&&
            memcmp(stream_decoded.data,stream_source.data(),stream_source.size())==0,
            id+": protected stream round trip mismatch");
    require(!makocode::palette_symbol_decode_stream(password_profile,stream,stream_erasures,"wrong",5u,
                                                     stream_decoded,0),
            id+": wrong password was accepted");

    makocode::ByteBuffer short_stream,short_decoded;
    makocode::EccSummary short_summary;
    require(makocode::palette_symbol_encode_stream(password_profile,stream_source.data(),stream_source.size(),
                                                   0.1,0u,0u,0,0u,short_stream,short_summary),
            id+": shortened stream encoding failed");
    require(short_summary.block_count==1u&&
            (u32)short_summary.block_data_symbols+short_summary.parity_symbols<
                password_profile.block_length,
            id+": small no-overlay stream did not shorten its RS block");
    require(makocode::palette_symbol_decode_stream(password_profile,short_stream,makocode::ByteBuffer(),
                                                    0,0u,short_decoded,0),
            id+": shortened stream decoding failed");
    require(short_decoded.size==stream_source.size()&&
            memcmp(short_decoded.data,stream_source.data(),stream_source.size())==0,
            id+": shortened stream round trip mismatch");
    makocode::ByteBuffer damaged_short,short_erasures,short_recovered;
    usize short_symbol_count=short_stream.size/4u;
    require(damaged_short.ensure(short_stream.size)&&short_erasures.ensure(short_symbol_count),
            id+": shortened ECC buffers allocation failed");
    damaged_short.size=short_stream.size;
    memcpy(damaged_short.data,short_stream.data,short_stream.size);
    short_erasures.size=short_symbol_count;
    memset(short_erasures.data,0,short_symbol_count);
    for(u32 i=0u;i<10u;++i){
        usize index=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+7u+i*3u;
        u32 value=read_le_u32(damaged_short.data+index*4u);
        write_le_u32(damaged_short.data+index*4u,
                     (value+31u+i)%password_profile.field_order);
    }
    for(u32 i=0u;i<5u;++i){
        usize index=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+41u+i*5u;
        u32 value=read_le_u32(damaged_short.data+index*4u);
        write_le_u32(damaged_short.data+index*4u,
                     (value+97u+i)%password_profile.field_order);
        short_erasures.data[index]=1u;
    }
    require(2u*10u+5u<=short_summary.parity_symbols&&
            makocode::palette_symbol_decode_stream(password_profile,damaged_short,
                short_erasures,0,0u,short_recovered,0),
            id+": shortened ECC failed within the mixed correction bound");
    require(short_recovered.size==stream_source.size()&&
            memcmp(short_recovered.data,stream_source.data(),stream_source.size())==0,
            id+": shortened ECC recovered the wrong payload");
    makocode::ByteBuffer over_budget_short;
    require(over_budget_short.ensure(short_stream.size),
            id+": shortened over-budget buffer allocation failed");
    over_budget_short.size=short_stream.size;
    memcpy(over_budget_short.data,short_stream.data,short_stream.size);
    u32 over_budget_errors=(u32)short_summary.parity_symbols/2u+1u;
    for(u32 i=0u;i<over_budget_errors;++i){
        usize index=makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+i;
        u32 value=read_le_u32(over_budget_short.data+index*4u);
        write_le_u32(over_budget_short.data+index*4u,
                     (value+151u+i)%password_profile.field_order);
    }
    require(2u*over_budget_errors>short_summary.parity_symbols&&
            !makocode::palette_symbol_decode_stream(password_profile,over_budget_short,
                makocode::ByteBuffer(),0,0u,short_recovered,0),
            id+": shortened ECC accepted errors beyond its correction bound");

    makocode::ByteBuffer compact_stream,compact_source_symbols,compact_decoded;
    makocode::EccSummary compact_summary;
    std::vector<u8> compact_source(3000u);
    for(usize i=0u;i<compact_source.size();++i)compact_source[i]=(u8)((i*53u+7u)&255u);
    require(makocode::palette_symbol_encode_stream(password_profile,compact_source.data(),compact_source.size(),
                                                   0.1,3u,2u,0,0u,compact_stream,compact_summary),
            id+": multi-block compact stream encoding failed");
    require(makocode::palette_bytes_to_symbols(password_profile,compact_source.data(),
                                               compact_source.size(),compact_source_symbols),
            id+": compact source conversion failed");
    usize compact_payload_symbol_count=compact_stream.size/4u-makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS;
    u64 expected_compact_symbol_count=compact_source_symbols.size/4u+
        compact_summary.block_count*compact_summary.parity_symbols;
    u32 compact_final_data_count=(u32)(compact_source_symbols.size/4u-
        (compact_summary.block_count-1u)*compact_summary.block_data_symbols);
    u32 compact_final_block_length=compact_final_data_count+
        compact_summary.parity_symbols;
    require(compact_summary.block_count>1u&&
                compact_payload_symbol_count==expected_compact_symbol_count&&
                compact_final_data_count<compact_summary.block_data_symbols&&
                compact_final_block_length<password_profile.block_length&&
                compact_summary.overlay_erasure_budget==3u,
            id+": overlay stream final RS block was not shortened");
    require(makocode::palette_symbol_decode_stream(password_profile,compact_stream,
                                                    makocode::ByteBuffer(),0,0u,compact_decoded,0)&&
                compact_decoded.size==compact_source.size()&&
                memcmp(compact_decoded.data,compact_source.data(),compact_source.size())==0,
            id+": unpadded multi-block stream did not round trip");
    makocode::ByteBuffer compact_damaged,compact_erasures,compact_recovered;
    usize compact_stream_symbols=compact_stream.size/4u;
    require(compact_damaged.ensure(compact_stream.size)&&
                compact_erasures.ensure(compact_stream_symbols),
            id+": shortened interleaved damage buffers allocation failed");
    compact_damaged.size=compact_stream.size;
    memcpy(compact_damaged.data,compact_stream.data,compact_stream.size);
    compact_erasures.size=compact_stream_symbols;
    memset(compact_erasures.data,0,compact_stream_symbols);
    for(u32 i=0u;i<3u;++i){
        u32 position=1u+i*3u;
        u64 interleaved=makocode::palette_symbol_interleaved_offset(
            compact_summary.block_count-1u,position,compact_summary.block_count,
            compact_final_block_length);
        usize index=(usize)(makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+
                            interleaved);
        u32 value=read_le_u32(compact_damaged.data+index*4u);
        write_le_u32(compact_damaged.data+index*4u,
                     (value+73u+i)%password_profile.field_order);
        compact_erasures.data[index]=1u;
    }
    for(u32 i=0u;i<2u;++i){
        u32 position=12u+i*4u;
        u64 interleaved=makocode::palette_symbol_interleaved_offset(
            compact_summary.block_count-1u,position,compact_summary.block_count,
            compact_final_block_length);
        usize index=(usize)(makocode::PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS+
                            interleaved);
        u32 value=read_le_u32(compact_damaged.data+index*4u);
        write_le_u32(compact_damaged.data+index*4u,
                     (value+109u+i)%password_profile.field_order);
    }
    require(2u*compact_summary.unknown_error_budget+
                compact_summary.overlay_erasure_budget<=compact_summary.parity_symbols&&
                makocode::palette_symbol_decode_stream(password_profile,
                    compact_damaged,compact_erasures,0,0u,compact_recovered,0)&&
                compact_recovered.size==compact_source.size()&&
                memcmp(compact_recovered.data,compact_source.data(),
                       compact_source.size())==0,
            id+": shortened final block failed interleaved mixed ECC recovery");
    {
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(2u, profile), id + ": high-error profile unavailable");
        const u32 data_count = 50u;
        makocode::ByteBuffer data, code, damaged, recovered;
        require(data.ensure(data_count * 4u), id + ": high-error data allocation failed");
        data.size = data_count * 4u;
        for (u32 i = 0u; i < data_count; ++i) {
            write_le_u32(data.data + i * 4u, (i * 73u + 19u) % profile.field_order);
        }
        require(makocode::palette_rs_encode(profile, data, profile.block_length,
                                            data_count, code),
                id + ": high-error RS encoding failed");
        require(damaged.ensure(code.size), id + ": high-error damage allocation failed");
        damaged.size = code.size;
        memcpy(damaged.data, code.data, code.size);
        for (u32 i = 0u; i < 58u; ++i) {
            usize position = (usize)((i * 37u) % profile.block_length);
            u32 value = read_le_u32(damaged.data + position * 4u);
            write_le_u32(damaged.data + position * 4u,
                         (value + i + 1u) % profile.field_order);
        }
        makocode::ByteBuffer no_erasures;
        require(makocode::palette_rs_decode(profile, damaged, profile.block_length,
                                            data_count, no_erasures, recovered),
                id + ": high-error RS recovery failed within its correction bound");
        require(recovered.size == data.size && memcmp(recovered.data, data.data, data.size) == 0,
                id + ": high-error RS correction mismatch");
    }
    {
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(4u, profile) &&
                    profile.block_length == 1024u,
                id + ": four-color profile did not use the 1,024-symbol cap");
        u32 maximum_parity = 0u;
        require(makocode::palette_symbol_parity_for_redundancy(
                    profile, 1023.0, maximum_parity) && maximum_parity == 1023u,
                id + ": maximum 1,023:1 parity/data ratio was rejected");
        const u32 block_length = 1024u;
        const u32 data_count = block_length - 2u;
        makocode::ByteBuffer data, code, damaged, erasures, recovered;
        require(data.ensure((usize)data_count * 4u) && erasures.ensure(block_length),
                id + ": 1,024-symbol test buffers allocation failed");
        data.size = (usize)data_count * 4u;
        for (u32 i = 0u; i < data_count; ++i) {
            write_le_u32(data.data + (usize)i * 4u,
                         (i * 7919u + 31u) % profile.field_order);
        }
        require(makocode::palette_rs_encode(profile, data, block_length, data_count, code) &&
                    damaged.ensure(code.size),
                id + ": 1,024-symbol RS encoding failed");
        damaged.size = code.size;
        memcpy(damaged.data, code.data, code.size);
        memset(erasures.data, 0, block_length);
        erasures.size = block_length;
        write_le_u32(damaged.data + 511u * 4u,
                     (read_le_u32(damaged.data + 511u * 4u) + 1u) % profile.field_order);
        require(makocode::palette_rs_decode(profile, damaged, block_length, data_count,
                                             erasures, recovered) &&
                    recovered.size == data.size &&
                    memcmp(recovered.data, data.data, data.size) == 0,
                id + ": 1,024-symbol RS correction failed");
    }
}

static void metadata_palette_marker_case() {
    const std::string id = "0131_metadata_palette_markers";
    const std::string payload_text = "makocode";
    struct MarkerFixture {
        const char* name;
        const char* palette;
        PaletteColor one;
    } fixtures[] = {
        {"q2", "White Black", {0u, 0u, 0u}},
        {"q4", "White Cyan Magenta Yellow", {255u, 255u, 0u}},
        {"q5", "White Cyan Magenta Yellow Black", {0u, 0u, 0u}},
        {"q8", "White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black",
         {0u, 0u, 0u}}
    };
    static const char expected_marker_bits[] =
        "11010001101111110011111101000101";
    for (const MarkerFixture& fixture : fixtures) {
        fs::path work = make_work_dir(id + "_" + fixture.name);
        fs::path payload = work / "payload.bin";
        fs::path encoded = work / "encoded";
        fs::create_directories(encoded);
        write_file(payload, payload_text);
        ProcessResult result = run_cli(
            work,
            encode_file_args(payload, encoded, "page",
                             {"--page-width=80", "--page-height=80",
                              "--no-fiducials",
                              "--palette", fixture.palette}),
            fixture.name);
        expect_success(result, id + " " + fixture.name + " encode");
        std::vector<std::string> pages = ppm_paths(encoded, "page");
        require(pages.size() == 1u, id + ": expected one metadata page");
        MetadataTile::Values tile = extract_tile_values(pages.front());
        u32 zero_index = tile.palette_count;
        u32 one_index = tile.palette_count;
        for (u32 color = 0u; color < tile.palette_count; ++color) {
            const PaletteColor& actual = tile.palette[color];
            if (actual.r == 255u && actual.g == 255u && actual.b == 255u) {
                zero_index = color;
            }
            if (actual.r == fixture.one.r && actual.g == fixture.one.g &&
                actual.b == fixture.one.b) {
                one_index = color;
            }
        }
        require(zero_index < tile.palette_count && one_index < tile.palette_count,
                id + ": marker palette lacks its fixed endpoint colors");
        u32 parsed_outer_length_bits = tile.metadata_outer_length_bits;
        makocode::ByteBuffer tile_bits;
        require(MetadataTile::build_tile_bits(tile, tile_bits),
                id + ": could not rebuild metadata tile");
        require(tile.metadata_outer_length_bits == parsed_outer_length_bits,
                id + ": metadata outer-length row was parsed noncanonically");
        PpmImage image = read_ppm(pages.front());
        MetadataTile::Placement placement =
            MetadataTile::compute_center_tile_placement(image.width, image.height);
        int tile_x = (int)placement.x0 - (int)placement.offset_x;
        int tile_y = (int)placement.y0 - (int)placement.offset_y;
        for (u32 bit = 0u; bit < 32u; ++bit) {
            bool one = expected_marker_bits[bit] == '1';
            u32 x = MetadataTile::tag_marker_x0() + (bit & 7u);
            u32 y = tile.metadata_tag_y0 + 1u + bit / 8u;
            usize cell = (usize)y * MetadataTile::TILE_SIDE + x;
            require(tile_bits.data[cell] == (u8)(one ? one_index : zero_index),
                    id + ": metadata marker uses the wrong palette color");
            int pixel_x = tile_x + (int)x;
            int pixel_y = tile_y + (int)y;
            require(pixel_x >= 0 && pixel_y >= 0 &&
                        pixel_x < (int)image.width && pixel_y < (int)image.height,
                    id + ": active metadata marker falls outside the page");
            usize pixel = ((usize)pixel_y * image.width + (usize)pixel_x) * 3u;
            const PaletteColor& expected = one ? fixture.one
                                               : PaletteColor{255u, 255u, 255u};
            require((u8)image.pixels[pixel] == expected.r &&
                        (u8)image.pixels[pixel + 1u] == expected.g &&
                        (u8)image.pixels[pixel + 2u] == expected.b,
                    id + ": printed marker color differs from its fixed palette color");
        }
        usize metadata_cells = (usize)tile.metadata_length_prefix_bits +
                               (usize)tile.metadata_symbol_count;
        usize first_row_cells = metadata_cells < tile.metadata_square_side
                                    ? metadata_cells
                                    : tile.metadata_square_side;
        require(first_row_cells > 0u,
                id + ": metadata square has no populated first row");
        for (usize cell = 0u; cell < first_row_cells; ++cell) {
            u32 local_x = tile.metadata_square_x0 + (u32)cell;
            u32 local_y = tile.metadata_square_y0;
            usize local = (usize)local_y * MetadataTile::TILE_SIDE + local_x;
            u32 symbol = tile_bits.data[local];
            require(symbol < tile.palette_count,
                    id + ": first metadata row contains an invalid palette symbol");
            int pixel_x = tile_x + (int)local_x;
            int pixel_y = tile_y + (int)local_y;
            usize pixel = ((usize)pixel_y * image.width + (usize)pixel_x) * 3u;
            const PaletteColor& expected = tile.palette[symbol];
            require((u8)image.pixels[pixel] == expected.r &&
                        (u8)image.pixels[pixel + 1u] == expected.g &&
                        (u8)image.pixels[pixel + 2u] == expected.b,
                    id + ": first metadata row was not rendered from its encoded symbols");
        }
        fs::path decoded = work / (std::string(fixture.name) + "_decoded");
        ProcessResult decode = run_cli(
            work,
            decode_file_args(pages, decoded,
                             {"--no-fiducials", "--palette", fixture.palette}),
            std::string(fixture.name) + "_decode");
        expect_success(decode, id + " " + fixture.name + " no-fiducial decode");
        compare_files(payload, decoded / "payload.bin",
                      id + " " + fixture.name + " no-fiducial payload");
    }
}

static std::vector<TestCase> build_cases() {
    std::vector<TestCase> cases;
    auto rt = [&](const char* id, const char* description, RoundTrip test) {
        test.id = id;
        cases.push_back({id, description, [test](const fs::path&) { run_roundtrip(test); }});
    };
    RoundTrip r;
    cases.push_back({"0139_byte_buffer_append",
                     "ByteBuffer append validates aliases, capacity, and null input",
                     [](const fs::path&) {
                         makocode::ByteBuffer buffer;
                         for (u8 value = 0u; value < 64u; ++value) {
                             require(buffer.push(value), "ByteBuffer setup allocation failed");
                         }
                         require(buffer.append_bytes(buffer.data + 8u, 56u),
                                 "ByteBuffer self-append failed");
                         require(buffer.size == 120u, "ByteBuffer self-append size changed");
                         for (usize i = 0u; i < 56u; ++i) {
                             require(buffer.data[64u + i] == (u8)(i + 8u),
                                     "ByteBuffer self-append contents changed");
                         }
                         require(!buffer.append_bytes(buffer.data, USIZE_MAX_VALUE),
                                 "ByteBuffer accepted an overflowing append length");
                         require(!buffer.append_bytes(nullptr, 1u),
                                 "ByteBuffer accepted a null non-empty append");
                         require(!buffer.reserve(buffer.size - 1u),
                                 "ByteBuffer accepted capacity below its current size");
                         require(buffer.reserve(buffer.capacity),
                                 "ByteBuffer rejected an existing capacity");
                     }});
    cases.push_back({"0140_ascii_u64_boundaries",
                     "Unsigned decimal parsing rejects overflow",
                     [](const fs::path&) {
                         u64 value = 0u;
                         const char maximum[] = "18446744073709551615";
                         const char overflow[] = "18446744073709551616";
                         const char wrapped[] = "36893488147419103231";
                         require(ascii_to_u64(maximum, sizeof(maximum) - 1u, &value) &&
                                     value == U64_MAX_VALUE,
                                 "Unsigned decimal parser rejected UINT64_MAX");
                         require(!ascii_to_u64(overflow, sizeof(overflow) - 1u, &value),
                                 "Unsigned decimal parser accepted UINT64_MAX + 1");
                         require(!ascii_to_u64(wrapped, sizeof(wrapped) - 1u, &value),
                                 "Unsigned decimal parser accepted a wrapped value");
                         require(!ascii_to_u64("", 0u, &value),
                                 "Unsigned decimal parser accepted an empty token");
                     }});
    cases.push_back({"0136_chacha20_poly1305_vectors",
                     "RFC 8439 authenticated encryption vectors",
                     [](const fs::path&) { chacha20_poly1305_vector_case(); }});
    cases.push_back({"0137_secure_random_padding",
                     "Padding uses independent operating-system randomness",
                     [](const fs::path&) { secure_random_padding_case(); }});
    cases.push_back({"0138_interactive_password",
                     "Passwords are prompted without terminal echo",
                     [](const fs::path&) { interactive_password_case(); }});
    r.size=1; r.literal="0"; r.encode_args={"--ecc=0.001","--no-metadata","--no-fiducials","--palette","White Black"}; r.decode_args={"--no-metadata","--no-fiducials","--palette","White Black"}; r.tiny_dimensions_check=true; r.marker_free_compact=true; rt("0000_tiny_0", "1 byte payload literal '0' encoded from stdin", r); r={};
    const char* makocode_ids[]={"0001_makocode_2_colors","0002_makocode_4_colors","0003_makocode_5_colors","0004_makocode_8_colors"};
    const char* makocode_palettes[]={"White Black","White Cyan Magenta Yellow","White Cyan Magenta Yellow Black","White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black"};
    const char* makocode_descriptions[]={"makocode encoded with 2 colors","makocode encoded with 4 colors","makocode encoded with 5 colors","makocode encoded with 8 colors"};
    for(size_t i=0;i<4;++i){r.size=8;r.literal="makocode";r.encode_args={"--ecc=0.001","--no-fiducials","--palette",makocode_palettes[i]};r.decode_args={"--no-fiducials","--palette",makocode_palettes[i]};r.tiny_dimensions_check=true;rt(makocode_ids[i],makocode_descriptions[i],r);r={};}
    r.size=8; r.literal="makocode"; r.default_ecc_check=true; rt("0130_default_ecc_2_colors", "Default q2 ECC and metadata marker", r); r={};
    cases.push_back({"0131_metadata_palette_markers", "Metadata markers use fixed endpoint colors", [](const fs::path&) { metadata_palette_marker_case(); }});
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
    r.size=102400; r.has_master_seed_override=true; r.master_seed_override=1501249248u; r.encode_args={"--page-width=1100","--page-height=1100"}; r.transform_args={"--scale-x","2.5","--scale-y","2.5","--seed","0"}; rt("0019_gray_100k_scaled","100 KiB scaled 2.5x",r); r={};
    r.size=102400; r.encode_args={"--page-width=1100","--page-height=1100"}; r.transform_args={"--scale-x","2.6","--scale-y","2.4","--seed","0"}; rt("0020_gray_100k_stretch_h26_v24","Grayscale horizontal 2.6 / vertical 2.4",r); r={};
    r.size=102400; r.encode_args={"--page-width=1100","--page-height=1100"}; r.transform_args={"--scale-x","2.4","--scale-y","2.6","--seed","0"}; rt("0021_gray_100k_stretch_h24_v26","Grayscale horizontal 2.4 / vertical 2.6",r); r={};
    r.size=65536; r.encode_args={"--page-width=900","--page-height=900"}; r.transform_args={"--scale-x","3.03","--scale-y","3.03","--seed","0"}; rt("0022_gray_scale_303_blur","Grayscale upsample 3.03x",r); r={};
    r.size=65536; r.has_master_seed_override=true; r.master_seed_override=2081436891u; r.encode_args={"--page-width=900","--page-height=900"}; r.transform_args={"--scale-x","3","--scale-y","3","--rotate","1","--seed","1337"}; rt("0023_gray_rotate_seeded","Grayscale rotation with deterministic seed",r); r={};
    r.size=65536; r.has_payload_seed_override=true; r.payload_seed_override=1254688731u; r.encode_args={"--page-width=900","--page-height=900","--ecc=fill"}; r.transform_args={"--scale-x","3","--scale-y","3","--skew-x","5","--skew-y","-5","--seed","1337"}; rt("0024_gray_skew_xy_seeded","Grayscale XY skew",r); r={};
    r.size=65536; r.has_payload_seed_override=true; r.payload_seed_override=1254688731u; r.encode_args={"--page-width=900","--page-height=900"}; r.transform_args={"--scale-x","3","--scale-y","3","--skew-x","-5","--skew-y","5","--seed","7331"}; rt("0025_gray_skew_xy_reverse_seeded","Grayscale reverse XY skew",r); r={};
    r.size=65536; r.has_payload_seed_override=true; r.payload_seed_override=1003199588u; r.encode_args={"--page-width=900","--page-height=900","--ecc=fill"}; r.transform_args={"--scale-x","3","--scale-y","3","--rotate","1","--skew-x","5","--skew-y","-5","--seed","1337"}; rt("0026_gray_rotate_skew_seeded","Grayscale rotation and skew",r); r={};
    r.size=32768; r.encode_args={"--password-cli=suite-password","--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"}; r.decode_args={"--password-cli=suite-password"}; rt("0027_password_ecc","Password-protected payload with ECC",r); r={};
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
    cases.push_back({"0206_cli_rejects_invalid_page_height","CLI rejects zero page height",[](const fs::path& work){
        ProcessResult result = run_cli(work, {"encode", "--page-height=0"}, "invalid_page_height");
        expect_failure(result, "0206 invalid page height");
        require(result.err.find("--page-height must be a positive integer number of pixels") != std::string::npos,
                "0206: invalid page-height diagnostic changed");
        require(result.err.find("unknown option") == std::string::npos,
                "0206: invalid page height was not reported as a parse failure");
    }});
    cases.push_back({"0207_cli_rejects_empty_page_height","CLI rejects an empty page-height value",[](const fs::path& work){
        ProcessResult result = run_cli(work, {"encode", "--page-height="}, "empty_page_height");
        expect_failure(result, "0207 empty page height");
        require(result.err.find("--page-height requires a value (pixels)") != std::string::npos,
                "0207: empty page-height diagnostic changed");
        require(result.err.find("unknown option") == std::string::npos,
                "0207: empty page height was not reported as a parse failure");
    }});
    cases.push_back({"0208_cli_page_height_separate_value","Page-height parser consumes a separate value",[](const fs::path&){
        char option[] = "--page-height";
        char value[] = "256";
        char* args[] = {option, value};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(process_image_mapping_option(2, args, &arg_index, mapping, "encode", &handled),
                "0208: separate page-height value was rejected");
        require(handled && arg_index == 1,
                "0208: parser did not consume the separate page-height value");
        require(mapping.page_height_set && mapping.page_height_pixels == 256u,
                "0208: parsed page height was not stored");
    }});
    cases.push_back({"0209_cli_rejects_missing_page_height","CLI rejects a missing separate page-height value",[](const fs::path& work){
        (void)work;
        char option[] = "--page-height";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0209: parser accepted a missing separate page-height value");
        require(!handled,
                "0209: parser marked a missing page-height value as handled");
    }});
    cases.push_back({"0210_cli_rejects_invalid_page_width","Page-width parser rejects zero pixels",[](const fs::path&){
        char option[] = "--page-width=0";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0210: parser accepted a zero page width");
        require(!handled && !mapping.page_width_set,
                "0210: rejected page width changed the mapping");
    }});
    cases.push_back({"0211_cli_rejects_empty_page_width","Page-width parser rejects an empty value",[](const fs::path&){
        char option[] = "--page-width=";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0211: parser accepted an empty page width");
        require(!handled && !mapping.page_width_set,
                "0211: rejected page width changed the mapping");
    }});
    cases.push_back({"0212_cli_page_width_separate_value","Page-width parser consumes a separate value",[](const fs::path&){
        char option[] = "--page-width";
        char value[] = "256";
        char* args[] = {option, value};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(process_image_mapping_option(2, args, &arg_index, mapping, "encode", &handled),
                "0212: separate page-width value was rejected");
        require(handled && arg_index == 1,
                "0212: parser did not consume the separate page-width value");
        require(mapping.page_width_set && mapping.page_width_pixels == 256u,
                "0212: parsed page width was not stored");
    }});
    cases.push_back({"0213_cli_rejects_missing_page_width","Page-width parser rejects a missing separate value",[](const fs::path&){
        char option[] = "--page-width";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0213: parser accepted a missing separate page-width value");
        require(!handled,
                "0213: parser marked a missing page-width value as handled");
    }});
    cases.push_back({"0214_cli_rejects_empty_palette","Palette parser rejects an empty value",[](const fs::path&){
        char option[] = "--palette=";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0214: parser accepted an empty palette");
        require(!handled && !mapping.palette_set,
                "0214: rejected palette changed the mapping");
    }});
    cases.push_back({"0215_cli_rejects_missing_palette","Palette parser rejects a missing separate value",[](const fs::path&){
        char option[] = "--palette";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0215: parser accepted a missing palette value");
        require(!handled && !mapping.palette_set,
                "0215: rejected palette changed the mapping");
    }});
    cases.push_back({"0216_cli_rejects_out_of_range_mapping_index","Image-mapping parser rejects an index at the argument count",[](const fs::path&){
        char option[] = "--palette";
        char extra[] = "White Black";
        char* args[] = {option, extra};
        int arg_index = 1;
        bool handled = false;
        ImageMappingConfig mapping;
        require(!process_image_mapping_option(1, args, &arg_index, mapping, "encode", &handled),
                "0216: parser accepted an argument index equal to arg_count");
        require(!handled && !mapping.palette_set,
                "0216: out-of-range argument changed the mapping");
    }});
    cases.push_back({"0217_cli_rejects_too_small_fiducial_spacing","Fiducial spacing parser rejects a value below the marker size",[](const fs::path&){
        char option[] = "--fiducial-spacing=1";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        FiducialGridDefaults defaults;
        require(!process_fiducial_spacing_option(1, args, &arg_index, defaults, "encode", &handled),
                "0217: parser accepted fiducial spacing smaller than the marker");
        require(!handled && defaults.spacing_pixels == 24u,
                "0217: rejected fiducial spacing changed defaults");
    }});
    cases.push_back({"0218_cli_rejects_non_numeric_fiducial_spacing","Fiducial spacing parser rejects non-numeric values",[](const fs::path&){
        char option[] = "--fiducial-spacing=abc";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        FiducialGridDefaults defaults;
        require(!process_fiducial_spacing_option(1, args, &arg_index, defaults, "encode", &handled),
                "0218: parser accepted non-numeric fiducial spacing");
        require(!handled && defaults.spacing_pixels == 24u,
                "0218: rejected fiducial spacing changed defaults");
    }});
    cases.push_back({"0219_cli_fiducial_spacing_separate_value","Fiducial spacing parser consumes a separate value",[](const fs::path&){
        char option[] = "--fiducial-spacing";
        char value[] = "48";
        char* args[] = {option, value};
        int arg_index = 0;
        bool handled = false;
        FiducialGridDefaults defaults;
        require(process_fiducial_spacing_option(2, args, &arg_index, defaults, "encode", &handled),
                "0219: parser rejected a valid separate fiducial spacing");
        require(handled && arg_index == 1 && defaults.spacing_pixels == 48u,
                "0219: parser did not consume or apply the separate spacing value");
    }});
    cases.push_back({"0220_cli_rejects_missing_fiducial_spacing","Fiducial spacing parser rejects a missing separate value",[](const fs::path&){
        char option[] = "--fiducial-spacing";
        char* args[] = {option};
        int arg_index = 0;
        bool handled = false;
        FiducialGridDefaults defaults;
        require(!process_fiducial_spacing_option(1, args, &arg_index, defaults, "encode", &handled),
                "0220: parser accepted missing fiducial spacing");
        require(!handled && defaults.spacing_pixels == 24u,
                "0220: rejected fiducial spacing changed defaults");
    }});
    cases.push_back({"0221_cli_rejects_out_of_range_fiducial_index","Fiducial spacing parser rejects an out-of-range argument index",[](const fs::path&){
        char option[] = "--unrelated-option";
        char* args[] = {option, option};
        int arg_index = 1;
        bool handled = true;
        FiducialGridDefaults defaults;
        require(!process_fiducial_spacing_option(1, args, &arg_index, defaults, "encode", &handled),
                "0221: parser accepted an argument index equal to arg_count");
        require(!handled,
                "0221: out-of-range argument index was marked handled");
    }});
    cases.push_back({"0222_metadata_guard_ring_uses_palette_noise","Palette-symbol metadata guard cells use non-endpoint palette noise",[](const fs::path& work){
        const std::string id = "0222_metadata_guard_ring_uses_palette_noise";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "metadata noise refill";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a single metadata page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        MetadataTile::PayloadRankMapCache rank_cache;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, &rank_cache, page_count),
                id + ": could not render the metadata page");
        fs::path rendered_path = work / (id + ".ppm");
        write_file(rendered_path,
                   std::string((const char*)rendered.data, rendered.size));
        PpmImage image = read_ppm(rendered_path);
        require(tile.palette_count == 4u && tile.ecc_enabled &&
                    tile.ecc_block_data > 0u && tile.ecc_parity > 0u,
                id + ": fixture did not use a four-color palette-symbol stream");
        makocode::ByteBuffer tile_bits, active_mask;
        u8 guard_mask[MetadataTile::TILE_SIDE * MetadataTile::TILE_SIDE] = {};
        require(MetadataTile::build_tile_bits(tile, tile_bits, &active_mask) &&
                    active_mask.size == sizeof(guard_mask) &&
                    MetadataTile::build_active_guard_mask(active_mask.data,
                                                          active_mask.size,
                                                          guard_mask,
                                                          sizeof(guard_mask)),
                id + ": could not rebuild metadata guard mask");
        MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(
            image.width, image.height);
        require(placement.valid, id + ": metadata tile placement is invalid");
        makocode::ByteBuffer fiducial_mask;
        u64 reserved_data_pixels = 0u;
        require(compute_fiducial_marker_mask(width, height, data_height,
                                             reserved_data_pixels, &fiducial_mask,
                                             false, 0u, 0u, 0u),
                id + ": could not rebuild the fiducial reservation mask");
        require(rank_cache.page_rank_map.size ==
                    (usize)width * data_height * sizeof(u64),
                id + ": renderer did not publish a complete payload rank map");
        const u64* page_ranks =
            (const u64*)rank_cache.page_rank_map.data;
        for (usize pixel = 0u; pixel < fiducial_mask.size; ++pixel) {
            if (fiducial_mask.data[pixel]) {
                require(page_ranks[pixel] == U64_MAX_VALUE,
                        id + ": fiducial marker pixel was assigned a payload rank");
            }
        }
        for (u32 y = 0u; y < MetadataTile::TILE_SIDE; ++y) {
            for (u32 x = 0u; x < MetadataTile::TILE_SIDE; ++x) {
                usize cell = (usize)y * MetadataTile::TILE_SIDE + x;
                if (!active_mask.data[cell]) continue;
                i64 page_x = (i64)placement.x0 + x - placement.offset_x;
                i64 page_y = (i64)placement.y0 + y - placement.offset_y;
                if (page_x < 0 || page_y < 0 || page_x >= width ||
                    page_y >= data_height) continue;
                usize pixel = (usize)page_y * width + (usize)page_x;
                require(page_ranks[pixel] == U64_MAX_VALUE,
                        id + ": metadata tile pixel was assigned a payload rank");
            }
        }
        u32 non_endpoint_guard_pixels = 0u;
        u64 guard_symbol_checksum = 1469598103934665603ull;
        for (u32 y = 0u; y < MetadataTile::TILE_SIDE; ++y) {
            for (u32 x = 0u; x < MetadataTile::TILE_SIDE; ++x) {
                usize cell = (usize)y * MetadataTile::TILE_SIDE + x;
                if (guard_mask[cell] != 2u) continue;
                u32 px = placement.x0 + x - placement.offset_x;
                u32 py = placement.y0 + y - placement.offset_y;
                usize pixel = ((usize)py * image.width + px) * 3u;
                u32 matched_symbol = tile.palette_count;
                for (u32 color_index = 0u; color_index < tile.palette_count; ++color_index) {
                    const PaletteColor& expected = tile.palette[color_index];
                    if ((u8)image.pixels[pixel] == expected.r &&
                        (u8)image.pixels[pixel + 1u] == expected.g &&
                        (u8)image.pixels[pixel + 2u] == expected.b) {
                        matched_symbol = color_index;
                        break;
                    }
                }
                require(matched_symbol < tile.palette_count,
                        id + ": guard cell is not rendered with a palette symbol");
                guard_symbol_checksum =
                    (guard_symbol_checksum ^ matched_symbol) * 1099511628211ull;
                if (matched_symbol >= 2u) ++non_endpoint_guard_pixels;
            }
        }
        require(non_endpoint_guard_pixels > 0u,
                id + ": guard cells did not render with palette noise beyond the light/dark endpoints");
        require(guard_symbol_checksum == 12247527806360534212ull,
                id + ": deterministic metadata guard noise changed: " +
                    std::to_string(guard_symbol_checksum));
    }});
    cases.push_back({"0223_metadata_outer_length_row_uses_palette_symbols","Outer metadata length row renders its encoded palette symbols",[](const fs::path& work){
        const std::string id = "0223_metadata_outer_length_row_uses_palette_symbols";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "outer length palette mapping";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a single metadata page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count),
                id + ": could not render the metadata page");
        fs::path rendered_path = work / "page.ppm";
        write_file(rendered_path,
                   std::string((const char*)rendered.data, rendered.size));
        PpmImage image = read_ppm(rendered_path);
        require(tile.palette_count == 4u && tile.metadata_outer_length_bits > 0u,
                id + ": fixture lacks a four-color outer length row");
        makocode::ByteBuffer tile_bits;
        require(MetadataTile::build_tile_bits(tile, tile_bits),
                id + ": could not rebuild metadata tile bits");
        MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(
            image.width, image.height);
        require(placement.valid, id + ": metadata tile placement is invalid");
        int tile_x = (int)placement.x0 - (int)placement.offset_x;
        int tile_y = (int)placement.y0 - (int)placement.offset_y;
        u32 length_row = tile.metadata_tag_y0 + MetadataTile::TILE_LENGTH_ROW;
        u32 length_x0 = (MetadataTile::TILE_SIDE - tile.metadata_outer_length_bits) / 2u;
        u32 one_symbols = 0u;
        for (u32 i = 0u; i < tile.metadata_outer_length_bits; ++i) {
            u32 x = length_x0 + i;
            usize cell = (usize)length_row * MetadataTile::TILE_SIDE + x;
            u32 symbol = tile_bits.data[cell];
            require(symbol < tile.palette_count,
                    id + ": length row contains a symbol outside its palette");
            int pixel_x = tile_x + (int)x;
            int pixel_y = tile_y + (int)length_row;
            usize pixel = ((usize)pixel_y * image.width + (usize)pixel_x) * 3u;
            const PaletteColor& expected = tile.palette[symbol];
            require((u8)image.pixels[pixel] == expected.r &&
                        (u8)image.pixels[pixel + 1u] == expected.g &&
                        (u8)image.pixels[pixel + 2u] == expected.b,
                    id + ": outer length symbol rendered with the wrong color");
            if (symbol == 1u) ++one_symbols;
        }
        require(one_symbols > 0u,
                id + ": length row has no one symbol to verify its palette mapping");
    }});
    cases.push_back({"0224_metadata_tag_rows_use_palette_symbols","Metadata tag rows render their encoded palette symbols",[](const fs::path& work){
        const std::string id = "0224_metadata_tag_rows_use_palette_symbols";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "metadata tag palette mapping";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a single metadata page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count),
                id + ": could not render the metadata page");
        fs::path rendered_path = work / (id + ".ppm");
        write_file(rendered_path,
                   std::string((const char*)rendered.data, rendered.size));
        PpmImage image = read_ppm(rendered_path);
        makocode::ByteBuffer tile_bits, active_mask;
        require(tile.palette_count == 4u &&
                    MetadataTile::build_tile_bits(tile, tile_bits, &active_mask),
                id + ": could not rebuild metadata tag symbols");
        MetadataTile::Placement placement = MetadataTile::compute_center_tile_placement(
            image.width, image.height);
        require(placement.valid, id + ": metadata tile placement is invalid");
        int tile_x = (int)placement.x0 - (int)placement.offset_x;
        int tile_y = (int)placement.y0 - (int)placement.offset_y;
        u32 colored_tag_symbols = 0u;
        for (u32 y = tile.metadata_tag_y0;
             y < tile.metadata_tag_y0 + MetadataTile::TILE_TAG_ROWS; ++y) {
            for (u32 x = 0u; x < MetadataTile::TILE_SIDE; ++x) {
                usize cell = (usize)y * MetadataTile::TILE_SIDE + x;
                if (!active_mask.data[cell]) continue;
                u32 symbol = tile_bits.data[cell];
                require(symbol < tile.palette_count,
                        id + ": tag row contains a symbol outside its palette");
                const PaletteColor& expected = tile.palette[symbol];
                int pixel_x = tile_x + (int)x;
                int pixel_y = tile_y + (int)y;
                usize pixel = ((usize)pixel_y * image.width + (usize)pixel_x) * 3u;
                require((u8)image.pixels[pixel] == expected.r &&
                            (u8)image.pixels[pixel + 1u] == expected.g &&
                            (u8)image.pixels[pixel + 2u] == expected.b,
                        id + ": tag symbol rendered with the wrong color");
                if ((expected.r == 255u && expected.g == 0u && expected.b == 0u) ||
                    (expected.r == 0u && expected.g == 0u && expected.b == 255u)) {
                    ++colored_tag_symbols;
                }
            }
        }
        require(colored_tag_symbols > 0u,
                id + ": tag rows lack a non-endpoint palette symbol");
    }});
    cases.push_back({"0225_render_rejects_overlapping_rank_map_cache","Page rendering rejects an overlapping rank-map cache",[](const fs::path&){
        const std::string id = "0225_render_rejects_overlapping_rank_map_cache";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "rank-map failure";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a single metadata page");

        MetadataTile::PayloadRankMapCache rank_cache;
        usize rank_map_bytes = (usize)width * data_height * sizeof(u64);
        u8* shared_rank_map = (u8*)makocode_platform_malloc(rank_map_bytes);
        require(shared_rank_map != 0,
                id + ": could not prepare the invalid rank-map cache");
        memset(shared_rank_map, 0, rank_map_bytes);
        rank_cache.page_rank_map.data = shared_rank_map;
        rank_cache.page_rank_map.capacity = rank_map_bytes;
        rank_cache.base_rank_map.data = shared_rank_map;
        rank_cache.base_rank_map.capacity = rank_map_bytes;

        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        bool ok = encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                     width, height, 1u, bits_per_page,
                                     &encoder.ecc_info(), 0.0, nullptr, 0u,
                                     footer, rendered,
                                     encoder.ecc_info().unknown_error_budget,
                                     encoder.ecc_info().overlay_erasure_budget,
                                     true, &tile, &rank_cache, page_count);
        rank_cache.page_rank_map.data = 0;
        rank_cache.page_rank_map.size = 0u;
        rank_cache.page_rank_map.capacity = 0u;
        require(!ok,
                id + ": page rendering accepted overlapping rank-map cache storage");
    }});
    cases.push_back({"0226_render_rejects_empty_page_width","Page rendering rejects an empty width before building metadata",[](const fs::path&){
        const std::string id = "0226_render_rejects_empty_page_width";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "invalid page width";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        FooterLayout footer;
        footer.data_height_pixels = 220u;
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        metadata_tile_build_call_count = 0u;
        bool ok = encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                     0u, 220u, 0u, frame_bit_count,
                                     &encoder.ecc_info(), 0.0, nullptr, 0u,
                                     footer, rendered,
                                     encoder.ecc_info().unknown_error_budget,
                                     encoder.ecc_info().overlay_erasure_budget,
                                     true, &tile, nullptr, 1u);
        require(!ok,
                id + ": page rendering accepted a zero-width page");
        require(metadata_tile_build_call_count == 0u,
                id + ": metadata tile was built before rejecting the invalid placement");
    }});
    cases.push_back({"0227_render_rejects_invalid_field_symbol","Page rendering rejects a field symbol outside its palette profile",[](const fs::path&){
        const std::string id = "0227_render_rejects_invalid_field_symbol";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "invalid field symbol";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count) &&
                    frame.size >= 4u,
                id + ": could not build a complete frame symbol");
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(mapping.custom_palette_count,
                                                  profile) &&
                    profile.field_order <= 0xFFFFFFFFu,
                id + ": could not build the palette-symbol profile");
        u32 invalid_symbol = (u32)profile.field_order;
        frame.data[0] = (u8)invalid_symbol;
        frame.data[1] = (u8)(invalid_symbol >> 8u);
        frame.data[2] = (u8)(invalid_symbol >> 16u);
        frame.data[3] = (u8)(invalid_symbol >> 24u);
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a single metadata page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        bool ok = encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                     width, height, 1u, bits_per_page,
                                     &encoder.ecc_info(), 0.0, nullptr, 0u,
                                     footer, rendered,
                                     encoder.ecc_info().unknown_error_budget,
                                     encoder.ecc_info().overlay_erasure_budget,
                                     true, &tile, nullptr, page_count);
        require(!ok,
                id + ": page rendering accepted a symbol outside the field");
    }});
    cases.push_back({"0228_render_rejects_unaligned_field_symbol_offset","Page rendering rejects a misaligned field-symbol offset before metadata construction",[](const fs::path&){
        const std::string id = "0228_render_rejects_unaligned_field_symbol_offset";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "unaligned field-symbol offset";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u && (bits_per_page & 31u) == 0u,
                id + ": could not lay out an aligned single metadata page");
        const u64 invalid_frame_bit_count = bits_per_page + 1u;
        usize required_frame_bytes = (usize)((invalid_frame_bit_count + 7u) / 8u);
        usize old_frame_size = frame.size;
        require(frame.ensure(required_frame_bytes),
                id + ": could not extend the frame backing buffer");
        if (required_frame_bytes > old_frame_size) {
            memset(frame.data + old_frame_size, 0,
                   required_frame_bytes - old_frame_size);
        }
        frame.size = required_frame_bytes;
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        metadata_tile_build_call_count = 0u;
        bool ok = encode_page_to_ppm(mapping, frame, invalid_frame_bit_count,
                                     1u, width, height, 1u, bits_per_page,
                                     &encoder.ecc_info(), 0.0, nullptr, 0u,
                                     footer, rendered,
                                     encoder.ecc_info().unknown_error_budget,
                                     encoder.ecc_info().overlay_erasure_budget,
                                     true, &tile, nullptr, page_count);
        require(!ok,
                id + ": page rendering accepted an unaligned field-symbol offset");
        require(metadata_tile_build_call_count == 0u,
                id + ": metadata construction began before alignment rejection");
    }});
    cases.push_back({"0229_render_rejects_header_allocation_failure","Page rendering rejects a PPM header allocation failure",[](const fs::path&){
        const std::string id = "0229_render_rejects_header_allocation_failure";
        const char palette_text[] = "#FF0000 #0000FF #FFFFFF #000000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the four-color palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "header allocation";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 100u, height = 100u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a single metadata page");
        bool found_header_allocation_failure = false;
        for (unsigned long failure_at = 1ul;
             failure_at <= 256ul && !found_header_allocation_failure;
             ++failure_at) {
            makocode::ByteBuffer rendered;
            require(rendered.append_ascii("sentinel"),
                    id + ": could not prepare the output marker");
            minify_allocation_count = 0ul;
            minify_allocation_failure_size = 0ul;
            minify_allocation_failure_at = failure_at;
            MetadataTile::Values tile;
            bool encoded = encode_page_to_ppm(
                mapping, frame, frame_bit_count, 0u, width, height, 1u,
                bits_per_page, &encoder.ecc_info(), 0.0, nullptr, 0u,
                footer, rendered, encoder.ecc_info().unknown_error_budget,
                encoder.ecc_info().overlay_erasure_budget, true, &tile,
                nullptr, page_count);
            unsigned long failed_allocation_size = minify_allocation_failure_size;
            minify_allocation_failure_at = 0ul;
            if (!encoded && rendered.size == 0u && failed_allocation_size == 64ul) {
                found_header_allocation_failure = true;
            }
        }
        require(found_header_allocation_failure,
                id + ": could not target the initial PPM header allocation");
    }});
    cases.push_back({"0230_render_rejects_mismatched_page_capacity","Page rendering rejects a page capacity that disagrees with the raster before metadata work",[](const fs::path&){
        const std::string id = "0230_render_rejects_mismatched_page_capacity";
        ImageMappingConfig mapping;
        const u32 width = 220u, height = 220u;
        makocode::ByteBuffer fiducial_mask;
        u64 reserved_data_pixels = 0u;
        require(compute_fiducial_marker_mask(width, height, height,
                                             reserved_data_pixels,
                                             &fiducial_mask),
                id + ": could not calculate fiducial reservation");
        u64 usable_data_pixels = (u64)width * height - reserved_data_pixels;
        u64 expected_capacity = mapping_capacity_bits(mapping, usable_data_pixels);
        require(expected_capacity != U64_MAX_VALUE,
                id + ": calculated page capacity overflowed");
        makocode::ByteBuffer frame, rendered;
        MetadataTile::Values tile;
        FooterLayout footer;
        footer.data_height_pixels = height;
        metadata_tile_build_call_count = 0u;
        bool encoded = encode_page_to_ppm(mapping, frame, 0u, 0u,
                                         width, height, 1u,
                                         expected_capacity + 1u, nullptr, 0.0,
                                         nullptr, 0u, footer, rendered, 0u, 0u,
                                         false, &tile, nullptr, 1u);
        require(!encoded,
                id + ": page rendering accepted a capacity inconsistent with its raster");
        require(metadata_tile_build_call_count == 0u,
                id + ": metadata construction began before rejecting the capacity mismatch");
    }});
    cases.push_back({"0231_render_rejects_reservation_mask_allocation_failure","Page rendering rejects a reservation-mask allocation failure",[](const fs::path&){
        const std::string id = "0231_render_rejects_reservation_mask_allocation_failure";
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame, rendered;
        MetadataTile::Values tile;
        FooterLayout footer;
        footer.data_height_pixels = 100u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool encoded = encode_page_to_ppm(mapping, frame, 0u, 0u,
                                         100u, 100u, 1u, 0u, nullptr, 0.0,
                                         nullptr, 0u, footer, rendered, 0u, 0u,
                                         false, &tile, nullptr, 1u);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!encoded,
                id + ": page rendering succeeded after reservation-mask allocation failed");
        require(allocation_count == 2ul && failed_allocation_size != 0ul,
                id + ": failure injection did not reach the reservation-mask allocation");
    }});
    cases.push_back({"0235_build_frame_rejects_frame_buffer_allocation_failure","Frame construction rejects final buffer allocation failure",[](const fs::path&){
        const std::string id = "0235_build_frame_rejects_frame_buffer_allocation_failure";
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 2u;
        encoder.payload_is_preencoded = true;
        const u8 payload[] = {0x41u};
        require(encoder.set_payload(payload, sizeof(payload)) && encoder.build(),
                id + ": could not build a small encoded frame");
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = ~0ul;
        bool baseline_ok = build_frame_bits(encoder, frame_bits,
                                            frame_bit_count);
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        frame_bits.release();
        require(baseline_ok && frame_bit_count > 0u && allocation_count >= 2ul,
                id + ": baseline did not allocate the writer and final frame buffers");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = allocation_count;
        bool built = build_frame_bits(encoder, frame_bits,
                                      frame_bit_count);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long failed_allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!built,
                id + ": frame construction succeeded after final-buffer allocation failed");
        require(failed_allocation_count == allocation_count &&
                    failed_allocation_size != 0ul,
                id + ": fault injection did not reach the final frame-buffer allocation");
    }});
    cases.push_back({"0236_build_frame_rejects_writer_allocation_failure","Frame construction rejects bit-writer allocation failure",[](const fs::path&){
        const std::string id = "0236_build_frame_rejects_writer_allocation_failure";
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 2u;
        encoder.payload_is_preencoded = true;
        const u8 payload[] = {0x41u};
        require(encoder.set_payload(payload, sizeof(payload)) && encoder.build(),
                id + ": could not build a small encoded frame");
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool built = build_frame_bits(encoder, frame_bits, frame_bit_count);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!built,
                id + ": frame construction succeeded after bit-writer allocation failed");
        require(allocation_count == 1ul && failed_allocation_size != 0ul,
                id + ": fault injection did not reach the bit-writer allocation");
    }});
    cases.push_back({"0237_page_filename_rejects_suffix_allocation_failure","Page filename construction rejects component allocation failures",[](const fs::path&){
        const std::string id = "0237_page_filename_rejects_suffix_allocation_failure";
        std::string timestamp(120u, 't');
        makocode::ByteBuffer output;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool timestamp_built = build_page_filename(output, timestamp.c_str(), 1u, 1u);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!timestamp_built,
                id + ": page filename succeeded after timestamp allocation failed");
        require(allocation_count == 1ul && failed_allocation_size != 0ul,
                id + ": fault injection did not reach the timestamp allocation");

        std::string prefix(64u, 'p');
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool built = build_page_filename(output, prefix.c_str(), 1u, 1u);
        failed_allocation_size = minify_allocation_failure_size;
        allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!built,
                id + ": page filename succeeded after suffix allocation failed");
        require(allocation_count == 2ul && failed_allocation_size != 0ul,
                id + ": fault injection did not reach the filename suffix allocation");

        std::string indexed_prefix(64u, 'i');
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool indexed = build_page_filename(output, indexed_prefix.c_str(), 1u, 2u);
        failed_allocation_size = minify_allocation_failure_size;
        allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!indexed,
                id + ": page filename succeeded after page component allocation failed");
        require(allocation_count == 2ul && failed_allocation_size != 0ul,
                id + ": fault injection did not reach the page component allocation");

        std::string long_prefix(120u, 'p');
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool padded = build_page_filename(output, long_prefix.c_str(), 1u, 2u);
        failed_allocation_size = minify_allocation_failure_size;
        allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!padded,
                id + ": page filename succeeded after zero-padding allocation failed");
        require(allocation_count == 2ul && failed_allocation_size != 0ul,
                id + ": fault injection did not reach the page-index zero-padding allocation");
    }});
    cases.push_back({"0238_page_filename_expands_for_large_page_count","Page filenames expand index padding beyond four digits",[](const fs::path&){
        const std::string id = "0238_page_filename_expands_for_large_page_count";
        makocode::ByteBuffer output;
        require(build_page_filename(output, "capture", 1u, 10000u),
                id + ": page filename construction failed");
        require(std::string(reinterpret_cast<const char*>(output.data)) ==
                    "capture_page_00001.ppm",
                id + ": index padding did not expand to match page-count digits");
        require(build_page_filename(output, "capture", 1u,
                                    100000000000000000ull),
                id + ": large page-count filename construction failed");
        require(std::string(reinterpret_cast<const char*>(output.data)) ==
                    "capture_page_0000000000000001.ppm",
                id + ": large page-count padding exceeded the 16-digit limit");
    }});
    cases.push_back({"0239_page_filename_rejects_invalid_arguments","Page filenames reject null timestamps and zero page indices",[](const fs::path&){
        const std::string id = "0239_page_filename_rejects_invalid_arguments";
        makocode::ByteBuffer output;
        require(!build_page_filename(output, nullptr, 1u, 1u),
                id + ": page filename accepted a null timestamp");
        require(!build_page_filename(output, "capture", 0u, 1u),
                id + ": page filename accepted a zero page index");
    }});
    cases.push_back({"0240_bits_per_page_rejects_fiducial_mask_allocation_failure","Page capacity calculation rejects fiducial-mask allocation failure",[](const fs::path&){
        ImageMappingConfig mapping;
        u64 bits_per_page = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool ok = compute_bits_per_page(mapping, 80u, 80u, 80u, bits_per_page);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!ok,
                "0240: page capacity succeeded after fiducial-mask allocation failed");
        require(allocation_count == 1ul && failed_allocation_size != 0ul,
                "0240: fault injection did not reach the fiducial-mask allocation");
    }});
    cases.push_back({"0241_fiducial_reservation_propagates_mask_failure","Fiducial reservation propagates mask allocation failure",[](const fs::path&){
        makocode::ByteBuffer mask;
        u64 reserved_pixels = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool ok = compute_fiducial_reservation(80u, 80u, 80u, reserved_pixels,
                                               &mask, false, 0u, 0u, 0u);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!ok,
                "0241: reservation succeeded after fiducial-mask allocation failed");
        require(allocation_count == 1ul && failed_allocation_size != 0ul,
                "0241: fault injection did not reach the fiducial-mask allocation");
    }});
    cases.push_back({"0242_fiducial_mask_counts_overlaps_once","Overlapping fiducials count each reserved pixel once",[](const fs::path&){
        makocode::ByteBuffer mask;
        u64 reserved_pixels = 0u;
        require(compute_fiducial_marker_mask(60u, 60u, 60u, reserved_pixels,
                                             &mask, true, 60u, 60u, 1u),
                "0242: could not build an overlapping fiducial mask");
        u32 mask_pixels = 0u;
        for (usize i = 0u; i < mask.size; ++i) {
            if (mask.data[i]) ++mask_pixels;
        }
        require(mask_pixels == reserved_pixels,
                "0242: reservation count differed from unique mask pixels");
    }});
    cases.push_back({"0299_fiducial_mask_clips_out_of_page_markers","Fiducial masks clip markers that begin outside the page",[](const fs::path&){
        makocode::ByteBuffer mask;
        u64 reserved_pixels = 0u;
        require(compute_fiducial_marker_mask(60u, 60u, 60u, reserved_pixels,
                                             &mask, true, 100u, 100u, 1u),
                "0299: could not build a mask for partially off-page markers");
        u64 mask_pixels = 0u;
        for (usize i = 0u; i < mask.size; ++i) {
            if (mask.data[i]) ++mask_pixels;
        }
        require(mask.size == 3600u && mask_pixels == reserved_pixels,
                "0299: clipped marker reservation exceeded the page mask");
    }});
    cases.push_back({"0300_affine_confidence_clips_border_neighbors","Affine confidence sampling clips neighbors beyond page edges",[](const fs::path&){
        const std::string id = "0300_affine_confidence_clips_border_neighbors";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "border sample");
        auto encoded = run_cli(work, encode_file_args(
            payload, pages, "page",
            {"--page-width=80", "--page-height=80", "--no-fiducials",
             "--fiducial-spacing=200"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> page_paths = ppm_paths(pages, "page");
        require(page_paths.size() == 1u, id + ": expected one encoded page");
        fs::path scan = work / "edge_shift.ppm";
        make_affine_scan(page_paths[0], scan, 100u, 100u, 1.25, 0.0, 0.0);
        makocode::ByteBuffer input;
        require(read_entire_file(scan.c_str(), input),
                id + ": could not read translated page");
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        PpmParserState state;
        require(ppm_extract_frame_bits(
                    input, mapping, frame_bits, frame_bit_count, state,
                    false, (bool*)0, 1u, (makocode::ByteBuffer*)0,
                    (u64*)0, (makocode::ByteBuffer*)0, (u16*)0, (u16*)0,
                    (bool*)0, true),
                id + ": forced affine confidence extraction failed");
        double first_x = state.affine_transform.a00 * 0.5 +
                         state.affine_transform.a01 * 0.5 +
                         state.affine_transform.tx;
        double first_y = state.affine_transform.a10 * 0.5 +
                         state.affine_transform.a11 * 0.5 +
                         state.affine_transform.ty;
        require(first_x >= 0.0 && first_x < 0.5 &&
                    first_y >= 0.0 && first_y < 0.5,
                id + ": affine did not map the first module to the image edge: " +
                    std::to_string(first_x) + ", " + std::to_string(first_y));
    }});
    cases.push_back({"0301_affine_confidence_skips_off_page_samples","Affine confidence sampling skips modules before the page origin",[](const fs::path&){
        const std::string id = "0301_affine_confidence_skips_off_page_samples";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "border sample");
        auto encoded = run_cli(work, encode_file_args(
            payload, pages, "page",
            {"--page-width=80", "--page-height=80", "--no-fiducials",
             "--fiducial-spacing=200"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> page_paths = ppm_paths(pages, "page");
        require(page_paths.size() == 1u, id + ": expected one encoded page");
        fs::path scan = work / "off_page.ppm";
        make_affine_scan(page_paths[0], scan, 100u, 100u, 1.25, -0.4, -0.4);
        makocode::ByteBuffer input;
        require(read_entire_file(scan.c_str(), input),
                id + ": could not read translated page");
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        PpmParserState state;
        require(ppm_extract_frame_bits(
                    input, mapping, frame_bits, frame_bit_count, state,
                    false, (bool*)0, 1u, (makocode::ByteBuffer*)0,
                    (u64*)0, (makocode::ByteBuffer*)0, (u16*)0, (u16*)0,
                    (bool*)0, true),
                id + ": forced affine confidence extraction failed");
        double first_x = state.affine_transform.a00 * 0.5 +
                         state.affine_transform.a01 * 0.5 +
                         state.affine_transform.tx;
        double first_y = state.affine_transform.a10 * 0.5 +
                         state.affine_transform.a11 * 0.5 +
                         state.affine_transform.ty;
        require(first_x < 0.0 && first_y < 0.0 && frame_bit_count > 0u,
                id + ": fixture did not place a sample before the image origin");
    }});
    cases.push_back({"0243_ppm_dimensions_reject_invalid_height","PPM dimensions reject a zero height",[](const fs::path&){
        makocode::ByteBuffer ppm;
        require(ppm.append_ascii("P6\n2 0\n255\n"),
                "0243: could not build invalid-height PPM header");
        u32 width = 0u, height = 0u;
        require(!ppm_measure_dimensions(ppm, width, height),
                "0243: PPM dimensions accepted a zero height");
    }});
    cases.push_back({"0245_ppm_dimensions_reject_invalid_width","PPM dimensions reject a non-numeric width",[](const fs::path&){
        makocode::ByteBuffer ppm;
        require(ppm.append_ascii("P6\nwidth 2\n255\n"),
                "0245: could not build invalid-width PPM header");
        u32 width = 0u, height = 0u;
        require(!ppm_measure_dimensions(ppm, width, height),
                "0245: PPM dimensions accepted a non-numeric width");
    }});
    cases.push_back({"0244_ppm_dimensions_reject_missing_height","PPM dimensions reject a missing height token",[](const fs::path&){
        makocode::ByteBuffer ppm;
        require(ppm.append_ascii("P6\n2\n"),
                "0244: could not build incomplete PPM header");
        u32 width = 0u, height = 0u;
        require(!ppm_measure_dimensions(ppm, width, height),
                "0244: PPM dimensions accepted a missing height token");
    }});
    cases.push_back({"0246_ppm_dimensions_reject_invalid_magic","PPM dimensions reject non-P6 magic",[](const fs::path&){
        makocode::ByteBuffer ppm;
        require(ppm.append_ascii("P3\n2 2\n255\n"),
                "0246: could not build non-P6 PPM header");
        u32 width = 0u, height = 0u;
        require(!ppm_measure_dimensions(ppm, width, height),
                "0246: PPM dimensions accepted non-P6 magic");
    }});
    cases.push_back({"0247_ppm_dimensions_reject_missing_magic","PPM dimensions reject a missing magic token",[](const fs::path&){
        makocode::ByteBuffer ppm;
        require(ppm.append_ascii("\n# no magic token\n"),
                "0247: could not build incomplete PPM header");
        u32 width = 0u, height = 0u;
        require(!ppm_measure_dimensions(ppm, width, height),
                "0247: PPM dimensions accepted a missing magic token");
    }});
    cases.push_back({"0348_frame_extraction_rejects_malformed_header","Frame extraction rejects an invalid PPM width token",[](const fs::path&){
        makocode::ByteBuffer ppm, frame;
        require(ppm.append_ascii("P6\ninvalid 2\n255\n"),
                "0348: could not build malformed PPM header");
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 frame_bit_count = 0u;
        require(!ppm_extract_frame_bits(ppm, mapping, frame, frame_bit_count, state,
                                        false),
                "0348: frame extraction accepted an invalid PPM width token");
    }});
    cases.push_back({"0248_fiducial_insert_propagates_header_allocation_failure","Fiducial insertion propagates PPM header allocation failure",[](const fs::path&){
        makocode::ByteBuffer input, output;
        const u32 width = 100u, height = 100u;
        std::string ppm("P6\n100 100\n255\n");
        ppm.append(static_cast<size_t>(width) * height * 3u, '\0');
        require(input.append_bytes((const u8*)ppm.data(), ppm.size()),
                "0248: could not build PPM input");
        const u8 white[3] = {255u, 255u, 255u};
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool inserted = ppm_insert_fiducial_grid(input, 0u, 0u, 0u, 0u,
                                                  white, output);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(!inserted,
                "0248: fiducial insertion succeeded after PPM header allocation failed");
        require(allocation_count == 2ul && failed_allocation_size != 0ul,
                "0248: fault injection did not reach PPM header allocation");
    }});
    cases.push_back({"0249_fiducial_insert_clips_marker_at_page_edges","Fiducial insertion clips markers to page edges",[](const fs::path&){
        makocode::ByteBuffer input, output;
        const u32 width = 100u, height = 100u;
        std::string ppm("P6\n100 100\n255\n");
        ppm.append(static_cast<size_t>(width) * height * 3u, '\0');
        require(input.append_bytes((const u8*)ppm.data(), ppm.size()),
                "0249: could not build PPM input");
        const u8 red[3] = {255u, 0u, 0u};
        require(ppm_insert_fiducial_grid(input, 2u, 2u, 100u, height,
                                         red, output),
                "0249: fiducial insertion failed for clipped edge markers");
        const char header[] = "P6\n100 100\n255\n";
        usize raster_offset = sizeof(header) - 1u;
        require(output.size == raster_offset + (usize)width * height * 3u &&
                    memcmp(output.data, header, raster_offset) == 0,
                "0249: fiducial insertion returned an invalid PPM");
        usize top_left = raster_offset;
        usize top_right = raster_offset + ((usize)width - 1u) * 3u;
        usize bottom_left = raster_offset + ((usize)(height - 1u) * width) * 3u;
        usize bottom_right = raster_offset + (((usize)height * width) - 1u) * 3u;
        require(output.data[top_left] == red[0] &&
                    output.data[top_left + 1u] == red[1] &&
                    output.data[top_left + 2u] == red[2] &&
                    output.data[top_right] == red[0] &&
                    output.data[top_right + 1u] == red[1] &&
                    output.data[top_right + 2u] == red[2] &&
                    output.data[bottom_left] == red[0] &&
                    output.data[bottom_left + 1u] == red[1] &&
                    output.data[bottom_left + 2u] == red[2] &&
                    output.data[bottom_right] == red[0] &&
                    output.data[bottom_right + 1u] == red[1] &&
                    output.data[bottom_right + 2u] == red[2],
                "0249: clipped fiducial markers do not reach all page edges");
    }});
    cases.push_back({"0250_fiducial_insert_rejects_too_small_image","Fiducial insertion rejects images smaller than a marker",[](const fs::path&){
        makocode::ByteBuffer input, output;
        std::string ppm("P6\n1 1\n255\n");
        ppm.append(3u, '\0');
        require(input.append_bytes((const u8*)ppm.data(), ppm.size()),
                "0250: could not build PPM input");
        const u8 red[3] = {255u, 0u, 0u};
        require(!ppm_insert_fiducial_grid(input, 1u, 1u, 1u, 1u,
                                          red, output),
                "0250: fiducial insertion accepted an image smaller than a marker");
    }});
    cases.push_back({"0251_fiducial_insert_rejects_truncated_raster","Fiducial insertion rejects a truncated PPM raster",[](const fs::path&){
        makocode::ByteBuffer input, output;
        require(input.append_ascii("P6\n48 48\n255\n"),
                "0251: could not build truncated PPM header");
        const u8 white[3] = {255u, 255u, 255u};
        require(!ppm_insert_fiducial_grid(input, 0u, 0u, 0u, 0u,
                                          white, output),
                "0251: fiducial insertion accepted a missing PPM raster");
    }});
    cases.push_back({"0252_fiducial_insert_rejects_non_255_maxval","Fiducial insertion rejects unsupported PPM max values",[](const fs::path&){
        makocode::ByteBuffer input, output;
        require(input.append_ascii("P6\n48 48\n254\n"),
                "0252: could not build unsupported-maxval PPM header");
        std::string raster(48u * 48u * 3u, '\0');
        require(input.append_bytes((const u8*)raster.data(), raster.size()),
                "0252: could not build PPM raster");
        const u8 white[3] = {255u, 255u, 255u};
        require(!ppm_insert_fiducial_grid(input, 0u, 0u, 0u, 0u,
                                          white, output),
                "0252: fiducial insertion accepted maxval 254");
    }});
    cases.push_back({"0253_fiducial_insert_requires_color","Fiducial insertion rejects a missing marker color",[](const fs::path&){
        makocode::ByteBuffer input, output;
        require(input.append_ascii("P6\n48 48\n255\n"),
                "0253: could not build PPM header");
        std::string raster(48u * 48u * 3u, '\0');
        require(input.append_bytes((const u8*)raster.data(), raster.size()),
                "0253: could not build PPM raster");
        require(!ppm_insert_fiducial_grid(input, 0u, 0u, 0u, 0u,
                                          nullptr, output),
                "0253: fiducial insertion accepted a missing marker color");
    }});
    cases.push_back({"0254_metadata_free_rejects_unrecognized_page","Metadata-free recovery rejects a page with no decodable symbols",[](const fs::path&){
        makocode::ByteBuffer input;
        require(input.append_ascii("P6\n80 80\n255\n"),
                "0254: could not build PPM header");
        std::string raster(80u * 80u * 3u, '\0');
        for (usize pixel = 0u; pixel < 80u * 80u; ++pixel) {
            u8 value = (pixel & 1u) ? 255u : 0u;
            usize offset = pixel * 3u;
            raster[offset] = (char)value;
            raster[offset + 1u] = (char)value;
            raster[offset + 2u] = (char)value;
        }
        require(input.append_bytes((const u8*)raster.data(), raster.size()),
                "0254: could not build PPM raster");
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame_bits, frame_erasures;
        u64 frame_bit_count = 0u, page_capacity_bits = 0u;
        PpmParserState state;
        MetadataTile::Values values;
        require(!recover_metadata_free_single_page(input, mapping, true,
                                                   frame_bits, frame_erasures,
                                                   frame_bit_count,
                                                   page_capacity_bits, state,
                                                   values, nullptr),
                "0254: metadata-free recovery accepted an undecodable page");
    }});
    cases.push_back({"0300_decode_page_dimension_overrides","Decode page-width and page-height overrides affect page capacity",[](const fs::path&){
        const std::string id = "0300_decode_page_dimension_overrides";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::create_directories(encoded_dir);
        write_file(payload, "page height override");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, encoded_dir, "source",
                             {"--page-width=220", "--page-height=220"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "source");
        require(pages.size() == 1u, id + ": expected one encoded page");
        makocode::ByteBuffer input, overridden_frame, overridden_width_frame,
            metadata_frame;
        require(read_entire_file(pages[0].c_str(), input),
                id + ": could not read the encoded page");
        ImageMappingConfig mapping;
        mapping.page_height_set = true;
        mapping.page_height_pixels = 400u;
        u64 overridden_frame_bit_count = 0u, metadata_frame_bit_count = 0u;
        u64 overridden_capacity = 0u, overridden_width_capacity = 0u;
        u64 metadata_capacity = 0u;
        PpmParserState overridden_state, overridden_width_state, metadata_state;
        ImageMappingConfig width_mapping;
        width_mapping.page_width_set = true;
        width_mapping.page_width_pixels = 400u;
        require(ppm_extract_frame_bits(input, mapping, overridden_frame,
                                       overridden_frame_bit_count,
                                       overridden_state, false, nullptr, 1u,
                                       nullptr, nullptr, nullptr, nullptr,
                                       nullptr, nullptr, false,
                                       &overridden_capacity) &&
                    ppm_extract_frame_bits(input, width_mapping,
                                           overridden_width_frame,
                                           overridden_frame_bit_count,
                                           overridden_width_state, false,
                                           nullptr, 1u, nullptr, nullptr, nullptr,
                                           nullptr, nullptr, nullptr, false,
                                           &overridden_width_capacity) &&
                    ppm_extract_frame_bits(input, ImageMappingConfig(),
                                           metadata_frame,
                                           metadata_frame_bit_count,
                                           metadata_state, false, nullptr, 1u,
                                           nullptr, nullptr, nullptr, nullptr,
                                           nullptr, nullptr, false,
                                           &metadata_capacity),
                id + ": could not extract the page using both geometry sources");
        require(overridden_capacity != metadata_capacity,
                id + ": explicit page height did not affect page capacity");
        require(overridden_width_capacity != metadata_capacity,
                id + ": explicit page width did not affect page capacity");
    }});
    cases.push_back({"0305_low_luminance_chromatic_two_color_decode","Low-luminance chromatic scans preserve palette color identity",[](const fs::path&){
        const std::string id = "0305_low_luminance_chromatic_two_color_decode";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::path decoded_dir = work / "decoded";
        fs::create_directories(encoded_dir);
        write_file(payload, "low contrast chromatic branch check payload");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, encoded_dir, "source",
                             {"--page-width=80", "--page-height=80",
                              "--palette", "White Black"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "source");
        require(pages.size() == 1u, id + ": expected one encoded page");
        PpmImage image = read_ppm(pages[0]);
        double minimum_luminance = 255.0;
        double maximum_luminance = 0.0;
        for (size_t offset = 0u; offset < image.pixels.size(); offset += 3u) {
            const u8 r = (u8)image.pixels[offset];
            const u8 g = (u8)image.pixels[offset + 1u];
            const u8 b = (u8)image.pixels[offset + 2u];
            const double luminance = 0.2126 * (double)r +
                                     0.7152 * (double)g +
                                     0.0722 * (double)b;
            const bool dark = luminance < 127.0;
            const u8 mapped_r = dark ? 200u : 0u;
            const u8 mapped_g = dark ? 0u : 80u;
            const u8 mapped_b = 0u;
            const double mapped_luminance = 0.2126 * (double)mapped_r +
                                            0.7152 * (double)mapped_g;
            if (mapped_luminance < minimum_luminance) {
                minimum_luminance = mapped_luminance;
            }
            if (mapped_luminance > maximum_luminance) {
                maximum_luminance = mapped_luminance;
            }
            image.pixels[offset] = (char)mapped_r;
            image.pixels[offset + 1u] = (char)mapped_g;
            image.pixels[offset + 2u] = (char)mapped_b;
        }
        require(maximum_luminance - minimum_luminance <= 20.0,
                id + ": chromatic scan did not enter low-luminance-contrast path");
        fs::path scan = work / "low_luminance.ppm";
        write_file(scan, "P6\n" + std::to_string(image.width) + " " +
                         std::to_string(image.height) + "\n255\n" + image.pixels);
        ProcessResult decoded = run_cli(work,
            decode_file_args({scan.string()}, decoded_dir,
                             {"--palette", "White Black"}),
            "decode");
        expect_success(decoded, id + " decode");
        compare_files(payload, decoded_dir / "payload.bin", id + " payload");
    }});
    cases.push_back({"0302_fiducial_partial_allocation_is_released","Fiducial extraction frees the first center array when the second allocation fails",[](const fs::path&){
        const std::string id = "0302_fiducial_partial_allocation_is_released";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::create_directories(encoded_dir);
        write_file(payload, "partial fiducial allocation");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, encoded_dir, "source",
                             {"--page-width=80", "--page-height=80"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "source");
        require(pages.size() == 1u, id + ": expected one encoded page");
        makocode::ByteBuffer input;
        require(read_entire_file(pages[0].c_str(), input),
                id + ": could not read the encoded page");
        ImageMappingConfig mapping;
        makocode::ByteBuffer baseline_frame;
        u64 baseline_bits = 0u;
        PpmParserState baseline_state;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = ~0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool baseline_extracted = ppm_extract_frame_bits(input, mapping,
                                                          baseline_frame,
                                                          baseline_bits,
                                                          baseline_state, false);
        record_platform_allocation_sizes = false;
        minify_allocation_failure_at = 0ul;
        require(baseline_extracted && baseline_state.has_fiducial_columns &&
                    baseline_state.has_fiducial_rows,
                id + ": baseline did not decode a fiducial grid");
        size_t center_array_size = (size_t)baseline_state.fiducial_columns_value *
                                   (size_t)baseline_state.fiducial_rows_value *
                                   sizeof(double);
        size_t first_center_allocation = platform_allocation_sizes.size();
        for (size_t index = 0u; index + 1u < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == center_array_size &&
                platform_allocation_sizes[index + 1u] == center_array_size) {
                first_center_allocation = index;
                break;
            }
        }
        require(first_center_allocation < platform_allocation_sizes.size(),
                id + ": did not observe the adjacent fiducial center allocations");

        makocode::ByteBuffer failed_frame;
        u64 failed_bits = 0u;
        PpmParserState failed_state;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        watch_platform_free_pointer = nullptr;
        watch_platform_free_count = 0ul;
        watch_platform_allocation_at = (unsigned long)first_center_allocation + 1ul;
        minify_allocation_failure_at = (unsigned long)first_center_allocation + 2ul;
        bool failure_extracted = ppm_extract_frame_bits(input, mapping,
                                                         failed_frame,
                                                         failed_bits,
                                                         failed_state, false);
        unsigned long failed_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        watch_platform_allocation_at = 0ul;
        require(failed_size == center_array_size,
                id + ": fault injection missed the second center allocation");
        require(watch_platform_free_count == 1ul &&
                    watch_platform_free_pointer == nullptr,
                id + ": the first center allocation was not freed after the second failed");
        (void)failure_extracted;
    }});
    cases.push_back({"0324_fiducial_center_allocation_failure_cleans_up","Fiducial center extraction releases prior allocations after a later allocation fails",[](const fs::path&){
        const std::string id = "0324_fiducial_center_allocation_failure_cleans_up";
        u8 pixels[12u * 12u * 3u] = {};
        double* centers_x = nullptr;
        double* centers_y = nullptr;
        u8* visible = nullptr;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        watch_platform_allocation_at = 1ul;
        watch_platform_free_pointer = nullptr;
        watch_platform_free_count = 0ul;
        bool sampled = sample_fiducial_centers(
            pixels, 12u, 12u, 1u, 1u, 1.0, 0u, 0u, 0u, false,
            centers_x, centers_y, nullptr, nullptr, &visible, false, nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        watch_platform_allocation_at = 0ul;
        require(failed_allocation_size == sizeof(double),
                id + ": fault injection missed the second center allocation");
        require(!sampled, id + ": sampling continued after center allocation failed");
        require(watch_platform_free_count == 1ul &&
                    watch_platform_free_pointer == nullptr,
                id + ": the first center allocation was not released");
    }});
    cases.push_back({"0303_footer_override_rejects_underflowing_data_height","Invalid footer and layout metadata are rejected before payload extraction",[](const fs::path&){
        const std::string id = "0303_footer_override_rejects_underflowing_data_height";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::create_directories(encoded_dir);
        write_file(payload, "invalid footer override");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, encoded_dir, "source",
                             {"--page-width=80", "--page-height=80"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "source");
        require(pages.size() == 1u, id + ": expected one encoded page");
        makocode::ByteBuffer input;
        require(read_entire_file(pages[0].c_str(), input),
                id + ": could not read the encoded page");
        MetadataTile::Values values = extract_tile_values(pages.front());
        values.footer_rows = 81u;
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame;
        u64 frame_bits = 0u;
        PpmParserState state;
        platform_allocation_limit_rejection_count = 0ul;
        platform_allocation_size_limit = 1024u * 1024u;
        bool extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &values, nullptr, 0u, 0u, 0u, true, false, false, false,
            nullptr);
        platform_allocation_size_limit = 0u;
        require(!extracted, id + ": accepted a footer override below zero data height");
        require(platform_allocation_limit_rejection_count == 0ul,
                id + ": attempted a large rank-map allocation before rejecting the footer");
        values.footer_rows = 0u;
        values.overwrite_layout = false;
        bool invalid_layout_extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &values, nullptr, 0u, 0u, 0u, true, false, false, false,
            nullptr);
        require(!invalid_layout_extracted,
                id + ": accepted metadata without overwrite layout");
        values.overwrite_layout = true;
        minify_allocation_count = 0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool baseline_extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &values, nullptr, 0u, 0u, 0u, true, false, false, false,
            nullptr);
        record_platform_allocation_sizes = false;
        require(baseline_extracted,
                id + ": valid metadata override failed before allocation injection");
        const size_t fiducial_mask_allocation_size = 8192u;
        size_t fiducial_mask_allocation = platform_allocation_sizes.size();
        for (size_t index = 0u; index < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == fiducial_mask_allocation_size) {
                fiducial_mask_allocation = index;
                break;
            }
        }
        require(fiducial_mask_allocation < platform_allocation_sizes.size(),
                id + ": did not observe the fiducial mask allocation");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at =
            (unsigned long)fiducial_mask_allocation + 1ul;
        bool allocation_failure_extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &values, nullptr, 0u, 0u, 0u, true, false, false, false,
            nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == fiducial_mask_allocation_size,
                id + ": fault injection missed the fiducial mask allocation");
        require(!allocation_failure_extracted,
                id + ": continued extraction after fiducial mask allocation failed");
    }});
    cases.push_back({"0310_ppm_extract_rank_map_allocation_failure","Frame extraction rejects a payload rank-map allocation failure",[](const fs::path&){
        const std::string id = "0310_ppm_extract_rank_map_allocation_failure";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::create_directories(encoded_dir);
        write_file(payload, "rank map allocation failure");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, encoded_dir, "source",
                             {"--page-width=80", "--page-height=80"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "source");
        require(pages.size() == 1u, id + ": expected one encoded page");
        makocode::ByteBuffer input;
        require(read_entire_file(pages[0].c_str(), input),
                id + ": could not read the encoded page");
        MetadataTile::Values values = extract_tile_values(pages.front());
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame;
        u64 frame_bits = 0u;
        PpmParserState state;
        minify_allocation_count = 0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool baseline_extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &values, nullptr, 0u, 0u, 0u, true, false, false, false,
            nullptr);
        record_platform_allocation_sizes = false;
        require(baseline_extracted,
                id + ": valid metadata override failed before allocation injection");
        const size_t rank_map_allocation_size = 65536u;
        size_t rank_map_allocation = platform_allocation_sizes.size();
        for (size_t index = 0u; index < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == rank_map_allocation_size) {
                rank_map_allocation = index;
                break;
            }
        }
        require(rank_map_allocation < platform_allocation_sizes.size(),
                id + ": did not observe the payload rank-map allocation");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = (unsigned long)rank_map_allocation + 1ul;
        bool allocation_failure_extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &values, nullptr, 0u, 0u, 0u, true, false, false, false,
            nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == rank_map_allocation_size,
                id + ": fault injection missed the payload rank-map allocation");
        require(!allocation_failure_extracted,
                id + ": continued extraction after payload rank-map allocation failed");
    }});
    cases.push_back({"0312_fiducial_border_bounds_affine","Forced fiducial fallback refines page bounds from all image borders",[](const fs::path&){
        const std::string id = "0312_fiducial_border_bounds_affine";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "border affine fixture");
        auto encoded = run_cli(work, encode_file_args(
            payload, pages, "page", {"--page-width=80", "--page-height=80",
                                      "--no-fiducials", "--fiducial-spacing=200"}), "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> page_paths = ppm_paths(pages, "page");
        require(page_paths.size() == 1u, id + ": expected one encoded page");
        MetadataTile::Values tile = extract_tile_values(page_paths[0]);
        fs::path scan = work / "padded.ppm";
        make_affine_scan(page_paths[0], scan, 180u, 180u, 2.0, 10.0, 10.0,
                         0u, 234u, false, true);
        makocode::ByteBuffer input, frame;
        require(read_entire_file(scan.c_str(), input), id + ": could not read scan");
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 frame_bits = 0u;
        require(ppm_extract_frame_bits(
                    input, mapping, frame, frame_bits, state, true, nullptr, 1u,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
                    nullptr, &tile, nullptr, 0u, 0u, 0u, true, false, false, false,
                    nullptr), id + ": forced fallback extraction failed");
        require(state.has_affine_transform && frame_bits > 0u,
                id + ": border fallback did not produce an affine frame");
        require(state.affine_transform.a00 > 1.99 && state.affine_transform.a00 < 2.01 &&
                    state.affine_transform.a11 > 1.99 && state.affine_transform.a11 < 2.01 &&
                    state.affine_transform.tx >= 9.0 && state.affine_transform.tx <= 9.5 &&
                    state.affine_transform.ty >= 9.25 && state.affine_transform.ty <= 9.75,
                id + ": border-derived affine did not locate the padded page: " +
                    std::to_string(state.affine_transform.a00) + ", " +
                    std::to_string(state.affine_transform.a11) + ", " +
                    std::to_string(state.affine_transform.tx) + ", " +
                    std::to_string(state.affine_transform.ty));
    }});
    cases.push_back({"0318_ppm_extract_rejects_missing_input_storage","Frame extraction rejects a nonempty input without storage",[](const fs::path&){
        const std::string id = "0318_ppm_extract_rejects_missing_input_storage";
        makocode::ByteBuffer input;
        input.size = 1u;
        ImageMappingConfig mapping;
        makocode::ByteBuffer frame;
        u64 frame_bits = 0u;
        PpmParserState state;
        require(!ppm_extract_frame_bits(input, mapping, frame, frame_bits,
                                        state, false),
                id + ": accepted an input buffer without backing storage");
    }});
    cases.push_back({"0319_uniform_region_excludes_metadata_header","Uniform blot regions do not bridge across the metadata header",[](const fs::path&){
        require(test_uniform_region_detector(),
                "0319_uniform_region_excludes_metadata_header: detector marked the metadata header");
    }});
    cases.push_back({"0320_vertical_scale_similarity_returns_divisor","Vertical similarity detector returns a repeated row divisor",[](const fs::path&){
        const u8 pixels[] = {
            10u,10u,10u, 10u,10u,10u,
            20u,20u,20u, 20u,20u,20u,
            30u,30u,30u, 30u,30u,30u
        };
        require(detect_vertical_scale_similarity(pixels, 1u, 6u) == 2u,
                "0320_vertical_scale_similarity_returns_divisor: expected vertical scale 2");
        const u8 identical_rows[] = {
            42u,42u,42u, 42u,42u,42u, 42u,42u,42u,
            42u,42u,42u, 42u,42u,42u, 42u,42u,42u
        };
        require(detect_vertical_scale_similarity(identical_rows, 1u, 6u) == 1u,
                "0320_vertical_scale_similarity_returns_divisor: identical rows produced a scale");
    }});
    cases.push_back({"0321_vertical_scale_similarity_rejects_partial_height_run","Vertical similarity detector rejects a scale that does not divide image height",[](const fs::path&){
        std::vector<u8> pixels(257u * 3u);
        for (u64 row = 0u; row < 257u; ++row) {
            u8 value = ((row / 2u) & 1u) ? 200u : 20u;
            pixels[(usize)row * 3u + 0u] = value;
            pixels[(usize)row * 3u + 1u] = value;
            pixels[(usize)row * 3u + 2u] = value;
        }
        require(detect_vertical_scale_similarity(pixels.data(), 1u, 257u) == 1u,
                "0321_vertical_scale_similarity_rejects_partial_height_run: returned a non-dividing scale");
    }});
    cases.push_back({"0370_rgb_sample_mapping_rejects_unknown_mode","RGB sample mapping rejects unsupported color modes",[](const fs::path&){
        const u8 rgb[] = {12u, 34u, 56u};
        u32 samples[1] = {0u};
        require(!map_rgb_to_samples(0u, rgb, samples),
                "0370_rgb_sample_mapping_rejects_unknown_mode: accepted mode zero");
    }});
    cases.push_back({"0390_footer_layout_rejects_zero_data_height","Footer layout rejects text that consumes the full page",[](const fs::path&){
        PageFooterConfig footer;
        footer.max_text_length = 1u;
        FooterLayout layout;
        require(!compute_footer_layout(5u, 8u, footer, layout),
                "0390_footer_layout_rejects_zero_data_height: accepted an empty data region");
    }});
    cases.push_back({"0392_downsample_preflight_skips_failed_target_allocation","Metadata extraction continues after a downsample target allocation fails",[](const fs::path&){
        const std::string id = "0392_downsample_preflight_skips_failed_target_allocation";
        fs::path work = make_work_dir(id), payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "downsample allocation fallback");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, pages, "source",
                             {"--page-width=620", "--page-height=620", "--ecc=fill"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> source = ppm_paths(pages, "source");
        require(source.size() == 1u, id + ": expected one encoded page");
        fs::path scaled = work / "scaled.ppm";
        expect_success(run_transform(work,
            {"transform", "--input", source.front(), "--output", scaled.string(),
             "--scale-x", "2.071", "--scale-y", "2.071"}, "scale"), id + " scale");

        makocode::ByteBuffer input, baseline_frame, baseline_erasures;
        require(read_entire_file(scaled.c_str(), input), id + ": could not read scaled page");
        ImageMappingConfig mapping;
        PpmParserState baseline_state;
        u64 baseline_bits = 0u;
        platform_allocation_sizes.clear();
        minify_allocation_count = 0ul;
        record_platform_allocation_sizes = true;
        bool baseline_ok = ppm_extract_frame_bits(input, mapping, baseline_frame,
                                                  baseline_bits, baseline_state,
                                                  false, nullptr, 1u,
                                                  &baseline_erasures);
        record_platform_allocation_sizes = false;
        require(baseline_ok && baseline_state.has_encoded_tile,
                id + ": baseline extraction did not find the metadata tile");

        PpmImage scaled_image = read_ppm(scaled);
        u64 logical_guess = estimate_square_page_from_image(scaled_image.width,
                                                            scaled_image.height);
        double geometric_mean = sqrt((double)scaled_image.width *
                                     (double)scaled_image.height);
        u64 geometric_target = (u64)floor(geometric_mean / 2.5 + 0.5);
        u64 sweep_min = (logical_guess * 60ull) / 100ull;
        if (sweep_min < MetadataTile::TILE_SIDE) sweep_min = MetadataTile::TILE_SIDE;
        u64 sweep_target = logical_guess;
        if (geometric_target >= sweep_min) {
            sweep_target = sweep_min + ((geometric_target - sweep_min) / 20ull) * 20ull;
        }
        u64 candidate_targets[] = {
            geometric_target, sweep_target,
            (u64)floor(geometric_mean / 2.0 + 0.5),
            (u64)floor(geometric_mean / 2.1 + 0.5),
            (u64)floor(geometric_mean / 3.0 + 0.5), logical_guess};
        bool target_failure_was_handled = false;
        unsigned matching_allocations = 0u;
        unsigned long last_expected_failure = 0ul;
        unsigned long last_observed_count = 0ul;
        unsigned long last_observed_size = 0ul;
        for (unsigned long allocation_index = 1ul;
             allocation_index <= platform_allocation_sizes.size() &&
                 !target_failure_was_handled;
             ++allocation_index) {
            size_t allocation_size = platform_allocation_sizes[allocation_index - 1ul];
            bool is_downsample_target = false;
            for (u64 target : candidate_targets) {
                if (target >= MetadataTile::TILE_SIDE &&
                    target < scaled_image.width && target < scaled_image.height &&
                    allocation_size >= target * target * 3u) {
                    usize capacity = 64u;
                    while (capacity < target * target * 3u &&
                           capacity <= (usize)USIZE_MAX_VALUE / 2u) capacity *= 2u;
                    if (allocation_size != capacity) continue;
                    is_downsample_target = true;
                }
            }
            if (!is_downsample_target) continue;
            ++matching_allocations;
            makocode::ByteBuffer failed_frame, failed_erasures;
            PpmParserState failed_state;
            u64 failed_bits = 0u;
            minify_allocation_count = 0ul;
            minify_allocation_failure_size = 0ul;
            minify_allocation_failure_at = allocation_index;
            bool recovered = ppm_extract_frame_bits(input, mapping, failed_frame,
                                                    failed_bits, failed_state,
                                                    false, nullptr, 1u,
                                                    &failed_erasures);
            unsigned long failed_at = minify_allocation_count;
            unsigned long failed_size = minify_allocation_failure_size;
            minify_allocation_failure_at = 0ul;
            last_expected_failure = allocation_index;
            last_observed_count = failed_at;
            last_observed_size = failed_size;
            target_failure_was_handled = failed_size == allocation_size;
            (void)recovered;
            (void)failed_state;
        }
        require(target_failure_was_handled,
                id + ": downsample allocation failure was not reported; matching allocations=" +
                    std::to_string(matching_allocations) + ", logical size=" +
                    std::to_string(logical_guess) + ", geometric target=" +
                    std::to_string(geometric_target) + ", expected index=" +
                    std::to_string(last_expected_failure) + ", observed count=" +
                    std::to_string(last_observed_count) + ", failed size=" +
                    std::to_string(last_observed_size));
    }});
    cases.push_back({"0385_rgb_sample_mapping_selects_palette_index","RGB mapping returns the nearest palette index for multicolor modes",[](const fs::path&){
        const u8 cyan[] = {0u, 255u, 255u};
        u32 sample = 0xFFFFFFFFu;
        require(map_rgb_to_samples(2u, cyan, &sample) && sample == 1u,
                "0385_rgb_sample_mapping_selects_palette_index: CMYW cyan mapped incorrectly");
        sample = 0xFFFFFFFFu;
        require(map_rgb_to_samples(3u, cyan, &sample) && sample == 4u,
                "0385_rgb_sample_mapping_selects_palette_index: RGB/CMY cyan mapped incorrectly");
    }});
    cases.push_back({"0350_vertical_palette_scale_rejects_low_run_support","Palette scale detection rejects a divisor supported by too few runs",[](const fs::path&){
        const u8 pixels[] = {
            0u,0u,0u, 0u,0u,0u,
            255u,255u,255u, 255u,255u,255u, 255u,255u,255u, 255u,255u,255u
        };
        require(detect_vertical_scale_palette(pixels, 1u, 6u, 1u, nullptr) == 1u,
                "0350_vertical_palette_scale_rejects_low_run_support: accepted sparse run evidence");
    }});
    cases.push_back({"0322_fiducial_sampling_integral_allocation_fallback","Fiducial sampling matches the direct calculation when its integral image allocation fails",[](const fs::path&){
        const std::string id = "0322_fiducial_sampling_integral_allocation_fallback";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "integral image fallback");
        ProcessResult encoded = run_cli(work, encode_file_args(
            payload, pages, "page", {"--page-width=240", "--page-height=240"}), "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> page_paths = ppm_paths(pages, "page");
        require(page_paths.size() == 1u, id + ": expected one encoded page");
        MetadataTile::Values tile = extract_tile_values(page_paths[0]);
        PpmImage image = read_ppm(page_paths[0]);
        ImageMappingConfig mapping;
        usize point_count = (usize)tile.fiducial_columns * (usize)tile.fiducial_rows;
        usize integral_allocation_size = ((usize)image.width + 1u) *
                                         ((usize)image.height + 1u) * sizeof(u64);
        double* integral_x = nullptr;
        double* integral_y = nullptr;
        u8* integral_visible = nullptr;
        minify_allocation_count = 0ul;
        minify_allocation_failure_at = 0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool integral_ok = sample_fiducial_centers(
            reinterpret_cast<const u8*>(image.pixels.data()), image.width, image.height,
            tile.fiducial_columns, tile.fiducial_rows,
            (double)tile.fiducial_spacing_pixels, tile.page_width_pixels,
            tile.page_height_pixels, tile.page_height_pixels - tile.footer_rows,
            true, integral_x, integral_y, nullptr, nullptr, &integral_visible,
            false, &mapping);
        record_platform_allocation_sizes = false;
        require(integral_ok, id + ": integral-image baseline failed");
        usize integral_allocation_index = platform_allocation_sizes.size();
        for (usize index = 0u; index < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == integral_allocation_size) {
                integral_allocation_index = index;
                break;
            }
        }
        require(integral_allocation_index < platform_allocation_sizes.size(),
                id + ": did not observe the integral-image allocation");
        usize visible_count = 0u;
        for (usize index = 0u; index < point_count; ++index) {
            if (integral_visible[index]) ++visible_count;
        }
        require(visible_count > 0u, id + ": baseline detected no visible fiducials");

        double* fallback_x = nullptr;
        double* fallback_y = nullptr;
        u8* fallback_visible = nullptr;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = (unsigned long)integral_allocation_index + 1ul;
        bool fallback_ok = sample_fiducial_centers(
            reinterpret_cast<const u8*>(image.pixels.data()), image.width, image.height,
            tile.fiducial_columns, tile.fiducial_rows,
            (double)tile.fiducial_spacing_pixels, tile.page_width_pixels,
            tile.page_height_pixels, tile.page_height_pixels - tile.footer_rows,
            true, fallback_x, fallback_y, nullptr, nullptr, &fallback_visible,
            false, &mapping);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == integral_allocation_size,
                id + ": fault injection missed the integral-image allocation");
        require(fallback_ok, id + ": direct fallback failed after integral allocation failure");
        for (usize index = 0u; index < point_count; ++index) {
            require(fallback_visible[index] == integral_visible[index] &&
                        fabs(fallback_x[index] - integral_x[index]) < 1e-9 &&
                        fabs(fallback_y[index] - integral_y[index]) < 1e-9,
                    id + ": direct fallback differed from integral-image sampling");
        }
        makocode_platform_free(integral_x);
        makocode_platform_free(integral_y);
        makocode_platform_free(integral_visible);
        makocode_platform_free(fallback_x);
        makocode_platform_free(fallback_y);
        makocode_platform_free(fallback_visible);
    }});
    cases.push_back({"0323_fiducial_dark_threshold_clamp","Low-intensity fiducial sampling clamps a negative dark threshold",[](const fs::path&){
        std::vector<u8> pixels(200u * 200u * 3u, 0u);
        for (u32 row = 86u; row < 111u; ++row) {
            usize unsampled_pixel = ((usize)row * 200u + 149u) * 3u;
            pixels[unsampled_pixel + 0u] = 2u;
            pixels[unsampled_pixel + 1u] = 2u;
            pixels[unsampled_pixel + 2u] = 2u;
        }
        double* centers_x = nullptr;
        double* centers_y = nullptr;
        require(sample_fiducial_centers(
                    pixels.data(), 200u, 200u, 1u, 1u, 12.0,
                    24u, 24u, 24u, false, centers_x, centers_y),
                "0323_fiducial_dark_threshold_clamp: sampling failed");
        double expected_center = fiducial_lattice_center(0u, 1u, 24u, 12.0) *
                                 (200.0 / 24.0);
        require(centers_x[0] < expected_center - 0.001,
                "0323_fiducial_dark_threshold_clamp: dark samples were not weighted: " +
                    std::to_string(centers_x[0]) + " vs " + std::to_string(expected_center));
        makocode_platform_free(centers_x);
        makocode_platform_free(centers_y);
    }});
    cases.push_back({"0325_robust_affine_singular_refinement_fallback","Robust affine fitting retains the initial fit when inlier geometry is singular",[](const fs::path&){
        const std::string id = "0325_robust_affine_singular_refinement_fallback";
        double centers_x[24] = {};
        double centers_y[24] = {};
        const double outlier_signs[12] = {1.0, 1.0, -1.0, -1.0, 1.0, -1.0,
                                          -1.0, 1.0, -1.0, -1.0, 1.0, 1.0};
        for (u32 row = 0u; row < 12u; ++row) {
            double logical_y = fiducial_lattice_center(row, 12u, 100u, 8.0);
            for (u32 col = 0u; col < 2u; ++col) {
                usize index = (usize)row * 2u + col;
                double logical_x = fiducial_lattice_center(col, 2u, 100u, 8.0);
                double outlier_offset = col == 1u ? outlier_signs[row] * 3.0 : 0.0;
                centers_x[index] = logical_x + outlier_offset;
                centers_y[index] = logical_y;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 2u, 12u, 100u, 100u, 8.0,
                    100u, 100u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, nullptr),
                id + ": rotation fit rejected a singular robust subset");
        require(affine_valid && fabs(affine.a00 - 1.0) < 1e-9 &&
                    fabs(affine.a01) < 1e-9 && fabs(affine.a10) < 1e-9 &&
                    fabs(affine.a11 - 1.0) < 1e-9 && fabs(affine.tx) < 1e-9 &&
                    fabs(affine.ty) < 1e-9,
                id + ": singular refinement replaced the valid initial fit: " +
                    std::to_string(affine.a00) + ", " + std::to_string(affine.a01) +
                    ", " + std::to_string(affine.a10) + ", " + std::to_string(affine.a11) +
                    ", " + std::to_string(affine.tx) + ", " + std::to_string(affine.ty));
    }});
    cases.push_back({"0326_singular_affine_fit_rejected","Singular fiducial geometry does not produce an affine fit",[](const fs::path&){
        double centers_x[12] = {};
        double centers_y[12] = {};
        for (u32 row = 0u; row < 12u; ++row) {
            centers_x[row] = 50.0;
            centers_y[row] = fiducial_lattice_center(row, 12u, 100u, 8.0);
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = true;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 1u, 12u, 100u, 100u, 8.0,
                    100u, 100u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, nullptr),
                "0326_singular_affine_fit_rejected: rotation analysis failed");
        require(!affine_valid,
                "0326_singular_affine_fit_rejected: singular geometry produced an affine fit");
    }});
    cases.push_back({"0357_visible_affine_fit_rejects_collinear_points","Visible fiducial points on a diagonal do not produce an affine fit",[](const fs::path&){
        const u32 side = 6u;
        double centers_x[side * side] = {};
        double centers_y[side * side] = {};
        u8 visible_mask[side * side] = {};
        for (u32 index = 0u; index < side; ++index) {
            usize point = (usize)index * side + index;
            visible_mask[point] = 1u;
            double center = fiducial_lattice_center(index, side, 100u, 8.0);
            centers_x[point] = center;
            centers_y[point] = center;
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u, rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = true;
        bool detected = auto_detect_rotation_from_fiducials(
            centers_x, centers_y, side, side, 100u, 100u, 8.0,
            100u, 100u, state, true, degrees, rotation_width,
            rotation_height, margin, &affine, &affine_valid, visible_mask);
        require(!detected && !affine_valid,
                "0357_visible_affine_fit_rejects_collinear_points: singular visible geometry produced an affine fit");
    }});
    cases.push_back({"0327_rotation_geometry_rejects_32bit_overflow","Rotation geometry rejects dimensions wider than its 32-bit representation",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = 20.0 + (double)col * 20.0;
                centers_y[index] = 20.0 + (double)row * 20.0;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(!auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 20.0,
                    0x100000000ull, 100u, state, false, degrees,
                    rotation_width, rotation_height, margin, nullptr, nullptr,
                    nullptr),
                "0327_rotation_geometry_rejects_32bit_overflow: accepted oversized rotation width");
    }});
    cases.push_back({"0352_fiducial_fit_rejects_bounds_exceeding_image","Affine fiducial fit rejects geometry larger than the source image",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        u8 visible[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                double logical_x = fiducial_lattice_center(col, 4u, 100u, 8.0);
                double logical_y = fiducial_lattice_center(row, 4u, 100u, 8.0);
                centers_x[index] = logical_x * 3.0;
                centers_y[index] = logical_y * 3.0;
                visible[index] = 1u;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(!auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 8.0,
                    100u, 100u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, visible),
                "0352_fiducial_fit_rejects_bounds_exceeding_image: accepted oversized fitted bounds");
    }});
    cases.push_back({"0353_wide_image_geometry_tolerance_uses_width","Affine fit geometry tolerance uses the larger width of a wide image",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        u8 visible[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 8.0) * 20.19;
                centers_y[index] = fiducial_lattice_center(row, 4u, 100u, 8.0) * 10.0;
                visible[index] = 1u;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 8.0,
                    2000u, 1000u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, visible),
                "0353_wide_image_geometry_tolerance_uses_width: fit rejected wide image bounds");
        require(affine_valid && rotation_width == 2019u && rotation_height == 1000u,
                "0353_wide_image_geometry_tolerance_uses_width: unexpected fitted geometry");
    }});
    cases.push_back({"0354_affine_fit_rejects_unrepresentable_fitted_width","Affine fit rejects a fitted dimension outside its 32-bit geometry range",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        u8 visible[16] = {};
        const u32 logical_width = 256u;
        const u32 logical_height = 256u;
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u,
                                                           logical_width, 8.0);
                centers_y[index] = fiducial_lattice_center(row, 4u,
                                                           logical_height, 8.0);
                visible[index] = 1u;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(!auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 0x100000100ull,
                    logical_height, 8.0, 5000000000ull, 1000u, state, true,
                    degrees, rotation_width, rotation_height, margin, &affine,
                    &affine_valid, visible),
                "0354_affine_fit_rejects_unrepresentable_fitted_width: accepted an oversized fitted width");
    }});
    cases.push_back({"0355_affine_robust_refinement_preserves_visible_grid_coverage","Affine refinement rejects an inlier subset that drops visible grid edges",[](const fs::path&){
        double centers_x[100] = {};
        double centers_y[100] = {};
        u8 visible[100] = {};
        for (u32 row = 2u; row <= 7u; ++row) {
            for (u32 col = 2u; col <= 7u; ++col) {
                usize index = (usize)row * 10u + col;
                centers_x[index] = fiducial_lattice_center(col, 10u, 200u, 8.0);
                centers_y[index] = fiducial_lattice_center(row, 10u, 200u, 8.0);
                visible[index] = 1u;
            }
        }
        const u32 edge_indices[] = {0u, 9u, 90u, 99u};
        for (u32 index : edge_indices) {
            u32 row = index / 10u;
            u32 col = index % 10u;
            centers_x[index] = fiducial_lattice_center(col, 10u, 200u, 8.0);
            centers_y[index] = fiducial_lattice_center(row, 10u, 200u, 8.0) +
                (row == 0u ? 12.0 : -12.0);
            visible[index] = 1u;
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 10u, 10u, 200u, 200u, 8.0,
                    200u, 200u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, visible),
                "0355_affine_robust_refinement_preserves_visible_grid_coverage: fit failed");
        require(affine_valid && affine.a11 > 0.84 && affine.a11 < 0.87,
                "0355_affine_robust_refinement_preserves_visible_grid_coverage: refinement accepted an edge-truncated inlier subset");
    }});
    cases.push_back({"0356_affine_robust_refinement_preserves_column_support","Affine refinement rejects a candidate that loses most fiducials in one column",[](const fs::path&){
        double centers_x[100] = {};
        double centers_y[100] = {};
        u8 visible[100] = {};
        for (u32 row = 0u; row < 10u; ++row) {
            for (u32 col = 0u; col < 10u; ++col) {
                usize index = (usize)row * 10u + col;
                centers_x[index] = fiducial_lattice_center(col, 10u, 200u, 8.0);
                centers_y[index] = fiducial_lattice_center(row, 10u, 200u, 8.0) +
                    (col == 0u ? 8.5 : 0.0);
                visible[index] = 1u;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 10u, 10u, 200u, 200u, 8.0,
                    300u, 300u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, visible),
                "0356_affine_robust_refinement_preserves_column_support: fit failed");
        require(affine_valid && fabs(affine.a10) > 0.03,
                "0356_affine_robust_refinement_preserves_column_support: shear=" +
                    std::to_string(affine.a10));
    }});
    cases.push_back({"0328_dimension_scale_uses_consistent_fiducials","Affine rotation scale preserves a consistent fiducial estimate near the image dimensions",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 1.0) * 2.2;
                centers_y[index] = fiducial_lattice_center(row, 4u, 100u, 1.0) * 2.2;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 1.0,
                    240u, 240u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0328_dimension_scale_uses_consistent_fiducials: fit failed");
        require(rotation_width == 220u && rotation_height == 220u,
                "0328_dimension_scale_uses_consistent_fiducials: expected the consistent 2.2x fit, got " +
                    std::to_string(rotation_width) + "x" + std::to_string(rotation_height));
    }});
    cases.push_back({"0359_rotation_fit_rejects_null_centers","Fiducial rotation fitting rejects missing center coordinates",[](const fs::path&){
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(!auto_detect_rotation_from_fiducials(
                    nullptr, nullptr, 4u, 4u, 100u, 100u, 1.0,
                    200u, 200u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0359_rotation_fit_rejects_null_centers: accepted missing coordinates");
    }});
    cases.push_back({"0329_dimension_scale_prefers_closer_integer_snap","Dimension scale wins when its integer snap is closer than the fiducial estimate",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 1.0) * 2.3;
                centers_y[index] = fiducial_lattice_center(row, 4u, 100u, 1.0) * 2.3;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 1.0,
                    220u, 220u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0329_dimension_scale_prefers_closer_integer_snap: fit failed");
        require(rotation_width == 220u && rotation_height == 220u,
                "0329_dimension_scale_prefers_closer_integer_snap: dimension estimate was not preferred: " +
                    std::to_string(rotation_width) + "x" + std::to_string(rotation_height));
    }});
    cases.push_back({"0339_rotation_fiducials_exclude_footer_rows","Rotation fitting maps fiducials over content height excluding footer rows",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 20.0);
                centers_y[index] = fiducial_lattice_center(row, 4u, 80u, 20.0);
            }
        }
        PpmParserState state;
        state.has_footer_rows = true;
        state.footer_rows_value = 20u;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 20.0,
                    100u, 100u, state, false, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, nullptr),
                "0339_rotation_fiducials_exclude_footer_rows: fit failed");
        require(rotation_width == 100u && rotation_height == 100u && affine_valid &&
                    fabs(affine.a11 - 1.0) < 1e-9 && fabs(affine.ty) < 1e-9,
                "0339_rotation_fiducials_exclude_footer_rows: footer rows distorted fitted geometry: " +
                    std::to_string(rotation_width) + "x" + std::to_string(rotation_height) +
                    " ty=" + std::to_string(affine.ty));
    }});
    cases.push_back({"0358_visible_rotation_fiducials_exclude_footer_rows","Visible-fiducial affine fitting excludes footer rows",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        u8 visible_mask[16];
        memset(visible_mask, 1, sizeof(visible_mask));
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 20.0);
                centers_y[index] = fiducial_lattice_center(row, 4u, 80u, 20.0);
            }
        }
        PpmParserState state;
        state.has_footer_rows = true;
        state.footer_rows_value = 20u;
        double degrees = 0.0;
        u64 rotation_width = 0u, rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 20.0,
                    100u, 100u, state, false, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, visible_mask),
                "0358_visible_rotation_fiducials_exclude_footer_rows: fit failed");
        require(affine_valid && fabs(affine.a11 - 1.0) < 1e-9 &&
                    fabs(affine.ty) < 1e-9,
                "0358_visible_rotation_fiducials_exclude_footer_rows: footer rows distorted visible affine fit: " +
                    std::to_string(affine.a11) + " ty=" + std::to_string(affine.ty));
    }});
    cases.push_back({"0337_rotation_uses_stored_dimensions_when_fiducials_are_unrotated","Stored source dimensions recover rotation when fiducial centers are unrotated",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 20.0);
                centers_y[index] = fiducial_lattice_center(row, 4u, 80u, 20.0);
            }
        }
        PpmParserState state;
        state.has_rotation_width = true;
        state.rotation_width_value = 100u;
        state.has_rotation_height = true;
        state.rotation_height_value = 80u;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 80u, 20.0,
                    112u, 96u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0337_rotation_uses_stored_dimensions_when_fiducials_are_unrotated: fit failed");
        require(degrees < -5.0 && degrees > -15.0,
                "0337_rotation_uses_stored_dimensions_when_fiducials_are_unrotated: dimension geometry did not recover the rotation: " +
                    std::to_string(degrees));
    }});
    cases.push_back({"0351_square_dimensions_recover_rotation","Square stored dimensions recover a rotation from the observed bounding box",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 8.0);
                centers_y[index] = fiducial_lattice_center(row, 4u, 100u, 8.0);
            }
        }
        PpmParserState state;
        state.has_rotation_width = true;
        state.rotation_width_value = 100u;
        state.has_rotation_height = true;
        state.rotation_height_value = 100u;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 4u, 4u, 100u, 100u, 8.0,
                    122u, 122u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0351_square_dimensions_recover_rotation: fit failed");
        require(degrees > 13.0 && degrees < 17.0,
                "0351_square_dimensions_recover_rotation: square bounding geometry did not recover rotation: " +
                    std::to_string(degrees));
    }});
    cases.push_back({"0331_vertical_scale_uses_average_span_after_pitch_allocation_failure","Vertical scale falls back to average fiducial span when pitch storage cannot be allocated",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        const double physical_rows[4] = {100.0, 18.24, 100.0, 112.48};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = fiducial_lattice_center(col, 4u, 100u, 8.0) * 1.6;
                centers_y[index] = physical_rows[row];
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 5ul;
        bool fit = auto_detect_rotation_from_fiducials(
            centers_x, centers_y, 4u, 4u, 100u, 100u, 8.0,
            160u, 160u, state, false, degrees, rotation_width,
            rotation_height, margin, nullptr, nullptr, nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == 9ul * sizeof(double),
                "0331_vertical_scale_uses_average_span_after_pitch_allocation_failure: missed pitch storage allocation");
        require(fit && rotation_height == 176u,
                "0331_vertical_scale_uses_average_span_after_pitch_allocation_failure: expected average-span height 176, got " +
                    std::to_string(rotation_height));
    }});
    cases.push_back({"0333_horizontal_scale_uses_average_span_after_pitch_allocation_failure","Horizontal scale falls back to average fiducial span when pitch storage cannot be allocated",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        const double physical_columns[4] = {100.0, 18.24, 100.0, 112.48};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                centers_x[index] = physical_columns[col];
                centers_y[index] = fiducial_lattice_center(row, 4u, 100u, 8.0) * 1.6;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 5ul;
        bool fit = auto_detect_rotation_from_fiducials(
            centers_x, centers_y, 4u, 4u, 100u, 100u, 8.0,
            160u, 160u, state, false, degrees, rotation_width,
            rotation_height, margin, nullptr, nullptr, nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == 9ul * sizeof(double),
                "0333_horizontal_scale_uses_average_span_after_pitch_allocation_failure: missed pitch storage allocation");
        require(fit && rotation_width == 176u,
                "0333_horizontal_scale_uses_average_span_after_pitch_allocation_failure: expected average-span width 176, got " +
                    std::to_string(rotation_width));
    }});
    cases.push_back({"0334_horizontal_scale_uses_average_span_when_axis_fit_is_nonpositive","Horizontal average fiducial span recovers a nonpositive regression scale",[](const fs::path&){
        double centers_x[16] = {};
        double centers_y[16] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 4u; ++col) {
                usize index = (usize)row * 4u + col;
                double logical_x = fiducial_lattice_center(col, 4u, 100u, 8.0);
                double logical_y = fiducial_lattice_center(row, 4u, 100u, 8.0);
                centers_x[index] = 200.0 - (logical_x - 37.5) * (200.0 / 24.0);
                centers_y[index] = 50.0 + (logical_y - 37.5) * 9.0;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 5ul;
        bool fit = auto_detect_rotation_from_fiducials(
            centers_x, centers_y, 4u, 4u, 100u, 100u, 8.0,
            216u, 900u, state, false, degrees, rotation_width,
            rotation_height, margin, nullptr, nullptr, nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == 9ul * sizeof(double),
                "0334_horizontal_scale_uses_average_span_when_axis_fit_is_nonpositive: missed pitch allocation");
        require(fit && rotation_width == 200u,
                "0334_horizontal_scale_uses_average_span_when_axis_fit_is_nonpositive: expected average horizontal span scale 2.0, got " +
                    std::to_string(rotation_width));
    }});
    cases.push_back({"0332_vertical_scale_uses_average_span_when_axis_fit_is_zero","Vertical scale uses average row spans when axis regression has no vertical component",[](const fs::path&){
        double centers_x[12] = {};
        double centers_y[12] = {};
        for (u32 row = 0u; row < 12u; ++row) {
            centers_x[row] = 20.0 + (200.0 / 11.0) * (double)row;
            centers_y[row] = 100.0;
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 1u, 12u, 100u, 100u, 8.0,
                    200u, 220u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0332_vertical_scale_uses_average_span_when_axis_fit_is_zero: fit failed");
        require(rotation_height == 200u,
                "0332_vertical_scale_uses_average_span_when_axis_fit_is_zero: expected average-span height 200, got " +
                    std::to_string(rotation_height));
    }});
    cases.push_back({"0335_vertical_average_ignores_subpixel_spans","Vertical average scale ignores subpixel fiducial gaps",[](const fs::path&){
        double centers_x[12] = {};
        double centers_y[12] = {};
        centers_x[0] = 20.0;
        centers_y[0] = 100.0;
        for (u32 row = 0u; row + 1u < 12u; ++row) {
            centers_x[row + 1u] = centers_x[row] + (row ? 0.5 : 20.0);
            centers_y[row + 1u] = 100.0;
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 1u, 12u, 100u, 100u, 8.0,
                    220u, 22u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0335_vertical_average_ignores_subpixel_spans: fit failed");
        require(rotation_height == 20u,
                "0335_vertical_average_ignores_subpixel_spans: subpixel gaps changed vertical scale to " +
                    std::to_string(rotation_height));
    }});
    cases.push_back({"0336_horizontal_average_ignores_subpixel_spans","Horizontal average scale ignores subpixel fiducial gaps",[](const fs::path&){
        double centers_x[12] = {};
        double centers_y[12] = {};
        centers_x[0] = 20.0;
        centers_y[0] = 100.0;
        for (u32 col = 0u; col + 1u < 12u; ++col) {
            centers_x[col + 1u] = centers_x[col] + (col ? 0.5 : 20.0);
            centers_y[col + 1u] = 100.0;
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 12u, 1u, 100u, 100u, 8.0,
                    22u, 220u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0336_horizontal_average_ignores_subpixel_spans: fit failed");
        require(rotation_width == 20u,
                "0336_horizontal_average_ignores_subpixel_spans: subpixel gaps changed horizontal scale to " +
                    std::to_string(rotation_width));
    }});
    cases.push_back({"0330_dimension_scale_fallback_without_horizontal_fiducials","Dimension scale fills an unavailable horizontal fiducial scale",[](const fs::path&){
        double centers_x[12] = {};
        double centers_y[12] = {};
        double fixed_x = fiducial_lattice_center(0u, 1u, 100u, 8.0) * 2.2;
        for (u32 row = 0u; row < 12u; ++row) {
            centers_x[row] = fixed_x;
            centers_y[row] = fiducial_lattice_center(row, 12u, 100u, 8.0) * 2.2;
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        require(auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 1u, 12u, 100u, 100u, 8.0,
                    220u, 220u, state, false, degrees, rotation_width,
                    rotation_height, margin, nullptr, nullptr, nullptr),
                "0330_dimension_scale_fallback_without_horizontal_fiducials: fit failed");
        require(rotation_width == 220u && rotation_height == 220u,
                "0330_dimension_scale_fallback_without_horizontal_fiducials: direct scale fallback failed: " +
                    std::to_string(rotation_width) + "x" + std::to_string(rotation_height));
    }});
    cases.push_back({"0338_rotation_fit_rejects_zero_logical_variance","Rotation fitting rejects a one-point logical grid",[](const fs::path&){
        double centers_x[1] = {50.0};
        double centers_y[1] = {50.0};
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        AffineTransform affine;
        bool affine_valid = false;
        require(!auto_detect_rotation_from_fiducials(
                    centers_x, centers_y, 1u, 1u, 100u, 100u, 8.0,
                    100u, 100u, state, true, degrees, rotation_width,
                    rotation_height, margin, &affine, &affine_valid, nullptr),
                "0338_rotation_fit_rejects_zero_logical_variance: one-point grid was accepted");
        require(!affine_valid,
                "0338_rotation_fit_rejects_zero_logical_variance: one-point grid produced an affine fit");
    }});
    cases.push_back({"0360_gradient_rotation_rejects_empty_sampling_window","Gradient rotation estimation rejects an empty sampling window",[](const fs::path&){
        u8 pixels[10u * 10u * 3u] = {};
        double degrees = 7.0;
        double scale = 3.0;
        require(!estimate_rotation_from_gradients(pixels, 10u, 10u, 10u, 10u,
                                                  5.0, degrees, scale),
                "0360_gradient_rotation_rejects_empty_sampling_window: accepted an empty sampling window");
    }});
    cases.push_back({"0389_gradient_rotation_uses_edge_magnitude","Gradient rotation estimator accepts a strong diagonal edge",[](const fs::path&){
        const u32 width = 64u, height = 64u;
        std::vector<u8> pixels((usize)width * height * 3u);
        for (u32 y = 0u; y < height; ++y) {
            for (u32 x = 0u; x < width; ++x) {
                u8 value = x > y ? 255u : 0u;
                usize offset = ((usize)y * width + x) * 3u;
                pixels[offset] = pixels[offset + 1u] = pixels[offset + 2u] = value;
            }
        }
        double degrees = 0.0, scale = 0.0;
        require(estimate_rotation_from_gradients(pixels.data(), width, height,
                                                 width, height, 2.0,
                                                 degrees, scale),
                "0389_gradient_rotation_uses_edge_magnitude: missed a strong diagonal edge");
        require(fabs(fabs(degrees) - 45.0) < 2.0 && scale > 0.0,
                "0389_gradient_rotation_uses_edge_magnitude: returned invalid rotation geometry");
    }});
    cases.push_back({"0361_page_rotation_rejects_scale_mismatch","Page rotation detector rejects a candidate scale far from the raster scale",[](const fs::path&){
        const std::string id = "0361_page_rotation_rejects_scale_mismatch";
        fs::path work = make_work_dir(id), payload = work / "payload.bin";
        fs::path pages = work / "pages", rotated_path = work / "rotated.ppm";
        fs::create_directories(pages);
        write_file(payload, "page rotation scale mismatch");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, pages, "page",
                             {"--page-width=300", "--page-height=300"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> page_paths = ppm_paths(pages, "page");
        require(page_paths.size() == 1u, id + ": expected one encoded page");
        expect_success(run_transform(work,
            {"transform", "--input", page_paths[0], "--output", rotated_path.string(),
             "--rotate", "7"}, "rotate"), id + " transform");
        PpmImage rotated = read_ppm(rotated_path);
        const unsigned canvas_side = 1800u;
        std::string canvas((size_t)canvas_side * canvas_side * 3u,
                           (char)234u);
        unsigned offset_x = (canvas_side - rotated.width) / 2u;
        unsigned offset_y = (canvas_side - rotated.height) / 2u;
        for (u32 y = 0u; y < rotated.height; ++y) {
            usize source_offset = (usize)y * rotated.width * 3u;
            usize destination_offset = ((usize)(offset_y + y) * canvas_side + offset_x) * 3u;
            memcpy(&canvas[destination_offset], rotated.pixels.data() + source_offset,
                   (usize)rotated.width * 3u);
        }
        PpmParserState state;
        bool detected = auto_detect_page_rotation(
            (const u8*)canvas.data(), canvas_side, canvas_side, 300u, 300u, state);
        require(!detected && !state.has_rotation_degrees,
                id + ": accepted a scale-mismatched rotation candidate");
    }});
    cases.push_back({"0386_page_rotation_rejects_unrepresentable_dimensions","Page rotation detector rejects dimensions before narrowing to unsigned",[](const fs::path&){
        const u64 oversized_width = 0x100000000ull;
        if ((u64)USIZE_MAX_VALUE < oversized_width) return;
        const u8 pixel[3] = {0u, 0u, 0u};
        PpmParserState state;
        platform_allocation_limit_rejection_count = 0ul;
        platform_allocation_size_limit = 1024u;
        bool detected = auto_detect_page_rotation(
            pixel, oversized_width, 1u, 256u, 256u, state);
        platform_allocation_size_limit = 0u;
        require(!detected && platform_allocation_limit_rejection_count == 0ul,
                "0386_page_rotation_rejects_unrepresentable_dimensions: attempted allocation after an out-of-range width");
    }});
    cases.push_back({"0387_page_rotation_rejects_zero_pixel_count","Page rotation detector rejects empty image dimensions before allocation",[](const fs::path&){
        const u8 pixel[3] = {0u, 0u, 0u};
        PpmParserState state;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool detected = auto_detect_page_rotation(pixel, 0u, 1u, 256u, 256u, state);
        record_platform_allocation_sizes = false;
        require(!detected && platform_allocation_sizes.empty(),
                "0387_page_rotation_rejects_zero_pixel_count: accepted an empty image or allocated its grayscale buffer");
    }});
    cases.push_back({"0388_page_rotation_rejects_grayscale_allocation_failure","Page rotation detector propagates grayscale-buffer allocation failure",[](const fs::path&){
        std::vector<u8> pixels((usize)256u * 256u * 3u, 0u);
        PpmParserState state;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool detected = auto_detect_page_rotation(pixels.data(), 256u, 256u,
                                                   256u, 256u, state);
        unsigned long failed_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        minify_allocation_count = 0ul;
        require(!detected && failed_size == 256ul * 256ul &&
                    !state.has_rotation_degrees && !state.has_rotation_width &&
                    !state.has_rotation_height,
                "0388_page_rotation_rejects_grayscale_allocation_failure: did not propagate grayscale-buffer allocation failure");
    }});
    cases.push_back({"0362_page_rotation_rejects_nonpositive_extent","Page rotation detector rejects inconsistent corner bounds",[](const fs::path&){
        const std::string id = "0362_page_rotation_rejects_nonpositive_extent";
        const u32 width = 5u, height = 5u;
        std::string pixels((usize)width * height * 3u, (char)255u);
        const u32 black_points[][2] = {{0u, 4u}, {2u, 3u}, {4u, 0u}};
        for (usize i = 0u; i < sizeof(black_points) / sizeof(black_points[0]); ++i) {
            usize offset = ((usize)black_points[i][1] * width + black_points[i][0]) * 3u;
            pixels[offset] = pixels[offset + 1u] = pixels[offset + 2u] = 0;
        }
        PpmParserState state;
        bool detected = auto_detect_page_rotation(
            (const u8*)pixels.data(), width, height, 256u, 256u, state);
        require(!detected && !state.has_rotation_degrees &&
                    !state.has_rotation_width && !state.has_rotation_height,
                id + ": accepted a nonpositive fitted width");
    }});
    cases.push_back({"0340_affine_bias_allocation_failure_cleans_up","Affine fitting releases earlier bias arrays when a later allocation fails",[](const fs::path&){
        const std::string id = "0340_affine_bias_allocation_failure_cleans_up";
        double centers_x[12] = {};
        double centers_y[12] = {};
        for (u32 row = 0u; row < 4u; ++row) {
            for (u32 col = 0u; col < 3u; ++col) {
                usize index = (usize)row * 3u + col;
                centers_x[index] = fiducial_lattice_center(col, 3u, 100u, 8.0) * 1.3 + 5.0;
                centers_y[index] = fiducial_lattice_center(row, 4u, 100u, 8.0) * 1.3 + 5.0;
            }
        }
        PpmParserState state;
        double degrees = 0.0;
        u64 rotation_width = 0u;
        u64 rotation_height = 0u;
        double margin = 0.0;
        minify_allocation_count = 0ul;
        minify_allocation_failure_at = ~0ul;
        minify_allocation_failure_size = 0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool baseline_fit = auto_detect_rotation_from_fiducials(
            centers_x, centers_y, 3u, 4u, 100u, 100u, 8.0, 130u, 130u,
            state, false, degrees, rotation_width, rotation_height, margin,
            nullptr, nullptr, nullptr);
        record_platform_allocation_sizes = false;
        minify_allocation_failure_at = 0ul;
        require(baseline_fit, id + ": baseline affine analysis failed");
        const size_t row_buffer_size = 4u * sizeof(double);
        const size_t column_buffer_size = 3u * sizeof(double);
        size_t bias_buffer_start = platform_allocation_sizes.size();
        for (size_t index = 0u; index + 3u < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == row_buffer_size &&
                platform_allocation_sizes[index + 1u] == row_buffer_size &&
                platform_allocation_sizes[index + 2u] == column_buffer_size &&
                platform_allocation_sizes[index + 3u] == column_buffer_size) {
                bias_buffer_start = index;
            }
        }
        require(bias_buffer_start < platform_allocation_sizes.size(),
                id + ": did not observe the four affine bias allocations");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = (unsigned long)bias_buffer_start + 2ul;
        watch_platform_allocation_at = (unsigned long)bias_buffer_start + 1ul;
        watch_platform_free_pointer = nullptr;
        watch_platform_free_count = 0ul;
        bool failed_fit = auto_detect_rotation_from_fiducials(
            centers_x, centers_y, 3u, 4u, 100u, 100u, 8.0, 130u, 130u,
            state, false, degrees, rotation_width, rotation_height, margin,
            nullptr, nullptr, nullptr);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        watch_platform_allocation_at = 0ul;
        require(failed_allocation_size == row_buffer_size,
                id + ": fault injection missed the second row bias allocation");
        require(!failed_fit, id + ": affine fit continued after a bias allocation failed");
        require(watch_platform_free_count == 1ul && watch_platform_free_pointer == nullptr,
                id + ": the first row bias allocation was not freed");
    }});
    cases.push_back({"0315_known_tile_palette_text_allocation_failure","Known tile palette text allocation failures stop extraction",[](const fs::path&){
        const std::string id = "0315_known_tile_palette_text_allocation_failure";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "palette text allocation");
        auto encoded = run_cli(work, encode_file_args(
            payload, pages, "page", {"--page-width=80", "--page-height=80"}), "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> page_paths = ppm_paths(pages, "page");
        require(page_paths.size() == 1u, id + ": expected one encoded page");
        MetadataTile::Values tile = extract_tile_values(page_paths[0]);
        PpmImage image = read_ppm(page_paths[0]);
        makocode::ByteBuffer input, frame;
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 bits = 0u;
        minify_allocation_count = 0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool extracted = ppm_extract_frame_bits(
            input, mapping, frame, bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &tile, reinterpret_cast<const u8*>(image.pixels.data()),
            image.width, image.height, 0u, true, false, false, false, nullptr);
        record_platform_allocation_sizes = false;
        require(extracted, id + ": baseline extraction failed");
        require(platform_allocation_sizes.size() >= 2u &&
                    platform_allocation_sizes[1u] == 64u,
                id + ": did not observe the palette text allocation after the pixel copy");
        makocode::ByteBuffer failed_frame;
        PpmParserState failed_state;
        u64 failed_bits = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool failure_extracted = ppm_extract_frame_bits(
            input, mapping, failed_frame, failed_bits, failed_state, false,
            nullptr, 1u, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
            false, nullptr, &tile,
            reinterpret_cast<const u8*>(image.pixels.data()), image.width,
            image.height, 0u, true, false, false, false, nullptr);
        unsigned long failed_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_size == 64u,
                id + ": fault injection missed palette text allocation");
        require(!failure_extracted,
                id + ": continued extraction after palette text allocation failed");
    }});
    cases.push_back({"0304_edge_fiducial_patch_candidates_stay_in_bounds","Affine fiducial patch search skips candidates beyond page edges",[](const fs::path&){
        const std::string id = "0304_edge_fiducial_patch_candidates_stay_in_bounds";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::create_directories(encoded_dir);
        write_file(payload, "edge fiducial patch bounds");
        ProcessResult encoded = run_cli(
            work,
            encode_file_args(payload, encoded_dir, "edge",
                             {"--page-width=80", "--page-height=80",
                              "--fiducial-spacing=39"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "edge");
        require(pages.size() == 1u, id + ": expected one encoded page");
        fs::path scaled = work / "scaled.ppm";
        ProcessResult scaled_result = run_transform(
            work, {"transform", "--input", pages[0], "--output",
                   scaled.string(), "--scale-x", "3", "--scale-y", "3"},
            "scale");
        expect_success(scaled_result, id + " scale");
        makocode::ByteBuffer input, frame;
        require(read_entire_file(scaled.c_str(), input),
                id + ": could not read the scaled page");
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 frame_bits = 0u;
        require(ppm_extract_frame_bits(input, mapping, frame, frame_bits, state,
                                       false),
                id + ": failed to extract frame around scaled edge fiducials");
        require(state.has_fiducial_spacing &&
                    state.fiducial_spacing_value == 39u &&
                    state.has_encoded_tile,
                id + ": edge fiducial fixture did not reach metadata extraction");
    }});
    cases.push_back({"0255_metadata_free_marks_invalid_palette_symbols_as_erasures","Metadata-free recovery marks unrepresentable palette dots as ECC erasures",[](const fs::path&){
        const std::string id = "0255_metadata_free_marks_invalid_palette_symbols_as_erasures";
        const u32 width = 80u, height = 80u;
        const char palette_text[] = "White Black Cyan Magenta Yellow #FF0000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test") &&
                    mapping.custom_palette_count == 6u,
                id + ": could not build a six-color palette");
        mapping.page_width_pixels = width;
        mapping.page_width_set = true;
        mapping.page_height_pixels = height;
        mapping.page_height_set = true;
        struct FiducialSpacingRestore {
            u32 spacing;
            ~FiducialSpacingRestore() { g_fiducial_defaults.spacing_pixels = spacing; }
        } restore{g_fiducial_defaults.spacing_pixels};
        g_fiducial_defaults.spacing_pixels = 0u;

        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(mapping.custom_palette_count, profile),
                id + ": could not build the palette-symbol profile");
        u8 invalid_dots[8];
        for (u32 dot = 0u; dot < 8u; ++dot) invalid_dots[dot] = 5u;
        u32 invalid_value = 0u;
        require(!makocode::palette_dots_to_symbol(profile, invalid_dots, invalid_value),
                id + ": fixture dots unexpectedly represent a field symbol");

        const PaletteColor& invalid_color = mapping.custom_palette[5u];
        std::string raster((usize)width * height * 3u, '\0');
        for (u64 pixel = 0u; pixel < (u64)width * height; ++pixel) {
            const PaletteColor& color = invalid_color;
            usize offset = (usize)pixel * 3u;
            raster[offset] = (char)color.r;
            raster[offset + 1u] = (char)color.g;
            raster[offset + 2u] = (char)color.b;
        }
        makocode::ByteBuffer input;
        require(input.append_ascii("P6\n80 80\n255\n") &&
                    input.append_bytes((const u8*)raster.data(), raster.size()),
                id + ": could not build the PPM fixture");
        makocode::ByteBuffer frame_bits, frame_erasures;
        u64 frame_bit_count = 0u, page_capacity_bits = 0u;
        PpmParserState state;
        MetadataTile::Values values;
        makocode::PaletteSymbolPageSalt page_salt;
        require(recover_metadata_free_single_page(input, mapping, true,
                                                   frame_bits, frame_erasures,
                                                   frame_bit_count,
                                                   page_capacity_bits, state,
                                                   values, &page_salt),
                id + ": metadata-free recovery rejected the valid raster fixture");
        u64 erased_count = 0u;
        for (usize group = 0u; group < frame_erasures.size; ++group) {
            erased_count += frame_erasures.data[group] != 0u;
        }
        require(frame_erasures.size > 0u && erased_count > 0u,
                id + ": unrepresentable palette-dot symbols were not marked as erasures");
    }});
    cases.push_back({"0232_metadata_tile_rejects_missing_schema_payload","Metadata tile construction rejects missing schema payload data",[](const fs::path&){
        const std::string id = "0232_metadata_tile_rejects_missing_schema_payload";
        ImageMappingConfig mapping;
        MetadataTile::Values tile;
        makocode::ByteBuffer tile_bits, active_mask;
        PaletteColor tile_light, tile_dark;
        require(!build_page_metadata_tile(mapping, 0u, 1u, 220u, 220u, 220u,
                                          nullptr, 0.0, 0u, 0u, tile,
                                          tile_bits, active_mask,
                                          tile_light, tile_dark),
                id + ": metadata tile accepted an absent ECC/schema payload");
    }});
    cases.push_back({"0233_metadata_tile_rejects_low_contrast_palette","Metadata tile construction rejects palettes with indistinguishable endpoints",[](const fs::path&){
        const std::string id = "0233_metadata_tile_rejects_low_contrast_palette";
        const char palette_text[] = "#808080 #818181 #828282 #838383";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the low-contrast palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "low contrast endpoints";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        MetadataTile::Values tile;
        makocode::ByteBuffer tile_bits, active_mask;
        PaletteColor tile_light, tile_dark;
        require(!build_page_metadata_tile(mapping, frame_bit_count, 0u,
                                          220u, 220u, 220u,
                                          &encoder.ecc_info(), 0.0,
                                          encoder.ecc_info().unknown_error_budget,
                                          encoder.ecc_info().overlay_erasure_budget,
                                          tile, tile_bits, active_mask,
                                          tile_light, tile_dark),
                id + ": metadata tile accepted palette endpoints without contrast");
    }});
    cases.push_back({"0256_render_rejects_low_contrast_palette","Page rendering propagates low-contrast metadata palette rejection",[](const fs::path&){
        const std::string id = "0256_render_rejects_low_contrast_palette";
        const char palette_text[] = "#808080 #818181 #828282 #838383";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the low-contrast palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "low contrast page render";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        FooterLayout footer;
        footer.data_height_pixels = height;
        u64 bits_per_page = 0u;
        require(compute_bits_per_page(mapping, width, height, height,
                                      bits_per_page),
                id + ": could not calculate page capacity");
        const u64 page_count = 1u;
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        metadata_tile_build_call_count = 0u;
        const bool encoded = encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                                 width, height, 1u, bits_per_page,
                                                 &encoder.ecc_info(), 0.0, nullptr, 0u,
                                                 footer, rendered,
                                                 encoder.ecc_info().unknown_error_budget,
                                                 encoder.ecc_info().overlay_erasure_budget,
                                                 true, &tile, nullptr, page_count);
        require(metadata_tile_build_call_count == 1u,
                id + ": renderer skipped metadata-tile validation");
        require(!encoded,
                id + ": renderer accepted a palette rejected by metadata construction");
    }});
    cases.push_back({"0234_metadata_tile_selects_darkest_palette_color","Metadata tile selects the darkest custom palette color",[](const fs::path&){
        const std::string id = "0234_metadata_tile_selects_darkest_palette_color";
        const char palette_text[] = "#808080 #FFFFFF #000000 #0000FF";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the custom palette");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "darkest palette endpoint";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        MetadataTile::Values tile;
        makocode::ByteBuffer tile_bits, active_mask;
        PaletteColor tile_light, tile_dark;
        require(build_page_metadata_tile(mapping, frame_bit_count, 0u,
                                          220u, 220u, 220u,
                                          &encoder.ecc_info(), 0.0,
                                          encoder.ecc_info().unknown_error_budget,
                                          encoder.ecc_info().overlay_erasure_budget,
                                          tile, tile_bits, active_mask,
                                          tile_light, tile_dark),
                id + ": metadata tile construction failed");
        require(tile_dark.r == 0u && tile_dark.g == 0u && tile_dark.b == 0u,
                id + ": metadata tile did not select the darkest palette color");
    }});
    cases.push_back({"0257_metadata_tile_selects_darkest_standard_color","Metadata tile selects the darkest standard palette color",[](const fs::path&){
        const std::string id = "0257_metadata_tile_selects_darkest_standard_color";
        ImageMappingConfig mapping;
        mapping.color_channels = 2u;
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "standard palette dark endpoint";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        MetadataTile::Values tile;
        makocode::ByteBuffer tile_bits, active_mask;
        PaletteColor tile_light, tile_dark;
        require(build_page_metadata_tile(mapping, frame_bit_count, 1u,
                                          220u, 220u, 220u,
                                          &encoder.ecc_info(), 0.0,
                                          encoder.ecc_info().unknown_error_budget,
                                          encoder.ecc_info().overlay_erasure_budget,
                                          tile, tile_bits, active_mask,
                                          tile_light, tile_dark),
                id + ": metadata tile construction failed");
        require(tile_dark.r == 255u && tile_dark.g == 0u && tile_dark.b == 255u,
                id + ": metadata tile did not select magenta as the darkest standard color");
    }});
    cases.push_back({"0258_frame_payload_rejects_null_data","Frame payload extraction rejects a null buffer even when empty",[](const fs::path&){
        const std::string id = "0258_frame_payload_rejects_null_data";
        PpmParserState metadata;
        makocode::ByteBuffer output, erased_symbols;
        const u8 stale_erasure = 1u;
        require(output.append_ascii("stale") &&
                    erased_symbols.append_bytes(&stale_erasure, 1u),
                id + ": could not prepare stale output values");
        u64 out_bit_count = 99u, encoded_bit_count = 99u;
        require(!frame_bits_to_payload(nullptr, 0u, metadata, output,
                                       out_bit_count, &encoded_bit_count,
                                       nullptr, 0u, &erased_symbols),
                id + ": null frame data was accepted");
        require(output.size == 0u && erased_symbols.size == 0u &&
                    out_bit_count == 0u,
                id + ": failed extraction did not clear its outputs");
    }});
    cases.push_back({"0259_parser_state_rejects_page_count_conflict","Parser-state merge rejects conflicting page counts",[](const fs::path&){
        const std::string id = "0259_parser_state_rejects_page_count_conflict";
        PpmParserState destination, source;
        destination.has_page_count = true;
        destination.page_count_value = 2u;
        source.has_page_count = true;
        source.page_count_value = 3u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting page counts were merged");
        require(destination.page_count_value == 2u,
                id + ": rejected page-count conflict changed the destination");
    }});
    cases.push_back({"0260_parser_state_rejects_page_height_conflict","Parser-state merge rejects conflicting page heights",[](const fs::path&){
        const std::string id = "0260_parser_state_rejects_page_height_conflict";
        PpmParserState destination, source;
        destination.has_page_height_pixels = true;
        destination.page_height_pixels_value = 220u;
        source.has_page_height_pixels = true;
        source.page_height_pixels_value = 240u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting page heights were merged");
        require(destination.page_height_pixels_value == 220u,
                id + ": rejected page-height conflict changed the destination");
    }});
    cases.push_back({"0261_parser_state_rejects_page_width_conflict","Parser-state merge rejects conflicting page widths",[](const fs::path&){
        const std::string id = "0261_parser_state_rejects_page_width_conflict";
        PpmParserState destination, source;
        destination.has_page_width_pixels = true;
        destination.page_width_pixels_value = 220u;
        source.has_page_width_pixels = true;
        source.page_width_pixels_value = 240u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting page widths were merged");
        require(destination.page_width_pixels_value == 220u,
                id + ": rejected page-width conflict changed the destination");
    }});
    cases.push_back({"0262_parser_state_rejects_fiducial_row_conflict","Parser-state merge rejects conflicting fiducial row counts",[](const fs::path&){
        const std::string id = "0262_parser_state_rejects_fiducial_row_conflict";
        PpmParserState destination, source;
        destination.has_fiducial_rows = true;
        destination.fiducial_rows_value = 3u;
        source.has_fiducial_rows = true;
        source.fiducial_rows_value = 4u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting fiducial row counts were merged");
        require(destination.fiducial_rows_value == 3u,
                id + ": rejected row-count conflict changed the destination");
    }});
    cases.push_back({"0263_parser_state_rejects_fiducial_column_conflict","Parser-state merge rejects conflicting fiducial column counts",[](const fs::path&){
        const std::string id = "0263_parser_state_rejects_fiducial_column_conflict";
        PpmParserState destination, source;
        destination.has_fiducial_columns = true;
        destination.fiducial_columns_value = 3u;
        source.has_fiducial_columns = true;
        source.fiducial_columns_value = 4u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting fiducial column counts were merged");
        require(destination.fiducial_columns_value == 3u,
                id + ": rejected column-count conflict changed the destination");
    }});
    cases.push_back({"0264_parser_state_rejects_fiducial_spacing_conflict","Parser-state merge rejects conflicting fiducial spacing",[](const fs::path&){
        const std::string id = "0264_parser_state_rejects_fiducial_spacing_conflict";
        PpmParserState destination, source;
        destination.has_fiducial_spacing = true;
        destination.fiducial_spacing_value = 24u;
        source.has_fiducial_spacing = true;
        source.fiducial_spacing_value = 32u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting fiducial spacing values were merged");
        require(destination.fiducial_spacing_value == 24u,
                id + ": rejected spacing conflict changed the destination");
    }});
    cases.push_back({"0265_parser_state_copies_page_symbols","Parser-state merge copies page-symbol metadata",[](const fs::path&){
        const std::string id = "0265_parser_state_copies_page_symbols";
        PpmParserState destination, source;
        source.has_page_symbols = true;
        source.page_symbols_value = 3600u;
        require(merge_parser_state(destination, source),
                id + ": compatible page-symbol metadata did not merge");
        require(destination.has_page_symbols &&
                    destination.page_symbols_value == 3600u,
                id + ": source page-symbol metadata was not copied");
    }});
    cases.push_back({"0266_parser_state_rejects_page_symbol_conflict","Parser-state merge rejects conflicting page-symbol counts",[](const fs::path&){
        const std::string id = "0266_parser_state_rejects_page_symbol_conflict";
        PpmParserState destination, source;
        destination.has_page_symbols = true;
        destination.page_symbols_value = 3600u;
        source.has_page_symbols = true;
        source.page_symbols_value = 3601u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting page-symbol counts were merged");
        require(destination.page_symbols_value == 3600u,
                id + ": rejected page-symbol conflict changed the destination");
    }});
    cases.push_back({"0267_parser_state_rejects_palette_base_conflict","Parser-state merge rejects conflicting palette bases",[](const fs::path&){
        const std::string id = "0267_parser_state_rejects_palette_base_conflict";
        PpmParserState destination, source;
        destination.has_palette_base = true;
        destination.palette_base_value = 4u;
        source.has_palette_base = true;
        source.palette_base_value = 5u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting palette bases were merged");
        require(destination.palette_base_value == 4u,
                id + ": rejected palette-base conflict changed the destination");
    }});
    cases.push_back({"0296_parser_state_copies_palette_base","Parser-state merge copies a palette base into an unset destination",[](const fs::path&){
        const std::string id = "0296_parser_state_copies_palette_base";
        PpmParserState destination, source;
        source.has_palette_base = true;
        source.palette_base_value = 7u;
        require(merge_parser_state(destination, source),
                id + ": palette base did not merge");
        require(destination.has_palette_base && destination.palette_base_value == 7u,
                id + ": merged palette base was not retained");
    }});
    cases.push_back({"0298_ppm_frame_extraction_rejects_legacy_raster","Frame extraction rejects metadata-free legacy raster input",[](const fs::path&){
        const std::string id = "0298_ppm_frame_extraction_rejects_legacy_raster";
        const u32 width = 64u, height = 64u;
        std::string ppm = "P6\n64 64\n255\n";
        ppm.reserve(ppm.size() + (usize)width * height * 3u);
        for (u32 y = 0u; y < height; ++y) {
            for (u32 x = 0u; x < width; ++x) {
                const char value = ((x + y) & 1u) ? (char)255 : (char)0;
                ppm.append(3u, value);
            }
        }
        makocode::ByteBuffer input, frame_bits;
        require(input.append_bytes((const u8*)ppm.data(), ppm.size()),
                id + ": could not build the raster fixture");
        ImageMappingConfig mapping;
        PpmParserState metadata;
        MetadataTile::Values legacy_tile;
        legacy_tile.schema_version = 0u;
        legacy_tile.palette_count = 2u;
        legacy_tile.palette[0] = PaletteColor{0u, 0u, 0u};
        legacy_tile.palette[1] = PaletteColor{255u, 255u, 255u};
        legacy_tile.page_width_pixels = width;
        legacy_tile.page_height_pixels = height;
        legacy_tile.page_bits = (u64)width * height;
        legacy_tile.page_index = 1u;
        legacy_tile.page_count = 1u;
        u64 frame_bit_count = 0u;
        require(!ppm_extract_frame_bits(input, mapping, frame_bits,
                                        frame_bit_count, metadata, true,
                                        (bool*)0, 1u, (makocode::ByteBuffer*)0,
                                        (u64*)0, (makocode::ByteBuffer*)0,
                                        (u16*)0, (u16*)0, (bool*)0, false,
                                        (u64*)0, &legacy_tile, (const u8*)0,
                                        0u, 0u, 0u, true, true, false, false),
                id + ": accepted metadata-free legacy raster input");
    }});
    cases.push_back({"0297_parser_state_rejects_ecc_original_byte_conflict","Parser-state merge copies and checks the original ECC byte count",[](const fs::path&){
        const std::string id = "0297_parser_state_rejects_ecc_original_byte_conflict";
        PpmParserState destination, source;
        source.has_ecc_original_bytes = true;
        source.ecc_original_bytes_value = 123u;
        require(merge_parser_state(destination, source) &&
                    destination.has_ecc_original_bytes &&
                    destination.ecc_original_bytes_value == 123u,
                id + ": original byte count was not copied");
        PpmParserState conflicting;
        conflicting.has_ecc_original_bytes = true;
        conflicting.ecc_original_bytes_value = 124u;
        require(!merge_parser_state(destination, conflicting) &&
                    destination.ecc_original_bytes_value == 123u,
                id + ": conflicting original byte count was accepted or changed state");
    }});
    cases.push_back({"0268_parser_state_rejects_palette_text_conflict","Parser-state merge rejects conflicting palette text",[](const fs::path&){
        const std::string id = "0268_parser_state_rejects_palette_text_conflict";
        PpmParserState destination, source;
        const char destination_text[] = "White Black";
        const char source_text[] = "White Cyan";
        destination.has_palette_text = true;
        destination.palette_text_length = sizeof(destination_text) - 1u;
        memcpy(destination.palette_text, destination_text, sizeof(destination_text));
        source.has_palette_text = true;
        source.palette_text_length = sizeof(source_text) - 1u;
        memcpy(source.palette_text, source_text, sizeof(source_text));
        require(!merge_parser_state(destination, source),
                id + ": conflicting palette text was merged");
        require(destination.palette_text_length == sizeof(destination_text) - 1u &&
                    memcmp(destination.palette_text, destination_text,
                           sizeof(destination_text)) == 0,
                id + ": rejected palette-text conflict changed the destination");
    }});
    cases.push_back({"0269_parser_state_rejects_ecc_block_count_conflict","Parser-state merge rejects conflicting ECC block counts",[](const fs::path&){
        const std::string id = "0269_parser_state_rejects_ecc_block_count_conflict";
        PpmParserState destination, source;
        destination.has_ecc_block_count = true;
        destination.ecc_block_count_value = 2u;
        source.has_ecc_block_count = true;
        source.ecc_block_count_value = 3u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting ECC block counts were merged");
        require(destination.ecc_block_count_value == 2u,
                id + ": rejected ECC block-count conflict changed the destination");
    }});
    cases.push_back({"0270_parser_state_rejects_ecc_parity_conflict","Parser-state merge rejects conflicting ECC parity counts",[](const fs::path&){
        const std::string id = "0270_parser_state_rejects_ecc_parity_conflict";
        PpmParserState destination, source;
        destination.has_ecc_parity = true;
        destination.ecc_parity_value = 16u;
        source.has_ecc_parity = true;
        source.ecc_parity_value = 20u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting ECC parity counts were merged");
        require(destination.ecc_parity_value == 16u,
                id + ": rejected ECC parity conflict changed the destination");
    }});
    cases.push_back({"0271_parser_state_rejects_ecc_block_data_conflict","Parser-state merge rejects conflicting ECC block data sizes",[](const fs::path&){
        const std::string id = "0271_parser_state_rejects_ecc_block_data_conflict";
        PpmParserState destination, source;
        destination.has_ecc_block_data = true;
        destination.ecc_block_data_value = 223u;
        source.has_ecc_block_data = true;
        source.ecc_block_data_value = 219u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting ECC block data sizes were merged");
        require(destination.ecc_block_data_value == 223u,
                id + ": rejected ECC block-data conflict changed the destination");
    }});
    cases.push_back({"0272_parser_state_rejects_ecc_flag_conflict","Parser-state merge rejects conflicting ECC enabled flags",[](const fs::path&){
        const std::string id = "0272_parser_state_rejects_ecc_flag_conflict";
        PpmParserState destination, source;
        destination.has_ecc_flag = true;
        destination.ecc_flag_value = 0u;
        source.has_ecc_flag = true;
        source.ecc_flag_value = 1u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting ECC enabled flags were merged");
        require(destination.ecc_flag_value == 0u,
                id + ": rejected ECC-flag conflict changed the destination");
    }});
    cases.push_back({"0273_parser_state_rejects_bits_conflict","Parser-state merge rejects conflicting bit counts",[](const fs::path&){
        const std::string id = "0273_parser_state_rejects_bits_conflict";
        PpmParserState destination, source;
        destination.has_bits = true;
        destination.bits_value = 256u;
        source.has_bits = true;
        source.bits_value = 320u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting bit counts were merged");
        require(destination.bits_value == 256u,
                id + ": rejected bit-count conflict changed the destination");
    }});
    cases.push_back({"0274_parser_state_rejects_page_bits_conflict","Parser-state merge rejects conflicting page bit counts",[](const fs::path&){
        const std::string id = "0274_parser_state_rejects_page_bits_conflict";
        PpmParserState destination, source;
        destination.has_page_bits = true;
        destination.page_bits_value = 256u;
        source.has_page_bits = true;
        source.page_bits_value = 320u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting page bit counts were merged");
        require(destination.page_bits_value == 256u,
                id + ": rejected page-bit conflict changed the destination");
    }});
    cases.push_back({"0275_parser_state_rejects_encoded_tile_page_bits_conflict","Parser-state merge rejects conflicting encoded-tile page sizes",[](const fs::path&){
        const std::string id = "0275_parser_state_rejects_encoded_tile_page_bits_conflict";
        PpmParserState destination, source;
        destination.has_encoded_tile = true;
        destination.encoded_tile.page_bits = 256u;
        source.has_encoded_tile = true;
        source.encoded_tile.page_bits = 320u;
        require(!merge_parser_state(destination, source),
                id + ": conflicting encoded-tile page sizes were merged");
        require(destination.encoded_tile.page_bits == 256u,
                id + ": rejected encoded-tile conflict changed the destination");
    }});
    cases.push_back({"0276_append_bits_from_buffer_propagates_writer_failure","Bit-buffer append propagates writer allocation failure",[](const fs::path&){
        const std::string id = "0276_append_bits_from_buffer_propagates_writer_failure";
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        makocode::BitWriter writer;
        const u8 value = 1u;
        const bool appended = append_bits_from_buffer(writer, &value, 1u);
        const unsigned long allocation_count = minify_allocation_count;
        const unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(!appended,
                id + ": bit-buffer append ignored a writer allocation failure");
        require(allocation_count == 1ul && failed_allocation_size != 0ul,
                id + ": allocation failure injection did not reach the writer");
    }});
    cases.push_back({"0277_decode_marks_invalid_palette_symbols_as_erasures","Invalid palette-symbol dots become erasures",[](const fs::path&){
        const std::string id = "0277_decode_marks_invalid_palette_symbols_as_erasures";
        const char palette_text[] = "White Black Cyan Magenta Yellow #FF0000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test") &&
                    mapping.custom_palette_count == 6u,
                id + ": could not build the six-color palette");
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(mapping.custom_palette_count, profile),
                id + ": could not build the palette-symbol profile");
        u8 invalid_dots[8];
        for (u32 dot = 0u; dot < 8u; ++dot) invalid_dots[dot] = 5u;
        u32 invalid_value = 0u;
        require(!makocode::palette_dots_to_symbol(profile, invalid_dots, invalid_value),
                id + ": fixture dots unexpectedly represent a field symbol");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = mapping.custom_palette_count;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "invalid palette symbol erasure";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out one palette-symbol page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        MetadataTile::PayloadRankMapCache rank_cache;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, &rank_cache, page_count),
                id + ": could not render a palette-symbol page");
        require(tile.palette_count == 6u && rank_cache.page_rank_map.data,
                id + ": page rank map or six-color metadata is missing");
        const u64* ranks = (const u64*)rank_cache.page_rank_map.data;
        std::string header = "P6\n220 220\n255\n";
        require(rendered.size == header.size() + (usize)width * height * 3u,
                id + ": rendered PPM has an unexpected size");
        usize changed = 0u;
        for (u64 rank = 0u; rank < 8u; ++rank) {
            usize pixel = 0u;
            while (pixel < (usize)width * height && ranks[pixel] != rank) ++pixel;
            require(pixel < (usize)width * height,
                    id + ": payload rank map is missing symbol dot " + std::to_string(rank));
            usize offset = header.size() + pixel * 3u;
            const PaletteColor& color = mapping.custom_palette[5u];
            rendered.data[offset] = color.r;
            rendered.data[offset + 1u] = color.g;
            rendered.data[offset + 2u] = color.b;
            ++changed;
        }
        require(changed == 8u,
                id + ": did not replace all eight dots for the invalid symbol");
        makocode::ByteBuffer extracted, erasures;
        u64 extracted_bits = 0u;
        PpmParserState metadata;
        bool retry_without_subgrid = false;
        require(ppm_extract_frame_bits(rendered, mapping, extracted,
                                       extracted_bits, metadata, false,
                                       &retry_without_subgrid, 1u, &erasures),
                id + ": could not extract the damaged page");
        require(erasures.size > 0u && erasures.data[0u] != 0u,
                id + ": invalid palette-symbol dots were not marked as an erasure");
    }});
    cases.push_back({"0278_decode_rejects_erasure_buffer_allocation_failure","Frame extraction propagates erasure-buffer allocation failure",[](const fs::path&){
        const std::string id = "0278_decode_rejects_erasure_buffer_allocation_failure";
        ImageMappingConfig mapping;
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 2u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "erasure allocation failure";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build a palette-symbol page");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out one palette-symbol page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count),
                id + ": could not render the palette-symbol page");
        makocode::ByteBuffer baseline_bits, baseline_erasures;
        u64 baseline_bit_count = 0u;
        PpmParserState baseline_metadata;
        bool retry_without_subgrid = false;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = ~0ul;
        require(ppm_extract_frame_bits(rendered, mapping, baseline_bits,
                                       baseline_bit_count, baseline_metadata, false,
                                       &retry_without_subgrid, 1u,
                                       &baseline_erasures),
                id + ": baseline frame extraction failed");
        const unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(allocation_count > 0ul && baseline_erasures.size > 0u,
                id + ": baseline extraction did not allocate erasure bytes");
        require(allocation_count > 1ul,
                id + ": baseline extraction did not allocate separate frame and erasure buffers");
        makocode::ByteBuffer failed_frame_bits, failed_frame_erasures;
        u64 failed_frame_bit_count = 0u;
        PpmParserState failed_frame_metadata;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = allocation_count - 1ul;
        bool frame_extracted = ppm_extract_frame_bits(rendered, mapping,
                                                       failed_frame_bits,
                                                       failed_frame_bit_count,
                                                       failed_frame_metadata, false,
                                                       &retry_without_subgrid, 1u,
                                                       &failed_frame_erasures);
        const unsigned long failed_frame_allocation_count = minify_allocation_count;
        const unsigned long failed_frame_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(!frame_extracted &&
                    failed_frame_allocation_count == allocation_count - 1ul &&
                    failed_frame_allocation_size > 0ul &&
                    failed_frame_bits.size == 0u && failed_frame_erasures.size > 0u,
                id + ": frame-buffer failure count=" +
                    std::to_string(failed_frame_allocation_count) + "/" +
                    std::to_string(allocation_count - 1ul) + " size=" +
                    std::to_string(failed_frame_allocation_size) + " buffers=" +
                    std::to_string(failed_frame_bits.size) + "/" +
                    std::to_string(failed_frame_erasures.size) + " result=" +
                    std::to_string(frame_extracted));
        makocode::ByteBuffer failed_bits, failed_erasures;
        u64 failed_bit_count = 0u;
        PpmParserState failed_metadata;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = allocation_count;
        bool extracted = ppm_extract_frame_bits(rendered, mapping, failed_bits,
                                                failed_bit_count, failed_metadata, false,
                                                &retry_without_subgrid, 1u,
                                                &failed_erasures);
        const unsigned long failed_allocation_count = minify_allocation_count;
        const unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(!extracted,
                id + ": extraction succeeded after erasure-buffer allocation failed");
        require(failed_allocation_count == allocation_count &&
                    failed_allocation_size >= baseline_erasures.size &&
                    failed_bits.size > 0u && failed_erasures.size == 0u,
                id + ": failure injection reached count=" +
                    std::to_string(failed_allocation_count) + "/" +
                    std::to_string(allocation_count) + " size=" +
                    std::to_string(failed_allocation_size) + "/" +
                    std::to_string(baseline_erasures.size) + " buffers=" +
                    std::to_string(failed_bits.size) + "/" +
                    std::to_string(failed_erasures.size));
    }});
    cases.push_back({"0279_decode_extracts_nonfirst_page_segment","Frame extraction selects the requested page segment",[](const fs::path&){
        const std::string id = "0279_decode_extracts_nonfirst_page_segment";
        ImageMappingConfig mapping;
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 2u;
        encoder.config.ecc_redundancy = 0.75;
        encoder.payload_is_preencoded = true;
        std::vector<u8> payload(1024u);
        for (usize i = 0u; i < payload.size(); ++i) {
            payload[i] = (u8)((i * 17u + 3u) & 0xFFu);
        }
        require(encoder.set_payload(payload.data(), payload.size()) &&
                    encoder.build(),
                id + ": could not build a multipage palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 80u, height = 80u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count > 1u && bits_per_page > 0u &&
                    (bits_per_page & 31u) == 0u,
                id + ": fixture did not produce aligned multiple pages");
        const u64 page_index = page_count;
        const u64 bit_offset = (page_index - 1u) * bits_per_page;
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, bit_offset,
                                   width, height, page_index, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count),
                id + ": could not render page two");
        makocode::ByteBuffer extracted, erasures;
        u64 extracted_bits = 0u;
        PpmParserState metadata;
        bool retry_without_subgrid = false;
        require(ppm_extract_frame_bits(rendered, mapping, extracted,
                                       extracted_bits, metadata, false,
                                       &retry_without_subgrid, page_index,
                                       &erasures),
                id + ": could not extract page two");
        u64 expected_segment_bits = tile.page_bits - bit_offset;
        if (expected_segment_bits > bits_per_page) expected_segment_bits = bits_per_page;
        require(expected_segment_bits < bits_per_page &&
                    metadata.has_encoded_tile &&
                    metadata.encoded_tile.page_index == page_index &&
                    extracted_bits == expected_segment_bits,
                id + ": extracted page metadata or segment length is incorrect");
        usize comparison_bytes = extracted.size < 32u ? extracted.size : 32u;
        usize source_offset = (usize)(bit_offset >> 3u);
        require(comparison_bytes > 0u && source_offset <= frame.size &&
                    comparison_bytes <= frame.size - source_offset &&
                    memcmp(extracted.data, frame.data + source_offset,
                           comparison_bytes) == 0,
                id + ": extracted symbols do not match the requested frame segment");
    }});
    cases.push_back({"0280_decode_rejects_page_index_beyond_stream","Frame extraction rejects a page index beyond the encoded stream",[](const fs::path&){
        const std::string id = "0280_decode_rejects_page_index_beyond_stream";
        ImageMappingConfig mapping;
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 2u;
        encoder.config.ecc_redundancy = 0.75;
        encoder.payload_is_preencoded = true;
        u8 payload[128];
        for (u32 i = 0u; i < sizeof(payload); ++i) payload[i] = (u8)(i * 29u + 7u);
        require(encoder.set_payload(payload, sizeof(payload)) && encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 80u, height = 80u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget),
                id + ": could not compute page layout");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count),
                id + ": could not render page one");
        makocode::ByteBuffer extracted, erasures;
        u64 extracted_bits = 0u;
        PpmParserState metadata;
        bool retry_without_subgrid = false;
        u64 out_of_range_page = page_count + 1u;
        bool extracted_ok = ppm_extract_frame_bits(
            rendered, mapping, extracted, extracted_bits, metadata, false,
            &retry_without_subgrid, out_of_range_page, &erasures,
            nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, &tile);
        require(!extracted_ok,
                id + ": accepted a page index beyond the encoded stream");
        makocode::ByteBuffer overflow_extracted, overflow_erasures;
        u64 overflow_bits = 0u;
        PpmParserState overflow_metadata;
        bool overflow_retry_without_subgrid = false;
        const u64 wrapping_page_hint = ((u64)1u << 56u) + 1u;
        bool overflow_extracted_ok = ppm_extract_frame_bits(
            rendered, mapping, overflow_extracted, overflow_bits,
            overflow_metadata, false, &overflow_retry_without_subgrid,
            wrapping_page_hint, &overflow_erasures, nullptr, nullptr,
            nullptr, nullptr, nullptr, false, nullptr, &tile);
        require(!overflow_extracted_ok,
                id + ": accepted a page index whose offset multiplication wraps");
    }});
    cases.push_back({"0282_decode_recovers_password_salt_erasures","Damaged password salt symbols recover through erasure correction",[](const fs::path&){
        const std::string id = "0282_decode_recovers_password_salt_erasures";
        const char palette_text[] = "White Black Cyan Magenta Yellow #FF0000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test") &&
                    mapping.custom_palette_count == 6u,
                id + ": could not build the six-color palette");
        u8 salt[makocode::ENCRYPTION_SALT_BYTES];
        for (u32 i = 0u; i < sizeof(salt); ++i) salt[i] = (u8)(i * 13u + 5u);
        makocode::PaletteSymbolPasswordContext placement_context;
        makocode::PaletteSymbolPasswordContext payload_context;
        const char password[] = "salt-erasure";
        require(placement_context.derive(password, sizeof(password) - 1u) &&
                    payload_context.derive_salted(placement_context, salt),
                id + ": could not derive password contexts");
        mapping.symbol_placement_salt = salt;
        mapping.symbol_placement_password_context = &placement_context;
        mapping.symbol_payload_password_context = &payload_context;
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(mapping.custom_palette_count, profile),
                id + ": could not build the palette-symbol profile");
        makocode::ByteBuffer encoded_salt;
        require(makocode::palette_symbol_encode_page_salt(profile, salt, 1u, 1u,
                                                          encoded_salt),
                id + ": could not encode the fixture page salt");
        u32 damaged_salt_symbols[17] = {};
        u32 damaged_salt_symbol_count = 0u;
        for (u32 symbol = 0u;
             symbol < makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS &&
             damaged_salt_symbol_count < 17u; ++symbol) {
            if (read_le_u32(encoded_salt.data + (usize)symbol * 4u) != 0u) {
                damaged_salt_symbols[damaged_salt_symbol_count++] = symbol;
            }
        }
        require(damaged_salt_symbol_count == 17u,
                id + ": fixture salt does not have seventeen nonzero symbols");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = mapping.custom_palette_count;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "recover damaged password salt";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out one password-salted page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        MetadataTile::PayloadRankMapCache rank_cache;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, &rank_cache, page_count),
                id + ": could not render a password-salted page");
        const u64* ranks = (const u64*)rank_cache.page_rank_map.data;
        std::string header = "P6\n220 220\n255\n";
        for (u32 damaged = 0u; damaged < damaged_salt_symbol_count; ++damaged) {
            for (u32 dot = 0u; dot < 8u; ++dot) {
                u64 rank = (u64)damaged_salt_symbols[damaged] * 8u + dot;
                usize pixel = 0u;
                while (pixel < (usize)width * height && ranks[pixel] != rank) ++pixel;
                require(pixel < (usize)width * height,
                        id + ": page rank map is missing a salt dot");
                usize offset = header.size() + pixel * 3u;
                const PaletteColor& invalid_color = mapping.custom_palette[5u];
                rendered.data[offset] = invalid_color.r;
                rendered.data[offset + 1u] = invalid_color.g;
                rendered.data[offset + 2u] = invalid_color.b;
            }
        }
        makocode::ByteBuffer extracted, erasures;
        u64 extracted_bits = 0u;
        PpmParserState metadata;
        bool retry_without_subgrid = false;
        makocode::PaletteSymbolPageSalt decoded_salt;
        bool extracted_ok = ppm_extract_frame_bits(
            rendered, mapping, extracted, extracted_bits, metadata, false,
            &retry_without_subgrid, 1u, &erasures, nullptr, nullptr,
            nullptr, nullptr, nullptr, false, nullptr, nullptr,
            nullptr, 0u, 0u, 0u, false, true, false, false,
            &decoded_salt);
        require(extracted_ok && decoded_salt.available &&
                    memcmp(decoded_salt.salt, salt, sizeof(salt)) == 0,
                id + ": damaged salt symbols were not recovered as erasures");
    }});
    cases.push_back({"0283_decode_rejects_palette_profile_mismatch","Frame extraction rejects a palette that conflicts with stream metadata",[](const fs::path&){
        const std::string id = "0283_decode_rejects_palette_profile_mismatch";
        const char six_color_text[] = "White Black Cyan Magenta Yellow #FF0000";
        const char four_color_text[] = "White Black Cyan Magenta";
        ImageMappingConfig encoding_mapping;
        ImageMappingConfig decoding_mapping;
        require(image_mapping_set_palette_text(encoding_mapping, six_color_text,
                                               sizeof(six_color_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(encoding_mapping, "test") &&
                    encoding_mapping.custom_palette_count == 6u &&
                    image_mapping_set_palette_text(decoding_mapping, four_color_text,
                                                   sizeof(four_color_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(decoding_mapping, "test") &&
                    decoding_mapping.custom_palette_count == 4u,
                id + ": could not build encoding and decoding palettes");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = encoding_mapping.custom_palette_count;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "palette profile mismatch";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the palette-symbol stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(encoding_mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out a six-color page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(encoding_mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count),
                id + ": could not render the six-color page");
        makocode::ByteBuffer extracted, erasures;
        u64 extracted_bits = 0u;
        PpmParserState metadata;
        bool retry_without_subgrid = false;
        bool extracted_ok = ppm_extract_frame_bits(
            rendered, decoding_mapping, extracted, extracted_bits, metadata,
            false, &retry_without_subgrid, 1u, &erasures);
        require(!extracted_ok,
                id + ": accepted a four-color profile for a six-color stream");
    }});
    cases.push_back({"0342_decode_preserves_non_gray_palette_override","Frame extraction does not treat a non-gray override as the built-in gray palette",[](const fs::path&){
        const std::string id = "0342_decode_preserves_non_gray_palette_override";
        const char palette_text[] = "White Black Cyan Magenta";
        ImageMappingConfig encoding_mapping;
        ImageMappingConfig decoding_mapping;
        require(image_mapping_set_palette_text(decoding_mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(decoding_mapping, "test") &&
                    decoding_mapping.custom_palette_count == 4u,
                id + ": could not build the non-gray decode override");
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 2u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "gray palette override mismatch";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the two-color stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        const u32 width = 220u, height = 220u;
        PageFooterConfig footer_config;
        FooterLayout footer;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(encoding_mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u,
                id + ": could not lay out the two-color page");
        makocode::ByteBuffer rendered;
        MetadataTile::Values tile;
        require(encode_page_to_ppm(encoding_mapping, frame, frame_bit_count, 0u,
                                   width, height, 1u, bits_per_page,
                                   &encoder.ecc_info(), 0.0, nullptr, 0u,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget,
                                   true, &tile, nullptr, page_count) &&
                    !tile.palette_is_custom,
                id + ": could not render the built-in gray page");
        makocode::ByteBuffer extracted, erasures;
        u64 extracted_bits = 0u;
        PpmParserState metadata;
        bool retry_without_subgrid = false;
        bool extracted_ok = ppm_extract_frame_bits(
            rendered, decoding_mapping, extracted, extracted_bits, metadata,
            false, &retry_without_subgrid, 1u, &erasures);
        require(!extracted_ok,
                id + ": accepted a non-gray palette override for a built-in gray stream");
    }});
    cases.push_back({"0284_bilinear_rgb_interpolation","Bilinear RGB sampling interpolates all channels",[](const fs::path&){
        const std::string id = "0284_bilinear_rgb_interpolation";
        const u8 pixels[12] = {
            0u, 10u, 20u, 100u, 110u, 120u,
            200u, 210u, 220u, 255u, 250u, 240u};
        u8 sampled[3] = {};
        require(sample_bilinear_rgb(pixels, 2u, 2u, 2u, 0.5, 0.5, sampled),
                id + ": bilinear sampling rejected a valid raster");
        require(sampled[0] == 139u && sampled[1] == 145u && sampled[2] == 150u,
                id + ": midpoint interpolation produced incorrect RGB values");
        require(sample_bilinear_rgb(pixels, 2u, 2u, 2u, -1.0, 2.0, sampled) &&
                    sampled[0] == 200u && sampled[1] == 210u && sampled[2] == 220u,
                id + ": edge coordinates were not clamped to the raster");
    }});
    cases.push_back({"0285_nearest_rgb_sampling","Nearest RGB sampling selects the closest raster pixel",[](const fs::path&){
        const std::string id = "0285_nearest_rgb_sampling";
        const u8 pixels[12] = {
            1u, 2u, 3u, 10u, 20u, 30u,
            100u, 110u, 120u, 200u, 210u, 220u};
        u8 sampled[3] = {};
        require(sample_nearest_rgb(pixels, 2u, 2u, 2u, 4u,
                                   0.6, 0.6, sampled) &&
                    sampled[0] == 200u && sampled[1] == 210u && sampled[2] == 220u,
                id + ": nearest sampling did not select the bottom-right pixel");
        require(sample_nearest_rgb(pixels, 2u, 2u, 2u, 4u,
                                   0.49, 0.49, sampled) &&
                    sampled[0] == 1u && sampled[1] == 2u && sampled[2] == 3u,
                id + ": nearest sampling rounded a point across the pixel boundary");
    }});
    cases.push_back({"0286_skew_raster_row_offset","Skew sampling offsets follow horizontal position",[](const fs::path&){
        const std::string id = "0286_skew_raster_row_offset";
        require(skew_raster_row_offset(0.0, 101u, 12.0) == 0.0 &&
                    skew_raster_row_offset(50.0, 101u, 12.0) == 6.0 &&
                    skew_raster_row_offset(100.0, 101u, 12.0) == 12.0,
                id + ": horizontal skew did not scale across the source width");
        require(skew_raster_row_offset(7.0, 1u, -4.0) == 0.0,
                id + ": a single-column source produced a skew offset");
    }});
    cases.push_back({"0287_rotate_sample_coordinates","Rotation sampling uses the full rotation matrix",[](const fs::path&){
        double output_x = 0.0, output_y = 0.0;
        rotate_sample_coordinates(4.0, 5.0, 2.0, 3.0, 0.0, 1.0,
                                  10.0, 20.0, output_x, output_y);
        require(output_x == 8.0 && output_y == 22.0,
                "0287: rotated sample coordinates are incorrect");
        skew_rotate_sample_coordinates(50.0, 5.0, 101u, 12.0,
                                       0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                                       output_x, output_y);
        require(output_x == -11.0 && output_y == 50.0,
                "0287: skew was not applied before rotation");
    }});
    cases.push_back({"0288_rotation_sampling_dimensions","Rotation sampling rejects empty geometry",[](const fs::path&){
        const std::string id = "0288_rotation_sampling_dimensions";
        require(rotation_sampling_dimensions_valid(1u, 1u, 1u, 1u),
                id + ": nonempty dimensions were rejected");
        require(!rotation_sampling_dimensions_valid(0u, 1u, 1u, 1u) &&
                    !rotation_sampling_dimensions_valid(1u, 0u, 1u, 1u) &&
                    !rotation_sampling_dimensions_valid(1u, 1u, 0u, 1u) &&
                    !rotation_sampling_dimensions_valid(1u, 1u, 1u, 0u),
                id + ": a zero sampling dimension was accepted");
    }});
    cases.push_back({"0289_frame_payload_rejects_short_erasure_map","Frame payload extraction rejects a truncated erasure map",[](const fs::path&){
        const std::string id = "0289_frame_payload_rejects_short_erasure_map";
        const u8 frame_data[4] = {1u, 2u, 3u, 4u};
        const u8 erasures[1] = {1u};
        PpmParserState metadata;
        makocode::ByteBuffer payload, erased_symbols;
        u64 payload_bits = 0u;
        require(!frame_bits_to_payload(frame_data, 32u, metadata, payload,
                                       payload_bits, 0, erasures, 0u,
                                       &erased_symbols),
                id + ": accepted an erasure bit count shorter than its symbol map");
    }});
    cases.push_back({"0295_frame_payload_rejects_unaligned_length","Frame payload extraction rejects a non-symbol-aligned frame length",[](const fs::path&){
        const std::string id = "0295_frame_payload_rejects_unaligned_length";
        const u8 frame_data[4] = {1u, 2u, 3u, 4u};
        PpmParserState metadata;
        makocode::ByteBuffer output, erased_symbols;
        u64 payload_bits = 0u;
        require(!frame_bits_to_payload(frame_data, 31u, metadata, output,
                                       payload_bits, 0, nullptr, 0u,
                                       &erased_symbols),
                id + ": accepted a frame length that is not symbol aligned");
        require(output.size == 0u && erased_symbols.size == 0u && payload_bits == 0u,
                id + ": rejected frame extraction left nonempty outputs");
    }});
    cases.push_back({"0290_frame_payload_zeroes_missing_erasure_map","Frame payload extraction initializes absent erasures to zero",[](const fs::path&){
        const std::string id = "0290_frame_payload_zeroes_missing_erasure_map";
        const u8 frame_data[4] = {1u, 2u, 3u, 4u};
        PpmParserState metadata;
        makocode::ByteBuffer payload, erased_symbols;
        u64 payload_bits = 0u;
        require(frame_bits_to_payload(frame_data, 32u, metadata, payload,
                                      payload_bits, 0, nullptr, 0u,
                                      &erased_symbols) &&
                    erased_symbols.size == 1u && erased_symbols.data[0] == 0u,
                id + ": absent erasure metadata was not represented as a zero map");
    }});
    cases.push_back({"0291_small_marker_free_page_falls_back_without_salt","Small marker-free pages skip unavailable salt prefixes",[](const fs::path&){
        const std::string id = "0291_small_marker_free_page_falls_back_without_salt";
        const usize salt_prefix_dots =
            (usize)makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS * 8u;
        require(!marker_free_salt_prefix_available(salt_prefix_dots - 1u) &&
                    marker_free_salt_prefix_available(salt_prefix_dots),
                id + ": salt prefix capacity boundary is incorrect");
        const u32 width = 25u, height = 25u;
        ImageMappingConfig mapping;
        const char palette_text[] = "White Black";
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the binary palette");
        mapping.page_width_set = true;
        mapping.page_height_set = true;
        mapping.page_width_pixels = width;
        mapping.page_height_pixels = height;
        makocode::ByteBuffer input;
        std::string header = "P6\n25 25\n255\n";
        std::vector<u8> pixels((usize)width * height * 3u, 255u);
        require(input.append_ascii(header.c_str()) &&
                    input.append_bytes(pixels.data(), pixels.size()),
                id + ": could not build the small PPM fixture");
        u32 old_spacing = g_fiducial_defaults.spacing_pixels;
        g_fiducial_defaults.spacing_pixels = 0u;
        makocode::ByteBuffer frame, erasures;
        u64 frame_bits = 0u, page_capacity = 0u;
        PpmParserState state;
        MetadataTile::Values values;
        makocode::PaletteSymbolPageSalt salt;
        bool decoded = recover_metadata_free_single_page(
            input, mapping, true, frame, erasures, frame_bits, page_capacity,
            state, values, &salt);
        g_fiducial_defaults.spacing_pixels = old_spacing;
        require(decoded && frame_bits > 0u && page_capacity > 0u &&
                    !salt.available,
                id + ": small page did not fall back to unsalted decoding");
    }});
    cases.push_back({"0292_marker_free_probe_requires_dominant_center","Metadata probing rejects a non-dominant tile center",[](const fs::path&){
        const std::string id = "0292_marker_free_probe_requires_dominant_center";
        const u32 total = MetadataTile::TILE_SIDE * MetadataTile::TILE_SIDE;
        const u32 threshold = (total * 75u + 99u) / 100u;
        require(metadata_center_color_is_dominant(false, threshold) &&
                    !metadata_center_color_is_dominant(false, threshold - 1u) &&
                    metadata_center_color_is_dominant(true, 0u),
                id + ": center-color dominance threshold was not applied");
    }});
    cases.push_back({"0293_marker_free_decode_propagates_rank_map_allocation_failure","Marker-free decoding propagates rank-map allocation failures",[](const fs::path&){
        const std::string id = "0293_marker_free_decode_propagates_rank_map_allocation_failure";
        const u32 width = 25u, height = 25u;
        ImageMappingConfig mapping;
        const char palette_text[] = "White Black";
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test") &&
                    image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not build the binary palette");
        mapping.page_width_set = true;
        mapping.page_height_set = true;
        mapping.page_width_pixels = width;
        mapping.page_height_pixels = height;
        makocode::ByteBuffer input;
        std::string header = "P6\n25 25\n255\n";
        std::vector<u8> pixels((usize)width * height * 3u, 255u);
        require(input.append_ascii(header.c_str()) &&
                    input.append_bytes(pixels.data(), pixels.size()),
                id + ": could not build the small PPM fixture");
        u32 old_spacing = g_fiducial_defaults.spacing_pixels;
        g_fiducial_defaults.spacing_pixels = 0u;
        bool found_rank_map_allocation_failure = false;
        for (unsigned long failure_at = 1ul; failure_at <= 16ul &&
             !found_rank_map_allocation_failure; ++failure_at) {
            minify_allocation_count = 0ul;
            minify_allocation_failure_size = 0ul;
            minify_allocation_failure_at = failure_at;
            makocode::ByteBuffer frame, erasures;
            u64 frame_bits = 0u, page_capacity = 0u;
            PpmParserState state;
            MetadataTile::Values values;
            bool decoded = recover_metadata_free_single_page(
                input, mapping, true, frame, erasures, frame_bits,
                page_capacity, state, values, nullptr);
            unsigned long failure_size = minify_allocation_failure_size;
            minify_allocation_failure_at = 0ul;
            unsigned long rank_map_bytes = (unsigned long)width * height *
                                           sizeof(u64);
            if (!decoded && failure_size >= rank_map_bytes &&
                failure_size < rank_map_bytes * 2ul) {
                found_rank_map_allocation_failure = true;
            }
        }
        minify_allocation_failure_at = 0ul;
        g_fiducial_defaults.spacing_pixels = old_spacing;
        require(found_rank_map_allocation_failure,
                id + ": could not target the payload rank-map allocation");
    }});
    cases.push_back({"0059_minify_literals","Minify preserves source literals and directives",[](const fs::path&){minify_literals_case();}});
    cases.push_back({"0061_cli_decoder_paths","Decoder image paths, LZMA, and match finder behavior",[](const fs::path&){cli_decoder_paths_case();}});
    cases.push_back({"0113_footer_features","Footer wrapping, page selection, over-barcode ECC, and page bounds",[](const fs::path&){footer_features_case();}});
    cases.push_back({"0103_rs_erasures","RS erasure decoder enforces mixed correction boundaries",[](const fs::path&){rs_erasures_case();}});
    cases.push_back({"0104_canonical_tile","Canonical center tile reserves ECC and page capacity",[](const fs::path&){canonical_tile_case();}});
    cases.push_back({"0105_canonical_overlay_multipage","Canonical overlay selectors merge across multipage streams",[](const fs::path&){canonical_overlay_multipage_case();}});
    cases.push_back({"0106_palette_base5_capacity","Base-5 palette exceeds two bits per pixel",[](const fs::path&){palette_base5_capacity_case();}});
    cases.push_back({"0114_decode_blob_erasures","Solid-color blot positions become ECC erasures",[](const fs::path&){blob_erasure_case();}});
    cases.push_back({"0129_palette_symbol_codec","Eight-dot finite-field packing profiles round-trip",[](const fs::path&){palette_symbol_codec_case();}});
    cases.push_back({"0107_metadata_tile_format","Variable metadata fields and multi-block tile Reed-Solomon framing",[](const fs::path&){metadata_tile_format_case();}});
    cases.push_back({"0111_partial_ecc_recovery","Partial ECC archive preserves earlier recoverable files",[](const fs::path&){partial_ecc_recovery_case();}});
    cases.push_back({"0116_decode_recover_palette","Damaged palette rows recover from ordered color candidates",[](const fs::path&){recover_palette_case();}});
    cases.push_back({"0313_palette_recovery_skips_duplicate_candidate","Palette recovery skips an explicit palette repeated in image candidates",[](const fs::path&){
        const std::string id = "0313_palette_recovery_skips_duplicate_candidate";
        fs::path work = make_work_dir(id);
        fs::path payload = work / "payload.bin";
        fs::path encoded_dir = work / "encoded";
        fs::path decoded_dir = work / "decoded";
        generate_bytes(work, payload, 4096u, 48621u, "payload");
        fs::create_directories(encoded_dir);
        const std::string actual_palette =
            "White #FCFC04 #04FCFC #FC04FC #040404 #FC0404";
        const std::string explicit_palette =
            "#FCFCFC #FCFC04 #04FCFC #FC04FC #FC0404 #040404";
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, encoded_dir, "page",
                             {"--page-width=700", "--page-height=700",
                              "--ecc=2.0", "--palette", actual_palette}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> pages = ppm_paths(encoded_dir, "page");
        require(pages.size() == 1u, id + ": expected one encoded page");
        fs::path damaged = damage_palette_row(pages[0], work / "damaged.ppm", 6u);
        ProcessResult decoded = run_cli(work,
            decode_file_args({damaged.string()}, decoded_dir,
                             {"--recover-palette", "--recover-limit", "6",
                              "--palette", explicit_palette}),
            "decode");
        expect_success(decoded, id + " decode");
        compare_files(payload, decoded_dir / "payload.bin", id);
    }});
    cases.push_back({"0343_palette_recovery_counter_grows_past_one_digit","Palette recovery reports attempts beyond nine",[](const fs::path&){
        const std::string id = "0343_palette_recovery_counter_grows_past_one_digit";
        fs::path work = make_work_dir(id);
        fs::path input = work / "three_color.ppm";
        fs::path decoded = work / "decoded";
        std::string pixels;
        pixels.reserve(96u * 96u * 3u);
        const u8 colors[3][3] = {{255u, 255u, 255u}, {0u, 0u, 0u}, {240u, 16u, 32u}};
        for (u32 y = 0u; y < 96u; ++y) {
            for (u32 x = 0u; x < 96u; ++x) {
                const u8* color = colors[(x / 32u + y / 32u) % 3u];
                pixels.push_back((char)color[0]);
                pixels.push_back((char)color[1]);
                pixels.push_back((char)color[2]);
            }
        }
        write_file(input, "P6\n96 96\n255\n" + pixels);
        ProcessResult result = run_cli(work,
            decode_file_args({input.string()}, decoded,
                             {"--recover-palette", "--recover-limit", "10"}),
            "decode");
        expect_failure(result, id + " decode");
        require((result.err + result.out).find(
                    "palette recovery tried 10 candidate(s); no palette matched") !=
                    std::string::npos,
                id + ": palette recovery attempt count did not grow past nine");
    }});
    cases.push_back({"0344_palette_recovery_accepts_leading_zero_limit","Palette recovery interprets a limit with leading zeroes",[](const fs::path&){
        const std::string id = "0344_palette_recovery_accepts_leading_zero_limit";
        fs::path work = make_work_dir(id);
        fs::path input = work / "three_color.ppm";
        fs::path decoded = work / "decoded";
        std::string pixels;
        pixels.reserve(96u * 96u * 3u);
        const u8 colors[3][3] = {{255u, 255u, 255u}, {0u, 0u, 0u}, {240u, 16u, 32u}};
        for (u32 y = 0u; y < 96u; ++y) {
            for (u32 x = 0u; x < 96u; ++x) {
                const u8* color = colors[(x / 32u + y / 32u) % 3u];
                pixels.push_back((char)color[0]);
                pixels.push_back((char)color[1]);
                pixels.push_back((char)color[2]);
            }
        }
        write_file(input, "P6\n96 96\n255\n" + pixels);
        ProcessResult result = run_cli(work,
            decode_file_args({input.string()}, decoded,
                             {"--recover-palette", "--recover-limit", "00010"}),
            "decode");
        expect_failure(result, id + " decode");
        require((result.err + result.out).find(
                    "palette recovery tried 10 candidate(s); no palette matched") !=
                    std::string::npos,
                id + ": leading zeroes changed the recovery limit");
    }});
    cases.push_back({"0349_rgb_override_rejects_pixel_buffer_allocation_failure","Frame extraction rejects an RGB override when its pixel buffer cannot be allocated",[](const fs::path&){
        const u8 pixels[4u * 4u * 3u] = {};
        makocode::ByteBuffer input, frame;
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 frame_bits = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_at = 1ul;
        minify_allocation_failure_size = 0ul;
        bool extracted = ppm_extract_frame_bits(
            input, mapping, frame, frame_bits, state, false, nullptr, 1u,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false,
            nullptr, nullptr, pixels, 4u, 4u);
        unsigned long failed_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        minify_allocation_count = 0ul;
        require(!extracted && failed_size == 64ul,
                "0349_rgb_override_rejects_pixel_buffer_allocation_failure: "
                "failed RGB override allocation was not rejected");
    }});
    cases.push_back({"0060_decode_stdin","Decoder reads one PPM page from stdin",[](const fs::path&){decode_stdin_case();}});
    cases.push_back({"0064_archive_symlink_roundtrip","Directory symlink target decodes as a regular file",[](const fs::path&){archive_symlink_case();}});
    cases.push_back({"0066_fiducials","Custom fiducial dimensions happy path",[](const fs::path&){fiducials_case();}});
    cases.push_back({"0067_default_encode_naming","Default encode output uses a timestamped filename",[](const fs::path&){default_naming_case();}});
    cases.push_back({"0070_ppm_transform_cli","ppm_transform help and fatal error paths",[](const fs::path&){ppm_transform_cli_case();}});
    cases.push_back({"0071_decode_ecc_retry","Canonical ECC failure retries without fiducial subgrid",[](const fs::path&){decode_ecc_retry_case();}});
    cases.push_back({"0069_decode_raw_fallback","Raw decode fallback is gated and writes recovered bytes",[](const fs::path&){raw_fallback_case();}});
    cases.push_back({"0062_prefix_ends_encoded","Prefix ending in _encoded keeps its filename and fiducials",[](const fs::path&){prefix_ends_encoded_case();}});
    cases.push_back({"0065_page_fit","Automatic page width and height fit independently",[](const fs::path&){page_fit_case();}});
    cases.push_back({"0137_ecc_fill_fit_limit","ECC fill search backs off when a parity candidate cannot fit",[](const fs::path&){
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        double ratio = -1.0, requested_ratio = -1.0;
        u16 block_data = 0u, parity = 0u;
        u64 frame_bits = 0u, bits_needed = 0u, page_count = 0u;
        require(find_best_ecc_fill(4096u, 0.01, 254.0, mapping, footer,
                                   0u, 80u, true, false, 0.0, 0.0, 2u, false,
                                   ratio, requested_ratio, block_data, parity,
                                   frame_bits, bits_needed, page_count, 0u, 1u),
                "0137: ECC fill did not find a feasible one-page candidate");
        require(parity > 200u && page_count == 1u,
                "0137: ECC fill did not retain the strongest parity candidate that fits");
    }});
    cases.push_back({"0138_ecc_fill_low_start_recovery","ECC fill recovers from an infeasible minimum ratio",[](const fs::path&){
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        double ratio = -1.0, requested_ratio = -1.0;
        u16 block_data = 0u, parity = 0u;
        u64 frame_bits = 0u, bits_needed = 0u, page_count = 0u;
        require(find_best_ecc_fill(256u, 0.001, 254.0, mapping, footer,
                                   0u, 80u, true, false, 0.0, 0.0, 2u, false,
                                   ratio, requested_ratio, block_data, parity,
                                   frame_bits, bits_needed, page_count, 0u, 1u),
                "0138: ECC fill failed after the minimum ratio proved infeasible");
        require(ratio > 0.0 && ratio < 0.02 && parity == 4u && page_count == 1u,
                "0138: ECC fill did not retain the lowest feasible redundancy");
    }});
    cases.push_back({"0141_ecc_fill_zero_minimum","Zero ECC minimum starts at one symbol per payload byte",[](const fs::path&){
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        double zero_ratio = -1.0, zero_requested_ratio = -1.0;
        u16 zero_block_data = 0u, zero_parity = 0u;
        u64 zero_frame_bits = 0u, zero_bits_needed = 0u, zero_page_count = 0u;
        double expected_ratio = -1.0, expected_requested_ratio = -1.0;
        u16 expected_block_data = 0u, expected_parity = 0u;
        u64 expected_frame_bits = 0u, expected_bits_needed = 0u, expected_page_count = 0u;
        require(find_best_ecc_fill(256u, 0.0, 254.0, mapping, footer,
                                   0u, 80u, true, false, 0.0, 0.0, 2u, false,
                                   zero_ratio, zero_requested_ratio, zero_block_data, zero_parity,
                                   zero_frame_bits, zero_bits_needed, zero_page_count, 0u, 1u),
                "0141: ECC fill rejected a zero minimum ratio");
        require(find_best_ecc_fill(256u, 1.0 / 256.0, 254.0, mapping, footer,
                                   0u, 80u, true, false, 0.0, 0.0, 2u, false,
                                   expected_ratio, expected_requested_ratio,
                                   expected_block_data, expected_parity,
                                   expected_frame_bits, expected_bits_needed,
                                   expected_page_count, 0u, 1u),
                "0141: ECC fill rejected its derived minimum ratio");
        require(zero_ratio == expected_ratio &&
                    zero_requested_ratio == expected_requested_ratio &&
                    zero_block_data == expected_block_data && zero_parity == expected_parity &&
                    zero_frame_bits == expected_frame_bits &&
                    zero_bits_needed == expected_bits_needed &&
                    zero_page_count == expected_page_count,
                "0141: zero minimum ratio did not use the one-symbol payload minimum");
    }});
    cases.push_back({"0142_empty_frame_page_layout","Empty frame layout still contains one page",[](const fs::path&){
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        FooterLayout layout;
        u32 data_height = 0u;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer, 0u, 500u, 500u,
                                    layout, data_height, bits_per_page, page_count,
                                    0, 0.0, 0u, 0u),
                "0142: empty frame layout failed");
        require(page_count == 1u && bits_per_page > 0u,
                "0142: empty frame layout lost its minimum page");
    }});
    cases.push_back({"0143_page_layout_insufficient_capacity","Page layout rejects space smaller than one data symbol",[](const fs::path&){
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        FooterLayout layout;
        u32 data_height = 0u;
        u64 bits_per_page = 0u, page_count = 0u;
        require(!compute_page_layout(mapping, footer, 1u, 7u, 1u,
                                     layout, data_height, bits_per_page, page_count,
                                     0, 0.0, 0u, 0u),
                "0143: page layout accepted a page without one data symbol of capacity");
    }});
    cases.push_back({"0144_metadata_page_layout_alloc_failure","Metadata page layout fails when a later metadata tile allocation fails",[](const fs::path&){
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(2u, profile), "0144: profile failed");
        std::vector<u8> bytes(4096u, 0x5Au);
        makocode::ByteBuffer stream;
        makocode::EccSummary summary;
        require(makocode::palette_symbol_encode_stream(profile, bytes.data(), bytes.size(),
                                                        0.05, 0u, 0u, 0, 0u,
                                                        stream, summary), "0144: encode failed");
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        FooterLayout layout;
        u32 data_height = 0u;
        u64 bits_per_page = 0u, page_count = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 25ul;
        bool ok = compute_page_layout(mapping, footer, (u64)stream.size * 8u,
                                      80u, 80u, layout, data_height, bits_per_page,
                                      page_count, &summary, 0.0,
                                      summary.unknown_error_budget,
                                      summary.overlay_erasure_budget);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0144: selected allocation did not fail while building a later metadata tile");
        require(!ok, "0144: page layout succeeded after a later metadata tile allocation failed");
    }});
    cases.push_back({"0145_metadata_fiducial_mask_alloc_failure","Metadata page layout fails when fiducial mask allocation fails",[](const fs::path&){
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(2u, profile), "0145: profile failed");
        std::vector<u8> bytes(256u, 0x5Au);
        makocode::ByteBuffer stream;
        makocode::EccSummary summary;
        require(makocode::palette_symbol_encode_stream(profile, bytes.data(), bytes.size(),
                                                        0.05, 0u, 0u, 0, 0u,
                                                        stream, summary), "0145: encode failed");
        ImageMappingConfig mapping;
        PageFooterConfig footer;
        FooterLayout layout;
        u32 data_height = 0u;
        u64 bits_per_page = 0u, page_count = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool ok = compute_page_layout(mapping, footer, (u64)stream.size * 8u,
                                      80u, 80u, layout, data_height, bits_per_page,
                                      page_count, &summary, 0.0,
                                      summary.unknown_error_budget,
                                      summary.overlay_erasure_budget);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0145: selected allocation did not fail while building the fiducial mask");
        require(!ok, "0145: page layout succeeded after fiducial mask allocation failed");
    }});
    cases.push_back({"0146_footer_invalid_range_end","Malformed footer ranges do not select a page",[](const fs::path&){
        PageFooterConfig footer;
        footer.text = "footer";
        footer.text_length = 6u;
        footer.pages = "1-bad";
        footer.pages_length = 5u;
        makocode::ByteBuffer text;
        require(footer_build_page_text(footer, 1u, 2u, text),
                "0146: malformed page range failed text construction");
        require(text.size == 0u,
                "0146: malformed range end incorrectly selected a footer page");
        text.release();
    }});
    cases.push_back({"0147_footer_last_page_selector","Footer last selector includes the final page",[](const fs::path&){
        PageFooterConfig footer;
        footer.text = "last";
        footer.text_length = 4u;
        footer.pages = "last";
        footer.pages_length = 4u;
        makocode::ByteBuffer text;
        require(footer_build_page_text(footer, 3u, 3u, text),
                "0147: final-page footer text construction failed");
        require(text.size == 4u && std::memcmp(text.data, "last", 4u) == 0,
                "0147: last selector omitted the final page footer");
        text.release();
    }});
    cases.push_back({"0148_footer_rejects_control_characters","Footer text rejects control characters",[](const fs::path&){
        require(footer_char_is_allowed('A'), "0148: printable footer character was rejected");
        require(!footer_char_is_allowed('\n'), "0148: newline was allowed in footer text");
        require(!footer_char_is_allowed('\x7f'), "0148: DEL was allowed in footer text");
    }});
    cases.push_back({"0366_write_all_fd_rejects_broken_pipe","A failed descriptor write is not reported as successful",[](const fs::path&){
        const std::string id = "0366_write_all_fd_rejects_broken_pipe";
        int pipe_fds[2] = {-1, -1};
        require(::pipe(pipe_fds) == 0, id + ": could not create pipe");
        struct sigaction ignored_action = {};
        struct sigaction previous_action = {};
        ignored_action.sa_handler = SIG_IGN;
        sigemptyset(&ignored_action.sa_mask);
        if (::sigaction(SIGPIPE, &ignored_action, &previous_action) != 0) {
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
            throw Failure(id + ": could not ignore SIGPIPE");
        }
        ::close(pipe_fds[0]);
        const u8 bytes[] = {0x4Du};
        bool write_ok = write_all_fd(pipe_fds[1], bytes, sizeof(bytes));
        ::close(pipe_fds[1]);
        int restore_result = ::sigaction(SIGPIPE, &previous_action, nullptr);
        require(restore_result == 0, id + ": could not restore SIGPIPE handler");
        require(!write_ok, id + ": reported a broken-pipe write as successful");
    }});
    cases.push_back({"0367_write_all_fd_rejects_null_data","A nonempty write rejects a null data pointer",[](const fs::path&){
        require(!write_all_fd(1, nullptr, 1u),
                "0367: accepted a null data pointer for a nonempty write");
    }});
    cases.push_back({"0371_bits_per_sample_rejects_unknown_mode","Unknown channel modes have no sample bits",[](const fs::path&){
        require(bits_per_sample(1u) == 1u && bits_per_sample(2u) == 2u &&
                    bits_per_sample(3u) == 3u,
                "0371: supported channel mode returned the wrong sample width");
        require(bits_per_sample(0u) == 0u && bits_per_sample(4u) == 0u &&
                    bits_per_sample(255u) == 0u,
                "0371: unknown channel mode was assigned sample bits");
    }});
    cases.push_back({"0372_cmyw_footer_uses_palette_text_color","CMYW footer text uses a visible palette color",[](const fs::path& work){
        const std::string id = "0372_cmyw_footer_uses_palette_text_color";
        ImageMappingConfig mapping;
        mapping.color_channels = 2u;
        makocode::EncoderContext encoder;
        encoder.config.palette_size = 4u;
        encoder.config.ecc_redundancy = 0.75;
        const char payload[] = "CMYW footer";
        require(encoder.set_payload((const u8*)payload, sizeof(payload) - 1u) &&
                    encoder.build(),
                id + ": could not build the four-color stream");
        makocode::ByteBuffer frame;
        u64 frame_bit_count = 0u;
        require(build_frame_bits(encoder, frame, frame_bit_count),
                id + ": could not build frame bits");
        PageFooterConfig footer_config;
        footer_config.text = "A";
        footer_config.text_length = 1u;
        FooterLayout footer;
        const u32 width = 220u, height = 220u;
        u32 data_height = height;
        u64 bits_per_page = 0u, page_count = 0u;
        require(compute_page_layout(mapping, footer_config, frame_bit_count,
                                    width, height, footer, data_height,
                                    bits_per_page, page_count, &encoder.ecc_info(),
                                    0.0, encoder.ecc_info().unknown_error_budget,
                                    encoder.ecc_info().overlay_erasure_budget) &&
                    page_count == 1u && footer.has_text && data_height < height,
                id + ": could not lay out the CMYW footer");
        makocode::ByteBuffer rendered;
        require(encode_page_to_ppm(mapping, frame, frame_bit_count, 0u,
                                   width, height, 0u, bits_per_page,
                                   &encoder.ecc_info(), 0.0,
                                   footer_config.text, footer_config.text_length,
                                   footer, rendered,
                                   encoder.ecc_info().unknown_error_budget,
                                   encoder.ecc_info().overlay_erasure_budget),
                id + ": could not render the CMYW footer");
        fs::path rendered_path = work / (id + ".ppm");
        write_file(rendered_path,
                   std::string((const char*)rendered.data, rendered.size));
        PpmImage image = read_ppm(rendered_path);
        bool saw_cyan_text = false;
        for (u32 y = data_height; y < height; ++y) {
            for (u32 x = 0u; x < width; ++x) {
                usize pixel = ((usize)y * width + x) * 3u;
                if ((u8)image.pixels[pixel] == 0u &&
                    (u8)image.pixels[pixel + 1u] == 255u &&
                    (u8)image.pixels[pixel + 2u] == 255u) {
                    saw_cyan_text = true;
                    break;
                }
            }
            if (saw_cyan_text) break;
        }
        require(saw_cyan_text,
                id + ": footer text did not use the CMYW cyan palette color");
    }});
    cases.push_back({"0368_rotate_right_u8_wraps_and_masks_amount","Byte rotation wraps bits and masks amounts to eight positions",[](const fs::path&){
        require(rotate_right_u8(0x96u, 1u) == 0x4Bu,
                "0368: one-bit right rotation produced the wrong byte");
        require(rotate_right_u8(0x81u, 1u) == 0xC0u,
                "0368: high bit did not wrap into the low seven-bit position");
        require(rotate_right_u8(0x96u, 8u) == 0x96u &&
                    rotate_right_u8(0x96u, 9u) == 0x4Bu,
                "0368: rotation amount was not reduced modulo eight");
    }});
    cases.push_back({"0369_rotate_left_u8_wraps_and_masks_amount","Left byte rotation wraps bits and masks amounts to eight positions",[](const fs::path&){
        require(rotate_left_u8(0x96u, 1u) == 0x2Du,
                "0369: one-bit left rotation produced the wrong byte");
        require(rotate_left_u8(0x81u, 1u) == 0x03u,
                "0369: high bit did not wrap into the low bit");
        require(rotate_left_u8(0x96u, 8u) == 0x96u &&
                    rotate_left_u8(0x96u, 9u) == 0x2Du,
                "0369: rotation amount was not reduced modulo eight");
    }});
    cases.push_back({"0149_archive_file_write_failure","Archive extraction reports a file write failure",[](const fs::path&){
        const std::string id = "0149_archive_file_write_failure";
        fs::path work = make_work_dir(id);
        fs::path output = work / "output";
        fs::create_directories(output / "blocked.txt");
        const u8 contents[] = {'x'};
        ArchiveBuildContext archive;
        require(archive_init(archive) && archive_add_file(archive, "blocked.txt", contents,
                                                         sizeof(contents)) && archive_finalize(archive),
                "0149: archive setup failed");
        require(!unpack_archive_to_directory(archive.buffer, output.c_str()),
                "0149: extraction succeeded when the target path was a directory");
    }});
    cases.push_back({"0150_archive_parent_symlink_race","Archive extraction rejects a parent directory replaced by a symlink",[](const fs::path&){
        const std::string id = "0150_archive_parent_symlink_race";
        fs::path work = make_work_dir(id), output = work / "output", outside = work / "outside";
        fs::create_directories(output);
        fs::create_directories(outside);
        fs::path race_dir = output / "race", moved_dir = work / "original_race";
        const char* prior_swap_path = ::getenv("MAKOCODE_TEST_SWAP_DIR_PATH");
        const char* prior_swap_backup = ::getenv("MAKOCODE_TEST_SWAP_DIR_BACKUP");
        const char* prior_swap_target = ::getenv("MAKOCODE_TEST_SWAP_DIR_TARGET");
        bool had_swap_path = prior_swap_path != 0;
        bool had_swap_backup = prior_swap_backup != 0;
        bool had_swap_target = prior_swap_target != 0;
        std::string saved_swap_path = had_swap_path ? prior_swap_path : "";
        std::string saved_swap_backup = had_swap_backup ? prior_swap_backup : "";
        std::string saved_swap_target = had_swap_target ? prior_swap_target : "";
        const u8 contents[] = {'x'};
        ArchiveBuildContext archive;
        require(archive_init(archive) &&
                    archive_add_file(archive, "race/payload.txt", contents, sizeof(contents)) &&
                    archive_finalize(archive),
                "0150: archive setup failed");
        setenv("MAKOCODE_TEST_SWAP_DIR_PATH", race_dir.c_str(), 1);
        setenv("MAKOCODE_TEST_SWAP_DIR_BACKUP", moved_dir.c_str(), 1);
        setenv("MAKOCODE_TEST_SWAP_DIR_TARGET", outside.c_str(), 1);
        bool extracted = unpack_archive_to_directory(archive.buffer, output.c_str());
        if (had_swap_path) setenv("MAKOCODE_TEST_SWAP_DIR_PATH", saved_swap_path.c_str(), 1);
        else unsetenv("MAKOCODE_TEST_SWAP_DIR_PATH");
        if (had_swap_backup) setenv("MAKOCODE_TEST_SWAP_DIR_BACKUP", saved_swap_backup.c_str(), 1);
        else unsetenv("MAKOCODE_TEST_SWAP_DIR_BACKUP");
        if (had_swap_target) setenv("MAKOCODE_TEST_SWAP_DIR_TARGET", saved_swap_target.c_str(), 1);
        else unsetenv("MAKOCODE_TEST_SWAP_DIR_TARGET");
        require(fs::is_symlink(race_dir) && fs::is_directory(moved_dir),
                "0150: parent directory was not replaced by the test symlink hook");
        require(!extracted, "0150: extraction accepted a parent replaced by a symlink");
        require(!fs::exists(outside / "payload.txt"),
                "0150: extraction wrote through the replaced parent symlink");
    }});
    cases.push_back({"0151_archive_parent_creation_failure","Archive extraction fails when a file blocks parent directory creation",[](const fs::path&){
        const std::string id = "0151_archive_parent_creation_failure";
        fs::path work = make_work_dir(id);
        fs::path output = work / "output";
        fs::create_directories(output);
        write_file(output / "blocked", "not a directory");
        const u8 contents[] = {'x'};
        ArchiveBuildContext archive;
        require(archive_init(archive) &&
                    archive_add_file(archive, "blocked/payload.txt", contents, sizeof(contents)) &&
                    archive_finalize(archive),
                "0151: archive setup failed");
        require(!unpack_archive_to_directory(archive.buffer, output.c_str()),
                "0151: extraction continued after parent directory creation failed");
        require(read_file(output / "blocked") == "not a directory",
                "0151: extraction changed the blocking file");
    }});
    cases.push_back({"0152_archive_directory_creation_failure","Archive extraction fails when a file blocks directory creation",[](const fs::path&){
        const std::string id = "0152_archive_directory_creation_failure";
        fs::path work = make_work_dir(id);
        fs::path output = work / "output";
        fs::create_directories(output);
        write_file(output / "blocked", "not a directory");
        ArchiveBuildContext archive;
        require(archive_init(archive) && archive_add_directory(archive, "blocked") &&
                    archive_finalize(archive),
                "0152: archive setup failed");
        require(!unpack_archive_to_directory(archive.buffer, output.c_str()),
                "0152: extraction continued after directory creation failed");
        require(read_file(output / "blocked") == "not a directory",
                "0152: extraction changed the blocking file");
    }});
    cases.push_back({"0153_archive_raw_stream_extract","Anonymous archive payload extracts as payload.bin",[](const fs::path&){
        const std::string id = "0153_archive_raw_stream_extract";
        fs::path work = make_work_dir(id);
        fs::path output = work / "output";
        fs::create_directories(output);
        const u8 contents[] = {0u, 'r', 'a', 'w', 0xFFu};
        ArchiveBuildContext archive;
        require(archive_init(archive) &&
                    MetadataTile::write_compact_u64(archive.entries, sizeof(contents)) &&
                    archive_append_bytes(archive.entries, contents, sizeof(contents)),
                "0153: raw archive setup failed");
        archive.raw_stream = true;
        require(archive_finalize(archive), "0153: raw archive finalization failed");
        require(unpack_archive_to_directory(archive.buffer, output.c_str()),
                "0153: raw archive extraction failed");
        require(read_file(output / "payload.bin") ==
                    std::string(reinterpret_cast<const char*>(contents), sizeof(contents)),
                "0153: raw archive payload changed during extraction");
    }});
    cases.push_back({"0192_archive_raw_stream_symlink_output","Anonymous archive payload rejects a symlinked output root",[](const fs::path&){
        const std::string id = "0192_archive_raw_stream_symlink_output";
        fs::path work = make_work_dir(id);
        fs::path target = work / "target";
        fs::path output = work / "output";
        fs::create_directories(target);
        fs::create_directory_symlink(target, output);
        const u8 contents[] = {0u, 'r', 'a', 'w', 0xFFu};
        ArchiveBuildContext archive;
        require(archive_init(archive) &&
                    MetadataTile::write_compact_u64(archive.entries, sizeof(contents)) &&
                    archive_append_bytes(archive.entries, contents, sizeof(contents)),
                "0192: raw archive setup failed");
        archive.raw_stream = true;
        require(archive_finalize(archive), "0192: raw archive finalization failed");
        require(!unpack_archive_to_directory(archive.buffer, output.c_str()),
                "0192: extraction accepted a symlinked output root");
        require(!fs::exists(target / "payload.bin"),
                "0192: extraction wrote through the output symlink");
    }});
    cases.push_back({"0154_join_output_path_backslashes","Output path joins normalize backslash separators",[](const fs::path&){
        makocode::ByteBuffer output;
        require(join_output_path("cache\\renders\\pages", "page.ppm", output),
                "0154: output path join failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "cache/renders/pages/page.ppm",
                "0154: backslash separators were not normalized");
    }});
    cases.push_back({"0155_join_output_path_allocation_failure","Output path join reports allocation failure",[](const fs::path&){
        makocode::ByteBuffer output;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool joined = join_output_path("output", "payload.bin", output);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0155: selected output-path allocation did not fail");
        require(!joined, "0155: path join succeeded after output allocation failed");
    }});
    cases.push_back({"0156_parent_path_allocation_failure","Parent path creation reports allocation failure",[](const fs::path&){
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool created = ensure_parent_directories("output/payload.bin");
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0156: selected parent-path allocation did not fail");
        require(!created,
                "0156: parent-path creation succeeded after allocation failed");
    }});
    cases.push_back({"0157_parent_path_empty_rejected","Empty file path is rejected by parent-directory preparation",[](const fs::path&){
        require(!ensure_parent_directories(""),
                "0157: empty file path was accepted");
    }});
    cases.push_back({"0158_directory_tree_allocation_failure","Directory tree creation reports allocation failure",[](const fs::path&){
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool created = ensure_directory_tree("output/subdirectory");
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0158: selected path allocation did not fail");
        require(!created,
                "0158: directory tree succeeded after allocation failed");
    }});
    cases.push_back({"0159_directory_tree_creation_failure","Directory tree creation reports a filesystem failure",[](const fs::path&){
        const std::string id = "0159_directory_tree_creation_failure";
        fs::path work = make_work_dir(id);
        write_file(work / "blocked", "not a directory");
        bool created = ensure_directory_tree((work / "blocked" / "child").c_str());
        require(!created,
                "0159: directory tree succeeded when a file blocked its path");
    }});
    cases.push_back({"0160_directory_creation_race","Directory tree accepts a directory created during mkdir",[](const fs::path&){
        const std::string id = "0160_directory_creation_race";
        fs::path work = make_work_dir(id);
        fs::path raced_directory = work / "created_by_racer";
        const char* prior_race_path = ::getenv("MAKOCODE_TEST_CREATE_DIR_RACE_PATH");
        bool had_race_path = prior_race_path != 0;
        std::string saved_race_path = had_race_path ? prior_race_path : "";
        setenv("MAKOCODE_TEST_CREATE_DIR_RACE_PATH", raced_directory.c_str(), 1);
        bool created = ensure_directory_tree(raced_directory.c_str());
        if (had_race_path) setenv("MAKOCODE_TEST_CREATE_DIR_RACE_PATH", saved_race_path.c_str(), 1);
        else unsetenv("MAKOCODE_TEST_CREATE_DIR_RACE_PATH");
        require(created,
                "0160: directory tree rejected a concurrently created directory");
        require(fs::is_directory(raced_directory),
                "0160: simulated concurrent directory was not created");
    }});
    cases.push_back({"0195_directory_tree_path_component_too_long","Directory tree rejects an uncreatable path component",[](const fs::path&){
        const std::string id = "0195_directory_tree_path_component_too_long";
        fs::path work = make_work_dir(id);
        std::string too_long_component(300u, 'x');
        bool created = ensure_directory_tree((work / too_long_component / "child").c_str());
        require(!created,
                "0195: directory tree succeeded when mkdir rejected an overlong component");
    }});
    cases.push_back({"0161_archive_file_size_usize_limit","Archive rejects a file size that exceeds the simulated usize limit",[](const fs::path&){
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bit(0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bits((u8)'f', 8u) &&
                    MetadataTile::write_compact_u64(archive_bits, 0x100000000ull),
                "0161: malformed archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0161: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        u64 previous_limit = archive_parse_usize_max_override;
        archive_parse_usize_max_override = 0xFFFFFFFFull;
        bool parsed = archive_parse(archive, (ArchiveEntryCallback)0,
                                    (void*)0, (u64*)0, true);
        archive_parse_usize_max_override = previous_limit;
        require(!parsed,
                "0161: archive parser accepted a file size above the usize limit");
    }});
    cases.push_back({"0162_archive_partial_truncated_file_size","Partial archive parsing retains earlier entries when a file size is truncated",[](const fs::path&){
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 2u) &&
                    archive_bits.write_bit(0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bits((u8)'a', 8u) &&
                    MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bits((u8)'x', 8u) &&
                    archive_bits.write_bit(0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bits((u8)'b', 8u) &&
                    archive_bits.write_bits(4u, 3u),
                "0162: truncated archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0162: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        require(archive_parse(archive, (ArchiveEntryCallback)0,
                              (void*)0, (u64*)0, true),
                "0162: partial parsing discarded a prior complete entry");
        require(!archive_parse(archive, (ArchiveEntryCallback)0,
                               (void*)0, (u64*)0, false),
                "0162: strict parsing accepted a truncated file size");
    }});
    cases.push_back({"0163_archive_partial_truncated_path","Partial archive parsing retains earlier entries when a path is truncated",[](const fs::path&){
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 2u) &&
                    archive_bits.write_bit(1u) &&
                    MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bits((u8)'a', 8u) &&
                    archive_bits.write_bit(0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 2u) &&
                    archive_bits.write_bits((u8)'b', 8u),
                "0163: truncated path archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0163: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        require(archive_parse(archive, (ArchiveEntryCallback)0,
                              (void*)0, (u64*)0, true),
                "0163: partial parsing discarded a prior complete entry");
        require(!archive_parse(archive, (ArchiveEntryCallback)0,
                               (void*)0, (u64*)0, false),
                "0163: strict parsing accepted a truncated path");
    }});
    cases.push_back({"0164_archive_path_length_usize_limit","Archive rejects a path length above the simulated usize limit",[](const fs::path&){
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bit(1u) &&
                    MetadataTile::write_compact_u64(archive_bits, 2u) &&
                    archive_bits.write_bits((u8)'o', 8u) &&
                    archive_bits.write_bits((u8)'k', 8u),
                "0164: archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0164: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        u64 previous_limit = archive_parse_usize_max_override;
        archive_parse_usize_max_override = 1u;
        bool parsed = archive_parse(archive, (ArchiveEntryCallback)0,
                                    (void*)0, (u64*)0, false);
        archive_parse_usize_max_override = previous_limit;
        require(!parsed,
                "0164: archive parser accepted a path above the usize limit");
    }});
    cases.push_back({"0165_archive_partial_raw_stream","Partial raw streams expose complete bytes despite a truncated final byte",[](const fs::path&){
        const u8 contents[] = {0u, 'r', 'a', 0xFFu};
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 5u) &&
                    archive_bits.write_bits(contents[0], 8u) &&
                    archive_bits.write_bits(contents[1], 8u) &&
                    archive_bits.write_bits(contents[2], 8u) &&
                    archive_bits.write_bits(0xBu, 4u),
                "0165: truncated raw archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0165: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        std::vector<u8> extracted;
        auto callback = [](u8 entry_type, const char*, usize, const u8* data,
                           usize length, u64, void* context) -> bool {
            if (entry_type != 2u) return false;
            std::vector<u8>* output = (std::vector<u8>*)context;
            output->assign(data, data + length);
            return true;
        };
        require(archive_parse(archive, callback, &extracted, (u64*)0, true),
                "0165: partial raw archive parsing failed");
        require(extracted.size() == 3u &&
                    memcmp(extracted.data(), contents, extracted.size()) == 0,
                "0165: partial raw archive did not expose its complete bytes");
        extracted.clear();
        require(!archive_parse(archive, callback, &extracted, (u64*)0, false),
                "0165: strict raw archive parsing accepted truncated data");
        require(extracted.empty(),
                "0165: strict parsing invoked the raw-stream callback");
    }});
    cases.push_back({"0193_archive_partial_raw_stream_callback_failure","Partial raw stream parsing propagates callback failure",[](const fs::path&){
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 2u) &&
                    archive_bits.write_bits((u8)'x', 8u),
                "0193: truncated raw archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0193: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        bool callback_called = false;
        auto callback = [](u8 entry_type, const char*, usize, const u8*,
                           usize, u64, void* context) -> bool {
            if (entry_type != 2u) return false;
            *(bool*)context = true;
            return false;
        };
        require(!archive_parse(archive, callback, &callback_called,
                               (u64*)0, true),
                "0193: partial raw archive ignored callback failure");
        require(callback_called,
                "0193: partial raw archive did not invoke its callback");
    }});
    cases.push_back({"0166_archive_raw_stream_usize_limit","Raw archive rejects a stream size above the simulated usize limit",[](const fs::path&){
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 0u) &&
                    MetadataTile::write_compact_u64(archive_bits, 0x100000000ull),
                "0166: malformed raw archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0166: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        u64 previous_limit = archive_parse_usize_max_override;
        archive_parse_usize_max_override = 0xFFFFFFFFull;
        bool parsed = archive_parse(archive, (ArchiveEntryCallback)0,
                                    (void*)0, (u64*)0, true);
        archive_parse_usize_max_override = previous_limit;
        require(!parsed,
                "0166: raw archive accepted a stream size above the usize limit");
    }});
    cases.push_back({"0167_archive_rejects_dot_path","Archive extraction rejects a single-dot path segment",[](const fs::path&){
        const std::string id = "0167_archive_rejects_dot_path";
        fs::path work = make_work_dir(id);
        fs::path output = work / "output";
        fs::create_directories(output);
        makocode::BitWriter archive_bits;
        require(MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bit(1u) &&
                    MetadataTile::write_compact_u64(archive_bits, 1u) &&
                    archive_bits.write_bits((u8)'.', 8u),
                "0167: malformed archive setup failed");
        makocode::ByteBuffer archive;
        require(archive.ensure(archive_bits.byte_size()),
                "0167: archive buffer allocation failed");
        archive.size = archive_bits.byte_size();
        memcpy(archive.data, archive_bits.data(), archive.size);
        require(!unpack_archive_to_directory(archive, output.c_str()),
                "0167: extraction accepted a single-dot archive path");
        require(fs::is_empty(output),
                "0167: rejected dot path changed the extraction directory");
    }});
    cases.push_back({"0168_archive_path_rejects_backslash_and_nul","Archive paths reject backslashes and embedded NUL bytes",[](const fs::path&){
        const std::string backslash_path = "a\\b";
        const std::string nul_path("a\0b", 3u);
        require(!is_safe_archive_path(backslash_path.data(), backslash_path.size()),
                "0168: archive path accepted a backslash");
        require(!is_safe_archive_path(nul_path.data(), nul_path.size()),
                "0168: archive path accepted an embedded NUL");
    }});
    cases.push_back({"0169_archive_path_rejects_absolute","Archive paths reject absolute paths",[](const fs::path&){
        const std::string absolute_path = "/tmp/archive.txt";
        require(!is_safe_archive_path(absolute_path.data(), absolute_path.size()),
                "0169: archive path accepted an absolute path");
    }});
    cases.push_back({"0170_archive_rejects_fifo_input","Archive collection rejects unsupported FIFO entries",[](const fs::path&){
        const std::string id = "0170_archive_rejects_fifo_input";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        fs::path fifo = input / "stream.pipe";
        require(::mkfifo(fifo.c_str(), 0600) == 0,
                "0170: could not create FIFO input");
        ArchiveBuildContext archive;
        require(archive_init(archive), "0170: archive initialization failed");
        require(!archive_collect_directory(input.c_str(), "input", archive),
                "0170: archive collection accepted a FIFO input");
    }});
    cases.push_back({"0171_archive_collection_entry_count_overflow","Archive collection propagates entry-count overflow",[](const fs::path&){
        const std::string id = "0171_archive_collection_entry_count_overflow";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        write_file(input / "payload.bin", "entry count boundary");
        ArchiveBuildContext archive;
        require(archive_init(archive), "0171: archive initialization failed");
        archive.entry_count = U64_MAX_VALUE - 1u;
        require(!archive_collect_directory(input.c_str(), "input", archive),
                "0171: archive collection accepted an overflowing entry count");
    }});
    cases.push_back({"0172_archive_collection_read_failure","Archive collection rejects a regular file that cannot be opened",[](const fs::path&){
        const std::string id = "0172_archive_collection_read_failure";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        fs::path unreadable = input / "unreadable.bin";
        write_file(unreadable, "cannot read this payload");
        const char* prior_failed_path = ::getenv("MAKOCODE_TEST_FAIL_OPEN_FILE");
        bool had_failed_path = prior_failed_path != 0;
        std::string saved_failed_path = had_failed_path ? prior_failed_path : "";
        ArchiveBuildContext archive;
        require(archive_init(archive), "0172: archive initialization failed");
        setenv("MAKOCODE_TEST_FAIL_OPEN_FILE", unreadable.c_str(), 1);
        bool collected = archive_collect_directory(input.c_str(), "input", archive);
        if (had_failed_path) setenv("MAKOCODE_TEST_FAIL_OPEN_FILE", saved_failed_path.c_str(), 1);
        else unsetenv("MAKOCODE_TEST_FAIL_OPEN_FILE");
        require(!collected,
                "0172: archive collection accepted a regular file that could not be opened");
    }});
    cases.push_back({"0173_archive_collect_nested_failure","Archive collection propagates nested-directory failures",[](const fs::path&){
        const std::string id = "0173_archive_collect_nested_failure";
        fs::path input = make_work_dir(id) / "input";
        fs::path nested = input / "nested";
        fs::create_directories(nested);
        fs::path fifo = nested / "stream.pipe";
        require(::mkfifo(fifo.c_str(), 0600) == 0,
                "0173: could not create nested FIFO input");
        ArchiveBuildContext archive;
        require(archive_init(archive), "0173: archive initialization failed");
        require(!archive_collect_directory(input.c_str(), "input", archive),
                "0173: parent collection accepted a failed nested scan");
    }});
    cases.push_back({"0174_archive_collection_stat_failure","Archive collection rejects a dangling symlink entry",[](const fs::path&){
        const std::string id = "0174_archive_collection_stat_failure";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        fs::path dangling = input / "dangling-link";
        fs::create_symlink("missing-target", dangling);
        ArchiveBuildContext archive;
        require(archive_init(archive), "0174: archive initialization failed");
        require(!archive_collect_directory(input.c_str(), "input", archive),
                "0174: archive collection accepted a dangling symlink");
    }});
    cases.push_back({"0175_archive_collect_join_rel_alloc_failure","Archive collection propagates relative-path allocation failure",[](const fs::path&){
        const std::string id = "0175_archive_collect_join_rel_alloc_failure";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        write_file(input / "payload.bin", "payload");
        ArchiveBuildContext archive;
        require(archive_init(archive) && archive.path_registry.ensure(128u) &&
                    archive.entries.buffer.ensure(128u),
                "0175: archive setup allocation failed");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool collected = archive_collect_directory(input.c_str(), "input", archive);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0175: selected relative-path allocation did not fail");
        require(!collected,
                "0175: archive collection succeeded after relative-path allocation failed");
        require(archive.path_registry.size == sizeof("input") &&
                    memcmp(archive.path_registry.data, "input", sizeof("input")) == 0,
                "0175: failed relative-path join registered a partial path");
    }});
    cases.push_back({"0176_archive_collect_join_fs_alloc_failure","Archive collection stops before stat after filesystem-path allocation failure",[](const fs::path&){
        const std::string id = "0176_archive_collect_join_fs_alloc_failure";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        write_file(input / "payload.bin", "payload");
        ArchiveBuildContext archive;
        require(archive_init(archive) && archive.path_registry.ensure(128u) &&
                    archive.entries.buffer.ensure(128u),
                "0176: archive setup allocation failed");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool collected = archive_collect_directory(input.c_str(), "input", archive);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0176: selected filesystem-path allocation did not fail");
        require(!collected,
                "0176: filesystem-path allocation failure was not returned cleanly");
    }});
    cases.push_back({"0177_archive_collect_open_failure","Archive collection fails when the input directory cannot be opened",[](const fs::path&){
        const std::string id = "0177_archive_collect_open_failure";
        fs::path missing = make_work_dir(id) / "missing";
        ArchiveBuildContext archive;
        require(archive_init(archive), "0177: archive initialization failed");
        require(!archive_collect_directory(missing.c_str(), "missing", archive),
                "0177: archive collection accepted a nonexistent input directory");
        require(archive.entry_count == 1u,
                "0177: archive root entry was not registered before opening the input");
    }});
    cases.push_back({"0178_archive_collect_root_registration_failure","Archive collection stops when root registration fails",[](const fs::path&){
        const std::string id = "0178_archive_collect_root_registration_failure";
        fs::path input = make_work_dir(id) / "input";
        fs::create_directories(input);
        ArchiveBuildContext archive;
        require(archive_init(archive), "0178: archive initialization failed");
        archive.entry_count = U64_MAX_VALUE;
        require(!archive_collect_directory(input.c_str(), "input", archive),
                "0178: archive collection continued after root registration overflow");
        require(archive.entry_count == U64_MAX_VALUE,
                "0178: failed root registration changed the entry count");
    }});
    cases.push_back({"0179_join_fs_path_existing_separator","Filesystem path joins preserve an existing separator",[](const fs::path&){
        makocode::ByteBuffer output;
        require(join_fs_path("base/", "file", output),
                "0179: slash-terminated path join failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "base/file",
                "0179: slash-terminated base gained a duplicate separator");
        require(join_fs_path("base\\", "file", output),
                "0179: backslash-terminated path join failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "base\\file",
                "0179: backslash-terminated base gained a separator");
    }});
    cases.push_back({"0180_join_rel_path_allocation_failure","Relative path join reports allocation failure",[](const fs::path&){
        makocode::ByteBuffer output;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool joined = join_rel_path("base", "child", output);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0180: selected relative-path allocation did not fail");
        require(!joined,
                "0180: relative path join succeeded after allocation failure");
    }});
    cases.push_back({"0196_join_fs_path_allocation_failure","Filesystem path join reports allocation failure",[](const fs::path&){
        makocode::ByteBuffer output;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool joined = join_fs_path("base", "child", output);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0196: selected filesystem-path allocation did not fail");
        require(!joined,
                "0196: filesystem path join succeeded after allocation failure");
    }});
    cases.push_back({"0181_join_rel_path_empty_name","Relative path join rejects an empty name",[](const fs::path&){
        makocode::ByteBuffer output;
        require(!join_rel_path("base", "", output),
                "0181: relative path join accepted an empty name");
    }});
    cases.push_back({"0182_join_rel_path_backslashes","Relative path joins normalize backslash separators",[](const fs::path&){
        makocode::ByteBuffer output;
        require(join_rel_path("directory", "nested\\file.txt", output),
                "0182: relative path join failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "directory/nested/file.txt",
                "0182: backslash separator was not normalized");
    }});
    cases.push_back({"0183_normalize_store_path_output_allocation_failure","Store path normalization reports output allocation failure",[](const fs::path&){
        makocode::ByteBuffer output;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 2ul;
        bool normalized = normalize_store_path("folder/file", output);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0183: output-buffer allocation did not fail");
        require(!normalized,
                "0183: path normalization succeeded after output allocation failed");
    }});
    cases.push_back({"0184_normalize_store_path_root_rejected","Store path normalization rejects a filesystem root",[](const fs::path&){
        makocode::ByteBuffer output;
        require(!normalize_store_path("/", output),
                "0184: filesystem root was accepted as a store basename");
    }});
    cases.push_back({"0185_normalize_store_path_current_directory","Store path normalization resolves the current directory basename",[](const fs::path&){
        const std::string expected = fs::current_path().filename().string();
        require(!expected.empty(),
                "0185: current directory has no basename");
        makocode::ByteBuffer output;
        require(normalize_store_path(".", output),
                "0185: current directory did not normalize");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    expected,
                "0185: current directory normalized to the wrong basename");
    }});
    cases.push_back({"0187_normalize_store_path_component_limit","Store path normalization rejects too many components",[](const fs::path&){
        std::string path;
        for (usize i = 0u; i <= MAX_ARCHIVE_PATH_COMPONENTS; ++i) {
            if (!path.empty()) path.push_back('/');
            path.push_back('a');
        }
        makocode::ByteBuffer output;
        require(!normalize_store_path(path.c_str(), output),
                "0187: path above the component limit was accepted");
    }});
    cases.push_back({"0188_normalize_store_path_parent_component","Store path normalization resolves parent components",[](const fs::path&){
        makocode::ByteBuffer output;
        require(normalize_store_path("root/sub/../file", output),
                "0188: path normalization failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "root/file",
                "0188: parent component was not removed");
    }});
    cases.push_back({"0189_normalize_store_path_dot_component","Store path normalization removes dot components",[](const fs::path&){
        makocode::ByteBuffer output;
        require(normalize_store_path("folder/./file", output),
                "0189: path normalization failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "folder/file",
                "0189: dot component was not removed");
    }});
    cases.push_back({"0190_normalize_store_path_backslashes","Store path normalization maps backslashes to slashes",[](const fs::path&){
        makocode::ByteBuffer output;
        require(normalize_store_path("folder\\file.txt", output),
                "0190: path normalization failed");
        require(std::string(reinterpret_cast<const char*>(output.data), output.size) ==
                    "folder/file.txt",
                "0190: backslash separator was not normalized");
    }});
    cases.push_back({"0191_normalize_store_path_scratch_allocation_failure","Store path normalization reports scratch allocation failure",[](const fs::path&){
        makocode::ByteBuffer output;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool normalized = normalize_store_path("folder/file.txt", output);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0191: selected scratch allocation did not fail");
        require(!normalized,
                "0191: path normalization succeeded after scratch allocation failed");
    }});
    cases.push_back({"0194_archive_add_file_duplicate_path","Archive files reject duplicate paths without changing entry count",[](const fs::path&){
        const u8 contents[] = {'x'};
        ArchiveBuildContext archive;
        require(archive_init(archive), "0194: archive initialization failed");
        require(archive_add_file(archive, "duplicate.txt", contents, sizeof(contents)),
                "0194: initial archive file was rejected");
        require(!archive_add_file(archive, "duplicate.txt", contents, sizeof(contents)),
                "0194: duplicate archive path was accepted");
        require(archive.entry_count == 1u,
                "0194: duplicate path changed the archive entry count");
    }});
    cases.push_back({"0197_archive_add_directory_duplicate_path","Archive directories reject duplicate paths without changing entry count",[](const fs::path&){
        ArchiveBuildContext archive;
        require(archive_init(archive), "0197: archive initialization failed");
        require(archive_add_directory(archive, "duplicate"),
                "0197: initial archive directory was rejected");
        require(!archive_add_directory(archive, "duplicate"),
                "0197: duplicate archive directory was accepted");
        require(archive.entry_count == 1u,
                "0197: duplicate path changed the archive entry count");
    }});
    cases.push_back({"0198_archive_add_directory_empty_path","Archive directories reject an empty path",[](const fs::path&){
        ArchiveBuildContext archive;
        require(archive_init(archive), "0198: archive initialization failed");
        require(!archive_add_directory(archive, ""),
                "0198: empty archive directory path was accepted");
        require(archive.entry_count == 0u,
                "0198: empty path changed the archive entry count");
    }});
    cases.push_back({"0199_archive_path_registry_allocation_failure","Archive path registration reports allocation failure",[](const fs::path&){
        ArchiveBuildContext archive;
        require(archive_init(archive), "0199: archive initialization failed");
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool added = archive_add_directory(archive, "directory");
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0199: path-registry allocation did not fail");
        require(!added,
                "0199: archive directory succeeded after path-registry allocation failed");
        require(archive.entry_count == 0u && archive.path_registry.size == 0u,
                "0199: failed path registration changed archive state");
    }});
    cases.push_back({"0200_archive_append_bytes_allocation_failure","Archive byte append reports writer allocation failure",[](const fs::path&){
        const u8 contents[] = {0xA5u};
        makocode::BitWriter writer;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        bool appended = archive_append_bytes(writer, contents, sizeof(contents));
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size != 0ul,
                "0200: selected writer allocation did not fail");
        require(!appended && writer.failed,
                "0200: archive byte append succeeded after writer allocation failed");
        require(writer.bit_size() == 0u,
                "0200: failed archive byte append advanced the writer");
    }});
    cases.push_back({"0201_read_entire_file_close_failure","File read reports close failure and releases its buffer",[](const fs::path&){
        const std::string id = "0201_read_entire_file_close_failure";
        fs::path work = make_work_dir(id);
        fs::path source = work / "source.bin";
        write_file(source, "read-close-failure");
        require(::setenv("MAKOCODE_TEST_FAIL_NEXT_CLOSE", "1", 1) == 0,
                "0201: could not enable close failure injection");
        makocode::ByteBuffer contents;
        bool read = read_entire_file(source.c_str(), contents);
        bool hook_consumed = ::getenv("MAKOCODE_TEST_FAIL_NEXT_CLOSE") == nullptr;
        ::unsetenv("MAKOCODE_TEST_FAIL_NEXT_CLOSE");
        require(hook_consumed,
                "0201: close failure injection was not consumed by file read");
        require(!read,
                "0201: file read succeeded after close failure");
        require(!contents.data && contents.size == 0u,
                "0201: file buffer was not released after close failure");
    }});
    cases.push_back({"0202_read_entire_file_size_overflow","File read rejects growth beyond its size limit",[](const fs::path&){
        const std::string id = "0202_read_entire_file_size_overflow";
        fs::path work = make_work_dir(id);
        fs::path source = work / "source.bin";
        write_file(source, std::string(8192u, 'r'));
        read_entire_file_usize_max_override = 8191u;
        makocode::ByteBuffer contents;
        bool read = read_entire_file(source.c_str(), contents);
        read_entire_file_usize_max_override = USIZE_MAX_VALUE;
        require(!read,
                "0202: file read succeeded after reaching its size limit");
        require(!contents.data && contents.size == 0u,
                "0202: file buffer was not released after the size-limit failure");
    }});
    cases.push_back({"0294_read_entire_file_read_failure","File read rejects a directory and releases its buffer",[](const fs::path&){
        const std::string id = "0294_read_entire_file_read_failure";
        fs::path work = make_work_dir(id);
        makocode::ByteBuffer contents;
        bool read = read_entire_file(work.c_str(), contents);
        require(!read,
                "0293: file read succeeded for a directory");
        require(!contents.data && contents.size == 0u,
                "0293: file buffer was not released after read failure");
    }});
    cases.push_back({"0203_read_entire_stdin_read_failure","Stdin read reports an invalid descriptor and releases its buffer",[](const fs::path&){
        int saved_stdin = ::dup(STDIN_FILENO);
        require(saved_stdin >= 0,
                "0203: could not save stdin before read-failure injection");
        if (::close(STDIN_FILENO) != 0) {
            ::close(saved_stdin);
            throw Failure("0203: could not close stdin for read-failure injection");
        }
        makocode::ByteBuffer contents;
        bool read = read_entire_stdin(contents);
        int restore_result = ::dup2(saved_stdin, STDIN_FILENO);
        ::close(saved_stdin);
        require(restore_result >= 0,
                "0203: could not restore stdin after read-failure injection");
        require(!read,
                "0203: stdin read succeeded with an invalid descriptor");
        require(!contents.data && contents.size == 0u,
                "0203: stdin buffer was not released after read failure");
    }});
    cases.push_back({"0204_read_entire_stdin_allocation_failure","Stdin read reports allocation failure and releases its buffer",[](const fs::path&){
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 1ul;
        makocode::ByteBuffer contents;
        bool read = read_entire_stdin(contents);
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(failed_allocation_size == 4096ul,
                "0204: stdin buffer allocation failure was not selected");
        require(!read,
                "0204: stdin read succeeded after buffer allocation failed");
        require(!contents.data && contents.size == 0u,
                "0204: stdin buffer was not released after allocation failure");
    }});
    cases.push_back({"0205_read_entire_stdin_size_overflow","Stdin read rejects growth beyond its size limit",[](const fs::path&){
        const std::string id = "0205_read_entire_stdin_size_overflow";
        fs::path work = make_work_dir(id);
        fs::path source = work / "stdin.bin";
        write_file(source, std::string(8192u, 's'));
        int input = ::open(source.c_str(), O_RDONLY);
        require(input >= 0,
                "0205: could not open stdin overflow input");
        int saved_stdin = ::dup(STDIN_FILENO);
        if (saved_stdin < 0 || ::dup2(input, STDIN_FILENO) < 0) {
            if (saved_stdin >= 0) ::close(saved_stdin);
            ::close(input);
            throw Failure("0205: could not redirect stdin for overflow test");
        }
        ::close(input);
        read_entire_stdin_usize_max_override = 8191u;
        makocode::ByteBuffer contents;
        bool read = read_entire_stdin(contents);
        read_entire_stdin_usize_max_override = USIZE_MAX_VALUE;
        int restore_result = ::dup2(saved_stdin, STDIN_FILENO);
        ::close(saved_stdin);
        require(restore_result >= 0,
                "0205: could not restore stdin after overflow test");
        require(!read,
                "0205: stdin read succeeded after reaching its size limit");
        require(!contents.data && contents.size == 0u,
                "0205: stdin buffer was not released after the size-limit failure");
    }});
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
    cases.push_back({"0345_forced_small_tile_angle_retry","Forced center sampling accepts a slightly rotated small tile",[](const fs::path&){
        const std::string id = "0345_forced_small_tile_angle_retry";
        fs::path work = make_work_dir(id), payload = work / "payload.bin";
        fs::path pages = work / "pages", rotated = work / "rotated.ppm";
        fs::create_directories(pages);
        write_file(payload, "small rotated tile retry");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, pages, "source",
                             {"--page-width=80", "--page-height=80", "--ecc=0.1"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> source = ppm_paths(pages, "source");
        require(source.size() == 1u, id + ": expected one encoded page");
        expect_success(run_transform(work,
            {"transform", "--input", source.front(), "--output",
             rotated.string(), "--scale-x", "1.75", "--scale-y", "1.75",
             "--rotate", "1.25"}, "rotate"), id + " transform");
        makocode::ByteBuffer input, frame, frame_erasures;
        require(read_entire_file(rotated.c_str(), input), id + ": could not read rotated page");
        MetadataTile::Values known_tile = extract_tile_values(source.front());
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 frame_bits = 0u;
        require(ppm_extract_frame_bits(input, mapping, frame, frame_bits, state,
                                       true, nullptr, 1u, &frame_erasures, nullptr,
                                       nullptr, nullptr, nullptr, nullptr, true,
                                       nullptr, &known_tile),
                id + ": forced center sampling failed to extract the page");
        require(state.has_encoded_tile && frame_bits > 0u,
                id + ": rotated page did not produce a usable small tile frame");
        double expected_angle = 1.25 * 3.14159265358979323846 / 180.0;
        require(fabs(state.affine_transform.a00 - 1.75 * cos(expected_angle)) < 0.01 &&
                    fabs(state.affine_transform.a01 + 1.75 * sin(expected_angle)) < 0.002 &&
                    fabs(state.affine_transform.a10 - 1.75 * sin(expected_angle)) < 0.002,
                id + ": small-tile retry lost the known scan rotation");
    }});
    cases.push_back({"0341_stream_page_capacity_mask_allocation_failure","Streamed page-capacity calculation rejects metadata-mask allocation failure",[](const fs::path&){
        const std::string id = "0341_stream_page_capacity_mask_allocation_failure";
        fs::path work = make_work_dir(id), payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "mask allocation probe");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, pages, "source",
                             {"--page-width=80", "--page-height=80", "--ecc=0.1"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> source = ppm_paths(pages, "source");
        require(source.size() == 1u, id + ": expected one encoded page");
        makocode::ByteBuffer input, frame, erasures;
        require(read_entire_file(source.front().c_str(), input), id + ": could not read page");
        MetadataTile::Values tile = extract_tile_values(source.front());
        ImageMappingConfig mapping;
        PpmParserState state;
        u64 frame_bits = 0u, capacity = 0u;
        platform_allocation_sizes.clear();
        minify_allocation_count = 0ul;
        record_platform_allocation_sizes = true;
        bool ok = ppm_extract_frame_bits(input, mapping, frame, frame_bits, state,
                                         false, nullptr, 1u, &erasures, nullptr,
                                         nullptr, nullptr, nullptr, nullptr, true,
                                         &capacity, &tile);
        record_platform_allocation_sizes = false;
        require(ok, id + ": baseline extraction failed");
        require(state.encoded_tile.ecc_enabled && state.encoded_tile.ecc_block_data > 0u &&
                    state.encoded_tile.ecc_parity > 0u && capacity > 0u,
                id + ": fixture did not enter streamed page-capacity calculation");

        makocode::ByteBuffer failed_frame, failed_erasures;
        PpmParserState failed_state;
        u64 failed_frame_bits = 0u, failed_capacity = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = 9ul;
        bool failure_extracted = ppm_extract_frame_bits(
            input, mapping, failed_frame, failed_frame_bits, failed_state,
            false, nullptr, 1u, &failed_erasures, nullptr, nullptr, nullptr,
            nullptr, nullptr, true, &failed_capacity, &tile);
        unsigned long allocation_count = minify_allocation_count;
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(!failure_extracted,
                id + ": extraction succeeded after candidate active-mask allocation failed");
        require(allocation_count == 9ul && failed_allocation_size == 4096ul,
                id + ": fault injection did not reach candidate active-mask allocation");
    }});
    cases.push_back({"0346_recover_palette_limit_allocation_failure","Palette recovery stops when its budget buffer allocation fails",[](const fs::path&){
        const std::string id = "0346_recover_palette_limit_allocation_failure";
        fs::path work = make_work_dir(id), payload = work / "payload.bin";
        fs::path pages = work / "pages";
        fs::create_directories(pages);
        write_file(payload, "palette recovery allocation");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, pages, "source",
                             {"--page-width=220", "--page-height=220",
                              "--palette", "White Cyan Magenta Yellow"}), "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> source = ppm_paths(pages, "source");
        require(source.size() == 1u, id + ": expected one encoded page");
        fs::path damaged = damage_palette_row(source.front(), work / "damaged.ppm", 4u);
        makocode::ByteBuffer input;
        require(read_entire_file(damaged.c_str(), input), id + ": could not read damaged page");
        char recovery_limit[66];
        memset(recovery_limit, '9', 65u);
        recovery_limit[65] = '\0';
        ImageMappingConfig mapping;
        mapping.recover_palette = true;
        mapping.recover_limit_text = recovery_limit;
        mapping.recover_limit_length = 65u;
        makocode::ByteBuffer baseline_frame, baseline_erasures;
        PpmParserState baseline_state;
        u64 baseline_bits = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = ~0ul;
        platform_allocation_sizes.clear();
        record_platform_allocation_sizes = true;
        bool baseline_ok = ppm_extract_frame_bits(input, mapping, baseline_frame,
                                                  baseline_bits, baseline_state,
                                                  false, nullptr, 1u,
                                                  &baseline_erasures);
        record_platform_allocation_sizes = false;
        unsigned long allocation_count = minify_allocation_count;
        minify_allocation_failure_at = 0ul;
        require(baseline_ok && allocation_count == platform_allocation_sizes.size(),
                id + ": baseline recovery failed or allocation trace was incomplete");
        unsigned long limit_allocation = 0ul;
        for (size_t index = 0u; index + 1u < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == 128u &&
                platform_allocation_sizes[index + 1u] == 64u) {
                if (limit_allocation == 0ul) {
                    limit_allocation = (unsigned long)index + 1ul;
                }
            }
        }
        require(limit_allocation != 0ul,
                id + ": did not observe recovery-limit and attempt-counter allocations");
        makocode::ByteBuffer failed_frame, failed_erasures;
        PpmParserState failed_state;
        u64 failed_bits = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = limit_allocation;
        bool failed_ok = ppm_extract_frame_bits(input, mapping, failed_frame,
                                                failed_bits, failed_state, false,
                                                nullptr, 1u, &failed_erasures);
        unsigned long failed_allocation_count = minify_allocation_count;
        unsigned long failed_allocation_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(!failed_ok && failed_allocation_count == limit_allocation &&
                    failed_allocation_size == 128ul,
                id + ": extraction continued after the recovery-limit allocation failed");
    }});
    cases.push_back({"0347_ppm_orientation_normalization_allocation_failure","Orientation normalization propagates its raster allocation failure",[](const fs::path&){
        const std::string id = "0347_ppm_orientation_normalization_allocation_failure";
        fs::path work = make_work_dir(id), payload = work / "payload.bin";
        fs::path pages = work / "pages", scan = work / "quarter_turn.ppm";
        fs::create_directories(pages);
        write_file(payload, "orientation allocation failure");
        ProcessResult encoded = run_cli(work,
            encode_file_args(payload, pages, "source",
                             {"--page-width=80", "--page-height=90", "--ecc=0.85"}),
            "encode");
        expect_success(encoded, id + " encode");
        std::vector<std::string> source = ppm_paths(pages, "source");
        require(source.size() == 1u, id + ": expected one source page");
        PpmImage source_image = read_ppm(source.front());
        MetadataTile::Values tile = extract_tile_values(source.front());
        unsigned tile_x = (source_image.width - MetadataTile::TILE_SIDE) / 2u;
        unsigned tile_y = (source_image.height - MetadataTile::TILE_SIDE) / 2u;
        for (unsigned bit = 0u; bit < 2u; ++bit) {
            unsigned x = tile_x + MetadataTile::tag_marker_x0() + (bit & 7u);
            unsigned y = tile_y + tile.metadata_tag_y0 + 1u + bit / 8u;
            size_t at = ((size_t)y * source_image.width + x) * 3u;
            char opposite = MetadataTile::makocode_tag_bit(bit) ? (char)255 : (char)0;
            source_image.pixels[at] = source_image.pixels[at + 1u] =
                source_image.pixels[at + 2u] = opposite;
        }
        make_oriented_scan(source_image, scan, 90.0, false, false, 1.0, 1u);
        PpmImage scan_image = read_ppm(scan);
        makocode::ByteBuffer input;
        require(read_entire_file(scan.c_str(), input), id + ": could not read oriented scan");

        ImageMappingConfig mapping;
        makocode::ByteBuffer frame, erasures;
        PpmParserState state;
        u64 frame_bits = 0u;
        platform_allocation_sizes.clear();
        minify_allocation_count = 0ul;
        record_platform_allocation_sizes = true;
        bool baseline_ok = ppm_extract_frame_bits(input, mapping, frame, frame_bits,
                                                   state, false, nullptr, 1u, &erasures,
                                                   nullptr, nullptr, nullptr, nullptr,
                                                   nullptr, false, nullptr, nullptr, nullptr,
                                                   0u, 0u, 0u, true, false, true);
        record_platform_allocation_sizes = false;
        require(!baseline_ok,
                id + ": metadata-only orientation probe unexpectedly completed raster extraction");

        unsigned long normalization_allocation = 0ul;
        unsigned matching_allocations = 0u;
        size_t normalized_bytes = (size_t)scan_image.width * scan_image.height * 3u;
        size_t normalized_capacity = 64u;
        while (normalized_capacity < normalized_bytes) normalized_capacity *= 2u;
        // The parser and earlier probe buffers use this same rounded capacity.
        // The third match is the orientation-normalized raster.
        for (size_t index = 0u; index < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] == normalized_capacity) {
                ++matching_allocations;
                if (matching_allocations == 3u) {
                    normalization_allocation = (unsigned long)index + 1ul;
                }
            }
        }
        std::string allocation_summary = " raster=" +
            std::to_string(scan_image.width) + "x" + std::to_string(scan_image.height) +
            " bytes=" + std::to_string(normalized_bytes) +
            " capacity=" + std::to_string(normalized_capacity) + " allocations=";
        for (size_t index = 0u; index < platform_allocation_sizes.size(); ++index) {
            if (platform_allocation_sizes[index] > normalized_bytes / 2u) {
                allocation_summary += std::to_string(platform_allocation_sizes[index]) + ",";
            }
        }
        require(normalization_allocation != 0ul,
                id + ": trace did not record the source and normalized raster allocations; matches=" +
                    std::to_string(matching_allocations) +
                    allocation_summary);

        makocode::ByteBuffer failed_frame, failed_erasures;
        PpmParserState failed_state;
        u64 failed_frame_bits = 0u;
        minify_allocation_count = 0ul;
        minify_allocation_failure_size = 0ul;
        minify_allocation_failure_at = normalization_allocation;
        bool failed_ok = ppm_extract_frame_bits(input, mapping, failed_frame,
                                                 failed_frame_bits, failed_state,
                                                 false, nullptr, 1u, &failed_erasures,
                                                 nullptr, nullptr, nullptr, nullptr,
                                                 nullptr, false, nullptr, nullptr, nullptr,
                                                 0u, 0u, 0u, true, false, true);
        unsigned long failure_count = minify_allocation_count;
        unsigned long failure_size = minify_allocation_failure_size;
        minify_allocation_failure_at = 0ul;
        require(!failed_ok,
                id + ": metadata probe succeeded after orientation-raster allocation failed");
        require(failure_count >= normalization_allocation &&
                    failure_size == normalized_capacity,
                id + ": fault injection missed the orientation-normalized raster");
    }});
    cases.push_back({"0363_ppm_binary_separator_consumes_crlf","PPM binary separator consumes both bytes of a CRLF pair",[](const fs::path&){
        const u8 input[] = {'\r', '\n', 'P'};
        PpmParserState state;
        state.data = input;
        state.size = sizeof(input);
        state.cursor = 0u;
        require(ppm_consume_binary_separator(state),
                "0363_ppm_binary_separator_consumes_crlf: rejected CRLF separator");
        require(state.cursor == 2u && state.data[state.cursor] == 'P',
                "0363_ppm_binary_separator_consumes_crlf: left LF as raster data");
    }});
    cases.push_back({"0364_ppm_rgb_pixel_count_overflow_rejected","PPM RGB reader rejects pixel counts whose byte size exceeds usize",[](const fs::path&){
        const u8 input[] = {0x12u, 0x34u};
        PpmParserState state;
        state.data = input;
        state.size = sizeof(input);
        makocode::ByteBuffer pixels;
        u64 pixel_count = ((u64)USIZE_MAX_VALUE / 3u) + 1u;
        require(!ppm_read_rgb_pixels(state, pixel_count, pixels),
                "0364_ppm_rgb_pixel_count_overflow_rejected: accepted an unrepresentable RGB raster");
        require(state.cursor == 0u && pixels.size == 0u,
                "0364_ppm_rgb_pixel_count_overflow_rejected: changed output after rejecting overflow");
    }});
    cases.push_back({"0365_ppm_rgb_reader_rejects_zero_pixels","PPM RGB reader rejects an empty raster",[](const fs::path&){
        const u8 input[] = {0x12u, 0x34u};
        PpmParserState state;
        state.data = input;
        state.size = sizeof(input);
        makocode::ByteBuffer pixels;
        require(!ppm_read_rgb_pixels(state, 0u, pixels),
                "0365_ppm_rgb_reader_rejects_zero_pixels: accepted an empty raster");
        require(state.cursor == 0u && pixels.size == 0u,
                "0365_ppm_rgb_reader_rejects_zero_pixels: changed state after rejection");
    }});
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
    cases.push_back({"0129_stealth_container_lengths","Encrypted descriptors recover exact visible and hidden stream lengths",[](const fs::path&){stealth_container_lengths_case();}});
    cases.push_back({"0089_decode_unit_scale_translation","Unit-scale padded scan translation",[](const fs::path&){unit_scale_scan_case("0089_decode_unit_scale_translation",false,false);}});
    cases.push_back({"0090_decode_unit_scale_edge_bleed","Unit-scale ink bleed keeps decoded geometry",[](const fs::path&){unit_scale_scan_case("0090_decode_unit_scale_edge_bleed",true,false);}});
    cases.push_back({"0091_decode_unit_scale_optical_blur","Unit-scale optical blur",[](const fs::path&){unit_scale_scan_case("0091_decode_unit_scale_optical_blur",false,true);}});
    cases.push_back({"0093_decode_lowres_optical_blur","Canonical 1.5x optical scan",[](const fs::path&){scan_registration_case("0093_decode_lowres_optical_blur",1.5,2.0,2.5,125,125,true,true);}});
    cases.push_back({"0095_decode_quarter_scale_registration","Canonical 1.25x optical registration",[](const fs::path&){scan_registration_case("0095_decode_quarter_scale_registration",1.25,2.25,3.25,106,107,true,true,false,true,1.25,2.25,3.25);}});
    cases.push_back({"0097_decode_midscale_low_contrast","Canonical 2.5x low contrast scan",[](const fs::path&){scan_registration_case("0097_decode_midscale_low_contrast",2.5,0.0,0.0,200,200,false,false,true);}});
    cases.push_back({"0099_decode_scale_relative_registration","Canonical 5.8x scale-relative registration",[](const fs::path&){scan_registration_case("0099_decode_scale_relative_registration",5.8,0.0,0.0,464,464,false,false,true);}});
    cases.push_back({"0101_decode_integer_scan_dimensions","Canonical padded integer scan dimensions",[](const fs::path&){scan_registration_case("0101_decode_integer_scan_dimensions",2.0,.25,.75,161,161,false,false,true);}});
    r.size=4096; r.encode_args={"--page-width=600","--page-height=600","--palette","White Black","--ecc=0.5"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--ink-blot-radius","180","--ink-blot-color","black","--seed","0"}; rt("0044_palette_white_black_blot_black","Black blot recovery on two-color page",r); r={};
    r.size=4096; r.encode_args={"--page-width=600","--page-height=600","--palette","White Black","--ecc=0.75"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--ink-blot-radius","180","--ink-blot-color","white","--seed","0"}; rt("0045_palette_white_black_blot_white","White blot recovery on two-color page",r); r={};
    r.size=8192; r.encode_args={"--page-width=300","--page-height=300","--palette","White Black"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--paper-color","FFF3D6","--paper-alpha","0.105","--paper-splotch-alpha","0.15","--paper-splotch-shade","0.06","--paper-splotch-px","96","--seed","424242"}; rt("0054_paper_offwhite_bw","Off-white paper tint BW",r); r={};
    r.size=8192; r.encode_args={"--page-width=320","--page-height=320","--palette","White Cyan Magenta Yellow"}; r.decode_args={"--palette","White Cyan Magenta Yellow"}; r.transform_args={"--paper-color","FFF3D6","--paper-alpha","0.105","--paper-splotch-alpha","0.15","--paper-splotch-shade","0.06","--paper-splotch-px","96","--seed","424242"}; rt("0055_paper_offwhite_cmyk","Off-white paper tint CMYK",r); r={};
    r.size=8192; r.encode_args={"--page-width=360","--page-height=360","--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.decode_args={"--palette","FFFFFF FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 000000"}; r.transform_args={"--paper-color","FFF3D6","--paper-alpha","0.105","--paper-splotch-alpha","0.15","--paper-splotch-shade","0.06","--paper-splotch-px","96","--seed","424242"}; rt("0056_paper_offwhite_cmykwrbg","Off-white paper tint eight-color",r); r={};
    r.size=4096; r.encode_args={"--page-width=640","--page-height=400","--footer","Archive title","--font-size","5"}; rt("0108_footer_geometry","Tall footer geometry roundtrip",r); r={};
    r.size=4096; r.encode_args={"--page-width=620","--page-height=620","--ecc=fill"}; r.transform_args={"--scale-x","2.071","--scale-y","2.071","--seed","2071"}; rt("0073_decode_scale_2071","Fractional affine scan scale 2.071x",r); r={};
    r.size=4096; r.has_payload_seed_override=true; r.payload_seed_override=2118103236u; r.encode_args={"--page-width=620","--page-height=620","--ecc=fill"}; r.transform_args={"--scale-x","2.701","--scale-y","2.701","--seed","2701"}; rt("0074_decode_scale_2701","Fractional affine scan scale 2.701x",r); r={};
    r.size=4096; r.has_payload_seed_override=true; r.payload_seed_override=282487847u; r.encode_args={"--page-width=620","--page-height=620","--ecc=fill"}; r.transform_args={"--scale-x","4.701","--scale-y","4.701","--seed","4701"}; rt("0075_decode_scale_4701","Fractional affine scan scale 4.701x",r); r={};
    r.size=8; r.literal="makocode"; r.encode_args={"--page-width=240","--page-height=240","--ecc=fill","--palette","White Black"}; r.decode_args={"--palette","White Black"}; r.transform_args={"--scale-x","2.701","--scale-y","2.701","--seed","2701"}; r.white_checker=true; rt("0076_tiny_palette_2_white_checker","Two-color tiny page with white checker overwrite",r); r={};
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
    const std::vector<std::pair<std::string,std::string>> scatter_palettes={{"2","White Black"},{"4","White Cyan Magenta Yellow"},{"5","White Cyan Magenta Yellow Black"},{"6","White #FF0000 #00FF00 #0000FF Cyan Black"},{"8","White #FF0000 #00FF00 #0000FF Cyan Magenta Yellow Black"}};
    for(const auto& item:scatter_palettes){
        oc=OverlayConfig{};oc.id="0124_overlay_palette_"+item.first;oc.size=24;oc.fraction="0.9990234375";oc.palette=item.second;oc.circle="128 64 0";oc.without_metadata=true;oc.without_fiducials=true;oc.encrypted=true;oc.scatter=true;overlay(oc);
    }
    for(const auto& item:scatter_palettes){
        oc=OverlayConfig{};oc.id="0128_scatter_password_"+item.first;oc.size=24;oc.fraction="0.9990234375";oc.palette=item.second;oc.circle="128 64 0";oc.without_metadata=true;oc.without_fiducials=true;oc.encrypted=true;oc.scatter=true;overlay(oc);
    }
    r.size=4096; r.encode_args={"--page-width=360","--page-height=360","--palette","Black White"}; r.decode_args={"--palette","Black White"}; r.transform_args={"--scale-x","2.5","--scale-y","2.5","--seed","0"}; rt("0131_palette_reversed_q2_scan","Scaled scan with reversed q2 palette order",r); r={};
    cases.push_back({"0132_palette_q2_center_specks","Five center-pixel specks are erased from area-vote disagreements",[](const fs::path&){palette_q2_center_speck_case();}});
    r.size=4096; r.encode_args={"--scatter"}; r.scatter_metadata_check=true; rt("0133_scatter_plain_roundtrip","Plain unencrypted payload round-trip with scattered layout inferred from metadata",r); r={};
    r.size=4096; r.encode_args={"--scatter","--no-metadata","--no-fiducials","--palette","White Black"}; r.decode_args={"--no-metadata","--no-fiducials","--palette","White Black","--password-cli=unused"}; r.marker_free_compact=true; rt("0134_scatter_public_seed_fallback","Metadata-free scatter autodetection retries both layouts with public placement when a decode password was supplied",r); r={};
    r.size=4096; r.encode_args={"--scatter","--no-metadata","--no-fiducials","--palette","White Black",u8"--password-cli=pāss🔐","--random-data=00112233445566778899aabbccddeeff102132435465768798a9bacb"}; r.decode_args={"--no-metadata","--no-fiducials","--palette","White Black",u8"--password-cli=pāss🔐"}; r.marker_free_compact=true; rt("0135_scatter_utf8_password","Metadata-free scatter round-trip uses the full password-derived key, including for bootstrap placement",r); r={};
    cases.push_back({"0136_page_salt_ecc_shuffle","Per-page salt has ECC and separates password, archive, and page placement",[](const fs::path&){
        makocode::PaletteSymbolProfile profile;
        require(makocode::palette_symbol_profile(2u,profile),"0136: q2 profile unavailable");
        const u8 salt_a[makocode::PALETTE_SYMBOL_PAGE_SALT_BYTES]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        const u8 salt_b[makocode::PALETTE_SYMBOL_PAGE_SALT_BYTES]={15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0};
        makocode::ByteBuffer encoded,erasures;
        require(makocode::palette_symbol_encode_page_salt(profile,salt_a,1u,2u,encoded),"0136: salt encoding failed");
        require(erasures.ensure(makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS),"0136: erasure allocation failed");
        erasures.size=makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS;
        memset(erasures.data,0,erasures.size);
        makocode::PaletteSymbolPageSalt decoded;
        require(makocode::palette_symbol_decode_page_salt(profile,encoded,erasures,1u,2u,decoded,0)&&decoded.available&&decoded.page_index==1u&&decoded.page_count==2u&&memcmp(decoded.salt,salt_a,sizeof(salt_a))==0,"0136: salt metadata round-trip failed");
        makocode::ByteBuffer damaged;
        require(damaged.append_bytes(encoded.data,encoded.size),"0136: damaged codeword allocation failed");
        for(u32 symbol=0u;symbol<12u;++symbol){u32 value=read_le_u32(damaged.data+(usize)symbol*4u);write_le_u32(damaged.data+(usize)symbol*4u,value^(0x51u+symbol));}
        require(makocode::palette_symbol_decode_page_salt(profile,damaged,erasures,1u,2u,decoded,0)&&decoded.available&&memcmp(decoded.salt,salt_a,sizeof(salt_a))==0,"0136: salt ECC did not correct 12 symbol errors");
        require(!makocode::palette_symbol_decode_page_salt(profile,encoded,erasures,3u,2u,decoded,0),"0136: salt accepted an invalid page index");
        makocode::ByteBuffer legacy_header_data,legacy_header_code;
        require(legacy_header_data.ensure(makocode::PALETTE_SYMBOL_BOOTSTRAP_DATA_SYMBOLS*4u),"0136: legacy header allocation failed");
        legacy_header_data.size=makocode::PALETTE_SYMBOL_BOOTSTRAP_DATA_SYMBOLS*4u;
        memset(legacy_header_data.data,0,legacy_header_data.size);
        write_le_u32(legacy_header_data.data,76u);
        require(makocode::palette_rs_encode(profile,legacy_header_data,
                    makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS,
                    makocode::PALETTE_SYMBOL_BOOTSTRAP_DATA_SYMBOLS,legacy_header_code),
                "0136: legacy header ECC encoding failed");
        require(!makocode::palette_symbol_decode_page_salt(profile,legacy_header_code,
                    erasures,1u,2u,decoded,0),
                "0136: legacy stream bootstrap was mistaken for a salt");
        makocode::PaletteSymbolPasswordContext locator_a,locator_b,salted_a,salted_b,salted_other_password;
        require(locator_a.derive("password one",12u)&&locator_b.derive("password two",12u)&&salted_a.derive_salted(locator_a,salt_a)&&salted_b.derive_salted(locator_a,salt_b)&&salted_other_password.derive_salted(locator_b,salt_a),"0136: password shuffle key derivation failed");
        auto build_map=[&](u64 page_index,const makocode::PaletteSymbolPasswordContext& locator,const makocode::PaletteSymbolPasswordContext& salted,const u8* salt,makocode::ByteBuffer& ranks,u64& pixel_count){
            MetadataTile::Values values;values.page_width_pixels=256u;values.page_height_pixels=256u;values.page_index=page_index;values.page_count=2u;values.palette_count=2u;
            MetadataTile::Placement placement=MetadataTile::compute_center_tile_placement(256u,256u);
            return placement.valid&&MetadataTile::build_payload_rank_map(256u,256u,placement,values,0,0,ranks,pixel_count,false,makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED,0,false,locator.shuffle_key(),salt,salted.shuffle_key());
        };
        makocode::ByteBuffer map_a,map_b,map_page_two,map_other_password;u64 count_a=0,count_b=0,count_page_two=0,count_other_password=0;
        require(build_map(1u,locator_a,salted_a,salt_a,map_a,count_a)&&build_map(1u,locator_a,salted_b,salt_b,map_b,count_b)&&build_map(2u,locator_a,salted_a,salt_a,map_page_two,count_page_two)&&build_map(1u,locator_b,salted_other_password,salt_a,map_other_password,count_other_password),"0136: page rank map construction failed");
        require(count_a==count_b&&count_a==count_page_two&&count_a==count_other_password,"0136: rank map capacities differ");
        const u64* ranks_a=(const u64*)map_a.data;const u64* ranks_b=(const u64*)map_b.data;const u64* ranks_page_two=(const u64*)map_page_two.data;const u64* ranks_other=(const u64*)map_other_password.data;bool archive_suffix_diff=false,page_suffix_diff=false,password_locator_diff=false;
        for(u64 pixel=0u;pixel<count_a;++pixel){
            if(ranks_a[pixel]<makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS*8u||ranks_b[pixel]<makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS*8u)require(ranks_a[pixel]==ranks_b[pixel],"0136: archive salt changed bootstrap positions");
            if(ranks_a[pixel]<makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS*8u||ranks_page_two[pixel]<makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS*8u)require(ranks_a[pixel]==ranks_page_two[pixel],"0136: page index changed bootstrap positions");
            if(ranks_a[pixel]!=ranks_b[pixel])archive_suffix_diff=true;
            if(ranks_a[pixel]!=ranks_page_two[pixel])page_suffix_diff=true;
            if(ranks_a[pixel]<makocode::PALETTE_SYMBOL_PAGE_SALT_SYMBOLS*8u&&ranks_a[pixel]!=ranks_other[pixel])password_locator_diff=true;
        }
        require(archive_suffix_diff&&page_suffix_diff&&password_locator_diff,"0136: salt, page, or password did not alter its intended shuffle");
    }});
    cases.push_back({"0373_custom_palette_rejects_duplicate_rgb_colors","Custom palette rejects distinct tokens that decode to the same RGB color",[](const fs::path&){
        const std::string id = "0373_custom_palette_rejects_duplicate_rgb_colors";
        const char palette_text[] = "White #FFFFFF";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test"),
                id + ": could not set the duplicate palette text");
        require(!image_mapping_build_custom_palette(mapping, "test"),
                id + ": accepted two palette tokens with identical RGB values");
        require(!mapping.custom_palette_valid && mapping.custom_palette_count == 0u,
                id + ": duplicate palette left a valid custom palette behind");
    }});
    cases.push_back({"0374_custom_palette_requires_two_colors","Custom palette rejects a single color",[](const fs::path&){
        const std::string id = "0374_custom_palette_requires_two_colors";
        const char palette_text[] = "White";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test"),
                id + ": could not set the one-color palette text");
        require(!image_mapping_build_custom_palette(mapping, "test"),
                id + ": accepted a palette with fewer than two colors");
        require(!mapping.custom_palette_valid && mapping.custom_palette_count == 0u,
                id + ": rejected palette left a valid custom palette behind");
    }});
    cases.push_back({"0375_custom_palette_rejects_unknown_color","Custom palette rejects an unknown color token",[](const fs::path&){
        const std::string id = "0375_custom_palette_rejects_unknown_color";
        const char palette_text[] = "White NotAColor";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test"),
                id + ": could not set the palette text");
        require(!image_mapping_build_custom_palette(mapping, "test"),
                id + ": accepted an unsupported color token");
        require(!mapping.custom_palette_valid && mapping.custom_palette_count == 0u,
                id + ": rejected palette left a valid custom palette behind");
    }});
    cases.push_back({"0378_named_palette_decoder_rejects_unknown_token","Named palette decoder rejects unsupported tokens without changing outputs",[](const fs::path&){
        const std::string id = "0378_named_palette_decoder_rejects_unknown_token";
        const char token[] = "NotAColor";
        u8 color_id = 0xA5u;
        PaletteColor color = {17u, 23u, 29u};
        require(!decode_named_palette_color(token, sizeof(token) - 1u,
                                            color_id, color),
                id + ": accepted an unsupported token");
        require(color_id == 0xA5u && color.r == 17u && color.g == 23u &&
                    color.b == 29u,
                id + ": rejected token modified output values");
    }});
    cases.push_back({"0379_custom_palette_rejects_invalid_hex_nibble","Custom palette rejects an invalid low hex nibble",[](const fs::path&){
        const std::string id = "0379_custom_palette_rejects_invalid_hex_nibble";
        const char palette_text[] = "White #0G0000";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test"),
                id + ": could not set the malformed palette text");
        require(!image_mapping_build_custom_palette(mapping, "test"),
                id + ": accepted an invalid hexadecimal color nibble");
        require(!mapping.custom_palette_valid && mapping.custom_palette_count == 0u,
                id + ": rejected malformed palette left a valid custom palette behind");
    }});
    cases.push_back({"0383_hex_palette_decoder_rejects_wrong_length","Hex palette decoder rejects non-six-digit tokens",[](const fs::path&){
        const std::string id = "0383_hex_palette_decoder_rejects_wrong_length";
        const char token[] = "123456";
        PaletteColor color = {17u, 23u, 29u};
        require(!decode_hex_palette_color(token, 5u, color),
                id + ": accepted a five-digit token backed by six valid digits");
        require(color.r == 17u && color.g == 23u && color.b == 29u,
                id + ": rejected token modified the output color");
    }});
    cases.push_back({"0384_custom_palette_tokens_stop_at_delimiters","Custom palette token lengths end at delimiters and input end",[](const fs::path&){
        const std::string id = "0384_custom_palette_tokens_stop_at_delimiters";
        const char palette_text[] = " White ; Black,\tCyan ";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test"),
                id + ": could not set palette text");
        require(image_mapping_build_custom_palette(mapping, "test"),
                id + ": could not parse delimited palette tokens");
        require(mapping.custom_palette_valid && mapping.custom_palette_count == 3u &&
                    mapping.custom_palette[0].r == 255u &&
                    mapping.custom_palette[0].g == 255u &&
                    mapping.custom_palette[0].b == 255u &&
                    mapping.custom_palette[1].r == 0u &&
                    mapping.custom_palette[1].g == 0u &&
                    mapping.custom_palette[1].b == 0u &&
                    mapping.custom_palette[2].r == 0u &&
                    mapping.custom_palette[2].g == 255u &&
                    mapping.custom_palette[2].b == 255u,
                id + ": token boundaries or parsed colors were incorrect");
    }});
    cases.push_back({"0376_custom_palette_rejects_more_than_sixteen_colors","Custom palette rejects more than sixteen colors",[](const fs::path&){
        const std::string id = "0376_custom_palette_rejects_more_than_sixteen_colors";
        const char palette_text[] =
            "#000001 #000002 #000003 #000004 #000005 #000006 #000007 #000008 "
            "#000009 #00000A #00000B #00000C #00000D #00000E #00000F #000010 #000011";
        ImageMappingConfig mapping;
        require(image_mapping_set_palette_text(mapping, palette_text,
                                               sizeof(palette_text) - 1u, "test"),
                id + ": could not set the palette text");
        require(!image_mapping_build_custom_palette(mapping, "test"),
                id + ": accepted more than sixteen palette colors");
        require(!mapping.custom_palette_valid && mapping.custom_palette_count == 0u,
                id + ": rejected palette left a valid custom palette behind");
    }});
    cases.push_back({"0377_custom_palette_text_rejects_oversize_input","Palette text rejects input at the destination capacity",[](const fs::path&){
        const std::string id = "0377_custom_palette_text_rejects_oversize_input";
        std::vector<char> palette_text(MAX_CUSTOM_PALETTE_TEXT, 'x');
        ImageMappingConfig mapping;
        require(!image_mapping_set_palette_text(mapping, palette_text.data(),
                                                palette_text.size(), "test"),
                id + ": accepted palette text at the destination capacity");
        require(!mapping.palette_set && mapping.palette_text_length == 0u,
                id + ": rejected palette text changed the mapping");
    }});
    cases.push_back({"0380_palette_mode_three_selects_rgb_cmy_wb","Palette mode three selects its eight-color palette",[](const fs::path&){
        const std::string id = "0380_palette_mode_three_selects_rgb_cmy_wb";
        const PaletteColor* colors = nullptr;
        u32 count = 0u;
        require(palette_for_mode(3u, colors, count),
                id + ": palette mode three was rejected");
        require(colors == PALETTE_RGB_CMY_WB &&
                    count == sizeof(PALETTE_RGB_CMY_WB) / sizeof(PALETTE_RGB_CMY_WB[0]),
                id + ": palette mode three returned the wrong colors or count");
    }});
    cases.push_back({"0381_color_mode_samples_rejects_unknown_mode","Unknown color modes have no pixel samples",[](const fs::path&){
        const std::string id = "0381_color_mode_samples_rejects_unknown_mode";
        require(color_mode_samples_per_pixel(1u) == 1u &&
                    color_mode_samples_per_pixel(2u) == 1u &&
                    color_mode_samples_per_pixel(3u) == 1u,
                id + ": supported color mode returned the wrong sample count");
        require(color_mode_samples_per_pixel(0u) == 0u &&
                    color_mode_samples_per_pixel(4u) == 0u &&
                    color_mode_samples_per_pixel(255u) == 0u,
                id + ": unknown color mode was assigned pixel samples");
    }});
    cases.push_back({"0382_footer_lookup_rejects_unsupported_glyph","Footer glyph lookup rejects unsupported bytes",[](const fs::path&){
        const std::string id = "0382_footer_lookup_rejects_unsupported_glyph";
        require(footer_lookup_glyph('A') != 0,
                id + ": supported uppercase glyph was not found");
        require(footer_lookup_glyph('a') != 0,
                id + ": lowercase glyph fallback was not found");
        require(footer_lookup_glyph('\x7f') == 0,
                id + ": unsupported DEL byte returned a glyph");
    }});
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

class ScopedTestQuietDiagnostics {
public:
    explicit ScopedTestQuietDiagnostics(bool quiet)
        : previous_(makocode_test_quiet_stderr) {
        makocode_test_quiet_stderr = quiet || previous_;
    }

    ~ScopedTestQuietDiagnostics() {
        makocode_test_quiet_stderr = previous_;
    }

private:
    bool previous_;
};

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--ppm-transform") == 0) return ppm_transform_cli_main(argc - 1, argv + 1);
    if (argc >= 2 && std::strcmp(argv[1], "--checkpoint-allocation-failure") == 0) {
        minify_allocation_count=0;minify_allocation_failure_at=1ul;
        char create[]="create",input_option[]="--input",input_value[]="current_a",output_option[]="--output",output_value[]="allocation_failure.mcp";
        char* args[]={create,input_option,input_value,output_option,output_value};
        return command_checkpoint(5,args)==1?0:1;
    }
    if (argc >= 5 && std::strcmp(argv[1], "--checkpoint-create-allocation-failure") == 0) {
        minify_allocation_count=0;minify_allocation_failure_at=std::strtoul(argv[2],nullptr,10);
        char create[]="create",input_option[]="--input",output_option[]="--output";
        char* args[]={create,input_option,argv[3],output_option,argv[4]};
        int status=command_checkpoint(5,args);
        std::cout<<"CHECKPOINT_STATUS:"<<status<<" ALLOCATION_COUNT:"<<minify_allocation_count<<" FAILURE_SIZE:"<<minify_allocation_failure_size<<'\n';
        return 0;
    }
    if (argc >= 4 && std::strcmp(argv[1], "--checkpoint-open-directory-failure") == 0) {
        ::setenv("MAKOCODE_TEST_FAIL_OPEN_DIRECTORY",argv[2],1);
        char create[]="create",input_option[]="--input",output_option[]="--output";
        char* args[]={create,input_option,argv[2],output_option,argv[3]};
        int status=command_checkpoint(5,args);
        std::cout<<"CHECKPOINT_STATUS:"<<status<<'\n';
        return 0;
    }
    if (argc >= 5 && std::strcmp(argv[1], "--checkpoint-restore-allocation-failure") == 0) {
        minify_allocation_count=0;minify_allocation_failure_at=std::strtoul(argv[2],nullptr,10);
        char restore[]="restore",input_option[]="--input",output_option[]="--output";
        int status=0;
        if(argc>=6){char base_option[]="--base";char* args[]={restore,input_option,argv[3],base_option,argv[4],output_option,argv[5]};status=command_checkpoint(7,args);}
        else{char* args[]={restore,input_option,argv[3],output_option,argv[4]};status=command_checkpoint(5,args);}
        std::cout<<"CHECKPOINT_STATUS:"<<status<<" ALLOCATION_COUNT:"<<minify_allocation_count<<" FAILURE_SIZE:"<<minify_allocation_failure_size<<" READ_4096_COUNT:"<<minify_4096_allocation_count<<'\n';
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "--minify-allocation-failure") == 0) {
        minify_allocation_count=0;minify_allocation_failure_at=argc>2?std::strtoul(argv[2],nullptr,10):3ul;
        char source_path[]="makocode.cpp",width_option[]="--width",width_value[]="240",height_option[]="--height",height_value[]="80";
        char* args[]={source_path,width_option,width_value,height_option,height_value};
        return command_minify(5,args)==1?0:1;
    }
    test_executable() = fs::canonical(fs::path(argv[0]));
    repo_root() = test_executable().parent_path();
    while (!repo_root().empty() && !fs::exists(repo_root() / "src" / "makocode.cpp")) {
        fs::path parent = repo_root().parent_path();
        if (parent == repo_root()) break;
        repo_root() = parent;
    }
    if (!fs::exists(repo_root() / "src" / "makocode.cpp")) {
        std::cerr << "Unable to locate repository root from " << test_executable() << '\n';
        return 2;
    }
    bool quiet = argc > 1 && std::strcmp(argv[argc - 1], "--quiet") == 0;
    int command_argc = quiet ? argc - 1 : argc;
    std::vector<TestCase> cases = build_cases();
    if (command_argc == 2 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& test : cases) std::cout << test.id << '\n';
        return 0;
    }
    if (!(command_argc == 2 && std::strcmp(argv[1], "--all") == 0) &&
        !(command_argc == 3 && std::strcmp(argv[1], "--case") == 0)) {
        std::cerr << "Usage: makocode_test --list | --case ID [--quiet] | --all [--quiet]\n";
        return 2;
    }
    bool stdout_color = isatty(STDOUT_FILENO) != 0;
    bool stderr_color = isatty(STDERR_FILENO) != 0;
    const char* test_label = stdout_color ? "\033[1;34m[TEST]\033[0m " : "[TEST] ";
    const char* pass_label = stdout_color ? "\033[32m[PASS]\033[0m " : "[PASS] ";
    const char* fail_label = stderr_color ? "\033[31m[FAIL]\033[0m " : "[FAIL] ";
    output_root() = repo_root() / "test_output";
    makocode_bin() = test_executable().parent_path() / "makocode";
    fs::create_directories(output_root() / "test_logs");
    master_seed = parse_master_seed();
    {
        std::ofstream seed_file(output_root() / "test_logs/test_seed.txt", std::ios::trunc);
        seed_file << master_seed << '\n';
    }
    std::cout << test_label << "Test seed: " << master_seed << "\n";
    if (command_argc == 3 && std::strcmp(argv[1], "--case") == 0) {
        auto found = std::find_if(cases.begin(), cases.end(), [&](const TestCase& test) { return test.id == std::string(argv[2]); });
        if (found == cases.end()) { std::cerr << "unknown test case: " << argv[2] << '\n'; return 2; }
        try { ScopedTestQuietDiagnostics quiet_scope(quiet); found->run(output_root()); std::cout << pass_label << found->id << '\n'; return 0; }
        catch (const std::exception& error) { std::cerr << fail_label << found->id << ": " << error.what() << '\n'; return 1; }
    }
    if (command_argc == 2 && std::strcmp(argv[1], "--all") == 0) {
        std::vector<TestCase> parallel_cases;
        std::vector<const TestCase*> startup_cases;
        std::vector<const TestCase*> isolated_cases;
        for (const auto& test : cases) {
            if (test.id == "0123_metadata_pages" ||
                test.id == "0345_forced_small_tile_angle_retry") {
                startup_cases.push_back(&test);
            } else if (test.id == "0118_decode_partial_tag_scan" ||
                       test.id == "0144_metadata_page_layout_alloc_failure" ||
                       test.id == "0145_metadata_fiducial_mask_alloc_failure" ||
                       test.id == "0150_archive_parent_symlink_race" ||
                       test.id == "0155_join_output_path_allocation_failure" ||
                       test.id == "0156_parent_path_allocation_failure" ||
                       test.id == "0158_directory_tree_allocation_failure" ||
                       test.id == "0341_stream_page_capacity_mask_allocation_failure" ||
                       test.id == "0346_recover_palette_limit_allocation_failure" ||
                       test.id == "0347_ppm_orientation_normalization_allocation_failure" ||
                       test.id == "0349_rgb_override_rejects_pixel_buffer_allocation_failure" ||
                       test.id == "0392_downsample_preflight_skips_failed_target_allocation" ||
                       test.id == "0175_archive_collect_join_rel_alloc_failure" ||
                       test.id == "0176_archive_collect_join_fs_alloc_failure" ||
                       test.id == "0196_join_fs_path_allocation_failure" ||
                       test.id == "0199_archive_path_registry_allocation_failure" ||
                       test.id == "0201_read_entire_file_close_failure" ||
                       test.id == "0203_read_entire_stdin_read_failure" ||
                       test.id == "0204_read_entire_stdin_allocation_failure" ||
                       test.id == "0205_read_entire_stdin_size_overflow" ||
                       test.id == "0366_write_all_fd_rejects_broken_pipe" ||
                       test.id == "0200_archive_append_bytes_allocation_failure" ||
                       test.id == "0172_archive_collection_read_failure" ||
                       test.id == "0180_join_rel_path_allocation_failure" ||
                       test.id == "0183_normalize_store_path_output_allocation_failure" ||
                       test.id == "0191_normalize_store_path_scratch_allocation_failure" ||
                       test.id == "0229_render_rejects_header_allocation_failure" ||
                       test.id == "0231_render_rejects_reservation_mask_allocation_failure" ||
                       test.id == "0023_gray_rotate_seeded" ||
                       test.id == "0043_header_copy_corruption" ||
                       test.id == "0313_palette_recovery_skips_duplicate_candidate" ||
                       test.id == "0312_fiducial_border_bounds_affine" ||
                       test.id == "0300_affine_confidence_clips_border_neighbors" ||
                       test.id == "0301_affine_confidence_skips_off_page_samples" ||
                       test.id == "0302_fiducial_partial_allocation_is_released" ||
                       test.id == "0310_ppm_extract_rank_map_allocation_failure" ||
                       test.id == "0315_known_tile_palette_text_allocation_failure" ||
                       test.id == "0322_fiducial_sampling_integral_allocation_fallback" ||
                       test.id == "0324_fiducial_center_allocation_failure_cleans_up" ||
                       test.id == "0331_vertical_scale_uses_average_span_after_pitch_allocation_failure" ||
                       test.id == "0333_horizontal_scale_uses_average_span_after_pitch_allocation_failure" ||
                       test.id == "0334_horizontal_scale_uses_average_span_when_axis_fit_is_nonpositive" ||
                       test.id == "0340_affine_bias_allocation_failure_cleans_up" ||
                       test.id == "0303_footer_override_rejects_underflowing_data_height" ||
                       test.id == "0228_render_rejects_unaligned_field_symbol_offset" ||
                       test.id == "0248_fiducial_insert_propagates_header_allocation_failure" ||
                       test.id == "0255_metadata_free_marks_invalid_palette_symbols_as_erasures" ||
                       test.id == "0276_append_bits_from_buffer_propagates_writer_failure" ||
                       test.id == "0278_decode_rejects_erasure_buffer_allocation_failure" ||
                       test.id == "0291_small_marker_free_page_falls_back_without_salt" ||
                       test.id == "0293_marker_free_decode_propagates_rank_map_allocation_failure" ||
                       test.id == "0235_build_frame_rejects_frame_buffer_allocation_failure" ||
                       test.id == "0236_build_frame_rejects_writer_allocation_failure" ||
                       test.id == "0237_page_filename_rejects_suffix_allocation_failure" ||
                       test.id == "0240_bits_per_page_rejects_fiducial_mask_allocation_failure" ||
                       test.id == "0160_directory_creation_race") {
                isolated_cases.push_back(&test);
            } else {
                parallel_cases.push_back(test);
            }
        }
        std::atomic<size_t> next{0}; std::atomic<int> failed{0};
        // Run the metadata page assembly case before the worker pool so it
        // starts without memory retained by earlier image stress cases.
        for (const TestCase* startup_case : startup_cases) {
            try {
                { ScopedTestQuietDiagnostics quiet_scope(quiet); startup_case->run(output_root()); }
                std::cout << pass_label << startup_case->id << "\n";
            } catch (const std::exception& error) { // LCOV_EXCL_BR_LINE
                ++failed;
                std::cerr << fail_label << startup_case->id << ": "
                          << error.what() << "\n";
            }
        }
        int jobs = jobs_from_makeflags();
        jobs = std::min<int>(jobs, static_cast<int>(parallel_cases.size()));
        std::vector<std::thread> workers;
        for (int worker = 0; worker < jobs; ++worker) workers.emplace_back([&] {
            for (;;) {
                size_t index = next.fetch_add(1);
                if (index >= parallel_cases.size()) break;
                const auto& test = parallel_cases[index];
                try {
                    { ScopedTestQuietDiagnostics quiet_scope(quiet); test.run(output_root()); }
                    std::lock_guard<std::mutex> lock(output_mutex); std::cout << pass_label << test.id << "\n";
                } catch (const std::exception& error) { // LCOV_EXCL_BR_LINE
                    failed.fetch_add(1);
                    std::lock_guard<std::mutex> lock(output_mutex); std::cerr << fail_label << test.id << ": " << error.what() << "\n";
                }
            }
        });
        for (auto& worker : workers) worker.join();
        // These metadata-heavy decoders are stable alone but can fail under
        // memory pressure while other image tests run in parallel.
        for (const TestCase* isolated_case : isolated_cases) {
            try {
                { ScopedTestQuietDiagnostics quiet_scope(quiet); isolated_case->run(output_root()); }
                std::cout << pass_label << isolated_case->id << "\n";
            } catch (const std::exception& error) { // LCOV_EXCL_BR_LINE
                ++failed;
                std::cerr << fail_label << isolated_case->id << ": "
                          << error.what() << "\n";
            }
        }
        std::cout << test_label << "Test cases: " << cases.size() << ", failures: " << failed.load() << '\n';
        return failed.load() ? 1 : 0;
    }
    return 2;
}
