/*
Copyright (C) 2026 Justin Alan Bass.
This file is part of MakoCode. MakoCode is free software: you can
redistribute it and/or modify it under the terms of the GNU Affero
General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version.
MakoCode is distributed without any warranty; without even the implied
warranty of merchantability or fitness for a particular purpose. See
the LICENSE file for the full license text.
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
    const int max_tag_y = (48 - 5) / 2 - 1;
    for (int tag_y = 0; tag_y <= max_tag_y; ++tag_y) {
        unsigned int colors = 0u;
        uint64_t best_distance = UINT64_MAX;
        for (unsigned int candidate = 2u; candidate <= 16u; ++candidate) {
            size_t first = ((size_t)(tile_y0 + tag_y) * (size_t)width + (size_t)tile_x0) * 3u;
            size_t end = first + (size_t)candidate * 3u;
            long dr = (long)pixels[first] - pixels[end];
            long dg = (long)pixels[first + 1u] - pixels[end + 1u];
            long db = (long)pixels[first + 2u] - pixels[end + 2u];
            uint64_t distance = (uint64_t)(dr * dr + dg * dg + db * db);
            if (distance > 3u * 40u * 40u || distance >= best_distance) continue;
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
            if (distinct) { colors = candidate; best_distance = distance; }
        }
        if (!colors) continue;
        double low = 255.0, high = 0.0;
        double samples[32];
        for (unsigned int bit = 0u; bit < 32u; ++bit) {
            unsigned int x = bit & 7u;
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
                tag_out = ((uint64_t)colors << 32u) | UINT64_C(0xD1BF3F45);
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
    FILE* f = fopen(path, "wb");
    if (!f) die2("ppm_transform: failed to write ", path);

    fprintf(f, "P6\n");
    if (comments) {
        for (size_t i = 0; i < comments->size; i++) fprintf(f, "%s\n", comments->data[i]);
    }
    fprintf(f, "%d %d\n", width, height);
    fprintf(f, "255\n");

    (void)row_compact;
    for (size_t i = 0; i < pixels->size; ++i) fputc(pixels->data[i], f);
    fclose(f);
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
            "  ppm_transform transform --input IN --output OUT [--scale-x F] [--scale-y F] [--rotate DEG]\n"
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
            "  ppm_transform bytes --output OUT --size N --seed N\n"
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
        // Keep the encoded default fiducial markers intact so the transformed
        // test still checks their exact decoded locations after the overwrite.
        const unsigned int marker_size = 4u;
        const unsigned int spacing = 24u;
        const unsigned int margin = 12u;
        double min_x = (margin < (unsigned int)ppm.width) ? (double)margin : 0.0;
        double max_x = (ppm.width > (int)margin)
                           ? (double)(ppm.width - 1 - (int)margin)
                           : (double)(ppm.width - 1);
        double min_y = (margin < (unsigned int)ppm.height) ? (double)margin : 0.0;
        double max_y = (ppm.height > (int)margin)
                           ? (double)(ppm.height - 1 - (int)margin)
                           : (double)(ppm.height - 1);
        if (max_x < min_x) max_x = min_x;
        if (max_y < min_y) max_y = min_y;
        unsigned int columns = (unsigned int)ceil((max_x - min_x) / (double)spacing) + 1u;
        unsigned int rows = (unsigned int)ceil((max_y - min_y) / (double)spacing) + 1u;
        for (unsigned int row = 0u; row < rows; ++row) {
            double t_y = (rows == 1u) ? 0.5 : (double)row / (double)(rows - 1u);
            double center_y = min_y + (max_y - min_y) * t_y;
            int start_y = (int)(center_y - ((double)marker_size - 1.0) * 0.5);
            for (unsigned int column = 0u; column < columns; ++column) {
                double t_x = (columns == 1u) ? 0.5 : (double)column / (double)(columns - 1u);
                double center_x = min_x + (max_x - min_x) * t_x;
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
        FILE* file = fopen(output, "wb");
        if (!file) die2("ppm_transform: unable to open ", output);
        unsigned char buffer[4096];
        int remaining = byte_count;
        while (remaining > 0) {
            size_t count = remaining < (int)sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
            for (size_t i = 0; i < count; i++) buffer[i] = (unsigned char)rand_u8(&rng);
            if (fwrite(buffer, 1, count, file) != count) {
                fclose(file);
                die2("ppm_transform: unable to write ", output);
            }
            remaining -= (int)count;
        }
        if (fclose(file) != 0) die2("ppm_transform: unable to close ", output);
        return 0;
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        usage();
        return 0;
    }
    die2("ppm_transform: unknown command ", cmd);
}
