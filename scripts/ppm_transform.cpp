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

static int starts_with(const char* s, const char* prefix) {
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
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

static Ppm read_ppm_p3_ascii(const char* path) {
    FILE* f = fopen(path, "r");
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
    if (strcmp(line, "P3") != 0) {
        free(line);
        fclose(f);
        die2("ppm_transform: ", "not an ASCII P3 PPM");
    }

    int have_w = 0, have_h = 0, have_max = 0;

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
            if (!have_w) {
                ppm.width = parse_i32(tok, "width");
                if (ppm.width <= 0) die("ppm_transform: invalid dimensions");
                have_w = 1;
                continue;
            }
            if (!have_h) {
                ppm.height = parse_i32(tok, "height");
                if (ppm.height <= 0) die("ppm_transform: invalid dimensions");
                have_h = 1;
                continue;
            }
            if (!have_max) {
                int maxval = parse_i32(tok, "maxval");
                if (maxval != 255) die("ppm_transform: expected maxval 255");
                have_max = 1;
                continue;
            }
            int v = parse_i32(tok, "pixel");
            if (v < 0 || v > 255) die("ppm_transform: pixel value out of range");
            intvec_push(&ppm.pixels, v);
        }
    }

    free(line);
    fclose(f);

    if (!have_w || !have_h || !have_max) die("ppm_transform: truncated header");

    size_t expected = (size_t)ppm.width * (size_t)ppm.height * 3;
    if (ppm.pixels.size != expected) die("ppm_transform: pixel count mismatch");
    return ppm;
}

static void write_ppm_p3_ascii(const char* path,
                               const StrVec* comments,
                               int width,
                               int height,
                               const IntVec* pixels,
                               int row_compact) {
    FILE* f = fopen(path, "w");
    if (!f) die2("ppm_transform: failed to write ", path);

    fprintf(f, "P3\n");
    if (comments) {
        for (size_t i = 0; i < comments->size; i++) fprintf(f, "%s\n", comments->data[i]);
    }
    fprintf(f, "%d %d\n", width, height);
    fprintf(f, "255\n");

    if (!row_compact) {
        for (size_t i = 0; i + 2 < pixels->size; i += 3) {
            fprintf(f, "%d %d %d\n", pixels->data[i], pixels->data[i + 1], pixels->data[i + 2]);
        }
        fclose(f);
        return;
    }

    size_t row_stride = (size_t)width * 3;
    for (int y = 0; y < height; y++) {
        size_t base = (size_t)y * row_stride;
        for (size_t i = 0; i < row_stride; i++) {
            if (i) fputc(' ', f);
            fprintf(f, "%d", pixels->data[base + i]);
        }
        fputc('\n', f);
    }
    fclose(f);
}

static StrVec strip_geometry_comments(const StrVec* comments) {
    static const char* prefixes[] = {
        "# skew_src_width",
        "# skew_src_height",
        "# skew_margin_x",
        "# skew_x_pixels",
        "# skew_bottom_x",
    };
    StrVec out;
    if (!comments) return out;
    for (size_t i = 0; i < comments->size; i++) {
        const char* line = comments->data[i];
        const char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        int drop = 0;
        for (size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++) {
            if (starts_with(s, prefixes[p])) {
                drop = 1;
                break;
            }
        }
        if (!drop) strvec_push(&out, line);
    }
    return out;
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
    double inv_x = 1.0 / scale_x;
    double inv_y = 1.0 / scale_y;
    size_t out_sz = (size_t)new_w * (size_t)new_h * 3;
    out->data = (int*)malloc(out_sz * sizeof(int));
    if (!out->data) die("ppm_transform: OOM");
    out->size = out_sz;
    out->cap = out_sz;
    for (size_t i = 0; i < out_sz; i++) out->data[i] = 255;
    for (int row = 0; row < new_h; row++) {
        int src_y = (int)llround(((row + 0.5) * inv_y) - 0.5);
        if (src_y < 0) src_y = 0;
        if (src_y >= height) src_y = height - 1;
        for (int col = 0; col < new_w; col++) {
            int src_x = (int)llround(((col + 0.5) * inv_x) - 0.5);
            if (src_x < 0) src_x = 0;
            if (src_x >= width) src_x = width - 1;
            size_t src_idx = ((size_t)src_y * width + (size_t)src_x) * 3;
            size_t dst_idx = ((size_t)row * new_w + (size_t)col) * 3;
            out->data[dst_idx] = in->data[src_idx];
            out->data[dst_idx + 1] = in->data[src_idx + 1];
            out->data[dst_idx + 2] = in->data[src_idx + 2];
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
        double shade = 1.0;
        for (size_t i = 0; i + 2 < pixels->size; i += 3) {
            for (int c = 0; c < 3; c++) {
                double v = pixels->data[i + (size_t)c];
                double mixed = v * (1.0 - alpha) + (double)paper_rgb[c] * alpha;
                mixed *= shade;
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
    double radius_sq = (double)radius * (double)radius;
    double cx = (width - 1) / 2.0;
    double cy = (height - 1) / 2.0;
    for (int y = 0; y < height; y++) {
        double dy = y - cy;
        double dy_sq = dy * dy;
        size_t row_base = (size_t)y * width * 3;
        for (int x = 0; x < width; x++) {
            double dx = x - cx;
            if (dx * dx + dy_sq <= radius_sq) {
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

static int footer_rows_from_comments(const StrVec* comments) {
    if (!comments) return 0;
    for (size_t i = 0; i < comments->size; i++) {
        const char* line = comments->data[i];
        const char* s = line;
        if (*s == '#') s++;
        s = trim_left((char*)s);
        if (!starts_with(s, "MAKOCODE_FOOTER_ROWS")) continue;
        s += strlen("MAKOCODE_FOOTER_ROWS");
        s = trim_left((char*)s);
        int rows = 0;
        if (sscanf(s, "%d", &rows) == 1) return rows;
        return 0;
    }
    return 0;
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
            "  ppm_transform solid --output OUT --width W --height H --r R --g G --b B\n"
            "  ppm_transform noise --output OUT --width W --height H --seed N\n"
            "  ppm_transform corrupt-footer-data-destroyed --input IN --output OUT [--seed N] [--footer-height-px N]\n"
            "  ppm_transform corrupt-footer-valid-data-too-corrupt --input IN --output OUT [--seed N] [--footer-height-px N] [--border-keep N]\n"
            "  ppm_transform overlay-mask --output OUT --circle-color \"R G B\" --background-color \"R G B\" [--width W] [--height H]\n"
            "  ppm_transform copy-footer-rows --encoded IN --merged INOUT\n"
            "  ppm_transform overlay-check --base IN --merged IN [--skip-grayscale 0|1]\n"
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
    write_ppm_p3_ascii(output, nullptr, width, height, &pixels, 0);
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
    write_ppm_p3_ascii(output, nullptr, width, height, &pixels, 0);
    intvec_free(&pixels);
}

static void cmd_corrupt_footer_data_destroyed(int argc, char** argv) {
    const char* input = nullptr;
    const char* output = nullptr;
    int seed = 424242;
    int footer_height_px = 12;
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
    Ppm ppm = read_ppm_p3_ascii(input);
    if (ppm.height <= footer_height_px) die("ppm_transform: corrupt-footer-data-destroyed: image too short");
    uint32_t rng = (uint32_t)seed ^ 0x12345678u;
    int stripe_top = ppm.height - footer_height_px;
    for (int y = 0; y < stripe_top; y++) {
        size_t row_base = (size_t)y * ppm.width * 3;
        for (int x = 0; x < ppm.width; x++) {
            size_t idx = row_base + (size_t)x * 3;
            ppm.pixels.data[idx] = rand_u8(&rng);
            ppm.pixels.data[idx + 1] = rand_u8(&rng);
            ppm.pixels.data[idx + 2] = rand_u8(&rng);
        }
    }
    write_ppm_p3_ascii(output, &ppm.comments, ppm.width, ppm.height, &ppm.pixels, 0);
    ppm_free(&ppm);
}

static void cmd_corrupt_footer_valid_data_too_corrupt(int argc, char** argv) {
    const char* input = nullptr;
    const char* output = nullptr;
    int seed = 20251215;
    int footer_height_px = 12;
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
    Ppm ppm = read_ppm_p3_ascii(input);
    int data_bottom = ppm.height - footer_height_px;
    if (data_bottom <= border_keep + 1) die("ppm_transform: corrupt-footer-valid-data-too-corrupt: image too short");
    int y0 = border_keep;
    int y1 = data_bottom - border_keep;
    if (y1 < y0 + 1) y1 = y0 + 1;
    int x0 = border_keep;
    int x1 = ppm.width - border_keep;
    if (x1 < x0 + 1) x1 = x0 + 1;
    uint32_t rng = (uint32_t)seed ^ 0xDEADBEEFu;
    for (int y = y0; y < y1; y++) {
        size_t row_base = (size_t)y * ppm.width * 3;
        for (int x = x0; x < x1; x++) {
            size_t idx = row_base + (size_t)x * 3;
            ppm.pixels.data[idx] = rand_u8(&rng);
            ppm.pixels.data[idx + 1] = rand_u8(&rng);
            ppm.pixels.data[idx + 2] = rand_u8(&rng);
        }
    }
    write_ppm_p3_ascii(output, &ppm.comments, ppm.width, ppm.height, &ppm.pixels, 0);
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

    // Intentionally do not emit any PPM header comment lines. Metadata must be
    // carried in pixels (e.g., footer stripe) rather than file headers, since
    // print/scan workflows discard container headers entirely.
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

    write_ppm_p3_ascii(output, &comments, width, height, &pixels, 1);

    free(circle_palette);
    intvec_free(&pixels);
    strvec_free(&comments);
}

static void cmd_copy_footer_rows(int argc, char** argv) {
    const char* encoded = nullptr;
    const char* merged = nullptr;
    for (int i = 2; i < argc; i++) {
        const char* arg = argv[i];
        auto require_value = [&](const char* flag) -> const char* {
            if (i + 1 >= argc) die2("ppm_transform: missing value for ", flag);
            return argv[++i];
        };
        if (strcmp(arg, "--encoded") == 0) encoded = require_value("--encoded");
        else if (strcmp(arg, "--merged") == 0) merged = require_value("--merged");
        else die2("ppm_transform: unknown flag ", arg);
    }
    if (!encoded || !merged) die("ppm_transform: copy-footer-rows requires --encoded/--merged");
    Ppm enc = read_ppm_p3_ascii(encoded);
    Ppm mer = read_ppm_p3_ascii(merged);
    if (enc.width != mer.width || enc.height != mer.height) die("ppm_transform: dimension mismatch");
    int footer_rows = footer_rows_from_comments(&enc.comments);
    if (footer_rows > 0 && footer_rows < enc.height) {
        size_t row_stride = (size_t)enc.width * 3;
        size_t start = (size_t)(enc.height - footer_rows) * row_stride;
        memcpy(mer.pixels.data + start, enc.pixels.data + start, (enc.pixels.size - start) * sizeof(int));
        const StrVec* comments = (mer.comments.size > 0) ? &mer.comments : &enc.comments;
        write_ppm_p3_ascii(merged, comments, mer.width, mer.height, &mer.pixels, 1);
    }
    ppm_free(&enc);
    ppm_free(&mer);
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

    Ppm base = read_ppm_p3_ascii(base_path);
    Ppm merged = read_ppm_p3_ascii(merged_path);
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
        else die2("ppm_transform: unknown flag ", arg);
    }

    if (!input || !output) die("ppm_transform: --input and --output are required");

    Ppm ppm = read_ppm_p3_ascii(input);
    StrVec comments = strip_geometry_comments(&ppm.comments);
    StrVec metadata;

    int w = ppm.width;
    int h = ppm.height;
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

    if (metadata.size > 0) {
        for (size_t i = 0; i < metadata.size; i++) strvec_push(&comments, metadata.data[i]);
    }

    if (have_paper && (paper_alpha > 0.0 || paper_splotch_alpha > 0.0 || paper_splotch_shade > 0.0)) {
        char buf[64] = {0};
        char line[256] = {0};
        snprintf(line, sizeof(line), "# paper_color %02X%02X%02X", paper_rgb[0], paper_rgb[1], paper_rgb[2]);
        strvec_push(&comments, line);
        format_float(paper_alpha, buf);
        snprintf(line, sizeof(line), "# paper_alpha %s", buf);
        strvec_push(&comments, line);
        format_float(paper_splotch_alpha, buf);
        snprintf(line, sizeof(line), "# paper_splotch_alpha %s", buf);
        strvec_push(&comments, line);
        format_float(paper_splotch_shade, buf);
        snprintf(line, sizeof(line), "# paper_splotch_shade %s", buf);
        strvec_push(&comments, line);
        snprintf(line, sizeof(line), "# paper_splotch_px %d", paper_splotch_px);
        strvec_push(&comments, line);
    }

    write_ppm_p3_ascii(output, &comments, w, h, &current, 0);

    intvec_free(&current);
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
    if (strcmp(cmd, "copy-footer-rows") == 0) {
        cmd_copy_footer_rows(argc, argv);
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
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        usage();
        return 0;
    }
    die2("ppm_transform: unknown command ", cmd);
}
