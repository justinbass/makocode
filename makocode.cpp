/*
    makocode.cpp
    ------------
    Self-contained CLI implementation of the MakoCode encoder/decoder. The program
    performs payload compression, bitstream framing, and image mapping without relying
    on headers or external libraries; every construct required to build the binary
    lives inside this translation unit.

    Major capabilities include:
        * Lossless payload handling via a shared 12-bit LZW codec used by encoder
          and decoder contexts.
        * Byte/bit utilities that assemble payload frames and translate them into
          pixel samples.
        * Portable PPM import/export that maps encoded payloads to RGB imagery with
          configurable color palettes.
        * Command-line entry points (`encode`, `decode`, `test`) that round-trip data,
          validate the codec, and emit artifacts for inspection.

    License: GNU AGPLv3 (intent inherited from the project notes).
*/

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long       usize;
typedef unsigned long long  u64;
typedef long long           i64;

static const usize USIZE_MAX_VALUE = (usize)~(usize)0;

extern "C" void* malloc(unsigned long size);
extern "C" void  free(void* ptr);
extern "C" int   write(int fd, const void* buf, unsigned long count);
extern "C" int   read(int fd, void* buf, unsigned long count);
extern "C" void  exit(int code);
extern "C" int   close(int fd);
extern "C" int   creat(const char* path, unsigned int mode);
extern "C" int   unlink(const char* path);
extern "C" int   open(const char* path, int flags, ...);
extern "C" void* realloc(void* ptr, unsigned long size);
extern "C" double sqrt(double value);
extern "C" double floor(double value);
extern "C" double ceil(double value);
extern "C" double sin(double value);
extern "C" double cos(double value);
extern "C" double atan2(double y, double x);
struct tm;
extern "C" long  time(long* tloc);
extern "C" struct tm* gmtime(const long* timep);
extern "C" unsigned long strftime(char* s, unsigned long max, const char* format, const struct tm* tm);
extern "C" char* realpath(const char* path, char* resolved_path);

static u16 read_le_u16(const u8* ptr);
static u32 read_le_u32(const u8* ptr);
static u64 read_le_u64(const u8* ptr);
static void write_le_u16(u8* ptr, u16 value);
static void write_le_u32(u8* ptr, u32 value);
static void write_le_u64(u8* ptr, u64 value);

static u16 read_le_u16(const u8* ptr) {
    return (u16)ptr[0] | ((u16)ptr[1] << 8);
}

static u32 read_le_u32(const u8* ptr) {
    return (u32)ptr[0] | ((u32)ptr[1] << 8) | ((u32)ptr[2] << 16) | ((u32)ptr[3] << 24);
}

static u64 read_le_u64(const u8* ptr) {
    u64 low = (u64)read_le_u32(ptr);
    u64 high = (u64)read_le_u32(ptr + 4u);
    return low | (high << 32u);
}

static void write_le_u16(u8* ptr, u16 value) {
    ptr[0] = (u8)(value & 0xFFu);
    ptr[1] = (u8)((value >> 8u) & 0xFFu);
}

static void write_le_u32(u8* ptr, u32 value) {
    ptr[0] = (u8)(value & 0xFFu);
    ptr[1] = (u8)((value >> 8u) & 0xFFu);
    ptr[2] = (u8)((value >> 16u) & 0xFFu);
    ptr[3] = (u8)((value >> 24u) & 0xFFu);
}

static void write_le_u64(u8* ptr, u64 value) {
    write_le_u32(ptr, (u32)(value & 0xFFFFFFFFull));
    write_le_u32(ptr + 4u, (u32)((value >> 32u) & 0xFFFFFFFFull));
}

static usize ascii_length(const char* text) {
    if (!text) {
        return 0;
    }
    const char* cursor = text;
    while (*cursor) {
        ++cursor;
    }
    return (usize)(cursor - text);
}

static int ascii_compare(const char* a, const char* b) {
    if (a == b) {
        return 0;
    }
    if (!a) {
        return -1;
    }
    if (!b) {
        return 1;
    }
    while (*a && (*a == *b)) {
        ++a;
        ++b;
    }
    return ((unsigned char)(*a) < (unsigned char)(*b)) ? -1 :
           ((unsigned char)(*a) > (unsigned char)(*b)) ?  1 : 0;
}

static bool ascii_starts_with(const char* text, const char* prefix) {
    if (!text || !prefix) {
        return false;
    }
    while (*prefix) {
        if (*text != *prefix) {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

static bool ascii_equals_token(const char* text, usize length, const char* keyword) {
    if (!text || !keyword) {
        return false;
    }
    usize index = 0u;
    while (keyword[index]) {
        if (index >= length || text[index] != keyword[index]) {
            return false;
        }
        ++index;
    }
    return (index == length);
}

static bool ascii_to_u64(const char* text, usize length, u64* out_value) {
    if (!text || !out_value || length == 0u) {
        return false;
    }
    u64 value = 0u;
    for (usize i = 0u; i < length; ++i) {
        char c = text[i];
        if (c < '0' || c > '9') {
            return false;
        }
        u64 digit = (u64)(c - '0');
        u64 next = value * 10u + digit;
        if (next < value) {
            return false;
        }
        value = next;
    }
    *out_value = value;
    return true;
}

static bool ascii_to_double(const char* text, usize length, double* out_value) {
    if (!text || !out_value || length == 0u) {
        return false;
    }
    u64 integer_part = 0u;
    u64 fraction_part = 0u;
    u64 fraction_scale = 1u;
    bool seen_decimal = false;
    for (usize i = 0u; i < length; ++i) {
        char c = text[i];
        if (c == '.') {
            if (seen_decimal) {
                return false;
            }
            seen_decimal = true;
            continue;
        }
        if (c < '0' || c > '9') {
            return false;
        }
        u64 digit = (u64)(c - '0');
        if (!seen_decimal) {
            integer_part = integer_part * 10u + digit;
        } else {
            if (fraction_scale > 1000000000u) {
                continue;
            }
            fraction_part = fraction_part * 10u + digit;
            fraction_scale *= 10u;
        }
    }
    double value = (double)integer_part;
    if (seen_decimal && fraction_scale > 1u) {
        value += ((double)fraction_part) / (double)fraction_scale;
    }
    *out_value = value;
    return true;
}

static void console_write(int fd, const char* text) {
    if (!text) {
        return;
    }
    usize len = ascii_length(text);
    if (len) {
        write(fd, text, len);
    }
}

static void console_line(int fd, const char* text) {
    console_write(fd, text);
    write(fd, "\n", 1u);
}

static void u64_to_ascii(u64 value, char* buffer, usize capacity) {
    if (!buffer || capacity == 0) {
        return;
    }
    char temp[32];
    usize index = 0;
    if (value == 0) {
        temp[index++] = '0';
    } else {
        while (value && index < (usize)sizeof(temp)) {
            u64 digit = value % 10u;
            temp[index++] = (char)('0' + digit);
            value /= 10u;
        }
    }
    usize count = (index < (capacity - 1u)) ? index : (capacity - 1u);
    for (usize i = 0; i < count; ++i) {
        buffer[i] = temp[count - 1u - i];
    }
    buffer[count] = '\0';
}

static void format_fixed_3(double value, char* buffer, usize capacity) {
    if (!buffer || capacity < 6u) {
        return;
    }
    if (value < 0.0) {
        buffer[0] = '-';
        format_fixed_3(-value, buffer + 1, capacity - 1);
        return;
    }
    u64 scaled = (u64)(value * 1000.0 + 0.5);
    u64 whole = scaled / 1000u;
    u64 frac = scaled % 1000u;
    char whole_buf[32];
    u64_to_ascii(whole, whole_buf, sizeof(whole_buf));
    usize whole_len = ascii_length(whole_buf);
    if (whole_len + 4u >= capacity) {
        whole_len = capacity > 5u ? capacity - 5u : 0u;
    }
    for (usize i = 0; i < whole_len; ++i) {
        buffer[i] = whole_buf[i];
    }
    buffer[whole_len] = '.';
    buffer[whole_len + 1u] = (char)('0' + (frac / 100u) % 10u);
    buffer[whole_len + 2u] = (char)('0' + (frac / 10u) % 10u);
    buffer[whole_len + 3u] = (char)('0' + frac % 10u);
    buffer[whole_len + 4u] = '\0';
}

static bool utc_timestamp_string(char* buffer, usize capacity) {
    if (!buffer || capacity == 0u) {
        return false;
    }
    long seconds = time((long*)0);
    if (seconds < 0L) {
        return false;
    }
    struct tm* utc = gmtime(&seconds);
    if (!utc) {
        return false;
    }
    static const char format[] = "%Y%m%dT%H%M%SZ";
    unsigned long written = strftime(buffer, (unsigned long)capacity, format, utc);
    if (!written || written >= capacity) {
        return false;
    }
    buffer[written] = '\0';
    return true;
}

namespace makocode {

namespace image {

enum LoadStatus {
    LoadSuccess = 0,
    LoadFileOpenFailed,
    LoadDecodeError,
    LoadUnsupportedFormat,
    LoadOutOfMemory
};

struct ImageBuffer {
    unsigned width;
    unsigned height;
    unsigned char* pixels;
};

struct Histogram {
    unsigned long long bins[256];
    unsigned char average;
};

struct CutLevels {
    unsigned char global_cut;
    unsigned char fill_cut;
};

struct LoadDiagnostics {
    char message[128];
};

static const int kMaxIterations = 32;

static void set_diag_message(LoadDiagnostics* diag, const char* text)
{
    if (!diag) return;
    if (!text) {
        diag->message[0] = '\0';
        return;
    }
    usize length = ::ascii_length(text);
    if (length >= (usize)sizeof(diag->message)) {
        length = (usize)sizeof(diag->message) - 1u;
    }
    for (usize i = 0; i < length; ++i) {
        diag->message[i] = text[i];
    }
    diag->message[length] = '\0';
}

void release(ImageBuffer& image)
{
    if (image.pixels) {
        free(image.pixels);
        image.pixels = 0;
    }
    image.width = 0;
    image.height = 0;
}

static bool read_entire_file(const char* path,
                             unsigned char** out_data,
                             usize* out_size,
                             LoadDiagnostics* diag)
{
    *out_data = 0;
    *out_size = 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_diag_message(diag, "failed to open file");
        return false;
    }

    usize capacity = 4096u;
    unsigned char* data = (unsigned char*)malloc(capacity);
    if (!data) {
        close(fd);
        set_diag_message(diag, "allocation failed");
        return false;
    }

    usize total = 0;
    while (1) {
        if (total >= capacity) {
            usize new_capacity = capacity ? capacity << 1u : 4096u;
            if (new_capacity <= capacity) {
                new_capacity = capacity + 4096u;
            }
            unsigned char* resized = (unsigned char*)realloc(data, new_capacity);
            if (!resized) {
                free(data);
                close(fd);
                set_diag_message(diag, "allocation failed");
                return false;
            }
            data = resized;
            capacity = new_capacity;
        }
        unsigned long request = (unsigned long)(capacity - total);
        if (request > 65536ul) request = 65536ul;
        int read_result = read(fd, data + total, request);
        if (read_result < 0) {
            free(data);
            close(fd);
            set_diag_message(diag, "read failure");
            return false;
        }
        if (read_result == 0) {
            break;
        }
        total += (usize)read_result;
    }
    close(fd);

    if (!total) {
        free(data);
        set_diag_message(diag, "empty file");
        return false;
    }

    *out_data = data;
    *out_size = total;
    return true;
}

LoadStatus load_ppm_grayscale(const char* path,
                              ImageBuffer& out,
                              LoadDiagnostics* diag)
{
    release(out);
    if (!path) {
        set_diag_message(diag, "null path");
        return LoadDecodeError;
    }

    unsigned char* data = 0;
    usize size = 0;
    if (!read_entire_file(path, &data, &size, diag)) {
        return LoadDecodeError;
    }

    const unsigned char* cursor = data;
    const unsigned char* end = data + size;

    if (cursor >= end) {
        free(data);
        set_diag_message(diag, "truncated header");
        return LoadDecodeError;
    }

    if (*cursor != 'P') {
        free(data);
        set_diag_message(diag, "not a PPM/PGM file");
        return LoadUnsupportedFormat;
    }
    cursor++;
    if (cursor >= end) {
        free(data);
        set_diag_message(diag, "truncated header");
        return LoadDecodeError;
    }
    unsigned char format = *cursor++;
    bool is_p6 = (format == '6');
    bool is_p5 = (format == '5');
    if (!(is_p6 || is_p5)) {
        free(data);
        set_diag_message(diag, "unsupported PPM format");
        return LoadUnsupportedFormat;
    }

    auto skip_whitespace_and_comments =
        [&](const unsigned char*& ptr)->bool {
            while (ptr < end) {
                unsigned char ch = *ptr;
                if (ch == '#') {
                    while (ptr < end && *ptr != '\n') ptr++;
                    continue;
                }
                if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                    ptr++;
                    continue;
                }
                break;
            }
            return ptr < end;
        };

    if (!skip_whitespace_and_comments(cursor)) {
        free(data);
        set_diag_message(diag, "failed to parse width");
        return LoadDecodeError;
    }

    auto parse_positive_int =
        [&](unsigned* out_value)->bool {
            unsigned value = 0;
            bool any = false;
            while (cursor < end) {
                unsigned char ch = *cursor;
                if (ch < '0' || ch > '9') break;
                any = true;
                value = value * 10u + (ch - '0');
                cursor++;
            }
            if (!any) return false;
            *out_value = value;
            return true;
        };

    unsigned width = 0;
    if (!parse_positive_int(&width) || width == 0) {
        free(data);
        set_diag_message(diag, "invalid width");
        return LoadDecodeError;
    }
    if (!skip_whitespace_and_comments(cursor)) {
        free(data);
        set_diag_message(diag, "failed to parse height");
        return LoadDecodeError;
    }
    unsigned height = 0;
    if (!parse_positive_int(&height) || height == 0) {
        free(data);
        set_diag_message(diag, "invalid height");
        return LoadDecodeError;
    }
    if (!skip_whitespace_and_comments(cursor)) {
        free(data);
        set_diag_message(diag, "failed to parse maxval");
        return LoadDecodeError;
    }
    unsigned maxval = 0;
    if (!parse_positive_int(&maxval)) {
        free(data);
        set_diag_message(diag, "invalid maxval");
        return LoadDecodeError;
    }
    if (maxval != 255u) {
        free(data);
        set_diag_message(diag, "maxval must be 255");
        return LoadUnsupportedFormat;
    }
    if (!skip_whitespace_and_comments(cursor)) {
        free(data);
        set_diag_message(diag, "missing raster data");
        return LoadDecodeError;
    }

    usize remaining = (usize)(end - cursor);
    usize expected_bytes = (usize)width * (usize)height * (is_p6 ? 3u : 1u);
    if (remaining < expected_bytes) {
        free(data);
        set_diag_message(diag, "truncated raster data");
        return LoadDecodeError;
    }

    unsigned char* pixels = (unsigned char*)malloc((usize)width * (usize)height);
    if (!pixels) {
        free(data);
        set_diag_message(diag, "allocation failed");
        return LoadOutOfMemory;
    }

    if (is_p5) {
        for (usize i = 0; i < (usize)width * (usize)height; ++i) {
            pixels[i] = cursor[i];
        }
    } else {
        const unsigned char* src = cursor;
        for (usize i = 0; i < (usize)width * (usize)height; ++i) {
            unsigned char r = src[0];
            unsigned char g = src[1];
            unsigned char b = src[2];
            pixels[i] = (unsigned char)((r + g + b) / 3u);
            src += 3;
        }
    }

    free(data);

    out.width = width;
    out.height = height;
    out.pixels = pixels;
    return LoadSuccess;
}

void compute_histogram(const ImageBuffer& image, Histogram& histogram)
{
    for (int i = 0; i < 256; ++i) {
        histogram.bins[i] = 0;
    }
    histogram.average = 0;

    if (!image.pixels) {
        return;
    }

    unsigned long long total = 0;
    usize total_pixels = (usize)image.width * (usize)image.height;
    if (!total_pixels) {
        return;
    }
    for (usize i = 0; i < total_pixels; ++i) {
        unsigned char value = image.pixels[i];
        histogram.bins[value]++;
        total += value;
    }
    unsigned long long rounded = total + (total_pixels >> 1u);
    histogram.average = (unsigned char)(rounded / total_pixels);
}

bool analyze_cut_levels(const Histogram& histogram,
                        double sync_white_cut,
                        CutLevels& out_levels)
{
    if (sync_white_cut < 0.0 || sync_white_cut > 1.0) {
        return false;
    }

    unsigned char global_cut = histogram.average;
    unsigned char fill_cut = histogram.average;

    for (int iter = 0; iter < kMaxIterations; ++iter) {
        unsigned char last_cut = global_cut;
        unsigned long long black_pixels = 0;
        unsigned long long white_pixels = 0;
        double black_rms = 0.0;
        double white_rms = 0.0;

        for (int i = 0; i < (int)global_cut; ++i) {
            unsigned long long count = histogram.bins[i];
            if (!count) {
                continue;
            }
            double diff = (double)((int)global_cut - i);
            black_rms += diff * diff * (double)count;
            black_pixels += count;
        }

        for (int i = (int)global_cut + 1; i < 256; ++i) {
            unsigned long long count = histogram.bins[i];
            if (!count) {
                continue;
            }
            double diff = (double)(i - (int)global_cut);
            white_rms += diff * diff * (double)count;
            white_pixels += count;
        }

        if (black_pixels == 0 || white_pixels == 0) {
            break;
        }

        black_rms = sqrt(black_rms / (double)black_pixels);
        white_rms = sqrt(white_rms / (double)white_pixels);

        double black = (double)global_cut - black_rms;
        double white = (double)global_cut + white_rms;

        double weighted_cut = white * sync_white_cut +
                              black * (1.0 - sync_white_cut);
        double fill = 0.5 * white + 0.5 * black;

        double rounded_global = floor(weighted_cut + 0.5);
        double rounded_fill = floor(fill + 0.5);
        if (rounded_global < 0.0) rounded_global = 0.0;
        if (rounded_global > 255.0) rounded_global = 255.0;
        if (rounded_fill < 0.0) rounded_fill = 0.0;
        if (rounded_fill > 255.0) rounded_fill = 255.0;

        global_cut = (unsigned char)rounded_global;
        fill_cut = (unsigned char)rounded_fill;

        if (global_cut == last_cut) {
            break;
        }
    }

    out_levels.global_cut = global_cut;
    out_levels.fill_cut = fill_cut;
    return true;
}

struct CornerDetectionConfig {
    u32 logical_width;
    u32 logical_height;
    u32 cross_half;
    double cross_trim;

    CornerDetectionConfig()
        : logical_width(0u),
          logical_height(0u),
          cross_half(3u),
          cross_trim(0.75) {}
};

struct CornerDetectionResult {
    bool valid;
    double corners[4][2];
    double hpixel;
    double vpixel;
    double pixelhx;
    double pixelhy;
    double pixelvx;
    double pixelvy;
    int chalf;
    int chalf_fine;
    double skew_degrees;
    double perpendicularity_degrees;
    unsigned left_edge;
    unsigned right_edge;
    unsigned top_edge;
    unsigned bottom_edge;

    CornerDetectionResult()
        : valid(false),
          hpixel(0.0),
          vpixel(0.0),
          pixelhx(0.0),
          pixelhy(0.0),
          pixelvx(0.0),
          pixelvy(0.0),
          chalf(0),
          chalf_fine(0),
          skew_degrees(0.0),
          perpendicularity_degrees(0.0),
          left_edge(0u),
          right_edge(0u),
          top_edge(0u),
          bottom_edge(0u) {
        for (int i = 0; i < 4; ++i) {
            corners[i][0] = 0.0;
            corners[i][1] = 0.0;
        }
    }
};

static unsigned char sample_pixel(const ImageBuffer& image, int x, int y)
{
    if (!image.pixels) {
        return 0xFFu;
    }
    if (x < 0 || y < 0) {
        return 0xFFu;
    }
    if ((unsigned)x >= image.width || (unsigned)y >= image.height) {
        return 0xFFu;
    }
    return image.pixels[(unsigned)y * image.width + (unsigned)x];
}

static void scan_diagonal(const ImageBuffer& image,
                          int start_x,
                          int start_y,
                          int outer_dx,
                          int step_y,
                          unsigned char cutlevel,
                          int& out_x,
                          int& out_y)
{
    unsigned limit = image.width < image.height ? image.width : image.height;
    int step_x = -outer_dx;
    for (unsigned diag = 0u; diag < limit; ++diag) {
        int x = start_x + outer_dx * (int)diag;
        int y = start_y;
        unsigned len = diag + 1u;
        for (unsigned step = 0u; step < len; ++step) {
            unsigned char value = sample_pixel(image, x, y);
            if (value < cutlevel) {
                out_x = x;
                out_y = y;
                return;
            }
            x += step_x;
            y += step_y;
        }
    }
    out_x = -1;
    out_y = -1;
}

static void normalize_vector(double& x, double& y)
{
    double length = sqrt(x * x + y * y);
    if (length <= 0.0) {
        x = 0.0;
        y = 0.0;
        return;
    }
    x /= length;
    y /= length;
}

static double angle(double x, double y)
{
    static const double kRadToDeg = 180.0 / 3.14159265358979323846;
    double radians = atan2(y, x);
    double degrees = radians * kRadToDeg;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

static double normalize_angle(double value)
{
    while (value <= -180.0) {
        value += 360.0;
    }
    while (value > 180.0) {
        value -= 360.0;
    }
    return value;
}

bool find_corners(const ImageBuffer& image,
                  unsigned char global_cut,
                  const CornerDetectionConfig& config,
                  CornerDetectionResult& result)
{
    result = CornerDetectionResult();
    if (!image.pixels || !image.width || !image.height) {
        return false;
    }
    if (config.logical_width == 0u || config.logical_height == 0u) {
        return false;
    }
    if (config.cross_half == 0u) {
        return false;
    }

    int x = 0;
    int y = 0;

    scan_diagonal(image, 0, 0, +1, +1, global_cut, x, y);
    if (x < 0) {
        return false;
    }
    result.corners[0][0] = (double)x;
    result.corners[0][1] = (double)y;

    scan_diagonal(image, (int)image.width - 1, 0, -1, +1, global_cut, x, y);
    if (x < 0) {
        return false;
    }
    result.corners[1][0] = (double)(x + 1);
    result.corners[1][1] = (double)y;

    scan_diagonal(image, 0, (int)image.height - 1, +1, -1, global_cut, x, y);
    if (x < 0) {
        return false;
    }
    result.corners[2][0] = (double)x;
    result.corners[2][1] = (double)(y + 1);

    scan_diagonal(image, (int)image.width - 1, (int)image.height - 1, -1, -1, global_cut, x, y);
    if (x < 0) {
        return false;
    }
    result.corners[3][0] = (double)(x + 1);
    result.corners[3][1] = (double)(y + 1);

    double left = result.corners[0][0] < result.corners[2][0] ? result.corners[0][0] : result.corners[2][0];
    double right = result.corners[1][0] > result.corners[3][0] ? result.corners[1][0] : result.corners[3][0];
    double top = result.corners[0][1] < result.corners[1][1] ? result.corners[0][1] : result.corners[1][1];
    double bottom = result.corners[2][1] > result.corners[3][1] ? result.corners[2][1] : result.corners[3][1];
    if (left < 0.0) left = 0.0;
    if (top < 0.0) top = 0.0;
    if (bottom < 0.0) bottom = 0.0;
    result.left_edge = (unsigned)left;
    result.right_edge = (unsigned)right;
    result.top_edge = (unsigned)top;
    result.bottom_edge = (unsigned)bottom;

    double hpixel = ((result.corners[1][0] + result.corners[3][0]) -
                     (result.corners[0][0] + result.corners[0][0])) * 0.5;
    hpixel /= (double)config.logical_width;
    double vpixel = ((result.corners[2][1] + result.corners[3][1]) -
                     (result.corners[0][1] + result.corners[1][1])) * 0.5;
    vpixel /= (double)config.logical_height;
    result.hpixel = hpixel;
    result.vpixel = vpixel;

    result.pixelhx = ((result.corners[1][0] + result.corners[3][0]) -
                      (result.corners[0][0] + result.corners[2][0])) * 0.5;
    result.pixelhy = ((result.corners[1][1] + result.corners[3][1]) -
                      (result.corners[0][1] + result.corners[2][1])) * 0.5;
    result.pixelvx = ((result.corners[2][0] + result.corners[3][0]) -
                      (result.corners[0][0] + result.corners[1][0])) * 0.5;
    result.pixelvy = ((result.corners[2][1] + result.corners[3][1]) -
                      (result.corners[0][1] + result.corners[1][1])) * 0.5;

    normalize_vector(result.pixelhx, result.pixelhy);
    normalize_vector(result.pixelvx, result.pixelvy);

    double horiz_angle = angle(result.pixelhx, -result.pixelhy);
    double vert_angle = angle(result.pixelvx, -result.pixelvy);
    result.skew_degrees = normalize_angle(horiz_angle + vert_angle + 90.0) * 0.5;
    result.perpendicularity_degrees = horiz_angle - vert_angle;

    double half_scale = (double)config.cross_half * 0.5;
    unsigned h_half = (unsigned)(hpixel * half_scale);
    unsigned v_half = (unsigned)(vpixel * half_scale);
    result.chalf = (int)(h_half < v_half ? h_half : v_half);

    double trim = config.cross_trim;
    if (trim < 0.0) {
        trim = 0.0;
    }
    double core = (double)config.cross_half - trim;
    if (core < 0.0) {
        core = 0.0;
    }
    unsigned h_fine = (unsigned)(hpixel * core);
    unsigned v_fine = (unsigned)(vpixel * core);
    result.chalf_fine = (int)(h_fine < v_fine ? h_fine : v_fine);

    result.valid = true;
    return true;
}

} /* namespace image */

struct ByteBuffer {
    u8*   data;
    usize size;
    usize capacity;

    ByteBuffer() : data(0), size(0), capacity(0) {}

    ~ByteBuffer() {
        release();
    }

    void release() {
        if (data) {
            free(data);
            data = 0;
        }
        size = 0;
        capacity = 0;
    }

    bool reserve(usize new_capacity) {
        if (new_capacity <= capacity) {
            return true;
        }
        if (new_capacity > (USIZE_MAX_VALUE / 2u)) {
            return false;
        }
        u8* new_data = (u8*)malloc(new_capacity);
        if (!new_data) {
            return false;
        }
        for (usize i = 0; i < size; ++i) {
            new_data[i] = data[i];
        }
        free(data);
        data = new_data;
        capacity = new_capacity;
        return true;
    }

    bool ensure(usize required) {
        if (required <= capacity) {
            return true;
        }
        usize grow = capacity ? capacity : (usize)64;
        while (grow < required) {
            usize doubled = grow + grow;
            if (doubled <= grow) {
                grow = required;
                break;
            }
            grow = doubled;
        }
        return reserve(grow);
    }

    bool push(u8 value) {
        if (!ensure(size + 1u)) {
            return false;
        }
        data[size++] = value;
        return true;
    }

    bool append_bytes(const u8* source, usize length) {
        if (!source || length == 0u) {
            return true;
        }
        if (!ensure(size + length)) {
            return false;
        }
        for (usize i = 0; i < length; ++i) {
            data[size + i] = source[i];
        }
        size += length;
        return true;
    }

    bool append_ascii(const char* text) {
        if (!text) {
            return true;
        }
        return append_bytes((const u8*)text, ascii_length(text));
    }

    bool append_char(char c) {
        return append_bytes((const u8*)&c, 1u);
    }
};

struct FloodQueue {
    usize* data;
    usize capacity;
    usize head;
    usize tail;

    FloodQueue() : data(0), capacity(0), head(0), tail(0) {}

    bool initialize(usize max_elements) {
        if (max_elements < 2u) {
            max_elements = 2u;
        }
        data = (usize*)malloc(max_elements * sizeof(usize));
        if (!data) {
            capacity = 0;
            head = tail = 0;
            return false;
        }
        capacity = max_elements;
        head = tail = 0;
        return true;
    }

    void release() {
        if (data) {
            free(data);
            data = 0;
        }
        capacity = 0;
        head = tail = 0;
    }

    void reset() {
        head = tail = 0;
    }

    bool push(usize value) {
        usize next = head + 1u;
        if (next >= capacity) {
            next = 0u;
        }
        if (next == tail) {
            return false;
        }
        data[head] = value;
        head = next;
        return true;
    }

    bool pop(usize& value) {
        if (head == tail) {
            return false;
        }
        value = data[tail];
        tail++;
        if (tail >= capacity) {
            tail = 0u;
        }
        return true;
    }
};

bool remove_border_dirt(image::ImageBuffer& image,
                        unsigned char fill_threshold,
                        ByteBuffer* out_mask)
{
    unsigned width = image.width;
    unsigned height = image.height;
    if (!image.pixels || width == 0u || height == 0u) {
        if (out_mask) {
            out_mask->release();
        }
        return false;
    }
    usize total_pixels = (usize)width * (usize)height;
    if (!total_pixels) {
        if (out_mask) {
            out_mask->release();
        }
        return false;
    }

    ByteBuffer mask;
    if (!mask.ensure(total_pixels)) {
        return false;
    }
    mask.size = total_pixels;
    for (usize i = 0; i < total_pixels; ++i) {
        mask.data[i] = 0xFFu;
    }

    usize queue_capacity = total_pixels + 1u;
    if (queue_capacity <= total_pixels) {
        mask.release();
        if (out_mask) {
            out_mask->release();
        }
        return false;
    }
    FloodQueue queue;
    if (!queue.initialize(queue_capacity)) {
        mask.release();
        return false;
    }

    bool queue_ok = true;
    unsigned char* pixels = image.pixels;

    auto try_copy = [&](unsigned x, unsigned y, bool require_white) {
        if (!queue_ok) {
            return;
        }
        if (x >= width || y >= height) {
            return;
        }
        usize index = (usize)y * (usize)width + (usize)x;
        if (mask.data[index] == 0u) {
            return;
        }
        if (require_white && pixels[index] < fill_threshold) {
            return;
        }
        mask.data[index] = 0u;
        if (!queue.push(index)) {
            queue_ok = false;
        }
    };

    auto flood = [&](unsigned start_x, unsigned start_y, bool require_white) {
        if (!queue_ok) {
            return;
        }
        queue.reset();
        try_copy(start_x, start_y, require_white);
        usize encoded = 0u;
        while (queue_ok && queue.pop(encoded)) {
            unsigned y = (unsigned)(encoded / (usize)width);
            unsigned x = (unsigned)(encoded - (usize)y * (usize)width);
            if (x + 1u < width) {
                try_copy(x + 1u, y, require_white);
            }
            if (x > 0u) {
                try_copy(x - 1u, y, require_white);
            }
            if (y + 1u < height) {
                try_copy(x, y + 1u, require_white);
            }
            if (y > 0u) {
                try_copy(x, y - 1u, require_white);
            }
        }
    };

    flood(0u, 0u, true);
    flood(width >> 1u, 0u, true);
    flood(width - 1u, 0u, true);
    flood(0u, height >> 1u, true);
    flood(0u, height - 1u, true);
    flood(width - 1u, height - 1u, true);
    flood(width - 1u, height >> 1u, true);
    flood(width >> 1u, height - 1u, true);
    flood(width >> 1u, height >> 1u, false);

    bool success = queue_ok;

    queue.release();

    if (!success) {
        mask.release();
        return false;
    }

    for (usize i = 0; i < total_pixels; ++i) {
        pixels[i] = (unsigned char)(pixels[i] | mask.data[i]);
    }

    if (out_mask) {
        out_mask->release();
        if (!out_mask->ensure(total_pixels)) {
            mask.release();
            return false;
        }
        out_mask->size = total_pixels;
        for (usize i = 0; i < total_pixels; ++i) {
            out_mask->data[i] = mask.data[i];
        }
    }

    mask.release();
    return true;
}

static bool generate_random_bytes(ByteBuffer& buffer, usize count, u64 seed) {
    buffer.release();
    if (!buffer.ensure(count)) {
        return false;
    }
    u64 state = seed ? seed : 0x1234abcdf00dbeefull;
    for (usize i = 0u; i < count; ++i) {
        state = state * 6364136223846793005ull + 0x9e3779b97f4a7c15ull;
        buffer.data[i] = (u8)((state >> 32u) & 0xFFu);
    }
    buffer.size = count;
    return true;
}

static const unsigned __int128 PCG64_MULTIPLIER =
    (((unsigned __int128)0x2360ED051FC65DA4ull) << 64u) | (unsigned __int128)0x4385DF649FCCF645ull;
static const unsigned __int128 PCG64_INCREMENT =
    (((unsigned __int128)0x5851F42D4C957F2Dull) << 64u) | (unsigned __int128)0x14057B7EF767814Full;

struct Pcg64Generator {
    unsigned __int128 state;

    Pcg64Generator() : state(0u) {}

    void seed(u64 seed_value) {
        state = 0u;
        (void)next();
        state += (unsigned __int128)seed_value;
        (void)next();
    }

    u64 next() {
        unsigned __int128 oldstate = state;
        state = oldstate * PCG64_MULTIPLIER + PCG64_INCREMENT;
        u64 xorshifted = (u64)(((oldstate >> 64u) ^ oldstate) >> 64u);
        u32 rot = (u32)(oldstate >> 122u);
        u32 rotate = rot & 63u;
        u32 inverse = (64u - rotate) & 63u;
        return (xorshifted >> rotate) | (xorshifted << inverse);
    }
};

static void crypto_random_bytes(u8* dest, usize count) {
    if (!dest || count == 0u) {
        return;
    }
    static Pcg64Generator rng;
    static bool seeded = false;
    if (!seeded) {
        u64 seed = (u64)time((long*)0);
        if (!seed) {
            seed = 0x726f6c6c6572756cull;
        }
        rng.seed(seed);
        seeded = true;
    }
    for (usize i = 0u; i < count; ) {
        u64 value = rng.next();
        for (u32 j = 0u; j < 8u && i < count; ++j) {
            dest[i++] = (u8)((value >> (j * 8u)) & 0xFFu);
        }
    }
}

static bool constant_time_equal(const u8* a, const u8* b, usize length) {
    if (!a || !b) {
        return false;
    }
    u8 diff = 0u;
    for (usize i = 0u; i < length; ++i) {
        diff |= (u8)(a[i] ^ b[i]);
    }
    return (diff == 0u);
}

static inline u32 rotl32(u32 value, u32 amount) {
    return (value << amount) | (value >> (32u - amount));
}

static inline u32 rotr32(u32 value, u32 amount) {
    return (value >> amount) | (value << (32u - amount));
}

static const u32 ENCRYPTION_HEADER_MAGIC = 0x4D454E43u;
static const u8  ENCRYPTION_HEADER_VERSION = 1u;
static const u8  ENCRYPTION_FLAG_PASSWORD = 0x01u;
static const u8  ENCRYPTION_KDF_PBKDF2_SHA256 = 1u;
static const usize ENCRYPTION_SALT_BYTES = (usize)16;
static const usize ENCRYPTION_NONCE_BYTES = (usize)12;
static const usize ENCRYPTION_TAG_BYTES = (usize)16;
static const usize ENCRYPTION_HEADER_BYTES = (usize)48;
static const u32 ENCRYPTION_PBKDF2_ITERATIONS = 60000u;

static const u32 SHA256_INITIAL_STATE[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};

static const u32 SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

struct Sha256State {
    u32 h[8];
    u8 buffer[64];
    u64 bit_length;
    u32 buffer_used;

    Sha256State() : h(), buffer(), bit_length(0u), buffer_used(0u) {}
};

static void sha256_process_block(Sha256State& state, const u8* block) {
    u32 w[64];
    for (u32 i = 0u; i < 16u; ++i) {
        u32 b0 = (u32)block[i * 4u + 0u];
        u32 b1 = (u32)block[i * 4u + 1u];
        u32 b2 = (u32)block[i * 4u + 2u];
        u32 b3 = (u32)block[i * 4u + 3u];
        w[i] = (b0 << 24u) | (b1 << 16u) | (b2 << 8u) | b3;
    }
    for (u32 i = 16u; i < 64u; ++i) {
        u32 s0 = rotr32(w[i - 15u], 7u) ^ rotr32(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        u32 s1 = rotr32(w[i - 2u], 17u) ^ rotr32(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    u32 a = state.h[0];
    u32 b = state.h[1];
    u32 c = state.h[2];
    u32 d = state.h[3];
    u32 e = state.h[4];
    u32 f = state.h[5];
    u32 g = state.h[6];
    u32 h = state.h[7];
    for (u32 i = 0u; i < 64u; ++i) {
        u32 s1 = rotr32(e, 6u) ^ rotr32(e, 11u) ^ rotr32(e, 25u);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 temp1 = h + s1 + ch + SHA256_K[i] + w[i];
        u32 s0 = rotr32(a, 2u) ^ rotr32(a, 13u) ^ rotr32(a, 22u);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state.h[0] += a;
    state.h[1] += b;
    state.h[2] += c;
    state.h[3] += d;
    state.h[4] += e;
    state.h[5] += f;
    state.h[6] += g;
    state.h[7] += h;
}

static void sha256_init(Sha256State& state) {
    for (u32 i = 0u; i < 8u; ++i) {
        state.h[i] = SHA256_INITIAL_STATE[i];
    }
    state.bit_length = 0u;
    state.buffer_used = 0u;
}

static void sha256_update(Sha256State& state, const u8* data, usize length) {
    if (!data || length == 0u) {
        return;
    }
    while (length > 0u) {
        usize to_copy = 64u - (usize)state.buffer_used;
        if (to_copy > length) {
            to_copy = length;
        }
        for (usize i = 0u; i < to_copy; ++i) {
            state.buffer[state.buffer_used + (u32)i] = data[i];
        }
        state.buffer_used += (u32)to_copy;
        data += to_copy;
        length -= to_copy;
        if (state.buffer_used == 64u) {
            sha256_process_block(state, state.buffer);
            state.bit_length += 512u;
            state.buffer_used = 0u;
        }
    }
}

static void sha256_finalize(Sha256State& state, u8 digest[32]) {
    state.bit_length += (u64)state.buffer_used * 8ull;
    state.buffer[state.buffer_used++] = 0x80u;
    if (state.buffer_used > 56u) {
        while (state.buffer_used < 64u) {
            state.buffer[state.buffer_used++] = 0u;
        }
        sha256_process_block(state, state.buffer);
        state.buffer_used = 0u;
    }
    while (state.buffer_used < 56u) {
        state.buffer[state.buffer_used++] = 0u;
    }
    u64 bits = state.bit_length;
    for (u32 i = 0u; i < 8u; ++i) {
        state.buffer[63u - i] = (u8)(bits & 0xFFu);
        bits >>= 8u;
    }
    sha256_process_block(state, state.buffer);
    for (u32 i = 0u; i < 8u; ++i) {
        digest[i * 4u + 0u] = (u8)((state.h[i] >> 24u) & 0xFFu);
        digest[i * 4u + 1u] = (u8)((state.h[i] >> 16u) & 0xFFu);
        digest[i * 4u + 2u] = (u8)((state.h[i] >> 8u) & 0xFFu);
        digest[i * 4u + 3u] = (u8)(state.h[i] & 0xFFu);
    }
}

static bool hmac_sha256(const u8* key,
                        usize key_length,
                        const u8* data,
                        usize data_length,
                        u8 output[32]) {
    if (!key || key_length == 0u || !output) {
        return false;
    }
    const usize block_size = 64u;
    u8 key_block[64];
    for (usize i = 0u; i < block_size; ++i) {
        key_block[i] = 0u;
    }
    if (key_length > block_size) {
        Sha256State temp;
        sha256_init(temp);
        sha256_update(temp, key, key_length);
        u8 hashed[32];
        sha256_finalize(temp, hashed);
        for (usize i = 0u; i < 32u; ++i) {
            key_block[i] = hashed[i];
        }
        for (usize i = 0u; i < 32u; ++i) {
            hashed[i] = 0u;
        }
    } else {
        for (usize i = 0u; i < key_length; ++i) {
            key_block[i] = key[i];
        }
    }
    u8 ipad[64];
    u8 opad[64];
    for (usize i = 0u; i < block_size; ++i) {
        ipad[i] = (u8)(key_block[i] ^ 0x36u);
        opad[i] = (u8)(key_block[i] ^ 0x5cu);
    }
    Sha256State inner;
    sha256_init(inner);
    sha256_update(inner, ipad, block_size);
    sha256_update(inner, data, data_length);
    u8 inner_digest[32];
    sha256_finalize(inner, inner_digest);
    Sha256State outer;
    sha256_init(outer);
    sha256_update(outer, opad, block_size);
    sha256_update(outer, inner_digest, 32u);
    sha256_finalize(outer, output);
    for (usize i = 0u; i < block_size; ++i) {
        key_block[i] = 0u;
        ipad[i] = 0u;
        opad[i] = 0u;
    }
    for (usize i = 0u; i < 32u; ++i) {
        inner_digest[i] = 0u;
    }
    return true;
}

static bool pbkdf2_hmac_sha256(const u8* password,
                               usize password_length,
                               const u8* salt,
                               usize salt_length,
                               u32 iterations,
                               u8* output,
                               usize output_length) {
    if (!password || password_length == 0u || !salt || !output || output_length == 0u || iterations == 0u) {
        return false;
    }
    u32 block_count = (u32)((output_length + 31u) / 32u);
    makocode::ByteBuffer salt_buffer;
    if (!salt_buffer.ensure(salt_length + 4u)) {
        return false;
    }
    for (usize i = 0u; i < salt_length; ++i) {
        salt_buffer.data[i] = salt[i];
    }
    salt_buffer.size = salt_length + 4u;
    u8 u[32];
    u8 t[32];
    for (u32 block_index = 1u; block_index <= block_count; ++block_index) {
        u32 bi = block_index;
        salt_buffer.data[salt_length + 0u] = (u8)((bi >> 24u) & 0xFFu);
        salt_buffer.data[salt_length + 1u] = (u8)((bi >> 16u) & 0xFFu);
        salt_buffer.data[salt_length + 2u] = (u8)((bi >> 8u) & 0xFFu);
        salt_buffer.data[salt_length + 3u] = (u8)(bi & 0xFFu);
        if (!hmac_sha256(password, password_length, salt_buffer.data, salt_length + 4u, u)) {
            salt_buffer.release();
            return false;
        }
        for (u32 i = 0u; i < 32u; ++i) {
            t[i] = u[i];
        }
        for (u32 iter = 1u; iter < iterations; ++iter) {
            if (!hmac_sha256(password, password_length, u, 32u, u)) {
                salt_buffer.release();
                return false;
            }
            for (u32 i = 0u; i < 32u; ++i) {
                t[i] ^= u[i];
            }
        }
        usize offset = (usize)(block_index - 1u) * 32u;
        usize remaining = output_length - offset;
        usize to_copy = (remaining < 32u) ? remaining : (usize)32u;
        for (usize i = 0u; i < to_copy; ++i) {
            output[offset + i] = t[i];
        }
    }
    for (u32 i = 0u; i < 32u; ++i) {
        u[i] = 0u;
        t[i] = 0u;
    }
    salt_buffer.release();
    return true;
}

static inline void chacha20_quarter_round(u32& a, u32& b, u32& c, u32& d) {
    a += b;
    d ^= a;
    d = rotl32(d, 16u);
    c += d;
    b ^= c;
    b = rotl32(b, 12u);
    a += b;
    d ^= a;
    d = rotl32(d, 8u);
    c += d;
    b ^= c;
    b = rotl32(b, 7u);
}

static void chacha20_block(const u8 key[32], const u8 nonce[12], u32 counter, u8 output[64]) {
    static const u32 constants[4] = {0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u};
    u32 state[16];
    state[0] = constants[0];
    state[1] = constants[1];
    state[2] = constants[2];
    state[3] = constants[3];
    for (u32 i = 0u; i < 8u; ++i) {
        state[4u + i] = read_le_u32(key + i * 4u);
    }
    state[12] = counter;
    state[13] = read_le_u32(nonce + 0u);
    state[14] = read_le_u32(nonce + 4u);
    state[15] = read_le_u32(nonce + 8u);
    u32 working[16];
    for (u32 i = 0u; i < 16u; ++i) {
        working[i] = state[i];
    }
    for (u32 round = 0u; round < 10u; ++round) {
        chacha20_quarter_round(working[0], working[4], working[8], working[12]);
        chacha20_quarter_round(working[1], working[5], working[9], working[13]);
        chacha20_quarter_round(working[2], working[6], working[10], working[14]);
        chacha20_quarter_round(working[3], working[7], working[11], working[15]);
        chacha20_quarter_round(working[0], working[5], working[10], working[15]);
        chacha20_quarter_round(working[1], working[6], working[11], working[12]);
        chacha20_quarter_round(working[2], working[7], working[8], working[13]);
        chacha20_quarter_round(working[3], working[4], working[9], working[14]);
    }
    for (u32 i = 0u; i < 16u; ++i) {
        working[i] += state[i];
        write_le_u32(output + i * 4u, working[i]);
    }
}

static bool chacha20_xor(const u8 key[32],
                         const u8 nonce[12],
                         u32 counter,
                         const u8* input,
                         u8* output,
                         usize length) {
    if ((!input && length > 0u) || !output) {
        return false;
    }
    u8 block[64];
    usize offset = 0u;
    while (offset < length) {
        chacha20_block(key, nonce, counter, block);
        counter += 1u;
        usize chunk = (length - offset >= 64u) ? 64u : (length - offset);
        for (usize i = 0u; i < chunk; ++i) {
            u8 keystream = block[i];
            u8 source = input ? input[offset + i] : 0u;
            output[offset + i] = (u8)(source ^ keystream);
        }
        offset += chunk;
    }
    for (u32 i = 0u; i < 64u; ++i) {
        block[i] = 0u;
    }
    return true;
}

struct Poly1305State {
    u64 r0, r1, r2, r3, r4;
    u64 s1, s2, s3, s4;
    u64 h0, h1, h2, h3, h4;

    Poly1305State()
        : r0(0u), r1(0u), r2(0u), r3(0u), r4(0u),
          s1(0u), s2(0u), s3(0u), s4(0u),
          h0(0u), h1(0u), h2(0u), h3(0u), h4(0u) {}
};

static void poly1305_init_state(Poly1305State& state, const u8 key[32]) {
    unsigned __int128 r = (unsigned __int128)read_le_u64(key) |
                          ((unsigned __int128)read_le_u64(key + 8u) << 64u);
    unsigned __int128 mask = (((unsigned __int128)0x0ffffffc0ffffffcull) << 64u) |
                             (unsigned __int128)0x0ffffffc0fffffffull;
    r &= mask;
    u64 r_low = (u64)(r & ((unsigned __int128)0xFFFFFFFFFFFFFFFFull));
    u64 r_high = (u64)(r >> 64u);
    state.r0 = r_low & 0x3ffffffu;
    state.r1 = ((r_low >> 26u) & 0x3ffffffu);
    state.r2 = (((r_low >> 52u) | (r_high << 12u)) & 0x3ffffffu);
    state.r3 = ((r_high >> 14u) & 0x3ffffffu);
    state.r4 = ((r_high >> 40u) & 0x3ffffffu);
    state.s1 = state.r1 * 5u;
    state.s2 = state.r2 * 5u;
    state.s3 = state.r3 * 5u;
    state.s4 = state.r4 * 5u;
    state.h0 = state.h1 = state.h2 = state.h3 = state.h4 = 0u;
}

static void poly1305_process_block(Poly1305State& state, const u8 block[16], u64 hibit) {
    u64 t0 = read_le_u64(block);
    u64 t1 = read_le_u64(block + 8u);
    state.h0 += t0 & 0x3ffffffu;
    state.h1 += (t0 >> 26u) & 0x3ffffffu;
    state.h2 += ((t0 >> 52u) | (t1 << 12u)) & 0x3ffffffu;
    state.h3 += (t1 >> 14u) & 0x3ffffffu;
    state.h4 += ((t1 >> 40u) & 0x3ffffffu) + hibit;

    unsigned __int128 d0 = (unsigned __int128)state.h0 * state.r0 +
                           (unsigned __int128)state.h1 * state.s4 +
                           (unsigned __int128)state.h2 * state.s3 +
                           (unsigned __int128)state.h3 * state.s2 +
                           (unsigned __int128)state.h4 * state.s1;
    unsigned __int128 d1 = (unsigned __int128)state.h0 * state.r1 +
                           (unsigned __int128)state.h1 * state.r0 +
                           (unsigned __int128)state.h2 * state.s4 +
                           (unsigned __int128)state.h3 * state.s3 +
                           (unsigned __int128)state.h4 * state.s2;
    unsigned __int128 d2 = (unsigned __int128)state.h0 * state.r2 +
                           (unsigned __int128)state.h1 * state.r1 +
                           (unsigned __int128)state.h2 * state.r0 +
                           (unsigned __int128)state.h3 * state.s4 +
                           (unsigned __int128)state.h4 * state.s3;
    unsigned __int128 d3 = (unsigned __int128)state.h0 * state.r3 +
                           (unsigned __int128)state.h1 * state.r2 +
                           (unsigned __int128)state.h2 * state.r1 +
                           (unsigned __int128)state.h3 * state.r0 +
                           (unsigned __int128)state.h4 * state.s4;
    unsigned __int128 d4 = (unsigned __int128)state.h0 * state.r4 +
                           (unsigned __int128)state.h1 * state.r3 +
                           (unsigned __int128)state.h2 * state.r2 +
                           (unsigned __int128)state.h3 * state.r1 +
                           (unsigned __int128)state.h4 * state.r0;

    u64 h0 = (u64)(d0 & 0x3ffffffu);
    u64 carry = (u64)(d0 >> 26u);
    d1 += carry;
    u64 h1 = (u64)(d1 & 0x3ffffffu);
    carry = (u64)(d1 >> 26u);
    d2 += carry;
    u64 h2 = (u64)(d2 & 0x3ffffffu);
    carry = (u64)(d2 >> 26u);
    d3 += carry;
    u64 h3 = (u64)(d3 & 0x3ffffffu);
    carry = (u64)(d3 >> 26u);
    d4 += carry;
    u64 h4 = (u64)(d4 & 0x3ffffffu);
    carry = (u64)(d4 >> 26u);
    h0 += carry * 5u;
    carry = h0 >> 26u;
    h0 &= 0x3ffffffu;
    h1 += carry;
    carry = h1 >> 26u;
    h1 &= 0x3ffffffu;
    h2 += carry;
    carry = h2 >> 26u;
    h2 &= 0x3ffffffu;
    h3 += carry;
    carry = h3 >> 26u;
    h3 &= 0x3ffffffu;
    h4 += carry;
    state.h0 = h0;
    state.h1 = h1;
    state.h2 = h2;
    state.h3 = h3;
    state.h4 = h4;
}

static void poly1305_update(Poly1305State& state, const u8* data, usize length) {
    if (!data || length == 0u) {
        return;
    }
    while (length >= 16u) {
        poly1305_process_block(state, data, (1ull << 24u));
        data += 16u;
        length -= 16u;
    }
    if (length > 0u) {
        u8 buffer[16];
        for (usize i = 0u; i < 16u; ++i) {
            buffer[i] = 0u;
        }
        for (usize i = 0u; i < length; ++i) {
            buffer[i] = data[i];
        }
        buffer[length] = 1u;
        poly1305_process_block(state, buffer, 0u);
    }
}

static void poly1305_pad16(Poly1305State& state, usize length) {
    usize remainder = length & 15u;
    if (!remainder) {
        return;
    }
    u8 padding[16];
    for (usize i = 0u; i < 16u; ++i) {
        padding[i] = 0u;
    }
    poly1305_update(state, padding, 16u - remainder);
}

static void poly1305_finish(Poly1305State& state, const u8 pad[16], u8 tag[16]) {
    u64 carry = state.h1 >> 26u;
    state.h1 &= 0x3ffffffu;
    state.h2 += carry;
    carry = state.h2 >> 26u;
    state.h2 &= 0x3ffffffu;
    state.h3 += carry;
    carry = state.h3 >> 26u;
    state.h3 &= 0x3ffffffu;
    state.h4 += carry;
    carry = state.h4 >> 26u;
    state.h4 &= 0x3ffffffu;
    state.h0 += carry * 5u;
    carry = state.h0 >> 26u;
    state.h0 &= 0x3ffffffu;
    state.h1 += carry;
    carry = state.h1 >> 26u;
    state.h1 &= 0x3ffffffu;
    state.h2 += carry;
    carry = state.h2 >> 26u;
    state.h2 &= 0x3ffffffu;
    state.h3 += carry;
    carry = state.h3 >> 26u;
    state.h3 &= 0x3ffffffu;
    state.h4 += carry;

    u64 g0 = state.h0 + 5u;
    carry = g0 >> 26u;
    g0 &= 0x3ffffffu;
    u64 g1 = state.h1 + carry;
    carry = g1 >> 26u;
    g1 &= 0x3ffffffu;
    u64 g2 = state.h2 + carry;
    carry = g2 >> 26u;
    g2 &= 0x3ffffffu;
    u64 g3 = state.h3 + carry;
    carry = g3 >> 26u;
    g3 &= 0x3ffffffu;
    u64 g4 = state.h4 + carry - (1ull << 26u);
    u64 mask = ((g4 >> 63u) - 1u);
    g0 &= mask;
    g1 &= mask;
    g2 &= mask;
    g3 &= mask;
    g4 &= mask;
    mask = ~mask;
    state.h0 = (state.h0 & mask) | g0;
    state.h1 = (state.h1 & mask) | g1;
    state.h2 = (state.h2 & mask) | g2;
    state.h3 = (state.h3 & mask) | g3;
    state.h4 = (state.h4 & mask) | g4;

    unsigned __int128 acc = (unsigned __int128)state.h0 |
                            ((unsigned __int128)state.h1 << 26u) |
                            ((unsigned __int128)state.h2 << 52u) |
                            ((unsigned __int128)state.h3 << 78u) |
                            ((unsigned __int128)state.h4 << 104u);
    unsigned __int128 sum0 = acc + (unsigned __int128)read_le_u64(pad);
    u64 f0 = (u64)sum0;
    unsigned __int128 sum1 = ((acc >> 64u) + (unsigned __int128)read_le_u64(pad + 8u) + (sum0 >> 64u));
    u64 f1 = (u64)sum1;
    write_le_u64(tag, f0);
    write_le_u64(tag + 8u, f1);
}

static bool chacha20_poly1305_encrypt(const u8* key,
                                      const u8* nonce,
                                      const u8* plaintext,
                                      usize plaintext_length,
                                      const u8* aad,
                                      usize aad_length,
                                      ByteBuffer& ciphertext,
                                      u8 tag[16]) {
    if (!key || !nonce || !tag) {
        return false;
    }
    ciphertext.release();
    if (plaintext_length > 0u && !ciphertext.ensure(plaintext_length)) {
        return false;
    }
    ciphertext.size = plaintext_length;
    if (plaintext_length > 0u && !ciphertext.data) {
        return false;
    }
    u8 initial_block[64];
    chacha20_block(key, nonce, 0u, initial_block);
    if (plaintext_length > 0u) {
        if (!chacha20_xor(key, nonce, 1u, plaintext, ciphertext.data, plaintext_length)) {
            for (u32 i = 0u; i < 64u; ++i) {
                initial_block[i] = 0u;
            }
            return false;
        }
    }
    Poly1305State mac;
    poly1305_init_state(mac, initial_block);
    if (aad && aad_length > 0u) {
        poly1305_update(mac, aad, aad_length);
    }
    poly1305_pad16(mac, aad_length);
    if (plaintext_length > 0u) {
        poly1305_update(mac, ciphertext.data, plaintext_length);
    }
    poly1305_pad16(mac, plaintext_length);
    u8 length_block[16];
    for (u32 i = 0u; i < 16u; ++i) {
        length_block[i] = 0u;
    }
    write_le_u64(length_block, (u64)aad_length);
    write_le_u64(length_block + 8u, (u64)plaintext_length);
    poly1305_process_block(mac, length_block, (1ull << 24u));
    poly1305_finish(mac, initial_block + 16u, tag);
    for (u32 i = 0u; i < 64u; ++i) {
        initial_block[i] = 0u;
    }
    return true;
}

static bool chacha20_poly1305_decrypt(const u8* key,
                                      const u8* nonce,
                                      const u8* ciphertext,
                                      usize ciphertext_length,
                                      const u8* aad,
                                      usize aad_length,
                                      ByteBuffer& plaintext,
                                      const u8 tag[16],
                                      bool* auth_failed) {
    if (!key || !nonce || (!ciphertext && ciphertext_length > 0u) || !tag) {
        return false;
    }
    if (auth_failed) {
        *auth_failed = false;
    }
    plaintext.release();
    u8 initial_block[64];
    chacha20_block(key, nonce, 0u, initial_block);
    Poly1305State mac;
    poly1305_init_state(mac, initial_block);
    if (aad && aad_length > 0u) {
        poly1305_update(mac, aad, aad_length);
    }
    poly1305_pad16(mac, aad_length);
    if (ciphertext_length > 0u) {
        poly1305_update(mac, ciphertext, ciphertext_length);
    }
    poly1305_pad16(mac, ciphertext_length);
    u8 length_block[16];
    for (u32 i = 0u; i < 16u; ++i) {
        length_block[i] = 0u;
    }
    write_le_u64(length_block, (u64)aad_length);
    write_le_u64(length_block + 8u, (u64)ciphertext_length);
    poly1305_process_block(mac, length_block, (1ull << 24u));
    u8 expected_tag[16];
    poly1305_finish(mac, initial_block + 16u, expected_tag);
    bool match = constant_time_equal(expected_tag, tag, ENCRYPTION_TAG_BYTES);
    for (u32 i = 0u; i < 16u; ++i) {
        expected_tag[i] = 0u;
    }
    if (!match) {
        if (auth_failed) {
            *auth_failed = true;
        }
        for (u32 i = 0u; i < 64u; ++i) {
            initial_block[i] = 0u;
        }
        return false;
    }
    if (ciphertext_length > 0u) {
        if (!plaintext.ensure(ciphertext_length)) {
            for (u32 i = 0u; i < 64u; ++i) {
                initial_block[i] = 0u;
            }
            return false;
        }
        plaintext.size = ciphertext_length;
        if (!chacha20_xor(key, nonce, 1u, ciphertext, plaintext.data, ciphertext_length)) {
            plaintext.release();
            for (u32 i = 0u; i < 64u; ++i) {
                initial_block[i] = 0u;
            }
            return false;
        }
    }
    for (u32 i = 0u; i < 64u; ++i) {
        initial_block[i] = 0u;
    }
    return true;
}

enum DecryptStatus {
    DecryptStatus_NotEncrypted = 0,
    DecryptStatus_Success = 1,
    DecryptStatus_AuthFailed = 2,
    DecryptStatus_FormatError = 3
};

static bool encrypt_payload_buffer(const ByteBuffer& plaintext,
                                   const char* password,
                                   usize password_length,
                                   ByteBuffer& output) {
    if (!password || password_length == 0u) {
        return false;
    }
    u8 salt[ENCRYPTION_SALT_BYTES];
    u8 nonce[ENCRYPTION_NONCE_BYTES];
    crypto_random_bytes(salt, ENCRYPTION_SALT_BYTES);
    crypto_random_bytes(nonce, ENCRYPTION_NONCE_BYTES);
    u8 key[32];
    if (!pbkdf2_hmac_sha256((const u8*)password,
                            password_length,
                            salt,
                            ENCRYPTION_SALT_BYTES,
                            ENCRYPTION_PBKDF2_ITERATIONS,
                            key,
                            32u)) {
        for (u32 i = 0u; i < 32u; ++i) {
            key[i] = 0u;
        }
        return false;
    }
    const u8* plain_ptr = plaintext.data;
    usize plain_size = plaintext.size;
    u8 header[ENCRYPTION_HEADER_BYTES];
    for (usize i = 0u; i < ENCRYPTION_HEADER_BYTES; ++i) {
        header[i] = 0u;
    }
    write_le_u32(header, ENCRYPTION_HEADER_MAGIC);
    header[4] = ENCRYPTION_HEADER_VERSION;
    header[5] = ENCRYPTION_FLAG_PASSWORD;
    header[6] = ENCRYPTION_KDF_PBKDF2_SHA256;
    header[7] = 0u;
    write_le_u32(header + 8u, ENCRYPTION_PBKDF2_ITERATIONS);
    write_le_u64(header + 12u, (u64)plain_size);
    for (usize i = 0u; i < ENCRYPTION_SALT_BYTES; ++i) {
        header[20u + i] = salt[i];
    }
    for (usize i = 0u; i < ENCRYPTION_NONCE_BYTES; ++i) {
        header[36u + i] = nonce[i];
    }
    ByteBuffer ciphertext;
    u8 tag[ENCRYPTION_TAG_BYTES];
    if (!chacha20_poly1305_encrypt(key,
                                   nonce,
                                   plain_ptr,
                                   plain_size,
                                   header,
                                   ENCRYPTION_HEADER_BYTES,
                                   ciphertext,
                                   tag)) {
        for (u32 i = 0u; i < 32u; ++i) {
            key[i] = 0u;
        }
        ciphertext.release();
        return false;
    }
    usize total_size = ENCRYPTION_HEADER_BYTES + ciphertext.size + ENCRYPTION_TAG_BYTES;
    output.release();
    if (!output.ensure(total_size)) {
        for (u32 i = 0u; i < 32u; ++i) {
            key[i] = 0u;
        }
        ciphertext.release();
        return false;
    }
    output.size = total_size;
    u8* dest = output.data;
    for (usize i = 0u; i < ENCRYPTION_HEADER_BYTES; ++i) {
        dest[i] = header[i];
    }
    if (ciphertext.size > 0u) {
        for (usize i = 0u; i < ciphertext.size; ++i) {
            dest[ENCRYPTION_HEADER_BYTES + i] = ciphertext.data[i];
        }
    }
    for (usize i = 0u; i < ENCRYPTION_TAG_BYTES; ++i) {
        dest[ENCRYPTION_HEADER_BYTES + ciphertext.size + i] = tag[i];
    }
    ciphertext.release();
    for (u32 i = 0u; i < 32u; ++i) {
        key[i] = 0u;
    }
    return true;
}

static DecryptStatus decrypt_payload_buffer(const u8* data,
                                            usize data_length,
                                            const char* password,
                                            usize password_length,
                                            ByteBuffer& plaintext) {
    plaintext.release();
    if (!data || data_length < ENCRYPTION_HEADER_BYTES + ENCRYPTION_TAG_BYTES) {
        return DecryptStatus_NotEncrypted;
    }
    u32 magic = read_le_u32(data);
    if (magic != ENCRYPTION_HEADER_MAGIC) {
        return DecryptStatus_NotEncrypted;
    }
    if (!password || password_length == 0u) {
        return DecryptStatus_FormatError;
    }
    u8 version = data[4];
    u8 flags = data[5];
    u8 kdf = data[6];
    if (version != ENCRYPTION_HEADER_VERSION || !(flags & ENCRYPTION_FLAG_PASSWORD) || kdf != ENCRYPTION_KDF_PBKDF2_SHA256) {
        return DecryptStatus_FormatError;
    }
    u32 iterations = read_le_u32(data + 8u);
    if (iterations == 0u) {
        return DecryptStatus_FormatError;
    }
    u64 plain_bytes = read_le_u64(data + 12u);
    if (plain_bytes > (u64)USIZE_MAX_VALUE) {
        return DecryptStatus_FormatError;
    }
    const u8* salt = data + 20u;
    const u8* nonce = data + 36u;
    if (data_length < ENCRYPTION_HEADER_BYTES + ENCRYPTION_TAG_BYTES) {
        return DecryptStatus_FormatError;
    }
    usize cipher_bytes = data_length - ENCRYPTION_HEADER_BYTES - ENCRYPTION_TAG_BYTES;
    if ((u64)cipher_bytes != plain_bytes) {
        return DecryptStatus_FormatError;
    }
    const u8* cipher = data + ENCRYPTION_HEADER_BYTES;
    const u8* tag = cipher + cipher_bytes;
    u8 key[32];
    if (!pbkdf2_hmac_sha256((const u8*)password,
                            password_length,
                            salt,
                            ENCRYPTION_SALT_BYTES,
                            iterations,
                            key,
                            32u)) {
        for (u32 i = 0u; i < 32u; ++i) {
            key[i] = 0u;
        }
        return DecryptStatus_FormatError;
    }
    bool auth_failed = false;
    if (!chacha20_poly1305_decrypt(key,
                                   nonce,
                                   cipher,
                                   cipher_bytes,
                                   data,
                                   ENCRYPTION_HEADER_BYTES,
                                   plaintext,
                                   tag,
                                   &auth_failed)) {
        for (u32 i = 0u; i < 32u; ++i) {
            key[i] = 0u;
        }
        plaintext.release();
        if (auth_failed) {
            return DecryptStatus_AuthFailed;
        }
        return DecryptStatus_FormatError;
    }
    if ((u64)plaintext.size != plain_bytes) {
        for (u32 i = 0u; i < 32u; ++i) {
            key[i] = 0u;
        }
        plaintext.release();
        return DecryptStatus_FormatError;
    }
    for (u32 i = 0u; i < 32u; ++i) {
        key[i] = 0u;
    }
    return DecryptStatus_Success;
}

static bool fisher_yates_shuffle(u8* data, usize count) {
    if (!data || count <= 1u) {
        return true;
    }
    Pcg64Generator rng;
    rng.seed(0u);
    for (usize i = count - 1u; i > 0u; --i) {
        u64 value = rng.next();
        usize j = (usize)(value % (u64)(i + 1u));
        u8 temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
    return true;
}

static bool fisher_yates_unshuffle(u8* data, usize count) {
    if (!data || count <= 1u) {
        return true;
    }
    if (count > 0u && count > (USIZE_MAX_VALUE / (usize)sizeof(usize))) {
        return false;
    }
    usize* history = (usize*)malloc(sizeof(usize) * (count ? count : 1u));
    if (!history) {
        return false;
    }
    if (count > 0u) {
        history[0] = 0u;
    }
    Pcg64Generator rng;
    rng.seed(0u);
    for (usize i = count - 1u; i > 0u; --i) {
        u64 value = rng.next();
        usize j = (usize)(value % (u64)(i + 1u));
        history[i] = j;
    }
    for (usize i = 1u; i < count; ++i) {
        usize j = history[i];
        u8 temp = data[i];
        data[i] = data[j];
        data[j] = temp;
    }
    free(history);
    return true;
}

static bool shuffle_encoded_stream(u8* data, usize byte_count, bool ecc_enabled);
static bool unshuffle_encoded_stream(u8* data, usize byte_count);

struct BitWriter {
    ByteBuffer buffer;
    usize bit_position;
    bool  failed;

    BitWriter() : buffer(), bit_position(0u), failed(false) {}

    void reset() {
        buffer.release();
        bit_position = 0u;
        failed = false;
    }

    bool ensure_bits(usize target_bits) {
        usize target_bytes = (target_bits + 7u) >> 3u;
        return buffer.ensure(target_bytes);
    }

    bool write_bit(u8 value) {
        if (failed) {
            return false;
        }
        if (!ensure_bits(bit_position + 1u)) {
            failed = true;
            return false;
        }
        usize byte_index = bit_position >> 3u;
        usize bit_offset = bit_position & 7u;
        u8 mask = (u8)(1u << bit_offset);
        if (byte_index >= buffer.size) {
            if (!buffer.push(0u)) {
                failed = true;
                return false;
            }
        }
        if (value & 1u) {
            buffer.data[byte_index] = (u8)(buffer.data[byte_index] | mask);
        } else {
            buffer.data[byte_index] = (u8)(buffer.data[byte_index] & (u8)(~mask));
        }
        ++bit_position;
        return true;
    }

    bool write_bits(u64 value, usize count) {
        if (count > 64u) {
            count = 64u;
        }
        for (usize i = 0; i < count; ++i) {
            u8 bit = (u8)((value >> i) & 1u);
            if (!write_bit(bit)) {
                return false;
            }
        }
        return true;
    }

    bool align_to_byte() {
        usize remainder = bit_position & 7u;
        if (!remainder) {
            return true;
        }
        usize missing = 8u - remainder;
        for (usize i = 0; i < missing; ++i) {
            if (!write_bit(0u)) {
                return false;
            }
        }
        return true;
    }

    const u8* data() const {
        return buffer.data;
    }

    usize bit_size() const {
        return bit_position;
    }

    usize byte_size() const {
        return (bit_position + 7u) >> 3u;
    }
};

struct BitReader {
    const u8* data;
    usize bit_count;
    usize cursor;
    bool failed;

    BitReader() : data(0), bit_count(0u), cursor(0u), failed(false) {}

    void reset(const u8* source, usize bits) {
        data = source;
        bit_count = bits;
        cursor = 0u;
        failed = false;
    }

    u8 read_bit() {
        if (!data || cursor >= bit_count) {
            failed = true;
            return 0u;
        }
        usize byte_index = cursor >> 3u;
        usize bit_offset = cursor & 7u;
        ++cursor;
        return (u8)((data[byte_index] >> bit_offset) & 1u);
    }

    u64 read_bits(usize count) {
        if (count > 64u) {
            count = 64u;
        }
        u64 result = 0u;
        for (usize i = 0; i < count; ++i) {
            u8 bit = read_bit();
            result |= ((u64)bit) << i;
        }
        return result;
    }

    bool align_to_byte() {
        usize remainder = cursor & 7u;
        if (!remainder) {
            return true;
        }
        if ((cursor + (8u - remainder)) > bit_count) {
            failed = true;
            return false;
        }
        cursor += (8u - remainder);
        return true;
    }
};

static const u16 LZW_MAX_CODES = 4096u;
static const u32 LZW_HASH_SIZE = 8192u;
static const u16 LZW_INVALID_CODE = 0xFFFFu;

static void lzw_hash_reset(u16* table) {
    if (!table) {
        return;
    }
    for (u32 i = 0u; i < LZW_HASH_SIZE; ++i) {
        table[i] = LZW_INVALID_CODE;
    }
}

static u16 lzw_hash_lookup(const u16* prefixes,
                           const u8* values,
                           const u16* table,
                           u16 prefix_code,
                           u8 value) {
    if (!prefixes || !values || !table) {
        return LZW_INVALID_CODE;
    }
    u32 mask = LZW_HASH_SIZE - 1u;
    u32 hash = (((u32)prefix_code) << 8u) ^ (u32)value;
    u32 slot = hash & mask;
    for (u32 attempt = 0u; attempt < LZW_HASH_SIZE; ++attempt) {
        u16 code = table[slot];
        if (code == LZW_INVALID_CODE) {
            return LZW_INVALID_CODE;
        }
        if (prefixes[code] == prefix_code && values[code] == value) {
            return code;
        }
        slot = (slot + 1u) & mask;
    }
    return LZW_INVALID_CODE;
}

static bool lzw_hash_insert(u16* prefixes,
                            u8* values,
                            u16* table,
                            u16 code,
                            u16 prefix_code,
                            u8 value) {
    if (!prefixes || !values || !table) {
        return false;
    }
    u32 mask = LZW_HASH_SIZE - 1u;
    u32 hash = (((u32)prefix_code) << 8u) ^ (u32)value;
    u32 slot = hash & mask;
    for (u32 attempt = 0u; attempt < LZW_HASH_SIZE; ++attempt) {
        if (table[slot] == LZW_INVALID_CODE) {
            table[slot] = code;
            prefixes[code] = prefix_code;
            values[code] = value;
            return true;
        }
        slot = (slot + 1u) & mask;
    }
    return false;
}

static bool lzw_compress(const u8* input, usize length, ByteBuffer& output) {
    output.release();
    if (!input || length == 0u) {
        return true;
    }
    u16 prefixes[LZW_MAX_CODES];
    u8  values[LZW_MAX_CODES];
    for (u16 i = 0u; i < LZW_MAX_CODES; ++i) {
        prefixes[i] = 0u;
        values[i] = 0u;
    }
    u16 hash_table[LZW_HASH_SIZE];
    lzw_hash_reset(hash_table);
    BitWriter writer;
    u16 dict_size = 256u;
    u16 current = (u16)input[0];
    for (usize index = 1u; index < length; ++index) {
        u8 symbol = input[index];
        u16 found = lzw_hash_lookup(prefixes, values, hash_table, current, symbol);
        if (found != LZW_INVALID_CODE) {
            current = found;
        } else {
            if (!writer.write_bits((u64)current, 12u)) {
                return false;
            }
            if (dict_size < LZW_MAX_CODES) {
                if (!lzw_hash_insert(prefixes, values, hash_table, dict_size, current, symbol)) {
                    return false;
                }
                ++dict_size;
            }
            current = (u16)symbol;
        }
    }
    if (!writer.write_bits((u64)current, 12u)) {
        return false;
    }
    if (!writer.align_to_byte()) {
        return false;
    }
    usize compressed_size = writer.byte_size();
    if (!output.ensure(compressed_size)) {
        return false;
    }
    const u8* data = writer.data();
    for (usize i = 0u; i < compressed_size; ++i) {
        output.data[i] = data ? data[i] : 0u;
    }
    output.size = compressed_size;
    return true;
}

static bool lzw_emit_sequence(u16 code,
                              const u16* prefixes,
                              const u8* values,
                              u8* scratch,
                              usize scratch_capacity,
                              ByteBuffer& dest,
                              u8* out_first) {
    if (!scratch || scratch_capacity == 0u) {
        return false;
    }
    usize length = 0u;
    u16 current = code;
    while (true) {
        if (current < 256u) {
            if (length >= scratch_capacity) {
                return false;
            }
            scratch[length++] = (u8)current;
            break;
        }
        if (current >= LZW_MAX_CODES) {
            return false;
        }
        if (length >= scratch_capacity) {
            return false;
        }
        scratch[length++] = values[current];
        current = prefixes[current];
    }
    if (length == 0u) {
        return false;
    }
    u8 first = scratch[length - 1u];
    if (out_first) {
        *out_first = first;
    }
    for (usize i = 0u; i < length; ++i) {
        if (!dest.push(scratch[length - 1u - i])) {
            return false;
        }
    }
    return true;
}

static bool lzw_decompress(const u8* input,
                           usize bit_count,
                           ByteBuffer& output) {
    output.release();
    if (!input) {
        return (bit_count == 0u);
    }
    if (bit_count == 0u) {
        return true;
    }
    u16 prefixes[LZW_MAX_CODES];
    u8  values[LZW_MAX_CODES];
    for (u16 i = 0u; i < LZW_MAX_CODES; ++i) {
        prefixes[i] = 0u;
        values[i] = 0u;
    }
    BitReader reader;
    reader.reset(input, bit_count);
    if (reader.bit_count < 12u) {
        return false;
    }
    u16 dict_size = 256u;
    u16 prev_code = (u16)reader.read_bits(12u);
    if (reader.failed) {
        return false;
    }
    u8 scratch[LZW_MAX_CODES];
    u8 prev_first = 0u;
    if (!lzw_emit_sequence(prev_code, prefixes, values, scratch, LZW_MAX_CODES, output, &prev_first)) {
        return false;
    }
    while ((reader.bit_count - reader.cursor) >= 12u) {
        u16 code = (u16)reader.read_bits(12u);
        if (reader.failed) {
            return false;
        }
        u8 current_first = 0u;
        if (code < dict_size) {
            if (!lzw_emit_sequence(code, prefixes, values, scratch, LZW_MAX_CODES, output, &current_first)) {
                return false;
            }
        } else if (code == dict_size) {
            if (!lzw_emit_sequence(prev_code, prefixes, values, scratch, LZW_MAX_CODES, output, &current_first)) {
                return false;
            }
            if (!output.push(prev_first)) {
                return false;
            }
            current_first = prev_first;
        } else {
            return false;
        }
        if (dict_size < LZW_MAX_CODES) {
            prefixes[dict_size] = prev_code;
            values[dict_size] = current_first;
            ++dict_size;
        }
        prev_code = code;
        prev_first = current_first;
    }
    return true;
}


struct EncoderConfig {
    u32 metadata_key_count;
    u32 fiducial_density;
    double ecc_redundancy;
    u32 max_parallelism;

    EncoderConfig()
        : metadata_key_count(0u),
          fiducial_density(0u),
          ecc_redundancy(0.0),
          max_parallelism(1u) {}
};

struct EccSummary {
    bool enabled;
    u16 block_data_symbols;
    u16 parity_symbols;
    double redundancy;
    u64 original_bytes;
    u64 block_count;

    EccSummary()
        : enabled(false),
          block_data_symbols(0u),
          parity_symbols(0u),
          redundancy(0.0),
          original_bytes(0u),
          block_count(0u) {}
};

static bool encode_payload_with_ecc(const ByteBuffer& compressed,
                                    double redundancy,
                                    BitWriter& writer,
                                    EccSummary& summary);

struct EncoderContext {
    EncoderConfig config;
    ByteBuffer payload_bytes;
    BitWriter bit_writer;
    ByteBuffer encryption_password;
    bool configured;
    bool encryption_enabled;

    EccSummary ecc_summary;

    EncoderContext()
        : config(),
          payload_bytes(),
          bit_writer(),
          encryption_password(),
          configured(false),
          encryption_enabled(false),
          ecc_summary() {}

    void reset() {
        payload_bytes.release();
        bit_writer.reset();
        encryption_password.release();
        configured = false;
        encryption_enabled = false;
        ecc_summary = EccSummary();
    }

    bool set_payload(const u8* data, usize size) {
        payload_bytes.release();
        if (!payload_bytes.ensure(size)) {
            return false;
        }
        for (usize i = 0; i < size; ++i) {
            payload_bytes.data[i] = data[i];
        }
        payload_bytes.size = size;
        return true;
    }

    bool set_password(const char* password, usize length) {
        encryption_password.release();
        encryption_enabled = false;
        if (!password || length == 0u) {
            return true;
        }
        if (!encryption_password.ensure(length)) {
            return false;
        }
        for (usize i = 0u; i < length; ++i) {
            encryption_password.data[i] = (u8)password[i];
        }
        encryption_password.size = length;
        encryption_enabled = true;
        return true;
    }

    bool encode_payload(ByteBuffer& output) {
        output.release();
        return lzw_compress(payload_bytes.data, payload_bytes.size, output);
    }

    bool build() {
        bit_writer.reset();
        ecc_summary = EccSummary();
        ByteBuffer compressed;
        if (!encode_payload(compressed)) {
            return false;
        }
        const ByteBuffer* payload_source = &compressed;
        ByteBuffer encrypted;
        if (encryption_enabled && encryption_password.size > 0u) {
            if (!encrypt_payload_buffer(compressed, (const char*)encryption_password.data, encryption_password.size, encrypted)) {
                return false;
            }
            payload_source = &encrypted;
        }
        double redundancy = (config.ecc_redundancy > 0.0) ? config.ecc_redundancy : 0.0;
        if (redundancy > 0.0) {
            if (payload_source->size > 0u) {
                if (!encode_payload_with_ecc(*payload_source, redundancy, bit_writer, ecc_summary)) {
                    return false;
                }
            } else {
                ecc_summary = EccSummary();
            }
        } else {
            for (usize i = 0u; i < payload_source->size; ++i) {
                u8 byte = payload_source->data ? payload_source->data[i] : 0u;
                if (!bit_writer.write_bits((u64)byte, 8u)) {
                    return false;
                }
            }
            if (payload_source->size == 0u) {
                ecc_summary = EccSummary();
            }
            ecc_summary.original_bytes = payload_source->size;
            ecc_summary.redundancy = 0.0;
            ecc_summary.block_count = 0u;
            ecc_summary.block_data_symbols = 0u;
            ecc_summary.parity_symbols = 0u;
            ecc_summary.enabled = false;
        }
        if (!bit_writer.align_to_byte()) {
            return false;
        }
        usize total_bytes = bit_writer.byte_size();
        if (total_bytes > 0u) {
            u8* stream = bit_writer.buffer.data;
            if (!stream) {
                return false;
            }
            if (!shuffle_encoded_stream(stream, total_bytes, ecc_summary.enabled)) {
                return false;
            }
        }
        configured = true;
        return true;
    }

    const EccSummary& ecc_info() const {
        return ecc_summary;
    }
};

static const u16 RS_FIELD_PRIMITIVE = 0x11d;
static const u16 RS_FIELD_SIZE = 255u;
static const usize RS_POLY_CAPACITY = 256u;

static const u16 ECC_HEADER_MAGIC = 0x4543u;
static const u8  ECC_HEADER_VERSION = 1u;
static const usize ECC_HEADER_BITS = 208u;
static const usize ECC_HEADER_BYTES = ECC_HEADER_BITS / 8u;

struct ReedSolomonTables {
    bool initialized;
    u8 exp_table[512];
    u8 log_table[256];

    ReedSolomonTables() : initialized(false), exp_table(), log_table() {}
};

static ReedSolomonTables g_rs_tables;

static void rs_ensure_tables() {
    if (g_rs_tables.initialized) {
        return;
    }
    u16 value = 1u;
    for (u16 i = 0u; i < RS_FIELD_SIZE; ++i) {
        u8 element = (u8)(value & 0xFFu);
        g_rs_tables.exp_table[i] = element;
        g_rs_tables.log_table[element] = (u8)i;
        value <<= 1u;
        if (value & 0x100u) {
            value ^= (u16)RS_FIELD_PRIMITIVE;
        }
    }
    for (u16 i = RS_FIELD_SIZE; i < 512u; ++i) {
        g_rs_tables.exp_table[i] = g_rs_tables.exp_table[i - RS_FIELD_SIZE];
    }
    g_rs_tables.log_table[0] = 0u;
    g_rs_tables.initialized = true;
}

static inline u8 gf_mul(u8 a, u8 b) {
    if (!a || !b) {
        return 0u;
    }
    rs_ensure_tables();
    u16 log_sum = (u16)g_rs_tables.log_table[a] + (u16)g_rs_tables.log_table[b];
    return g_rs_tables.exp_table[log_sum % RS_FIELD_SIZE];
}

static inline u8 gf_div(u8 a, u8 b) {
    if (!b) {
        return 0u;
    }
    if (!a) {
        return 0u;
    }
    rs_ensure_tables();
    u16 log_a = g_rs_tables.log_table[a];
    u16 log_b = g_rs_tables.log_table[b];
    u16 diff = (u16)((log_a + RS_FIELD_SIZE - log_b) % RS_FIELD_SIZE);
    return g_rs_tables.exp_table[diff];
}



static inline u8 gf_pow_alpha(u32 power) {
    rs_ensure_tables();
    power %= RS_FIELD_SIZE;
    return g_rs_tables.exp_table[power];
}

static u16 poly_trim(u8* poly, u16 length) {
    while (length > 0u && poly[length - 1u] == 0u) {
        --length;
    }
    return length;
}

static u16 poly_scale_shift_add(u8* target,
                                u16 target_size,
                                const u8* source,
                                u16 source_size,
                                u8 scale,
                                u16 shift) {
    if (!scale || !source || source_size == 0u) {
        return target_size;
    }
    u16 required = (u16)(source_size + shift);
    if (required > RS_POLY_CAPACITY) {
        required = (u16)RS_POLY_CAPACITY;
    }
    if (target_size < required) {
        for (u16 i = target_size; i < required; ++i) {
            target[i] = 0u;
        }
        target_size = required;
    }
    for (u16 i = 0u; i < source_size; ++i) {
        u16 index = (u16)(i + shift);
        if (index >= RS_POLY_CAPACITY) {
            break;
        }
        target[index] ^= gf_mul(source[i], scale);
    }
    return target_size;
}

static bool rs_build_generator(u16 parity_symbols, u8* generator, u16& out_length) {
    if (!generator || parity_symbols == 0u || parity_symbols >= RS_FIELD_SIZE) {
        return false;
    }
    rs_ensure_tables();
    for (usize i = 0u; i < RS_POLY_CAPACITY; ++i) {
        generator[i] = 0u;
    }
    generator[0] = 1u;
    u16 length = 1u;
    for (u16 i = 0u; i < parity_symbols; ++i) {
        u8 root = gf_pow_alpha((u32)(i + 1u));
        u8 temp[RS_POLY_CAPACITY];
        for (u16 j = 0u; j < RS_POLY_CAPACITY; ++j) {
            temp[j] = 0u;
        }
        for (u16 j = 0u; j < length; ++j) {
            u8 coeff = generator[j];
            temp[j] ^= coeff;
            u16 index = (u16)(j + 1u);
            if (index < RS_POLY_CAPACITY) {
                temp[index] ^= gf_mul(coeff, root);
            }
        }
        ++length;
        if (length > RS_POLY_CAPACITY) {
            return false;
        }
        for (u16 j = 0u; j < length; ++j) {
            generator[j] = temp[j];
        }
    }
    out_length = length;
    return true;
}

static void rs_compute_parity(const u8* generator,
                              u16 parity_symbols,
                              const u8* data_symbols,
                              u16 data_length,
                              u8* parity_out) {
    if (!generator || !parity_out || parity_symbols == 0u) {
        return;
    }
    for (u16 i = 0u; i < parity_symbols; ++i) {
        parity_out[i] = 0u;
    }
    for (u16 i = 0u; i < data_length; ++i) {
        u8 feedback = data_symbols ? data_symbols[i] : 0u;
        feedback ^= parity_out[0];
        if (parity_symbols > 1u) {
            for (u16 j = 0u; j < (parity_symbols - 1u); ++j) {
                parity_out[j] = parity_out[j + 1u];
            }
        }
        parity_out[parity_symbols - 1u] = 0u;
        if (!feedback) {
            continue;
        }
        for (u16 j = 0u; j < parity_symbols; ++j) {
            u8 coeff = generator[j + 1u];
            if (coeff) {
                parity_out[j] ^= gf_mul(coeff, feedback);
            }
        }
    }
}

static bool rs_compute_syndromes(const u8* codeword,
                                 u16 codeword_length,
                                 u16 parity_symbols,
                                 u8* syndromes,
                                 bool& all_zero) {
    if (!codeword || !syndromes || parity_symbols == 0u || codeword_length == 0u) {
        return false;
    }
    all_zero = true;
    for (u16 i = 0u; i < parity_symbols; ++i) {
        u8 evaluation = 0u;
        u8 root = gf_pow_alpha(i + 1u);
        for (u16 j = 0u; j < codeword_length; ++j) {
            evaluation = gf_mul(evaluation, root) ^ codeword[j];
        }
        syndromes[i] = evaluation;
        if (evaluation) {
            all_zero = false;
        }
    }
    return true;
}

static bool rs_berlekamp_massey(const u8* syndromes,
                                u16 parity_symbols,
                                u8* locator,
                                u16& locator_size) {
    if (!syndromes || !locator) {
        return false;
    }
    u8 C[RS_POLY_CAPACITY];
    u8 B[RS_POLY_CAPACITY];
    for (u16 i = 0u; i < RS_POLY_CAPACITY; ++i) {
        C[i] = 0u;
        B[i] = 0u;
    }
    C[0] = 1u;
    B[0] = 1u;
    u16 C_size = 1u;
    u16 B_size = 1u;
    u16 L = 0u;
    u16 m = 1u;
    u8 b = 1u;
    for (u16 n = 0u; n < parity_symbols; ++n) {
        u8 delta = syndromes[n];
        for (u16 i = 1u; i <= L; ++i) {
            if (i >= C_size) {
                break;
            }
            u8 c = C[i];
            u8 s = syndromes[n - i];
            if (c && s) {
                delta ^= gf_mul(c, s);
            }
        }
        if (delta) {
            u8 T[RS_POLY_CAPACITY];
            u16 T_size = C_size;
            for (u16 i = 0u; i < RS_POLY_CAPACITY; ++i) {
                T[i] = C[i];
            }
            u8 factor = gf_div(delta, b);
            C_size = poly_scale_shift_add(C, C_size, B, B_size, factor, m);
            if ((2u * L) <= n) {
                L = (u16)(n + 1u - L);
                for (u16 i = 0u; i < RS_POLY_CAPACITY; ++i) {
                    B[i] = T[i];
                }
                B_size = T_size;
                b = delta;
                m = 1u;
            } else {
                ++m;
            }
        } else {
            ++m;
        }
    }
    for (u16 i = 0u; i < C_size; ++i) {
        locator[i] = C[i];
    }
    locator_size = poly_trim(locator, C_size);
    return (locator_size > 0u);
}

static u8 poly_eval(const u8* poly, u16 length, u8 x) {
    if (!poly || length == 0u) {
        return 0u;
    }
    u8 result = 0u;
    for (u16 i = length; i > 0u; --i) {
        result = gf_mul(result, x) ^ poly[i - 1u];
    }
    return result;
}

static bool rs_find_error_locations(const u8* locator,
                                    u16 locator_size,
                                    u16 codeword_length,
                                    u16* positions,
                                    u16& position_count) {
    if (!locator || !positions || locator_size <= 1u) {
        return false;
    }
    position_count = 0u;
    for (u16 i = 0u; i < codeword_length; ++i) {
        u8 x = gf_pow_alpha(i);
        if (poly_eval(locator, locator_size, x) == 0u) {
            if (position_count >= (locator_size - 1u)) {
                return false;
            }
            u16 position = (i == 0u) ? (u16)(codeword_length - 1u) : (u16)(i - 1u);
            positions[position_count++] = position;
        }
    }
    return (position_count == (locator_size - 1u));
}

static u16 rs_compute_error_evaluator(const u8* locator,
                                      u16 locator_size,
                                      const u8* syndromes,
                                      u16 parity_symbols,
                                      u8* evaluator) {
    for (u16 i = 0u; i < parity_symbols; ++i) {
        evaluator[i] = 0u;
    }
    for (u16 i = 0u; i < locator_size; ++i) {
        u8 coeff = locator[i];
        if (!coeff) {
            continue;
        }
        for (u16 j = 0u; j < parity_symbols; ++j) {
            u16 index = (u16)(i + j);
            if (index >= parity_symbols) {
                break;
            }
            evaluator[index] ^= gf_mul(coeff, syndromes[j]);
        }
    }
    return poly_trim(evaluator, parity_symbols);
}

static u16 rs_compute_locator_derivative(const u8* locator,
                                         u16 locator_size,
                                         u8* derivative) {
    if (!locator || !derivative || locator_size <= 1u) {
        return 0u;
    }
    u16 size = locator_size - 1u;
    for (u16 i = 0u; i < size; ++i) {
        derivative[i] = 0u;
    }
    for (u16 i = 1u; i < locator_size; ++i) {
        if (i & 1u) {
            derivative[i - 1u] = locator[i];
        }
    }
    return poly_trim(derivative, size);
}

static bool rs_correct_errors(u8* codeword,
                              u16 codeword_length,
                              const u8* omega,
                              u16 omega_size,
                              const u8* locator_derivative,
                              u16 derivative_size,
                              const u16* positions,
                              u16 position_count) {
    if (!codeword || !omega || !locator_derivative || !positions) {
        return false;
    }
    for (u16 i = 0u; i < position_count; ++i) {
        u16 pos = positions[i];
        if (pos >= codeword_length) {
            return false;
        }
        u16 exponent = (u16)((pos + 1u) % RS_FIELD_SIZE);
        u8 root = gf_pow_alpha(exponent);
        u8 numerator = poly_eval(omega, omega_size, root);
        u8 denominator = poly_eval(locator_derivative, derivative_size, root);
        if (!denominator) {
            return false;
        }
        u8 magnitude = gf_div(numerator, denominator);
        codeword[pos] ^= magnitude;
    }
    return true;
}



static bool rs_decode_block(u8* block,
                            u16 data_symbols,
                            u16 parity_symbols) {
    if (!block || parity_symbols == 0u || data_symbols == 0u) {
        return false;
    }
    u16 codeword_length = (u16)(data_symbols + parity_symbols);
    if (codeword_length > RS_FIELD_SIZE) {
        return false;
    }
    u8 syndromes[RS_POLY_CAPACITY];
    bool all_zero = false;
    if (!rs_compute_syndromes(block, codeword_length, parity_symbols, syndromes, all_zero)) {
        return false;
    }
    if (all_zero) {
        return true;
    }
    u8 locator[RS_POLY_CAPACITY];
    for (u16 i = 0u; i < RS_POLY_CAPACITY; ++i) {
        locator[i] = 0u;
    }
    u16 locator_size = 0u;
    if (!rs_berlekamp_massey(syndromes, parity_symbols, locator, locator_size)) {
        return false;
    }
    if (locator_size <= 1u) {
        return false;
    }
    u16 error_positions[RS_POLY_CAPACITY];
    u16 error_count = 0u;
    if (!rs_find_error_locations(locator, locator_size, codeword_length, error_positions, error_count)) {
        return false;
    }
    if ((error_count * 2u) > parity_symbols) {
        return false;
    }
    u8 evaluator[RS_POLY_CAPACITY];
    u16 evaluator_size = rs_compute_error_evaluator(locator, locator_size, syndromes, parity_symbols, evaluator);
    u8 locator_derivative[RS_POLY_CAPACITY];
    u16 derivative_size = rs_compute_locator_derivative(locator, locator_size, locator_derivative);
    if (derivative_size == 0u) {
        return false;
    }
    return rs_correct_errors(block, codeword_length, evaluator, evaluator_size, locator_derivative, derivative_size, error_positions, error_count);
}



static bool compute_ecc_layout(usize data_bytes,
                               double requested_redundancy,
                               u16& block_data,
                               u16& parity_symbols,
                               u64& block_count) {
    block_data = 0u;
    parity_symbols = 0u;
    block_count = 0u;
    if (data_bytes == 0u || requested_redundancy <= 0.0) {
        return true;
    }
    double ratio = requested_redundancy;
    if (ratio < 0.000001) {
        ratio = 0.000001;
    }
    double max_data = (double)RS_FIELD_SIZE / (1.0 + ratio);
    if (max_data < 1.0) {
        max_data = 1.0;
    }
    u16 candidate = (u16)max_data;
    if (candidate == 0u) {
        candidate = 1u;
    }
    for (; candidate >= 1u; --candidate) {
        double predicted = ratio * (double)candidate;
        u16 parity = (u16)(predicted + 0.999999);
        if (parity < 2u) {
            parity = 2u;
        }
        while ((u32)candidate + parity > RS_FIELD_SIZE) {
            if (parity <= 2u) {
                break;
            }
            --parity;
        }
        if ((u32)candidate + parity <= RS_FIELD_SIZE) {
            block_data = candidate;
            parity_symbols = parity;
            break;
        }
    }
    if (block_data == 0u || parity_symbols == 0u) {
        return false;
    }
    block_count = (u64)((data_bytes + (usize)block_data - 1u) / (usize)block_data);
    if (block_count == 0u) {
        block_count = 1u;
    }
    u64 total_symbols = (u64)(block_data + parity_symbols) * block_count;
    if (total_symbols > (u64)USIZE_MAX_VALUE) {
        return false;
    }
    return true;
}

static bool encode_payload_with_ecc(const ByteBuffer& compressed,
                                    double redundancy,
                                    BitWriter& writer,
                                    EccSummary& summary) {
    if (!compressed.data || compressed.size == 0u) {
        return false;
    }
    u16 block_data = 0u;
    u16 parity_symbols = 0u;
    u64 block_count = 0u;
    if (!compute_ecc_layout(compressed.size, redundancy, block_data, parity_symbols, block_count)) {
        return false;
    }
    u64 total_symbols = (u64)(block_data + parity_symbols) * block_count;
    if (total_symbols == 0u || total_symbols > (u64)USIZE_MAX_VALUE) {
        return false;
    }
    ByteBuffer encoded;
    if (!encoded.ensure((usize)total_symbols)) {
        return false;
    }
    encoded.size = (usize)total_symbols;
    u8 generator[RS_POLY_CAPACITY];
    u16 generator_size = 0u;
    if (!rs_build_generator(parity_symbols, generator, generator_size)) {
        return false;
    }
    (void)generator_size;
    u8 parity_buffer[RS_POLY_CAPACITY];
    u8 data_buffer[RS_POLY_CAPACITY];
    usize payload_offset = 0u;
    u16 block_symbols = (u16)(block_data + parity_symbols);
    for (u64 block_index = 0u; block_index < block_count; ++block_index) {
        for (u16 i = 0u; i < block_data; ++i) {
            usize src_index = payload_offset + (usize)i;
            data_buffer[i] = (src_index < compressed.size) ? compressed.data[src_index] : 0u;
        }
        rs_compute_parity(generator, parity_symbols, data_buffer, block_data, parity_buffer);
        usize base = (usize)block_index * (usize)block_symbols;
        for (u16 i = 0u; i < block_data; ++i) {
            encoded.data[base + i] = data_buffer[i];
        }
        for (u16 j = 0u; j < parity_symbols; ++j) {
            encoded.data[base + block_data + j] = parity_buffer[j];
        }
        payload_offset += (usize)block_data;
    }
    if (!writer.write_bits((u64)ECC_HEADER_MAGIC, 16u)) {
        return false;
    }
    if (!writer.write_bits((u64)ECC_HEADER_VERSION, 8u)) {
        return false;
    }
    u8 flags = 0x01u;
    if (!writer.write_bits((u64)flags, 8u)) {
        return false;
    }
    if (!writer.write_bits((u64)block_data, 16u)) {
        return false;
    }
    if (!writer.write_bits((u64)parity_symbols, 16u)) {
        return false;
    }
    if (!writer.write_bits(0u, 16u)) {
        return false;
    }
    if (!writer.write_bits(block_count, 64u)) {
        return false;
    }
    if (!writer.write_bits((u64)compressed.size, 64u)) {
        return false;
    }
    for (usize i = 0u; i < encoded.size; ++i) {
        if (!writer.write_bits((u64)encoded.data[i], 8u)) {
            return false;
        }
    }
    summary.enabled = true;
    summary.block_data_symbols = block_data;
    summary.parity_symbols = parity_symbols;
    summary.block_count = block_count;
    summary.original_bytes = compressed.size;
    summary.redundancy = (block_data ? ((double)parity_symbols) / (double)block_data : 0.0);
    return true;
}

struct EccHeaderInfo {
    bool detected;
    bool valid;
    bool enabled;
    u16 block_data;
    u16 parity;
    u64 block_count;
    u64 original_bytes;

    EccHeaderInfo()
        : detected(false),
          valid(false),
          enabled(false),
          block_data(0u),
          parity(0u),
          block_count(0u),
          original_bytes(0u) {}
};

static bool build_ecc_header_bytes(u8* dest,
                                   usize dest_capacity,
                                   u16 block_data,
                                   u16 parity,
                                   u64 block_count,
                                   u64 original_bytes) {
    if (!dest || dest_capacity < ECC_HEADER_BYTES) {
        return false;
    }
    if (block_data == 0u || parity == 0u || block_count == 0u) {
        return false;
    }
    if (block_data >= RS_FIELD_SIZE || parity >= RS_FIELD_SIZE) {
        return false;
    }
    if ((u32)block_data + parity > RS_FIELD_SIZE) {
        return false;
    }
    dest[0] = (u8)(ECC_HEADER_MAGIC & 0xFFu);
    dest[1] = (u8)((ECC_HEADER_MAGIC >> 8u) & 0xFFu);
    dest[2] = ECC_HEADER_VERSION;
    dest[3] = 0x01u;
    write_le_u16(dest + 4u, block_data);
    write_le_u16(dest + 6u, parity);
    write_le_u16(dest + 8u, 0u);
    write_le_u64(dest + 10u, block_count);
    write_le_u64(dest + 18u, original_bytes);
    return true;
}

static bool parse_ecc_header(const u8* bytes,
                             usize byte_count,
                             EccHeaderInfo& header) {
    header = EccHeaderInfo();
    if (!bytes || byte_count < 2u) {
        return false;
    }
    u16 magic = read_le_u16(bytes);
    if (magic != ECC_HEADER_MAGIC) {
        return false;
    }
    header.detected = true;
    if (byte_count < ECC_HEADER_BYTES) {
        return false;
    }
    u8 version = bytes[2];
    u8 flags = bytes[3];
    u16 block_data = read_le_u16(bytes + 4u);
    u16 parity = read_le_u16(bytes + 6u);
    u16 reserved = read_le_u16(bytes + 8u);
    u64 block_count = read_le_u64(bytes + 10u);
    u64 original_bytes = read_le_u64(bytes + 18u);
    (void)reserved;
    if (version != ECC_HEADER_VERSION) {
        return false;
    }
    if (!(flags & 0x01u)) {
        return false;
    }
    if (block_data == 0u || parity == 0u || block_data >= RS_FIELD_SIZE || parity >= RS_FIELD_SIZE) {
        return false;
    }
    if ((u32)block_data + parity > RS_FIELD_SIZE) {
        return false;
    }
    if (block_count == 0u) {
        return false;
    }
    if (original_bytes > (u64)USIZE_MAX_VALUE) {
        return false;
    }
    header.valid = true;
    header.enabled = true;
    header.block_data = block_data;
    header.parity = parity;
    header.block_count = block_count;
    header.original_bytes = original_bytes;
    return true;
}

static bool decode_ecc_payload(const u8* bytes,
                               const EccHeaderInfo& header,
                               ByteBuffer& output) {
    if (!bytes || !header.valid || !header.enabled) {
        return false;
    }
    u16 block_total = (u16)(header.block_data + header.parity);
    u64 expected_bytes = (u64)block_total * header.block_count;
    if (expected_bytes > (u64)USIZE_MAX_VALUE) {
        return false;
    }
    if (!output.ensure((usize)header.original_bytes)) {
        return false;
    }
    output.size = (usize)header.original_bytes;
    u64 written = 0u;
    u8 block_buffer[RS_POLY_CAPACITY];
    for (u64 block_index = 0u; block_index < header.block_count; ++block_index) {
        usize offset = (usize)block_index * (usize)block_total;
        for (u16 i = 0u; i < block_total; ++i) {
            block_buffer[i] = bytes[offset + i];
        }
        if (!rs_decode_block(block_buffer, header.block_data, header.parity)) {
            return false;
        }
        u16 copy = header.block_data;
        if (written + copy > header.original_bytes) {
            copy = (u16)(header.original_bytes - written);
        }
        for (u16 i = 0u; i < copy; ++i) {
            output.data[(usize)written + i] = block_buffer[i];
        }
        written += copy;
        if (written >= header.original_bytes) {
            break;
        }
    }
    return (written == header.original_bytes);
}

static bool shuffle_encoded_stream(u8* data, usize byte_count, bool ecc_enabled) {
    if (!data || byte_count <= 1u) {
        return true;
    }
    if (!ecc_enabled) {
        return fisher_yates_shuffle(data, byte_count);
    }
    if (byte_count <= ECC_HEADER_BYTES) {
        return true;
    }
    return fisher_yates_shuffle(data + ECC_HEADER_BYTES, byte_count - ECC_HEADER_BYTES);
}

static bool unshuffle_encoded_stream(u8* data, usize byte_count) {
    if (!data || byte_count <= 1u) {
        return true;
    }
    bool treat_as_ecc = false;
    if (byte_count >= ECC_HEADER_BYTES) {
        u16 magic = read_le_u16(data);
        if (magic == ECC_HEADER_MAGIC) {
            treat_as_ecc = true;
        }
    }
    EccHeaderInfo header_probe;
    if (parse_ecc_header(data, byte_count, header_probe) && header_probe.valid && header_probe.enabled) {
        treat_as_ecc = true;
    }
    if (!treat_as_ecc) {
        return fisher_yates_unshuffle(data, byte_count);
    }
    if (byte_count <= ECC_HEADER_BYTES) {
        return true;
    }
    return fisher_yates_unshuffle(data + ECC_HEADER_BYTES, byte_count - ECC_HEADER_BYTES);
}

struct DecoderContext {
    ByteBuffer payload;
    bool has_payload;
    bool ecc_failed;
    bool password_attempted;
    bool password_failed;
    bool password_not_encrypted;

    DecoderContext()
        : payload(),
          has_payload(false),
          ecc_failed(false),
          password_attempted(false),
          password_failed(false),
          password_not_encrypted(false) {}

    void reset() {
        payload.release();
        has_payload = false;
        ecc_failed = false;
        password_attempted = false;
        password_failed = false;
        password_not_encrypted = false;
    }

    bool parse(u8* data, usize size_in_bits, const char* password, usize password_length) {
        payload.release();
        has_payload = false;
        ecc_failed = false;
        password_attempted = false;
        password_failed = false;
        password_not_encrypted = false;
        if (size_in_bits == 0u) {
            has_payload = true;
            return true;
        }
        if (!data) {
            return false;
        }
        usize byte_count = (size_in_bits + 7u) >> 3u;
        if (byte_count > 0u && !unshuffle_encoded_stream(data, byte_count)) {
            return false;
        }
        EccHeaderInfo header;
        bool header_ok = parse_ecc_header(data, byte_count, header);
        bool have_password = (password && password_length > 0u);
        if (header_ok && header.valid && header.enabled) {
            u16 block_total = (u16)(header.block_data + header.parity);
            u64 expected_bytes = (u64)block_total * header.block_count;
            u64 available_bytes = (u64)byte_count;
            if (available_bytes < (u64)ECC_HEADER_BYTES + expected_bytes) {
                ecc_failed = true;
                return false;
            }
            const u8* encoded = data + ECC_HEADER_BYTES;
            ByteBuffer compressed;
            if (!decode_ecc_payload(encoded, header, compressed)) {
                char debug_buffer[128];
                console_line(2, "debug parse: decode_ecc_payload failure");
                if (header.block_data || header.parity || header.block_count) {
                    console_write(2, "debug parse: block_data=");
                    u64_to_ascii((u64)header.block_data, debug_buffer, sizeof(debug_buffer));
                    console_line(2, debug_buffer);
                    console_write(2, "debug parse: parity=");
                    u64_to_ascii((u64)header.parity, debug_buffer, sizeof(debug_buffer));
                    console_line(2, debug_buffer);
                    console_write(2, "debug parse: block_count=");
                    u64_to_ascii(header.block_count, debug_buffer, sizeof(debug_buffer));
                    console_line(2, debug_buffer);
                    console_write(2, "debug parse: original_bytes=");
                    u64_to_ascii(header.original_bytes, debug_buffer, sizeof(debug_buffer));
                    console_line(2, debug_buffer);
                    console_write(2, "debug parse: byte_count=");
                    u64_to_ascii((u64)byte_count, debug_buffer, sizeof(debug_buffer));
                    console_line(2, debug_buffer);
                    console_write(2, "debug parse: expected_bytes=");
                    u64_to_ascii((u64)((u64)(header.block_data + header.parity) * header.block_count), debug_buffer, sizeof(debug_buffer));
                    console_line(2, debug_buffer);
                }
                ecc_failed = true;
                return false;
            }
            const ByteBuffer* working = &compressed;
            ByteBuffer decrypted;
            if (have_password) {
                password_attempted = true;
                DecryptStatus status = decrypt_payload_buffer(compressed.data,
                                                              compressed.size,
                                                              password,
                                                              password_length,
                                                              decrypted);
                if (status == DecryptStatus_NotEncrypted) {
                    password_not_encrypted = true;
                } else if (status == DecryptStatus_Success) {
                    working = &decrypted;
                } else {
                    password_failed = true;
                    return false;
                }
            }
            if (!working->size) {
                has_payload = true;
                return true;
            }
            u64 bit_total = (u64)working->size * 8u;
            if (bit_total > (u64)USIZE_MAX_VALUE) {
                ecc_failed = true;
                return false;
            }
            if (!lzw_decompress(working->data, (usize)bit_total, payload)) {
                ecc_failed = true;
                return false;
            }
            has_payload = true;
            return true;
        }
        if (header.detected && !header.valid) {
            ecc_failed = true;
            return false;
        }
        const u8* decode_data = data;
        usize decode_bits = size_in_bits;
        ByteBuffer decrypted;
        if (have_password) {
            password_attempted = true;
            DecryptStatus status = decrypt_payload_buffer(data,
                                                          byte_count,
                                                          password,
                                                          password_length,
                                                          decrypted);
            if (status == DecryptStatus_NotEncrypted) {
                password_not_encrypted = true;
            } else if (status == DecryptStatus_Success) {
                decode_data = decrypted.data;
                decode_bits = decrypted.size * 8u;
            } else {
                password_failed = true;
                return false;
            }
        }
        if (decode_bits == 0u) {
            has_payload = true;
            return true;
        }
        if (!lzw_decompress(decode_data, decode_bits, payload)) {
            return false;
        }
        has_payload = true;
        return true;
    }

    bool ecc_correction_failed() const {
        return ecc_failed;
    }

    bool password_attempt_made() const {
        return password_attempted;
    }

    bool password_auth_failed() const {
        return password_failed;
    }

    bool password_was_ignored() const {
        return password_not_encrypted;
    }
};

} // namespace makocode

static const u32 DEFAULT_PAGE_WIDTH_PIXELS  = 2480u; // matches prior A4 default
static const u32 DEFAULT_PAGE_HEIGHT_PIXELS = 3508u; // matches prior A4 default

static void byte_buffer_move(makocode::ByteBuffer& dest, makocode::ByteBuffer& src) {
    dest.release();
    dest.data = src.data;
    dest.size = src.size;
    dest.capacity = src.capacity;
    src.data = 0;
    src.size = 0;
    src.capacity = 0;
}

struct ImageMappingConfig {
    u8  color_channels;
    bool color_set;
    u32 page_width_pixels;
    bool page_width_set;
    u32 page_height_pixels;
    bool page_height_set;

    ImageMappingConfig()
        : color_channels(1u),
          color_set(false),
          page_width_pixels(DEFAULT_PAGE_WIDTH_PIXELS),
          page_width_set(false),
          page_height_pixels(DEFAULT_PAGE_HEIGHT_PIXELS),
          page_height_set(false) {}
};

static bool compute_page_dimensions(const ImageMappingConfig& config,
                                    u32& out_width_pixels,
                                    u32& out_height_pixels) {
    u64 width_pixels = (u64)config.page_width_pixels;
    u64 height_pixels = (u64)config.page_height_pixels;
    if (width_pixels == 0u || height_pixels == 0u) {
        return false;
    }
    if (width_pixels > 0xFFFFFFFFu || height_pixels > 0xFFFFFFFFu) {
        return false;
    }
    out_width_pixels = config.page_width_pixels;
    out_height_pixels = config.page_height_pixels;
    return true;
}

static const u32 FOOTER_BASE_GLYPH_WIDTH  = 5u;
static const u32 FOOTER_BASE_GLYPH_HEIGHT = 7u;

struct PageFooterConfig {
    const char* title_text;
    usize title_length;
    const char* filename_text;
    usize filename_length;
    u32 font_size;
    usize max_text_length;
    bool has_title;
    bool has_filename;
    bool display_page_info;
    bool display_filename;

    PageFooterConfig()
        : title_text(0),
          title_length(0u),
          filename_text(0),
          filename_length(0u),
          font_size(1u),
          max_text_length(0u),
          has_title(false),
          has_filename(false),
          display_page_info(true),
          display_filename(true) {}
};

struct FiducialGridDefaults {
    u32 marker_size_pixels;
    u32 spacing_pixels;
    u32 margin_pixels;

    FiducialGridDefaults()
        : marker_size_pixels(4u),
          spacing_pixels(24u),
          margin_pixels(12u) {}
};

static FiducialGridDefaults g_fiducial_defaults;

struct FooterLayout {
    bool has_text;
    u32 font_size;
    u32 glyph_width_pixels;
    u32 glyph_height_pixels;
    u32 char_spacing_pixels;
    u32 footer_height_pixels;
    u32 data_height_pixels;
    u32 text_top_row;
    u32 text_left_column;
    u32 text_pixel_width;

    FooterLayout()
        : has_text(false),
          font_size(0u),
          glyph_width_pixels(0u),
          glyph_height_pixels(0u),
          char_spacing_pixels(0u),
          footer_height_pixels(0u),
          data_height_pixels(0u),
          text_top_row(0u),
          text_left_column(0u),
          text_pixel_width(0u) {}
};

struct GlyphPattern {
    char symbol;
    const char* rows[FOOTER_BASE_GLYPH_HEIGHT];
};

static const GlyphPattern FOOTER_GLYPHS[] = {
    {' ', {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
    {'!', {"00100", "00100", "00100", "00100", "00100", "00000", "00100"}},
    {'"', {"01010", "01010", "00000", "00000", "00000", "00000", "00000"}},
    {'#', {"01010", "01010", "11111", "01010", "11111", "01010", "01010"}},
    {'$', {"00100", "01111", "10100", "01110", "00101", "11110", "00100"}},
    {'%', {"11001", "11001", "00010", "00100", "01000", "10011", "10011"}},
    {'&', {"01100", "10010", "10100", "01000", "10101", "10010", "01101"}},
    {'\'', {"00100", "00100", "00000", "00000", "00000", "00000", "00000"}},
    {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
    {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
    {'*', {"00000", "00100", "10101", "01110", "10101", "00100", "00000"}},
    {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
    {',', {"00000", "00000", "00000", "00000", "00100", "00100", "01000"}},
    {'-', {"00000", "00000", "11111", "00000", "00000", "00000", "00000"}},
    {'.', {"00000", "00000", "00000", "00000", "00000", "00100", "00000"}},
    {'/', {"00001", "00010", "00100", "01000", "10000", "00000", "00000"}},
    {'0', {"01110", "10001", "11001", "10101", "10011", "10001", "01110"}},
    {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
    {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
    {'3', {"01110", "10001", "00001", "00110", "00001", "10001", "01110"}},
    {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
    {'5', {"11111", "10000", "11110", "00001", "00001", "10001", "01110"}},
    {'6', {"01110", "10000", "11110", "10001", "10001", "10001", "01110"}},
    {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
    {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
    {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
    {':', {"00000", "00100", "00000", "00000", "00100", "00000", "00000"}},
    {';', {"00000", "00100", "00000", "00000", "00100", "00100", "01000"}},
    {'<', {"00010", "00100", "01000", "10000", "01000", "00100", "00010"}},
    {'=', {"00000", "11111", "00000", "11111", "00000", "00000", "00000"}},
    {'>', {"01000", "00100", "00010", "00001", "00010", "00100", "01000"}},
    {'?', {"01110", "10001", "00010", "00100", "00100", "00000", "00100"}},
    {'@', {"01110", "10001", "10111", "10101", "10111", "10000", "01110"}},
    {'[', {"01110", "01000", "01000", "01000", "01000", "01000", "01110"}},
    {'\\', {"10000", "01000", "00100", "00010", "00001", "00000", "00000"}},
    {']', {"01110", "00010", "00010", "00010", "00010", "00010", "01110"}},
    {'^', {"00100", "01010", "10001", "00000", "00000", "00000", "00000"}},
    {'_', {"00000", "00000", "00000", "00000", "00000", "11111", "00000"}},
    {'`', {"00100", "00010", "00000", "00000", "00000", "00000", "00000"}},
    {'{', {"00011", "00100", "00100", "01000", "00100", "00100", "00011"}},
    {'|', {"00100", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'}', {"11000", "00100", "00100", "00010", "00100", "00100", "11000"}},
    {'~', {"00000", "00000", "01001", "10110", "00000", "00000", "00000"}},
    {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
    {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
    {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
    {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
    {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
    {'G', {"01110", "10001", "10000", "10000", "10011", "10001", "01110"}},
    {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
    {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
    {'J', {"00111", "00010", "00010", "00010", "10010", "10010", "01100"}},
    {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
    {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
    {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
    {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
    {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
    {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
    {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
    {'S', {"01110", "10001", "10000", "01110", "00001", "10001", "01110"}},
    {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
    {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
    {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
    {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
    {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
    {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
    {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
    {'a', {"00000", "00000", "01110", "00001", "01111", "10001", "01111"}},
    {'b', {"10000", "10000", "11110", "10001", "10001", "10001", "11110"}},
    {'c', {"00000", "00000", "01110", "10001", "10000", "10001", "01110"}},
    {'d', {"00001", "00001", "01111", "10001", "10001", "10001", "01111"}},
    {'e', {"00000", "00000", "01110", "10001", "11111", "10000", "01110"}},
    {'f', {"00110", "01001", "01000", "11110", "01000", "01000", "01000"}},
    {'g', {"00000", "01110", "10001", "10001", "01111", "00001", "01110"}},
    {'h', {"10000", "10000", "11110", "10001", "10001", "10001", "10001"}},
    {'i', {"00100", "00000", "01100", "00100", "00100", "00100", "01110"}},
    {'j', {"00010", "00000", "00110", "00010", "00010", "00010", "01100"}},
    {'k', {"10000", "10000", "10010", "10100", "11000", "10100", "10010"}},
    {'l', {"01100", "00100", "00100", "00100", "00100", "00100", "01110"}},
    {'m', {"00000", "00000", "11010", "10101", "10101", "10101", "10101"}},
    {'n', {"00000", "00000", "11110", "10001", "10001", "10001", "10001"}},
    {'o', {"00000", "00000", "01110", "10001", "10001", "10001", "01110"}},
    {'p', {"00000", "00000", "11110", "10001", "10001", "11110", "10000"}},
    {'q', {"00000", "00000", "01110", "10001", "10001", "01111", "00001"}},
    {'r', {"00000", "00000", "10110", "11001", "10000", "10000", "10000"}},
    {'s', {"00000", "00000", "01110", "10000", "01110", "00001", "11110"}},
    {'t', {"01000", "01000", "11110", "01000", "01000", "01001", "00110"}},
    {'u', {"00000", "00000", "10001", "10001", "10001", "10011", "01101"}},
    {'v', {"00000", "00000", "10001", "10001", "10001", "01010", "00100"}},
    {'w', {"00000", "00000", "10001", "10001", "10101", "11011", "10001"}},
    {'x', {"00000", "00000", "10001", "01010", "00100", "01010", "10001"}},
    {'y', {"00000", "00000", "10001", "10001", "10001", "01111", "00001"}},
    {'z', {"00000", "00000", "11111", "00010", "00100", "01000", "11111"}}
};

static const usize FOOTER_GLYPH_COUNT = (usize)(sizeof(FOOTER_GLYPHS) / sizeof(FOOTER_GLYPHS[0]));

static const GlyphPattern* footer_lookup_glyph(char c) {
    for (usize i = 0u; i < FOOTER_GLYPH_COUNT; ++i) {
        if (FOOTER_GLYPHS[i].symbol == c) {
            return &FOOTER_GLYPHS[i];
        }
    }
    if (c >= 'a' && c <= 'z') {
        char upper = (char)(c - 'a' + 'A');
        for (usize i = 0u; i < FOOTER_GLYPH_COUNT; ++i) {
            if (FOOTER_GLYPHS[i].symbol == upper) {
                return &FOOTER_GLYPHS[i];
            }
        }
    }
    return 0;
}

static bool compute_footer_layout(u32 page_width_pixels,
                                  u32 page_height_pixels,
                                  const PageFooterConfig& footer,
                                  FooterLayout& layout) {
    layout = FooterLayout();
    layout.font_size = footer.font_size;
    layout.data_height_pixels = page_height_pixels;
    if (footer.max_text_length == 0u) {
        return true;
    }
    if (footer.font_size == 0u) {
        return false;
    }
    if (page_width_pixels == 0u || page_height_pixels == 0u) {
        return false;
    }
    if (footer.max_text_length > (USIZE_MAX_VALUE / FOOTER_BASE_GLYPH_WIDTH)) {
        return false;
    }
    if (footer.font_size > 2048u) {
        return false;
    }
    u32 scale = footer.font_size;
    u64 glyph_width_pixels = (u64)FOOTER_BASE_GLYPH_WIDTH * (u64)scale;
    u64 glyph_height_pixels = (u64)FOOTER_BASE_GLYPH_HEIGHT * (u64)scale;
    u64 char_spacing_pixels = (u64)scale;
    u64 top_margin_pixels = (u64)scale;
    u64 bottom_margin_pixels = (u64)scale;
    u64 text_pixel_width = (u64)footer.max_text_length * glyph_width_pixels;
    if (footer.max_text_length > 1u) {
        text_pixel_width += (u64)(footer.max_text_length - 1u) * char_spacing_pixels;
    }
    if (text_pixel_width > (u64)page_width_pixels) {
        return false;
    }
    u64 footer_height_pixels = glyph_height_pixels + top_margin_pixels + bottom_margin_pixels;
    if (footer_height_pixels >= (u64)page_height_pixels) {
        return false;
    }
    u32 footer_height_u32 = (u32)footer_height_pixels;
    u32 data_height = page_height_pixels - footer_height_u32;
    if (data_height == 0u) {
        return false;
    }
    u32 text_width_u32 = (u32)text_pixel_width;
    u32 text_left = 0u;
    if (page_width_pixels > text_width_u32) {
        text_left = (page_width_pixels - text_width_u32) / 2u;
    }
    u32 text_top = data_height + (u32)top_margin_pixels;
    layout.has_text = true;
    layout.font_size = scale;
    layout.glyph_width_pixels = (u32)glyph_width_pixels;
    layout.glyph_height_pixels = (u32)glyph_height_pixels;
    layout.char_spacing_pixels = (u32)char_spacing_pixels;
    layout.footer_height_pixels = footer_height_u32;
    layout.data_height_pixels = data_height;
    layout.text_top_row = text_top;
    layout.text_left_column = text_left;
    layout.text_pixel_width = text_width_u32;
    return true;
}

static bool footer_is_text_pixel(const char* text,
                                 usize text_length,
                                 const FooterLayout& layout,
                                 u32 column,
                                 u32 row) {
    if (!text || text_length == 0u || !layout.has_text) {
        return false;
    }
    if (layout.font_size == 0u) {
        return false;
    }
    if (row < layout.text_top_row || row >= (layout.text_top_row + layout.glyph_height_pixels)) {
        return false;
    }
    if (column < layout.text_left_column || column >= (layout.text_left_column + layout.text_pixel_width)) {
        return false;
    }
    u32 char_span = layout.glyph_width_pixels + layout.char_spacing_pixels;
    if (char_span == 0u) {
        return false;
    }
    u32 local_x = column - layout.text_left_column;
    u32 glyph_index = local_x / char_span;
    if (glyph_index >= text_length) {
        return false;
    }
    u32 within_char = local_x - glyph_index * char_span;
    if (within_char >= layout.glyph_width_pixels) {
        return false;
    }
    u32 local_y = row - layout.text_top_row;
    u32 glyph_x = within_char / layout.font_size;
    u32 glyph_y = local_y / layout.font_size;
    if (glyph_x >= FOOTER_BASE_GLYPH_WIDTH || glyph_y >= FOOTER_BASE_GLYPH_HEIGHT) {
        return false;
    }
    const GlyphPattern* glyph = footer_lookup_glyph(text[glyph_index]);
    if (!glyph) {
        return false;
    }
    const char* row_pattern = glyph->rows[glyph_y];
    if (!row_pattern) {
        return false;
    }
    char bit = row_pattern[glyph_x];
    return (bit == '1');
}

static u8 color_mode_samples_per_pixel(u8 mode) {
    if (mode >= 1u && mode <= 3u) {
        return 1u;
    }
    return 0u;
}

struct PaletteColor {
    u8 r;
    u8 g;
    u8 b;
};

static const PaletteColor PALETTE_GRAY[2] = {
    {0u, 0u, 0u},     // black
    {255u, 255u, 255u} // white
};

static const PaletteColor PALETTE_CMYW[4] = {
    {255u, 255u, 255u}, // white
    {0u, 255u, 255u},   // cyan
    {255u, 0u, 255u},   // magenta
    {255u, 255u, 0u}    // yellow
};

static const PaletteColor PALETTE_RGB_CMY_WB[8] = {
    {255u, 255u, 255u}, // white
    {0u, 0u, 0u},       // black
    {255u, 0u, 0u},     // red
    {0u, 255u, 0u},     // green
    {0u, 0u, 255u},     // blue
    {0u, 255u, 255u},   // cyan
    {255u, 0u, 255u},   // magenta
    {255u, 255u, 0u}    // yellow
};

static bool palette_for_mode(u8 mode, const PaletteColor*& colors, u32& count) {
    if (mode == 1u) {
        colors = PALETTE_GRAY;
        count = (u32)(sizeof(PALETTE_GRAY) / sizeof(PALETTE_GRAY[0]));
        return true;
    }
    if (mode == 2u) {
        colors = PALETTE_CMYW;
        count = (u32)(sizeof(PALETTE_CMYW) / sizeof(PALETTE_CMYW[0]));
        return true;
    }
    if (mode == 3u) {
        colors = PALETTE_RGB_CMY_WB;
        count = (u32)(sizeof(PALETTE_RGB_CMY_WB) / sizeof(PALETTE_RGB_CMY_WB[0]));
        return true;
    }
    colors = 0;
    count = 0u;
    return false;
}

static void footer_select_colors(u8 color_mode, u8* text_rgb, u8* background_rgb) {
    if (!text_rgb || !background_rgb) {
        return;
    }
    background_rgb[0] = 255u;
    background_rgb[1] = 255u;
    background_rgb[2] = 255u;
    text_rgb[0] = 0u;
    text_rgb[1] = 0u;
    text_rgb[2] = 0u;
    if (color_mode == 1u) {
        background_rgb[0] = PALETTE_GRAY[1].r;
        background_rgb[1] = PALETTE_GRAY[1].g;
        background_rgb[2] = PALETTE_GRAY[1].b;
        text_rgb[0] = PALETTE_GRAY[0].r;
        text_rgb[1] = PALETTE_GRAY[0].g;
        text_rgb[2] = PALETTE_GRAY[0].b;
        return;
    }
    if (color_mode == 2u) {
        background_rgb[0] = PALETTE_CMYW[0].r;
        background_rgb[1] = PALETTE_CMYW[0].g;
        background_rgb[2] = PALETTE_CMYW[0].b;
        text_rgb[0] = PALETTE_CMYW[1].r;
        text_rgb[1] = PALETTE_CMYW[1].g;
        text_rgb[2] = PALETTE_CMYW[1].b;
        return;
    }
    if (color_mode == 3u) {
        background_rgb[0] = PALETTE_RGB_CMY_WB[0].r;
        background_rgb[1] = PALETTE_RGB_CMY_WB[0].g;
        background_rgb[2] = PALETTE_RGB_CMY_WB[0].b;
        text_rgb[0] = PALETTE_RGB_CMY_WB[1].r;
        text_rgb[1] = PALETTE_RGB_CMY_WB[1].g;
        text_rgb[2] = PALETTE_RGB_CMY_WB[1].b;
        return;
    }
}

static u8 bits_per_sample(u8 mode) {
    if (mode == 1u) {
        return 1u;
    }
    if (mode == 2u) {
        return 2u;
    }
    if (mode == 3u) {
        return 3u;
    }
    return 0u;
}

static bool map_samples_to_rgb(u8 mode, const u32* samples, u8* rgb) {
    const PaletteColor* palette = 0;
    u32 palette_size = 0u;
    if (!palette_for_mode(mode, palette, palette_size)) {
        return false;
    }
    u8 samples_per_pixel = color_mode_samples_per_pixel(mode);
    if (samples_per_pixel != 1u) {
        return false;
    }
    u32 value = samples[0];
    if (mode == 1u) {
        if (value > 1u) {
            return false;
        }
        value ^= 1u;
    }
    if (value >= palette_size) {
        return false;
    }
    const PaletteColor& color = palette[value];
    rgb[0] = color.r;
    rgb[1] = color.g;
    rgb[2] = color.b;
    return true;
}

static bool map_rgb_to_samples(u8 mode, const u8* rgb, u32* samples) {
    const PaletteColor* palette = 0;
    u32 palette_size = 0u;
    if (!palette_for_mode(mode, palette, palette_size)) {
        return false;
    }
    u8 samples_per_pixel = color_mode_samples_per_pixel(mode);
    if (samples_per_pixel != 1u) {
        return false;
    }
    if (!samples) {
        return false;
    }
    u32 best_index = 0u;
    u64 best_score = (u64)USIZE_MAX_VALUE;
    for (u32 palette_index = 0u; palette_index < palette_size; ++palette_index) {
        const PaletteColor& candidate = palette[palette_index];
        i64 dr = (i64)candidate.r - (i64)rgb[0];
        i64 dg = (i64)candidate.g - (i64)rgb[1];
        i64 db = (i64)candidate.b - (i64)rgb[2];
        u64 score = (u64)(dr * dr + dg * dg + db * db);
        if (palette_index == 0u || score < best_score) {
            best_score = score;
            best_index = palette_index;
        }
    }
    if (mode == 1u) {
        if (best_index > 1u) {
            return false;
        }
        samples[0] = best_index ^ 1u;
        return true;
    }
    samples[0] = best_index;
    return true;
}

static u8 rotate_left_u8(u8 value, u8 amount) {
    amount &= 7u;
    if (!amount) {
        return value;
    }
    return (u8)((u8)(value << amount) | (u8)(value >> (8u - amount)));
}

static u8 rotate_right_u8(u8 value, u8 amount) {
    amount &= 7u;
    if (!amount) {
        return value;
    }
    return (u8)((u8)(value >> amount) | (u8)(value << (8u - amount)));
}

static bool write_all_fd(int fd, const u8* data, usize length) {
    if (length == 0u) {
        return true;
    }
    if (!data) {
        return false;
    }
    usize written = 0u;
    while (written < length) {
        unsigned long slice = (unsigned long)(length - written);
        int result = write(fd, data + written, slice);
        if (result <= 0) {
            return false;
        }
        written += (usize)result;
    }
    return true;
}

static bool write_bytes_to_file(const char* path, const u8* data, usize length) {
    if (!path) {
        return false;
    }
    int fd = creat(path, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    if (length) {
        ok = write_all_fd(fd, data, length);
    }
    close(fd);
    return ok;
}

static bool write_ppm_with_fiducials_to_file(const char* path, const makocode::ByteBuffer& buffer);

static bool buffer_contains_fiducial_metadata(const makocode::ByteBuffer& buffer) {
    if (!buffer.data || buffer.size == 0u) {
        return false;
    }
    static const char fiducial_prefix[] = "MAKOCODE_FIDUCIAL";
    const usize prefix_length = (usize)sizeof(fiducial_prefix) - 1u;
    if (buffer.size < prefix_length) {
        return false;
    }
    for (usize i = 0u; i <= buffer.size - prefix_length; ++i) {
        bool match = true;
        for (usize j = 0u; j < prefix_length; ++j) {
            if (buffer.data[i + j] != (u8)fiducial_prefix[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static bool path_contains_substring(const char* path, const char* needle) {
    if (!path || !needle || !needle[0]) {
        return false;
    }
    for (const char* cursor = path; *cursor; ++cursor) {
        const char* a = cursor;
        const char* b = needle;
        while (*a && *b && (*a == *b)) {
            ++a;
            ++b;
        }
        if (*b == '\0') {
            return true;
        }
    }
    return false;
}

static bool should_embed_fiducials_for_path(const char* path, const makocode::ByteBuffer& buffer) {
    if (!path) {
        return false;
    }
    usize path_length = ascii_length(path);
    if (path_length < 4u) {
        return false;
    }
    const char* suffix = path + (path_length - 4u);
    if (suffix[0] != '.' || suffix[1] != 'p' || suffix[2] != 'p' || suffix[3] != 'm') {
        return false;
    }
    if (!path_contains_substring(path, "_encoded")) {
        return false;
    }
    if (buffer_contains_fiducial_metadata(buffer)) {
        return false;
    }
    return true;
}

static bool write_buffer_to_file(const char* path, const makocode::ByteBuffer& buffer) {
    if (should_embed_fiducials_for_path(path, buffer)) {
        return write_ppm_with_fiducials_to_file(path, buffer);
    }
    return write_bytes_to_file(path, buffer.data, buffer.size);
}


struct PpmParserState {
    const u8* data;
    usize size;
    usize cursor;
    bool has_bytes;
    u64 bytes_value;
    bool has_bits;
    u64 bits_value;
    bool has_ecc_flag;
    u64 ecc_flag_value;
    bool has_ecc_block_data;
    u64 ecc_block_data_value;
    bool has_ecc_parity;
    u64 ecc_parity_value;
    bool has_ecc_block_count;
    u64 ecc_block_count_value;
    bool has_ecc_original_bytes;
    u64 ecc_original_bytes_value;
    bool has_color_channels;
    u64 color_channels_value;
    bool has_page_width_pixels;
    u64 page_width_pixels_value;
    bool has_page_height_pixels;
    u64 page_height_pixels_value;
    bool has_page_index;
    u64 page_index_value;
    bool has_page_count;
    u64 page_count_value;
    bool has_page_bits;
    u64 page_bits_value;
    bool has_footer_rows;
    u64 footer_rows_value;
    bool has_font_size;
    u64 font_size_value;
    bool has_rotation_degrees;
    double rotation_degrees_value;
    bool has_rotation_width;
    u64 rotation_width_value;
    bool has_rotation_height;
    u64 rotation_height_value;
    bool has_rotation_margin;
    double rotation_margin_value;
    bool has_skew_src_width;
    u64 skew_src_width_value;
    bool has_skew_src_height;
    u64 skew_src_height_value;
    bool has_skew_margin_x;
    double skew_margin_x_value;
    bool has_skew_x_pixels;
    double skew_x_pixels_value;
    bool has_skew_bottom_x;
    double skew_bottom_x_value;
    bool has_fiducial_size;
    u64 fiducial_size_value;
    bool has_fiducial_columns;
    u64 fiducial_columns_value;
    bool has_fiducial_rows;
    u64 fiducial_rows_value;
    bool has_fiducial_margin;
    u64 fiducial_margin_value;

    PpmParserState()
        : data(0),
          size(0u),
          cursor(0u),
          has_bytes(false),
          bytes_value(0u),
          has_bits(false),
          bits_value(0u),
          has_ecc_flag(false),
          ecc_flag_value(0u),
          has_ecc_block_data(false),
          ecc_block_data_value(0u),
          has_ecc_parity(false),
          ecc_parity_value(0u),
          has_ecc_block_count(false),
          ecc_block_count_value(0u),
          has_ecc_original_bytes(false),
          ecc_original_bytes_value(0u),
          has_color_channels(false),
          color_channels_value(0u),
          has_page_width_pixels(false),
          page_width_pixels_value(0u),
          has_page_height_pixels(false),
          page_height_pixels_value(0u),
          has_page_index(false),
          page_index_value(0u),
          has_page_count(false),
          page_count_value(0u),
          has_page_bits(false),
          page_bits_value(0u),
          has_footer_rows(false),
          footer_rows_value(0u),
          has_font_size(false),
          font_size_value(0u),
          has_rotation_degrees(false),
          rotation_degrees_value(0.0),
          has_rotation_width(false),
          rotation_width_value(0u),
          has_rotation_height(false),
          rotation_height_value(0u),
          has_rotation_margin(false),
          rotation_margin_value(0.0),
          has_skew_src_width(false),
          skew_src_width_value(0u),
          has_skew_src_height(false),
          skew_src_height_value(0u),
          has_skew_margin_x(false),
          skew_margin_x_value(0.0),
          has_skew_x_pixels(false),
          skew_x_pixels_value(0.0),
          has_skew_bottom_x(false),
          skew_bottom_x_value(0.0),
          has_fiducial_size(false),
          fiducial_size_value(0u),
          has_fiducial_columns(false),
          fiducial_columns_value(0u),
          has_fiducial_rows(false),
          fiducial_rows_value(0u),
          has_fiducial_margin(false),
          fiducial_margin_value(0u) {}
};

static void ppm_consume_comment(PpmParserState& state, usize start, usize length) {
    const char* comment = (const char*)(state.data + start);
    usize index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char bytes_tag[] = "MAKOCODE_BYTES";
    const char bits_tag[] = "MAKOCODE_BITS";
    const char ecc_tag[] = "MAKOCODE_ECC";
    const char ecc_block_tag[] = "MAKOCODE_ECC_BLOCK_DATA";
    const char ecc_parity_tag[] = "MAKOCODE_ECC_PARITY";
    const char ecc_block_count_tag[] = "MAKOCODE_ECC_BLOCK_COUNT";
    const char ecc_original_tag[] = "MAKOCODE_ECC_ORIGINAL_BYTES";
    const char color_tag[] = "MAKOCODE_COLOR_CHANNELS";
    const char fiducial_size_tag[] = "MAKOCODE_FIDUCIAL_SIZE";
    const char fiducial_columns_tag[] = "MAKOCODE_FIDUCIAL_COLUMNS";
    const char fiducial_rows_tag[] = "MAKOCODE_FIDUCIAL_ROWS";
    const char fiducial_margin_tag[] = "MAKOCODE_FIDUCIAL_MARGIN";
    const usize bytes_tag_len = (usize)sizeof(bytes_tag) - 1u;
    const usize bits_tag_len = (usize)sizeof(bits_tag) - 1u;
    const usize ecc_tag_len = (usize)sizeof(ecc_tag) - 1u;
    const usize ecc_block_tag_len = (usize)sizeof(ecc_block_tag) - 1u;
    const usize ecc_parity_tag_len = (usize)sizeof(ecc_parity_tag) - 1u;
    const usize ecc_block_count_tag_len = (usize)sizeof(ecc_block_count_tag) - 1u;
    const usize ecc_original_tag_len = (usize)sizeof(ecc_original_tag) - 1u;
    const usize color_tag_len = (usize)sizeof(color_tag) - 1u;
    const usize fiducial_size_tag_len = (usize)sizeof(fiducial_size_tag) - 1u;
    const usize fiducial_columns_tag_len = (usize)sizeof(fiducial_columns_tag) - 1u;
    const usize fiducial_rows_tag_len = (usize)sizeof(fiducial_rows_tag) - 1u;
    const usize fiducial_margin_tag_len = (usize)sizeof(fiducial_margin_tag) - 1u;
    if ((length - index) >= bytes_tag_len) {
        bool match = true;
        for (usize i = 0u; i < bytes_tag_len; ++i) {
            if (comment[index + i] != bytes_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += bytes_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_bytes = true;
                    state.bytes_value = value;
                }
            }
            return;
        }
    }
    if ((length - index) >= bits_tag_len) {
        bool match = true;
        for (usize i = 0u; i < bits_tag_len; ++i) {
            if (comment[index + i] != bits_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += bits_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_bits = true;
                    state.bits_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= ecc_tag_len) {
        bool match = true;
        for (usize i = 0u; i < ecc_tag_len; ++i) {
            if (comment[index + i] != ecc_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            usize next = index + ecc_tag_len;
            if (next < length) {
                char next_char = comment[next];
                if (next_char != ' ' && next_char != '\t') {
                    match = false;
                }
            }
        }
        if (match) {
            index += ecc_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_ecc_flag = true;
                    state.ecc_flag_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= ecc_block_tag_len) {
        bool match = true;
        for (usize i = 0u; i < ecc_block_tag_len; ++i) {
            if (comment[index + i] != ecc_block_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            usize next = index + ecc_block_tag_len;
            if (next < length) {
                char next_char = comment[next];
                if (next_char != ' ' && next_char != '\t') {
                    match = false;
                }
            }
        }
        if (match) {
            index += ecc_block_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_ecc_block_data = true;
                    state.ecc_block_data_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= ecc_parity_tag_len) {
        bool match = true;
        for (usize i = 0u; i < ecc_parity_tag_len; ++i) {
            if (comment[index + i] != ecc_parity_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            usize next = index + ecc_parity_tag_len;
            if (next < length) {
                char next_char = comment[next];
                if (next_char != ' ' && next_char != '\t') {
                    match = false;
                }
            }
        }
        if (match) {
            index += ecc_parity_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_ecc_parity = true;
                    state.ecc_parity_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= ecc_block_count_tag_len) {
        bool match = true;
        for (usize i = 0u; i < ecc_block_count_tag_len; ++i) {
            if (comment[index + i] != ecc_block_count_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            usize next = index + ecc_block_count_tag_len;
            if (next < length) {
                char next_char = comment[next];
                if (next_char != ' ' && next_char != '\t') {
                    match = false;
                }
            }
        }
        if (match) {
            index += ecc_block_count_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_ecc_block_count = true;
                    state.ecc_block_count_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= ecc_original_tag_len) {
        bool match = true;
        for (usize i = 0u; i < ecc_original_tag_len; ++i) {
            if (comment[index + i] != ecc_original_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            usize next = index + ecc_original_tag_len;
            if (next < length) {
                char next_char = comment[next];
                if (next_char != ' ' && next_char != '\t') {
                    match = false;
                }
            }
        }
        if (match) {
            index += ecc_original_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_ecc_original_bytes = true;
                    state.ecc_original_bytes_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= color_tag_len) {
        bool match = true;
        for (usize i = 0u; i < color_tag_len; ++i) {
            if (comment[index + i] != color_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += color_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_color_channels = true;
                    state.color_channels_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= fiducial_size_tag_len) {
        bool match = true;
        for (usize i = 0u; i < fiducial_size_tag_len; ++i) {
            if (comment[index + i] != fiducial_size_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += fiducial_size_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_fiducial_size = true;
                    state.fiducial_size_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= fiducial_columns_tag_len) {
        bool match = true;
        for (usize i = 0u; i < fiducial_columns_tag_len; ++i) {
            if (comment[index + i] != fiducial_columns_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += fiducial_columns_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_fiducial_columns = true;
                    state.fiducial_columns_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= fiducial_rows_tag_len) {
        bool match = true;
        for (usize i = 0u; i < fiducial_rows_tag_len; ++i) {
            if (comment[index + i] != fiducial_rows_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += fiducial_rows_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_fiducial_rows = true;
                    state.fiducial_rows_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    if ((length - index) >= fiducial_margin_tag_len) {
        bool match = true;
        for (usize i = 0u; i < fiducial_margin_tag_len; ++i) {
            if (comment[index + i] != fiducial_margin_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += fiducial_margin_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_fiducial_margin = true;
                    state.fiducial_margin_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char width_tag[] = "MAKOCODE_PAGE_WIDTH_PX";
    const usize width_tag_len = (usize)sizeof(width_tag) - 1u;
    if ((length - index) >= width_tag_len) {
        bool match = true;
        for (usize i = 0u; i < width_tag_len; ++i) {
            if (comment[index + i] != width_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += width_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_page_width_pixels = true;
                    state.page_width_pixels_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char height_tag[] = "MAKOCODE_PAGE_HEIGHT_PX";
    const usize height_tag_len = (usize)sizeof(height_tag) - 1u;
    if ((length - index) >= height_tag_len) {
        bool match = true;
        for (usize i = 0u; i < height_tag_len; ++i) {
            if (comment[index + i] != height_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += height_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_page_height_pixels = true;
                    state.page_height_pixels_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char page_index_tag[] = "MAKOCODE_PAGE_INDEX";
    const usize page_index_tag_len = (usize)sizeof(page_index_tag) - 1u;
    if ((length - index) >= page_index_tag_len) {
        bool match = true;
        for (usize i = 0u; i < page_index_tag_len; ++i) {
            if (comment[index + i] != page_index_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += page_index_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_page_index = true;
                    state.page_index_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char page_count_tag[] = "MAKOCODE_PAGE_COUNT";
    const usize page_count_tag_len = (usize)sizeof(page_count_tag) - 1u;
    if ((length - index) >= page_count_tag_len) {
        bool match = true;
        for (usize i = 0u; i < page_count_tag_len; ++i) {
            if (comment[index + i] != page_count_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += page_count_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_page_count = true;
                    state.page_count_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char page_bits_tag[] = "MAKOCODE_PAGE_BITS";
    const usize page_bits_tag_len = (usize)sizeof(page_bits_tag) - 1u;
    if ((length - index) >= page_bits_tag_len) {
        bool match = true;
        for (usize i = 0u; i < page_bits_tag_len; ++i) {
            if (comment[index + i] != page_bits_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += page_bits_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_page_bits = true;
                    state.page_bits_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char footer_rows_tag[] = "MAKOCODE_FOOTER_ROWS";
    const usize footer_rows_tag_len = (usize)sizeof(footer_rows_tag) - 1u;
    if ((length - index) >= footer_rows_tag_len) {
        bool match = true;
        for (usize i = 0u; i < footer_rows_tag_len; ++i) {
            if (comment[index + i] != footer_rows_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += footer_rows_tag_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_footer_rows = true;
                    state.footer_rows_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char font_size_tag[] = "MAKOCODE_FONT_SIZE";
    const usize font_size_tag_len = (usize)sizeof(font_size_tag) - 1u;
    const char legacy_font_tag[] = "MAKOCODE_TITLE_FONT";
    const usize legacy_font_tag_len = (usize)sizeof(legacy_font_tag) - 1u;
    bool matched_font_tag = false;
    if ((length - index) >= font_size_tag_len) {
        matched_font_tag = true;
        for (usize i = 0u; i < font_size_tag_len; ++i) {
            if (comment[index + i] != font_size_tag[i]) {
                matched_font_tag = false;
                break;
            }
        }
        if (matched_font_tag) {
            index += font_size_tag_len;
        }
    }
    if (!matched_font_tag && (length - index) >= legacy_font_tag_len) {
        matched_font_tag = true;
        for (usize i = 0u; i < legacy_font_tag_len; ++i) {
            if (comment[index + i] != legacy_font_tag[i]) {
                matched_font_tag = false;
                break;
            }
        }
        if (matched_font_tag) {
            index += legacy_font_tag_len;
        }
    }
    if (matched_font_tag) {
        while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
            ++index;
        }
        usize number_start = index;
        while (index < length) {
            char c = comment[index];
            if (c < '0' || c > '9') {
                break;
            }
            ++index;
        }
        usize number_length = index - number_start;
        if (number_length) {
            u64 value = 0u;
            if (ascii_to_u64(comment + number_start, number_length, &value)) {
                state.has_font_size = true;
                state.font_size_value = value;
            }
        }
        return;
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char rotation_deg_tag[] = "rotation_deg";
    const usize rotation_deg_len = (usize)sizeof(rotation_deg_tag) - 1u;
    if ((length - index) >= rotation_deg_len) {
        bool match = true;
        for (usize i = 0u; i < rotation_deg_len; ++i) {
            if (comment[index + i] != rotation_deg_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += rotation_deg_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            bool negative = false;
            if (index < length && comment[index] == '-') {
                negative = true;
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if ((c < '0' || c > '9') && c != '.') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                double value = 0.0;
                if (ascii_to_double(comment + number_start, number_length, &value)) {
                    if (negative) {
                        value = -value;
                    }
                    state.has_rotation_degrees = true;
                    state.rotation_degrees_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char rotation_width_tag[] = "rotation_src_width";
    const usize rotation_width_len = (usize)sizeof(rotation_width_tag) - 1u;
    if ((length - index) >= rotation_width_len) {
        bool match = true;
        for (usize i = 0u; i < rotation_width_len; ++i) {
            if (comment[index + i] != rotation_width_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += rotation_width_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_rotation_width = true;
                    state.rotation_width_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char rotation_height_tag[] = "rotation_src_height";
    const usize rotation_height_len = (usize)sizeof(rotation_height_tag) - 1u;
    if ((length - index) >= rotation_height_len) {
        bool match = true;
        for (usize i = 0u; i < rotation_height_len; ++i) {
            if (comment[index + i] != rotation_height_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += rotation_height_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_rotation_height = true;
                    state.rotation_height_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char rotation_margin_tag[] = "rotation_margin";
    const usize rotation_margin_len = (usize)sizeof(rotation_margin_tag) - 1u;
    if ((length - index) >= rotation_margin_len) {
        bool match = true;
        for (usize i = 0u; i < rotation_margin_len; ++i) {
            if (comment[index + i] != rotation_margin_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += rotation_margin_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if ((c < '0' || c > '9') && c != '.') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                double value = 0.0;
                if (ascii_to_double(comment + number_start, number_length, &value)) {
                    state.has_rotation_margin = true;
                    state.rotation_margin_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char skew_src_width_tag[] = "skew_src_width";
    const usize skew_src_width_len = (usize)sizeof(skew_src_width_tag) - 1u;
    if ((length - index) >= skew_src_width_len) {
        bool match = true;
        for (usize i = 0u; i < skew_src_width_len; ++i) {
            if (comment[index + i] != skew_src_width_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += skew_src_width_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_skew_src_width = true;
                    state.skew_src_width_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char skew_src_height_tag[] = "skew_src_height";
    const usize skew_src_height_len = (usize)sizeof(skew_src_height_tag) - 1u;
    if ((length - index) >= skew_src_height_len) {
        bool match = true;
        for (usize i = 0u; i < skew_src_height_len; ++i) {
            if (comment[index + i] != skew_src_height_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += skew_src_height_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if (c < '0' || c > '9') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                u64 value = 0u;
                if (ascii_to_u64(comment + number_start, number_length, &value)) {
                    state.has_skew_src_height = true;
                    state.skew_src_height_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char skew_margin_tag[] = "skew_margin_x";
    const usize skew_margin_len = (usize)sizeof(skew_margin_tag) - 1u;
    if ((length - index) >= skew_margin_len) {
        bool match = true;
        for (usize i = 0u; i < skew_margin_len; ++i) {
            if (comment[index + i] != skew_margin_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += skew_margin_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            bool negative = false;
            if (index < length && comment[index] == '-') {
                negative = true;
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if ((c < '0' || c > '9') && c != '.') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                double value = 0.0;
                if (ascii_to_double(comment + number_start, number_length, &value)) {
                    if (negative) {
                        value = -value;
                    }
                    state.has_skew_margin_x = true;
                    state.skew_margin_x_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char skew_amount_tag[] = "skew_x_pixels";
    const usize skew_amount_len = (usize)sizeof(skew_amount_tag) - 1u;
    if ((length - index) >= skew_amount_len) {
        bool match = true;
        for (usize i = 0u; i < skew_amount_len; ++i) {
            if (comment[index + i] != skew_amount_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += skew_amount_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            bool negative = false;
            if (index < length && comment[index] == '-') {
                negative = true;
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if ((c < '0' || c > '9') && c != '.') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                double value = 0.0;
                if (ascii_to_double(comment + number_start, number_length, &value)) {
                    if (negative) {
                        value = -value;
                    }
                    state.has_skew_x_pixels = true;
                    state.skew_x_pixels_value = value;
                }
            }
            return;
        }
    }
    index = 0u;
    while (index < length) {
        char c = comment[index];
        if (c != ' ' && c != '\t') {
            break;
        }
        ++index;
    }
    const char skew_bottom_tag[] = "skew_bottom_x";
    const usize skew_bottom_len = (usize)sizeof(skew_bottom_tag) - 1u;
    if ((length - index) >= skew_bottom_len) {
        bool match = true;
        for (usize i = 0u; i < skew_bottom_len; ++i) {
            if (comment[index + i] != skew_bottom_tag[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            index += skew_bottom_len;
            while (index < length && (comment[index] == ' ' || comment[index] == '\t')) {
                ++index;
            }
            bool negative = false;
            if (index < length && comment[index] == '-') {
                negative = true;
                ++index;
            }
            usize number_start = index;
            while (index < length) {
                char c = comment[index];
                if ((c < '0' || c > '9') && c != '.') {
                    break;
                }
                ++index;
            }
            usize number_length = index - number_start;
            if (number_length) {
                double value = 0.0;
                if (ascii_to_double(comment + number_start, number_length, &value)) {
                    if (negative) {
                        value = -value;
                    }
                    state.has_skew_bottom_x = true;
                    state.skew_bottom_x_value = value;
                }
            }
            return;
        }
    }
}

static bool ppm_next_token(PpmParserState& state, const char** out_start, usize* out_length) {
    while (state.cursor < state.size) {
        char current = (char)state.data[state.cursor];
        if (current == '#') {
            ++state.cursor;
            usize comment_start = state.cursor;
            while (state.cursor < state.size) {
                char c = (char)state.data[state.cursor];
                if (c == '\n' || c == '\r') {
                    break;
                }
                ++state.cursor;
            }
            usize comment_length = state.cursor - comment_start;
            ppm_consume_comment(state, comment_start, comment_length);
            while (state.cursor < state.size) {
                char newline_char = (char)state.data[state.cursor];
                if (newline_char == '\n' || newline_char == '\r') {
                    ++state.cursor;
                } else {
                    break;
                }
            }
            continue;
        }
        if (current <= ' ') {
            ++state.cursor;
            continue;
        }
        usize start = state.cursor;
        while (state.cursor < state.size) {
            char c = (char)state.data[state.cursor];
            if (c <= ' ' || c == '#') {
                break;
            }
            ++state.cursor;
        }
        *out_start = (const char*)(state.data + start);
        *out_length = state.cursor - start;
        return true;
    }
    return false;
}

static bool map_rgb_to_samples(u8 mode, const u8* rgb, u32* samples);

static u64 gcd_u64(u64 a, u64 b) {
    while (b) {
        u64 temp = a % b;
        a = b;
        b = temp;
    }
    return a ? a : b;
}

static bool rgb_within_tolerance(const u8* a, const u8* b, u8 tolerance) {
    if (!a || !b) {
        return false;
    }
    int dr = (int)a[0] - (int)b[0];
    if (dr < 0) {
        dr = -dr;
    }
    int dg = (int)a[1] - (int)b[1];
    if (dg < 0) {
        dg = -dg;
    }
    int db = (int)a[2] - (int)b[2];
    if (db < 0) {
        db = -db;
    }
    int limit = (int)tolerance;
    if (limit < 0) {
        limit = 0;
    }
    return dr <= limit && dg <= limit && db <= limit;
}

static bool ppm_read_rgb_pixels(PpmParserState& state,
                                u64 pixel_count,
                                makocode::ByteBuffer& pixel_buffer) {
    if (pixel_count == 0u) {
        return false;
    }
    u64 total_bytes = pixel_count * 3u;
    if (total_bytes > (u64)USIZE_MAX_VALUE) {
        return false;
    }
    if (!pixel_buffer.ensure((usize)total_bytes)) {
        return false;
    }
    for (u64 pixel = 0u; pixel < pixel_count; ++pixel) {
        for (u32 channel = 0u; channel < 3u; ++channel) {
            const char* token = 0;
            usize length = 0u;
            if (!ppm_next_token(state, &token, &length)) {
                return false;
            }
            u64 value = 0u;
            if (!ascii_to_u64(token, length, &value) || value > 255u) {
                return false;
            }
            pixel_buffer.data[pixel * 3u + channel] = (u8)value;
        }
    }
    pixel_buffer.size = (usize)total_bytes;
    return true;
}

static u64 detect_horizontal_scale_similarity(const u8* pixels, u64 width, u64 height) {
    if (!pixels || width == 0u || height == 0u) {
        return 1u;
    }
    u64 row_limit = (height < 256u) ? height : 256u;
    u64 scale = 0u;
    for (u64 row = 0u; row < row_limit; ++row) {
        const u8* row_ptr = pixels + (row * width * 3u);
        u64 run = 1u;
        for (u64 col = 1u; col < width; ++col) {
            const u8* current = row_ptr + col * 3u;
            const u8* previous = row_ptr + (col - 1u) * 3u;
            if (rgb_within_tolerance(current, previous, 6u)) {
                ++run;
            } else {
                scale = scale ? gcd_u64(scale, run) : run;
                run = 1u;
            }
            if (scale == 1u) {
                return 1u;
            }
        }
        scale = scale ? gcd_u64(scale, run) : run;
        if (scale == 1u) {
            return 1u;
        }
    }
    if (!scale || scale > width || scale == width) {
        return 1u;
    }
    if ((width % scale) != 0u) {
        scale = gcd_u64(scale, width);
        if (!scale || (width % scale) != 0u) {
            return 1u;
        }
    }
    return scale ? scale : 1u;
}

static u64 detect_vertical_scale_similarity(const u8* pixels, u64 width, u64 height) {
    if (!pixels || width == 0u || height == 0u) {
        return 1u;
    }
    u64 column_limit = (width < 256u) ? width : 256u;
    u64 row_limit = (height < 256u) ? height : 256u;
    u64 scale = 0u;
    for (u64 col = 0u; col < column_limit; ++col) {
        u64 run = 1u;
        for (u64 row = 1u; row < row_limit; ++row) {
            const u8* current = pixels + (row * width + col) * 3u;
            const u8* previous = pixels + ((row - 1u) * width + col) * 3u;
            if (rgb_within_tolerance(current, previous, 6u)) {
                ++run;
            } else {
                scale = scale ? gcd_u64(scale, run) : run;
                run = 1u;
            }
            if (scale == 1u) {
                return 1u;
            }
        }
        scale = scale ? gcd_u64(scale, run) : run;
        if (scale == 1u) {
            return 1u;
        }
    }
    if (!scale || scale > height || scale == height) {
        return 1u;
    }
    if ((height % scale) != 0u) {
        scale = gcd_u64(scale, height);
        if (!scale || (height % scale) != 0u) {
            return 1u;
        }
    }
    return scale ? scale : 1u;
}

static u64 detect_horizontal_scale_palette(const u8* pixels,
                                           u64 width,
                                           u64 height,
                                           u8 color_mode) {
    if (!pixels || width < 2u || height == 0u) {
        return 1u;
    }
    if (color_mode == 0u || color_mode > 3u) {
        return 1u;
    }
    u64 row_limit = (height < 256u) ? height : 256u;
    u64 scale = 0u;
    u64 run_support = 0u;
    for (u64 row = 0u; row < row_limit; ++row) {
        const u8* row_ptr = pixels + (row * width * 3u);
        u32 prev_sample = 0u;
        if (!map_rgb_to_samples(color_mode, row_ptr, &prev_sample)) {
            continue;
        }
        u64 run_length = 1u;
        bool row_ok = true;
        for (u64 col = 1u; col < width; ++col) {
            const u8* pixel_ptr = row_ptr + col * 3u;
            u32 current_sample = 0u;
            if (!map_rgb_to_samples(color_mode, pixel_ptr, &current_sample)) {
                row_ok = false;
                break;
            }
            if (current_sample == prev_sample) {
                ++run_length;
                continue;
            }
            if (run_length > 1u && run_length < width) {
                scale = scale ? gcd_u64(scale, run_length) : run_length;
                ++run_support;
                if (scale == 1u) {
                    return 1u;
                }
            }
            prev_sample = current_sample;
            run_length = 1u;
        }
        if (!row_ok) {
            continue;
        }
        if (run_length > 1u && run_length < width) {
            scale = scale ? gcd_u64(scale, run_length) : run_length;
            ++run_support;
            if (scale == 1u) {
                return 1u;
            }
        }
    }
    if (scale > 1u && run_support >= 4u) {
        if ((width % scale) != 0u) {
            scale = gcd_u64(scale, width);
        }
        if (scale > 1u && (width % scale) == 0u) {
            return scale;
        }
    }
    return 1u;
}

static u64 detect_vertical_scale_palette(const u8* pixels,
                                         u64 width,
                                         u64 height,
                                         u8 color_mode) {
    if (!pixels || width == 0u || height < 2u) {
        return 1u;
    }
    if (color_mode == 0u || color_mode > 3u) {
        return 1u;
    }
    u64 column_limit = (width < 256u) ? width : 256u;
    u64 row_limit = (height < 256u) ? height : 256u;
    u64 scale = 0u;
    u64 run_support = 0u;
    for (u64 col = 0u; col < column_limit; ++col) {
        u32 prev_sample = 0u;
        const u8* first_pixel = pixels + (col * 3u);
        if (!map_rgb_to_samples(color_mode, first_pixel, &prev_sample)) {
            continue;
        }
        u64 run_length = 1u;
        bool column_ok = true;
        for (u64 row = 1u; row < row_limit; ++row) {
            const u8* pixel_ptr = pixels + ((row * width + col) * 3u);
            u32 current_sample = 0u;
            if (!map_rgb_to_samples(color_mode, pixel_ptr, &current_sample)) {
                column_ok = false;
                break;
            }
            if (current_sample == prev_sample) {
                ++run_length;
                continue;
            }
            if (run_length > 1u && run_length < height) {
                scale = scale ? gcd_u64(scale, run_length) : run_length;
                ++run_support;
                if (scale == 1u) {
                    return 1u;
                }
            }
            prev_sample = current_sample;
            run_length = 1u;
        }
        if (!column_ok) {
            continue;
        }
        if (run_length > 1u && run_length < height) {
            scale = scale ? gcd_u64(scale, run_length) : run_length;
            ++run_support;
            if (scale == 1u) {
                return 1u;
            }
        }
    }
    if (scale > 1u && run_support >= 4u) {
        if ((height % scale) != 0u) {
            scale = gcd_u64(scale, height);
        }
        if (scale > 1u && (height % scale) == 0u) {
            return scale;
        }
    }
    return 1u;
}

static u64 detect_horizontal_scale(const u8* pixels,
                                   u64 width,
                                   u64 height,
                                   u8 color_mode) {
    if (!pixels || width == 0u || height == 0u) {
        return 1u;
    }
    u64 palette_scale = detect_horizontal_scale_palette(pixels, width, height, color_mode);
    if (palette_scale > 1u) {
        return palette_scale;
    }
    return detect_horizontal_scale_similarity(pixels, width, height);
}

static u64 detect_vertical_scale(const u8* pixels,
                                 u64 width,
                                 u64 height,
                                 u8 color_mode) {
    if (!pixels || width == 0u || height == 0u) {
        return 1u;
    }
    u64 palette_scale = detect_vertical_scale_palette(pixels, width, height, color_mode);
    if (palette_scale > 1u) {
        return palette_scale;
    }
    return detect_vertical_scale_similarity(pixels, width, height);
}

static bool validate_integer_scale(const u8* pixels,
                                   u64 width,
                                   u64 height,
                                   u64 scale_x,
                                   u64 scale_y,
                                   u8 color_mode) {
    if (!pixels || width == 0u || height == 0u) {
        return false;
    }
    if (scale_x <= 1u && scale_y <= 1u) {
        return true;
    }
    if (scale_x == 0u || scale_y == 0u) {
        return false;
    }
    if ((width % scale_x) != 0u || (height % scale_y) != 0u) {
        return false;
    }
    if (color_mode == 0u || color_mode > 3u) {
        return false;
    }
    u64 logical_width = width / scale_x;
    u64 logical_height = height / scale_y;
    for (u64 logical_row = 0u; logical_row < logical_height; ++logical_row) {
        u64 base_row = logical_row * scale_y;
        for (u64 logical_col = 0u; logical_col < logical_width; ++logical_col) {
            u64 base_col = logical_col * scale_x;
            const u8* origin = pixels + ((base_row * width) + base_col) * 3u;
            u32 reference_sample = 0u;
            if (!map_rgb_to_samples(color_mode, origin, &reference_sample)) {
                return false;
            }
            for (u64 dy = 0u; dy < scale_y; ++dy) {
                u64 row_index = base_row + dy;
                if (row_index >= height) {
                    return false;
                }
                const u8* row_ptr = pixels + ((row_index * width) + base_col) * 3u;
                for (u64 dx = 0u; dx < scale_x; ++dx) {
                    const u8* sample = row_ptr + dx * 3u;
                    u32 sample_value = 0u;
                    if (!map_rgb_to_samples(color_mode, sample, &sample_value)) {
                        return false;
                    }
                    if (sample_value != reference_sample) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

static bool ppm_extract_frame_bits(const makocode::ByteBuffer& input,
                                   const ImageMappingConfig& overrides,
                                   makocode::ByteBuffer& frame_bits,
                                   u64& frame_bit_count,
                                   PpmParserState& metadata_out) {
    if (!input.data || input.size == 0u) {
        return false;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    char cursor_buffer[32];
    u64_to_ascii((u64)state.cursor, cursor_buffer, sizeof(cursor_buffer));
    console_write(2, "debug initial cursor: ");
    console_line(2, cursor_buffer);
    console_write(2, "debug first bytes: ");
    for (int i = 0; i < 8 && i < (int)state.size; ++i) {
        char value_buffer[32];
        u64_to_ascii((u64)(unsigned char)state.data[i], value_buffer, sizeof(value_buffer));
        console_write(2, value_buffer);
        if (i < 7 && (i + 1) < (int)state.size) {
            console_write(2, " ");
        }
    }
    console_line(2, "");
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length)) {
        console_line(2, "debug: missing magic token");
        return false;
    }
    if (!ascii_equals_token(token, token_length, "P3")) {
        char debug_token[32];
        usize debug_count = (token_length < (usize)(sizeof(debug_token) - 1u)) ? token_length : (sizeof(debug_token) - 1u);
        for (usize i = 0u; i < debug_count; ++i) {
            debug_token[i] = token[i];
        }
        debug_token[debug_count] = '\0';
        console_line(2, "debug: magic token not P3");
        console_write(2, "debug token: ");
        console_line(2, debug_token);
        char length_buffer[32];
        u64_to_ascii((u64)token_length, length_buffer, sizeof(length_buffer));
        console_write(2, "debug length: ");
        console_line(2, length_buffer);
        usize offset = (usize)(token - (const char*)state.data);
        char offset_buffer[32];
        u64_to_ascii((u64)offset, offset_buffer, sizeof(offset_buffer));
        console_write(2, "debug offset: ");
        console_line(2, offset_buffer);
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        console_line(2, "debug: missing width token");
        return false;
    }
    u64 width = 0u;
    if (!ascii_to_u64(token, token_length, &width) || width == 0u) {
        console_line(2, "debug: invalid width token");
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        console_line(2, "debug: missing height token");
        return false;
    }
    u64 height = 0u;
    if (!ascii_to_u64(token, token_length, &height) || height == 0u) {
        console_line(2, "debug: invalid height token");
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        console_line(2, "debug: missing max value token");
        return false;
    }
    u64 max_value = 0u;
    if (!ascii_to_u64(token, token_length, &max_value) || max_value != 255u) {
        console_line(2, "debug: invalid max value token");
        return false;
    }
    u64 raw_pixel_count = width * height;
    if (raw_pixel_count == 0u) {
        return false;
    }
    makocode::ByteBuffer pixel_buffer;
    if (!ppm_read_rgb_pixels(state, raw_pixel_count, pixel_buffer)) {
        return false;
    }
    const u8* pixel_data = pixel_buffer.data;
    if (!pixel_data) {
        return false;
    }
    bool has_rotation = state.has_rotation_degrees &&
                        state.rotation_degrees_value != 0.0 &&
                        state.has_rotation_width &&
                        state.has_rotation_height;
    unsigned rotated_width = (unsigned)width;
    unsigned rotated_height = (unsigned)height;
    unsigned rotation_width = has_rotation ? (unsigned)state.rotation_width_value : 0u;
    unsigned rotation_height = has_rotation ? (unsigned)state.rotation_height_value : 0u;
    double rotation_margin = (has_rotation && state.has_rotation_margin) ? state.rotation_margin_value : 4.0;
    if (rotation_margin < 0.0) {
        rotation_margin = 0.0;
    }
    double rotation_radians = has_rotation ? (state.rotation_degrees_value * (3.14159265358979323846 / 180.0)) : 0.0;
    double rotation_cos = has_rotation ? cos(rotation_radians) : 1.0;
    double rotation_sin = has_rotation ? sin(rotation_radians) : 0.0;
    double rotation_center_x = has_rotation ? (((double)rotation_width - 1.0) * 0.5) : 0.0;
    double rotation_center_y = has_rotation ? (((double)rotation_height - 1.0) * 0.5) : 0.0;
    double rotation_offset_x = 0.0;
    double rotation_offset_y = 0.0;
    if (has_rotation) {
        double min_x = 0.0;
        double max_x = 0.0;
        double min_y = 0.0;
        double max_y = 0.0;
        for (int i = 0; i < 4; ++i) {
            double corner_x = (i & 1) ? (double)(rotation_width - 1u) : 0.0;
            double corner_y = (i & 2) ? (double)(rotation_height - 1u) : 0.0;
            double dx = corner_x - rotation_center_x;
            double dy = corner_y - rotation_center_y;
            double rx = dx * rotation_cos - dy * rotation_sin;
            double ry = dx * rotation_sin + dy * rotation_cos;
            if (i == 0) {
                min_x = max_x = rx;
                min_y = max_y = ry;
            } else {
                if (rx < min_x) min_x = rx;
                if (rx > max_x) max_x = rx;
                if (ry < min_y) min_y = ry;
                if (ry > max_y) max_y = ry;
            }
        }
        rotation_offset_x = -min_x + rotation_margin;
        rotation_offset_y = -min_y + rotation_margin;
    }
    bool has_skew = state.has_skew_src_width &&
                    state.has_skew_src_height &&
                    state.has_skew_margin_x &&
                    state.has_skew_x_pixels;
    u64 skew_src_width = has_skew ? state.skew_src_width_value : 0u;
    u64 skew_src_height = has_skew ? state.skew_src_height_value : 0u;
    double skew_margin = has_skew ? state.skew_margin_x_value : 0.0;
    double skew_top = has_skew ? state.skew_x_pixels_value : 0.0;
    double skew_bottom = has_skew && state.has_skew_bottom_x ? state.skew_bottom_x_value : 0.0;
    if (has_skew) {
        if (skew_src_width == 0u || skew_src_height == 0u) {
            has_skew = false;
        }
    }
    u8 detection_color_mode = overrides.color_channels;
    if (overrides.color_set) {
        detection_color_mode = overrides.color_channels;
    } else if (state.has_color_channels &&
               state.color_channels_value >= 1u &&
               state.color_channels_value <= 3u) {
        detection_color_mode = (u8)state.color_channels_value;
    }
    if (detection_color_mode == 0u || detection_color_mode > 3u) {
        detection_color_mode = 1u;
    }
    double scale_x = 1.0;
    double scale_y = 1.0;
    bool scale_x_integer = true;
    bool scale_y_integer = true;
    u64 scale_x_int = 1u;
    u64 scale_y_int = 1u;
    bool width_known = false;
    bool height_known = false;
    u64 expected_width = 0u;
    u64 expected_height = 0u;
    if (overrides.page_width_set && overrides.page_width_pixels) {
        expected_width = overrides.page_width_pixels;
        width_known = true;
    } else if (state.has_page_width_pixels && state.page_width_pixels_value) {
        expected_width = state.page_width_pixels_value;
        width_known = true;
    }
    if (overrides.page_height_set && overrides.page_height_pixels) {
        expected_height = overrides.page_height_pixels;
        height_known = true;
    } else if (state.has_page_height_pixels && state.page_height_pixels_value) {
        expected_height = state.page_height_pixels_value;
        height_known = true;
    }
    u64 analysis_width = has_rotation ? (u64)rotation_width : width;
    u64 analysis_height = has_rotation ? (u64)rotation_height : height;
    if (!has_rotation && has_skew) {
        analysis_width = skew_src_width;
        analysis_height = skew_src_height;
    }
    if (width_known && expected_width) {
        double ratio_x = (double)analysis_width / (double)expected_width;
        if (ratio_x > 0.0) {
            scale_x = ratio_x;
            double rounded_ratio_x = floor(ratio_x + 0.5);
            double diff_x = ratio_x - rounded_ratio_x;
            if (diff_x < 0.000001 && diff_x > -0.000001 && rounded_ratio_x >= 1.0) {
                scale_x_int = (u64)rounded_ratio_x;
                scale_x_integer = true;
            } else {
                scale_x_integer = false;
            }
        }
    }
    if (height_known && expected_height) {
        double ratio_y = (double)analysis_height / (double)expected_height;
        if (ratio_y > 0.0) {
            scale_y = ratio_y;
            double rounded_ratio_y = floor(ratio_y + 0.5);
            double diff_y = ratio_y - rounded_ratio_y;
            if (diff_y < 0.000001 && diff_y > -0.000001 && rounded_ratio_y >= 1.0) {
                scale_y_int = (u64)rounded_ratio_y;
                scale_y_integer = true;
            } else {
                scale_y_integer = false;
            }
        }
    }
    if (scale_x_integer) {
        if (scale_x_int == 0u || (analysis_width % scale_x_int) != 0u) {
            scale_x = 1.0;
            scale_x_int = 1u;
            scale_x_integer = true;
        }
    } else {
        if (scale_x <= 0.0) {
            return false;
        }
    }
    if (scale_y_integer) {
        if (scale_y_int == 0u || (analysis_height % scale_y_int) != 0u) {
            scale_y = 1.0;
            scale_y_int = 1u;
            scale_y_integer = true;
        }
    } else {
        if (scale_y <= 0.0) {
            return false;
        }
    }
    if (!has_rotation && !has_skew && scale_x_integer && scale_x_int == 1u && width > 1u) {
        u64 detected_x = detect_horizontal_scale(pixel_data, width, height, detection_color_mode);
        if (detected_x > 1u && (width % detected_x) == 0u) {
            scale_x_int = detected_x;
            scale_x = (double)detected_x;
        }
    }
    if (!has_rotation && !has_skew && scale_y_integer && scale_y_int == 1u && height > 1u) {
        u64 detected_y = detect_vertical_scale(pixel_data, width, height, detection_color_mode);
        if (detected_y > 1u && (height % detected_y) == 0u) {
            scale_y_int = detected_y;
            scale_y = (double)detected_y;
        }
    }
    if (!has_rotation && !has_skew && scale_x_integer && scale_y_integer) {
        if (!validate_integer_scale(pixel_data, width, height, scale_x_int, scale_y_int, detection_color_mode)) {
            scale_x = 1.0;
            scale_y = 1.0;
            scale_x_int = 1u;
            scale_y_int = 1u;
            scale_x_integer = true;
            scale_y_integer = true;
        }
    }
    if (scale_x_integer) {
        if (scale_x_int == 0u || (analysis_width % scale_x_int) != 0u) {
            scale_x = 1.0;
            scale_x_int = 1u;
            scale_x_integer = true;
        }
    } else if (scale_x <= 0.0) {
        return false;
    }
    if (scale_y_integer) {
        if (scale_y_int == 0u || (analysis_height % scale_y_int) != 0u) {
            scale_y = 1.0;
            scale_y_int = 1u;
            scale_y_integer = true;
        }
    } else if (scale_y <= 0.0) {
        return false;
    }
    double logical_width_d = (double)analysis_width / scale_x;
    double logical_height_d = (double)analysis_height / scale_y;
    u64 logical_width = (u64)(logical_width_d + 0.5);
    u64 logical_height = (u64)(logical_height_d + 0.5);
    double width_error = logical_width_d - (double)logical_width;
    if (width_error < 0.0) {
        width_error = -width_error;
    }
    double height_error = logical_height_d - (double)logical_height;
    if (height_error < 0.0) {
        height_error = -height_error;
    }
    if (logical_width == 0u || logical_height == 0u) {
        return false;
    }
    if (width_error > 0.01 || height_error > 0.01) {
        return false;
    }
    u64 footer_rows = 0u;
    if (state.has_footer_rows) {
        footer_rows = state.footer_rows_value;
        if (footer_rows > logical_height) {
            return false;
        }
    }
    u64 data_height = logical_height - footer_rows;
    if (data_height == 0u) {
        return false;
    }
    u8 color_mode = overrides.color_channels;
    if (overrides.color_set) {
        color_mode = overrides.color_channels;
    } else if (state.has_color_channels) {
        if (state.color_channels_value == 0u || state.color_channels_value > 3u) {
            return false;
        }
        color_mode = (u8)state.color_channels_value;
    }
    if (color_mode == 0u || color_mode > 3u) {
        return false;
    }
    const u8 sample_bits = bits_per_sample(color_mode);
    if (sample_bits == 0u) {
        return false;
    }
    const PaletteColor* palette = 0;
    u32 palette_size = 0u;
    if (!palette_for_mode(color_mode, palette, palette_size)) {
        return false;
    }
    if (palette_size != (1u << sample_bits)) {
        return false;
    }
    (void)palette;
    u8 samples_per_pixel = color_mode_samples_per_pixel(color_mode);
    makocode::BitWriter writer;
    u64 raw_width = width;
    u64 pixel_stride = raw_width;
    double scale_xd = scale_x;
    double scale_yd = scale_y;
    struct FiducialSubgridCell {
        double tl_x;
        double tl_y;
        double tr_x;
        double tr_y;
        double bl_x;
        double bl_y;
        double br_x;
        double br_y;
        u64 col_start;
        u64 col_end;
        u64 row_start;
        u64 row_end;
    };

    struct FiducialGridStorage {
        double* centers_x;
        double* centers_y;
        double* column_weights;
        double* row_weights;
        u64* column_offsets;
        u64* row_offsets;
        FiducialSubgridCell* cells;

        FiducialGridStorage()
            : centers_x(0),
              centers_y(0),
              column_weights(0),
              row_weights(0),
              column_offsets(0),
              row_offsets(0),
              cells(0) {}

        ~FiducialGridStorage() {
            if (centers_x) free(centers_x);
            if (centers_y) free(centers_y);
            if (column_weights) free(column_weights);
            if (row_weights) free(row_weights);
            if (column_offsets) free(column_offsets);
            if (row_offsets) free(row_offsets);
            if (cells) free(cells);
        }
    } fiducial_storage;

    bool use_fiducial_subgrid = false;
    FiducialSubgridCell* fiducial_cells = 0;
    u64* fiducial_column_offsets = 0;
    u64* fiducial_row_offsets = 0;
    u32 fiducial_subgrid_columns = 0u;
    u32 fiducial_subgrid_rows = 0u;

    if (!has_rotation && !has_skew &&
        state.has_fiducial_columns && state.has_fiducial_rows &&
        state.has_fiducial_size &&
        state.fiducial_columns_value >= 2u &&
        state.fiducial_rows_value >= 2u) {
        u64 fiducial_columns_value = state.fiducial_columns_value;
        u64 fiducial_rows_value = state.fiducial_rows_value;
        if (fiducial_columns_value <= 0xFFFFFFFFull && fiducial_rows_value <= 0xFFFFFFFFull) {
            u32 fiducial_columns = (u32)fiducial_columns_value;
            u32 fiducial_rows = (u32)fiducial_rows_value;
            u32 sub_cols = fiducial_columns ? (fiducial_columns - 1u) : 0u;
            u32 sub_rows = fiducial_rows ? (fiducial_rows - 1u) : 0u;
            if (sub_cols > 0u && sub_rows > 0u &&
                logical_width >= (u64)sub_cols &&
                logical_height >= (u64)sub_rows) {
                usize fiducial_point_count = (usize)fiducial_columns * (usize)fiducial_rows;
                if (fiducial_point_count > 0u) {
                    fiducial_storage.centers_x = (double*)malloc(fiducial_point_count * sizeof(double));
                    fiducial_storage.centers_y = (double*)malloc(fiducial_point_count * sizeof(double));
                    if (fiducial_storage.centers_x && fiducial_storage.centers_y) {
                        double margin_pixels = state.has_fiducial_margin ? (double)state.fiducial_margin_value : 0.0;
                        double min_x = margin_pixels;
                        double max_x = (width > 0u) ? ((double)(width - 1u) - margin_pixels) : 0.0;
                        if (max_x < min_x) {
                            max_x = min_x;
                        }
                        double min_y = margin_pixels;
                        double max_y = (height > 0u) ? ((double)(height - 1u) - margin_pixels) : 0.0;
                        if (max_y < min_y) {
                            max_y = min_y;
                        }
                        double fiducial_size_pixels = (state.fiducial_size_value > 0u)
                                                          ? (double)state.fiducial_size_value
                                                          : 1.0;
                        double search_radius_d = fiducial_size_pixels * 3.5 + 8.0;
                        if (search_radius_d < 6.0) {
                            search_radius_d = 6.0;
                        }
                        u32 search_radius = (u32)(search_radius_d + 0.5);
                        if (search_radius < 6u) {
                            search_radius = 6u;
                        }
                        double inv_radius_sq = 1.0 / ((double)search_radius * (double)search_radius + 1.0);
                        for (u32 row_index = 0u; row_index < fiducial_rows; ++row_index) {
                            double t_row = (fiducial_rows == 1u) ? 0.5 : ((double)row_index / (double)(fiducial_rows - 1u));
                            double approx_y = min_y + (max_y - min_y) * t_row;
                            for (u32 col_index = 0u; col_index < fiducial_columns; ++col_index) {
                                double t_col = (fiducial_columns == 1u) ? 0.5 : ((double)col_index / (double)(fiducial_columns - 1u));
                                double approx_x = min_x + (max_x - min_x) * t_col;
                                double center_x = approx_x;
                                double center_y = approx_y;
                                double sum_w = 0.0;
                                double sum_x = 0.0;
                                double sum_y = 0.0;
                                int y_start = (int)(approx_y) - (int)search_radius;
                                int y_end = (int)(approx_y) + (int)search_radius;
                                if (y_start < 0) {
                                    y_start = 0;
                                }
                                if (y_end >= (int)height) {
                                    y_end = (int)height - 1;
                                }
                                int x_start = (int)(approx_x) - (int)search_radius;
                                int x_end = (int)(approx_x) + (int)search_radius;
                                if (x_start < 0) {
                                    x_start = 0;
                                }
                                if (x_end >= (int)width) {
                                    x_end = (int)width - 1;
                                }
                                for (int sample_y_idx = y_start; sample_y_idx <= y_end; ++sample_y_idx) {
                                    for (int sample_x_idx = x_start; sample_x_idx <= x_end; ++sample_x_idx) {
                                        usize sample_index = ((usize)sample_y_idx * (usize)width + (usize)sample_x_idx) * 3u;
                                        u32 r_val = pixel_data[sample_index + 0u];
                                        u32 g_val = pixel_data[sample_index + 1u];
                                        u32 b_val = pixel_data[sample_index + 2u];
                                        double intensity = ((double)r_val + (double)g_val + (double)b_val) / 3.0;
                                        if (intensity >= 140.0) {
                                            double weight = intensity - 139.0;
                                            double dx = (double)sample_x_idx - approx_x;
                                            double dy = (double)sample_y_idx - approx_y;
                                            double distance_sq = dx * dx + dy * dy;
                                            double falloff = 1.0 / (1.0 + distance_sq * inv_radius_sq);
                                            weight *= falloff;
                                            sum_w += weight;
                                            sum_x += weight * (double)sample_x_idx;
                                            sum_y += weight * (double)sample_y_idx;
                                        }
                                    }
                                }
                                if (sum_w > 0.0) {
                                    center_x = sum_x / sum_w;
                                    center_y = sum_y / sum_w;
                                }
                                usize point_index = (usize)row_index * (usize)fiducial_columns + (usize)col_index;
                                fiducial_storage.centers_x[point_index] = center_x;
                                fiducial_storage.centers_y[point_index] = center_y;
                            }
                        }

                        fiducial_storage.column_weights = (double*)malloc((usize)sub_cols * sizeof(double));
                        fiducial_storage.row_weights = (double*)malloc((usize)sub_rows * sizeof(double));
                        fiducial_storage.column_offsets = (u64*)malloc(((usize)sub_cols + 1u) * sizeof(u64));
                        fiducial_storage.row_offsets = (u64*)malloc(((usize)sub_rows + 1u) * sizeof(u64));
                        usize cell_total = (usize)sub_cols * (usize)sub_rows;
                        fiducial_storage.cells = (cell_total > 0u)
                                                    ? (FiducialSubgridCell*)malloc(cell_total * sizeof(FiducialSubgridCell))
                                                    : (FiducialSubgridCell*)0;

                        if (fiducial_storage.column_weights && fiducial_storage.row_weights &&
                            fiducial_storage.column_offsets && fiducial_storage.row_offsets &&
                            fiducial_storage.cells && cell_total) {
                            double total_col_weight = 0.0;
                            for (u32 col_index = 0u; col_index < sub_cols; ++col_index) {
                                double sum_len = 0.0;
                                for (u32 row_index = 0u; row_index < fiducial_rows; ++row_index) {
                                    usize left_index = (usize)row_index * (usize)fiducial_columns + (usize)col_index;
                                    usize right_index = left_index + 1u;
                                    double dx = fiducial_storage.centers_x[right_index] - fiducial_storage.centers_x[left_index];
                                    double dy = fiducial_storage.centers_y[right_index] - fiducial_storage.centers_y[left_index];
                                    double len = sqrt(dx * dx + dy * dy);
                                    if (len <= 0.0) {
                                        len = 1.0;
                                    }
                                    sum_len += len;
                                }
                                double weight = sum_len / (double)fiducial_rows;
                                if (weight <= 0.0) {
                                    weight = 1.0;
                                }
                                fiducial_storage.column_weights[col_index] = weight;
                                total_col_weight += weight;
                            }
                            if (total_col_weight <= 0.0) {
                                total_col_weight = (double)sub_cols;
                            }

                            double total_row_weight = 0.0;
                            for (u32 row_index = 0u; row_index < sub_rows; ++row_index) {
                                double sum_len = 0.0;
                                for (u32 col_index = 0u; col_index < fiducial_columns; ++col_index) {
                                    usize top_index = (usize)row_index * (usize)fiducial_columns + (usize)col_index;
                                    usize bottom_index = top_index + (usize)fiducial_columns;
                                    double dx = fiducial_storage.centers_x[bottom_index] - fiducial_storage.centers_x[top_index];
                                    double dy = fiducial_storage.centers_y[bottom_index] - fiducial_storage.centers_y[top_index];
                                    double len = sqrt(dx * dx + dy * dy);
                                    if (len <= 0.0) {
                                        len = 1.0;
                                    }
                                    sum_len += len;
                                }
                                double weight = sum_len / (double)fiducial_columns;
                                if (weight <= 0.0) {
                                    weight = 1.0;
                                }
                                fiducial_storage.row_weights[row_index] = weight;
                                total_row_weight += weight;
                            }
                            if (total_row_weight <= 0.0) {
                                total_row_weight = (double)sub_rows;
                            }

                            double left_margin_accum = 0.0;
                            double right_margin_accum = 0.0;
                            for (u32 row_index = 0u; row_index < fiducial_rows; ++row_index) {
                                usize left_index = (usize)row_index * (usize)fiducial_columns;
                                usize right_index = left_index + (usize)(fiducial_columns - 1u);
                                left_margin_accum += fiducial_storage.centers_x[left_index];
                                right_margin_accum += ((double)(width > 0u ? (double)(width - 1u) : 0.0) -
                                                       fiducial_storage.centers_x[right_index]);
                            }
                            double average_left_margin = left_margin_accum / (double)fiducial_rows;
                            double average_right_margin = right_margin_accum / (double)fiducial_rows;
                            if (sub_cols > 0u) {
                                fiducial_storage.column_weights[0] += average_left_margin;
                                fiducial_storage.column_weights[sub_cols - 1u] += average_right_margin;
                            }
                            total_col_weight = 0.0;
                            for (u32 col_index = 0u; col_index < sub_cols; ++col_index) {
                                if (fiducial_storage.column_weights[col_index] <= 0.0) {
                                    fiducial_storage.column_weights[col_index] = 1.0;
                                }
                                total_col_weight += fiducial_storage.column_weights[col_index];
                            }

                            double top_margin_accum = 0.0;
                            double bottom_margin_accum = 0.0;
                            for (u32 col_index = 0u; col_index < fiducial_columns; ++col_index) {
                                top_margin_accum += fiducial_storage.centers_y[col_index];
                                bottom_margin_accum += ((double)(height > 0u ? (double)(height - 1u) : 0.0) -
                                                        fiducial_storage.centers_y[(usize)(fiducial_rows - 1u) * (usize)fiducial_columns + (usize)col_index]);
                            }
                            double average_top_margin = top_margin_accum / (double)fiducial_columns;
                            double average_bottom_margin = bottom_margin_accum / (double)fiducial_columns;
                            if (sub_rows > 0u) {
                                fiducial_storage.row_weights[0] += average_top_margin;
                                fiducial_storage.row_weights[sub_rows - 1u] += average_bottom_margin;
                            }
                            total_row_weight = 0.0;
                            for (u32 row_index = 0u; row_index < sub_rows; ++row_index) {
                                if (fiducial_storage.row_weights[row_index] <= 0.0) {
                                    fiducial_storage.row_weights[row_index] = 1.0;
                                }
                                total_row_weight += fiducial_storage.row_weights[row_index];
                            }

                            fiducial_storage.column_offsets[0] = 0u;
                            double cumulative_weight = 0.0;
                            u64 assigned_columns = 0u;
                            for (u32 col_index = 0u; col_index < sub_cols; ++col_index) {
                                cumulative_weight += fiducial_storage.column_weights[col_index];
                                u64 target = (u64)((cumulative_weight / total_col_weight) * (double)logical_width + 0.5);
                                if (target <= assigned_columns) {
                                    target = assigned_columns + 1u;
                                }
                                if ((col_index + 1u) == sub_cols) {
                                    target = logical_width;
                                } else if (target > logical_width) {
                                    target = logical_width;
                                }
                                fiducial_storage.column_offsets[col_index + 1u] = target;
                                assigned_columns = target;
                            }
                            if (fiducial_storage.column_offsets[sub_cols] != logical_width) {
                                fiducial_storage.column_offsets[sub_cols] = logical_width;
                            }

                            fiducial_storage.row_offsets[0] = 0u;
                            cumulative_weight = 0.0;
                            u64 assigned_rows = 0u;
                            for (u32 row_index = 0u; row_index < sub_rows; ++row_index) {
                                cumulative_weight += fiducial_storage.row_weights[row_index];
                                u64 target = (u64)((cumulative_weight / total_row_weight) * (double)logical_height + 0.5);
                                if (target <= assigned_rows) {
                                    target = assigned_rows + 1u;
                                }
                                if ((row_index + 1u) == sub_rows) {
                                    target = logical_height;
                                } else if (target > logical_height) {
                                    target = logical_height;
                                }
                                fiducial_storage.row_offsets[row_index + 1u] = target;
                                assigned_rows = target;
                            }
                            if (fiducial_storage.row_offsets[sub_rows] != logical_height) {
                                fiducial_storage.row_offsets[sub_rows] = logical_height;
                            }

                            for (u32 row_index = 0u; row_index < sub_rows; ++row_index) {
                                for (u32 col_index = 0u; col_index < sub_cols; ++col_index) {
                                    usize cell_index = (usize)row_index * (usize)sub_cols + (usize)col_index;
                                    FiducialSubgridCell& cell = fiducial_storage.cells[cell_index];
                                    cell.row_start = fiducial_storage.row_offsets[row_index];
                                    cell.row_end = fiducial_storage.row_offsets[row_index + 1u];
                                    cell.col_start = fiducial_storage.column_offsets[col_index];
                                    cell.col_end = fiducial_storage.column_offsets[col_index + 1u];
                                    usize top_left_index = (usize)row_index * (usize)fiducial_columns + (usize)col_index;
                                    usize top_right_index = top_left_index + 1u;
                                    usize bottom_left_index = top_left_index + (usize)fiducial_columns;
                                    usize bottom_right_index = bottom_left_index + 1u;
                                    cell.tl_x = fiducial_storage.centers_x[top_left_index];
                                    cell.tl_y = fiducial_storage.centers_y[top_left_index];
                                    cell.tr_x = fiducial_storage.centers_x[top_right_index];
                                    cell.tr_y = fiducial_storage.centers_y[top_right_index];
                                    cell.bl_x = fiducial_storage.centers_x[bottom_left_index];
                                    cell.bl_y = fiducial_storage.centers_y[bottom_left_index];
                                    cell.br_x = fiducial_storage.centers_x[bottom_right_index];
                                    cell.br_y = fiducial_storage.centers_y[bottom_right_index];
                                }
                            }

                            use_fiducial_subgrid = true;
                            fiducial_cells = fiducial_storage.cells;
                            fiducial_column_offsets = fiducial_storage.column_offsets;
                            fiducial_row_offsets = fiducial_storage.row_offsets;
                            fiducial_subgrid_columns = sub_cols;
                            fiducial_subgrid_rows = sub_rows;
                        }
                    }
                }
            }
        }
    }

    u32 active_row_cell = 0u;
    u64 active_row_start = use_fiducial_subgrid ? fiducial_row_offsets[0] : 0u;
    u64 active_row_end = use_fiducial_subgrid && fiducial_subgrid_rows > 0u
                             ? fiducial_row_offsets[1]
                             : data_height;

    for (u64 logical_row = 0u; logical_row < data_height; ++logical_row) {
        if (use_fiducial_subgrid) {
            while (active_row_cell + 1u < fiducial_subgrid_rows && logical_row >= active_row_end) {
                ++active_row_cell;
                active_row_start = fiducial_row_offsets[active_row_cell];
                active_row_end = fiducial_row_offsets[active_row_cell + 1u];
            }
            if (active_row_end <= active_row_start) {
                active_row_end = active_row_start + 1u;
            }
        }

        u32 active_col_cell = 0u;
        u64 active_col_start = use_fiducial_subgrid ? fiducial_column_offsets[0] : 0u;
        u64 active_col_end = use_fiducial_subgrid && fiducial_subgrid_columns > 0u
                                 ? fiducial_column_offsets[1]
                                 : logical_width;
        FiducialSubgridCell* row_cells = use_fiducial_subgrid
                                             ? (fiducial_cells + ((usize)active_row_cell * (usize)fiducial_subgrid_columns))
                                             : (FiducialSubgridCell*)0;

        for (u64 logical_col = 0u; logical_col < logical_width; ++logical_col) {
            if (use_fiducial_subgrid) {
                while (active_col_cell + 1u < fiducial_subgrid_columns && logical_col >= active_col_end) {
                    ++active_col_cell;
                    active_col_start = fiducial_column_offsets[active_col_cell];
                    active_col_end = fiducial_column_offsets[active_col_cell + 1u];
                }
                if (active_col_end <= active_col_start) {
                    active_col_end = active_col_start + 1u;
                }
            }

            double sample_row = 0.0;
            double sample_col = 0.0;
            if (use_fiducial_subgrid) {
                const double clamp_min = 0.0005;
                const double clamp_max = 1.0 - clamp_min;
                FiducialSubgridCell& cell = row_cells[active_col_cell];
                double local_u = 0.5;
                double local_v = 0.5;
                if (cell.col_end > cell.col_start) {
                    local_u = (((double)(logical_col - cell.col_start) + 0.5) /
                               (double)(cell.col_end - cell.col_start));
                }
                if (cell.row_end > cell.row_start) {
                    local_v = (((double)(logical_row - cell.row_start) + 0.5) /
                               (double)(cell.row_end - cell.row_start));
                }
                if (local_u < clamp_min) local_u = clamp_min;
                if (local_u > clamp_max) local_u = clamp_max;
                if (local_v < clamp_min) local_v = clamp_min;
                if (local_v > clamp_max) local_v = clamp_max;
                double top_x = cell.tl_x + (cell.tr_x - cell.tl_x) * local_u;
                double top_y = cell.tl_y + (cell.tr_y - cell.tl_y) * local_u;
                double bottom_x = cell.bl_x + (cell.br_x - cell.bl_x) * local_u;
                double bottom_y = cell.bl_y + (cell.br_y - cell.bl_y) * local_u;
                sample_col = top_x + (bottom_x - top_x) * local_v;
                sample_row = top_y + (bottom_y - top_y) * local_v;
            } else {
                sample_row = ((double)logical_row + 0.5) * scale_yd - 0.5;
                sample_col = ((double)logical_col + 0.5) * scale_xd - 0.5;
            }

            const u8* rgb = 0;
            u8 rotated_rgb[3];
            u8 skew_rgb[3];
            if (has_rotation) {
                if (rotation_width == 0u || rotation_height == 0u || rotated_width == 0u || rotated_height == 0u) {
                    return false;
                }
                double dx = sample_col - rotation_center_x;
                double dy = sample_row - rotation_center_y;
                double sample_x = dx * rotation_cos - dy * rotation_sin + rotation_offset_x;
                double sample_y = dx * rotation_sin + dy * rotation_cos + rotation_offset_y;
                if (sample_x < 0.0) sample_x = 0.0;
                if (sample_y < 0.0) sample_y = 0.0;
                double max_sample_x = (rotated_width > 0u) ? (double)(rotated_width - 1u) : 0.0;
                double max_sample_y = (rotated_height > 0u) ? (double)(rotated_height - 1u) : 0.0;
                if (sample_x > max_sample_x) sample_x = max_sample_x;
                if (sample_y > max_sample_y) sample_y = max_sample_y;
                unsigned x0 = (unsigned)floor(sample_x);
                unsigned y0 = (unsigned)floor(sample_y);
                unsigned x1 = (x0 + 1u < rotated_width) ? (x0 + 1u) : x0;
                unsigned y1 = (y0 + 1u < rotated_height) ? (y0 + 1u) : y0;
                double fx = sample_x - (double)x0;
                double fy = sample_y - (double)y0;
                usize idx00 = ((usize)y0 * (usize)rotated_width + (usize)x0) * 3u;
                usize idx10 = ((usize)y0 * (usize)rotated_width + (usize)x1) * 3u;
                usize idx01 = ((usize)y1 * (usize)rotated_width + (usize)x0) * 3u;
                usize idx11 = ((usize)y1 * (usize)rotated_width + (usize)x1) * 3u;
                for (u32 channel = 0u; channel < 3u; ++channel) {
                    double v00 = (double)pixel_data[idx00 + channel];
                    double v10 = (double)pixel_data[idx10 + channel];
                    double v01 = (double)pixel_data[idx01 + channel];
                    double v11 = (double)pixel_data[idx11 + channel];
                    double top = v00 + (v10 - v00) * fx;
                    double bottom = v01 + (v11 - v01) * fx;
                    double value = top + (bottom - top) * fy;
                    if (value < 0.0) {
                        value = 0.0;
                    }
                    if (value > 255.0) {
                        value = 255.0;
                    }
                    rotated_rgb[channel] = (u8)(value + 0.5);
                }
                rgb = rotated_rgb;
            } else if (has_skew) {
                if (sample_row < 0.0) sample_row = 0.0;
                if (sample_col < 0.0) sample_col = 0.0;
                double max_row = (analysis_height > 0u) ? (double)(analysis_height - 1u) : 0.0;
                double max_col = (analysis_width > 0u) ? (double)(analysis_width - 1u) : 0.0;
                if (sample_row > max_row) sample_row = max_row;
                if (sample_col > max_col) sample_col = max_col;
                double normalized_row = (skew_src_height > 1u) ? (sample_row / (double)(skew_src_height - 1u)) : 0.0;
                double row_shift = skew_top * (1.0 - normalized_row) + skew_bottom * normalized_row;
                double dest_x = sample_col + skew_margin + row_shift;
                double dest_y = sample_row;
                if (dest_x < 0.0) dest_x = 0.0;
                if (dest_y < 0.0) dest_y = 0.0;
                double max_dest_x = (width > 0u) ? (double)(width - 1u) : 0.0;
                double max_dest_y = (height > 0u) ? (double)(height - 1u) : 0.0;
                if (dest_x > max_dest_x) dest_x = max_dest_x;
                if (dest_y > max_dest_y) dest_y = max_dest_y;
                unsigned nearest_x = (unsigned)(dest_x + 0.5);
                unsigned nearest_y = (unsigned)(dest_y + 0.5);
                if (nearest_x >= width) {
                    nearest_x = width - 1u;
                }
                if (nearest_y >= height) {
                    nearest_y = height - 1u;
                }
                usize idx = ((usize)nearest_y * (usize)width + (usize)nearest_x) * 3u;
                for (u32 channel = 0u; channel < 3u; ++channel) {
                    skew_rgb[channel] = pixel_data[idx + channel];
                }
                rgb = skew_rgb;
            } else {
                if (sample_row < 0.0) sample_row = 0.0;
                if (sample_col < 0.0) sample_col = 0.0;
                double max_row = (analysis_height > 0u) ? (double)(analysis_height - 1u) : 0.0;
                double max_col = (analysis_width > 0u) ? (double)(analysis_width - 1u) : 0.0;
                if (sample_row > max_row) sample_row = max_row;
                if (sample_col > max_col) sample_col = max_col;
                u64 raw_row = (u64)(sample_row + 0.5);
                u64 raw_col = (u64)(sample_col + 0.5);
                u64 pixel_index = (raw_row * pixel_stride) + raw_col;
                if (pixel_index >= raw_pixel_count) {
                    return false;
                }
                rgb = pixel_data + (usize)(pixel_index * 3u);
            }

            u32 samples_raw[3] = {0u, 0u, 0u};
            if (!map_rgb_to_samples(color_mode, rgb, samples_raw)) {
                return false;
            }
            for (u8 sample_index = 0u; sample_index < samples_per_pixel; ++sample_index) {
                u32 sample = samples_raw[sample_index];
                if (!writer.write_bits(sample, sample_bits)) {
                    return false;
                }
            }
        }
    }
    if (!writer.align_to_byte()) {
        return false;
    }
    if (color_mode == 3u) {
        usize total_bytes = writer.byte_size();
        if (total_bytes) {
            u8* data_bytes = writer.buffer.data;
            if (!data_bytes && total_bytes) {
                return false;
            }
            for (usize i = 0u; i < total_bytes; ++i) {
                u8 rotate = (u8)((i % 3u) + 1u);
                data_bytes[i] = rotate_right_u8(data_bytes[i], rotate);
            }
        }
    }
    frame_bit_count = writer.bit_size();
    usize frame_bytes = writer.byte_size();
    frame_bits.release();
    if (frame_bytes && !frame_bits.ensure(frame_bytes)) {
        return false;
    }
    const u8* writer_data = writer.data();
    for (usize i = 0u; i < frame_bytes; ++i) {
        frame_bits.data[i] = writer_data ? writer_data[i] : 0u;
    }
    frame_bits.size = frame_bytes;
    metadata_out = state;
    metadata_out.data = 0;
    metadata_out.size = 0u;
    metadata_out.cursor = 0u;
    return true;
}

static bool append_bits_from_buffer(makocode::BitWriter& writer,
                                    const u8* data,
                                    u64 bit_count) {
    for (u64 bit_index = 0u; bit_index < bit_count; ++bit_index) {
        u8 bit_value = 0u;
        if (data) {
            usize byte_index = (usize)(bit_index >> 3u);
            u8 mask = (u8)(1u << (bit_index & 7u));
            bit_value = (data[byte_index] & mask) ? 1u : 0u;
        }
        if (!writer.write_bit(bit_value)) {
            return false;
        }
    }
    return true;
}

static bool merge_parser_state(PpmParserState& dest, const PpmParserState& src) {
    if (src.has_bytes) {
        if (dest.has_bytes && dest.bytes_value != src.bytes_value) {
            return false;
        }
        dest.has_bytes = true;
        dest.bytes_value = src.bytes_value;
    }
    if (src.has_bits) {
        if (dest.has_bits && dest.bits_value != src.bits_value) {
            return false;
        }
        dest.has_bits = true;
        dest.bits_value = src.bits_value;
    }
    if (src.has_ecc_flag) {
        if (dest.has_ecc_flag && dest.ecc_flag_value != src.ecc_flag_value) {
            return false;
        }
        dest.has_ecc_flag = true;
        dest.ecc_flag_value = src.ecc_flag_value;
    }
    if (src.has_ecc_block_data) {
        if (dest.has_ecc_block_data && dest.ecc_block_data_value != src.ecc_block_data_value) {
            return false;
        }
        dest.has_ecc_block_data = true;
        dest.ecc_block_data_value = src.ecc_block_data_value;
    }
    if (src.has_ecc_parity) {
        if (dest.has_ecc_parity && dest.ecc_parity_value != src.ecc_parity_value) {
            return false;
        }
        dest.has_ecc_parity = true;
        dest.ecc_parity_value = src.ecc_parity_value;
    }
    if (src.has_ecc_block_count) {
        if (dest.has_ecc_block_count && dest.ecc_block_count_value != src.ecc_block_count_value) {
            return false;
        }
        dest.has_ecc_block_count = true;
        dest.ecc_block_count_value = src.ecc_block_count_value;
    }
    if (src.has_ecc_original_bytes) {
        if (dest.has_ecc_original_bytes && dest.ecc_original_bytes_value != src.ecc_original_bytes_value) {
            return false;
        }
        dest.has_ecc_original_bytes = true;
        dest.ecc_original_bytes_value = src.ecc_original_bytes_value;
    }
    if (src.has_color_channels) {
        if (dest.has_color_channels && dest.color_channels_value != src.color_channels_value) {
            return false;
        }
        dest.has_color_channels = true;
        dest.color_channels_value = src.color_channels_value;
    }
    if (src.has_fiducial_size) {
        if (dest.has_fiducial_size && dest.fiducial_size_value != src.fiducial_size_value) {
            return false;
        }
        dest.has_fiducial_size = true;
        dest.fiducial_size_value = src.fiducial_size_value;
    }
    if (src.has_fiducial_columns) {
        if (dest.has_fiducial_columns && dest.fiducial_columns_value != src.fiducial_columns_value) {
            return false;
        }
        dest.has_fiducial_columns = true;
        dest.fiducial_columns_value = src.fiducial_columns_value;
    }
    if (src.has_fiducial_rows) {
        if (dest.has_fiducial_rows && dest.fiducial_rows_value != src.fiducial_rows_value) {
            return false;
        }
        dest.has_fiducial_rows = true;
        dest.fiducial_rows_value = src.fiducial_rows_value;
    }
    if (src.has_fiducial_margin) {
        if (dest.has_fiducial_margin && dest.fiducial_margin_value != src.fiducial_margin_value) {
            return false;
        }
        dest.has_fiducial_margin = true;
        dest.fiducial_margin_value = src.fiducial_margin_value;
    }
    if (src.has_page_width_pixels) {
        if (dest.has_page_width_pixels && dest.page_width_pixels_value != src.page_width_pixels_value) {
            return false;
        }
        dest.has_page_width_pixels = true;
        dest.page_width_pixels_value = src.page_width_pixels_value;
    }
    if (src.has_page_height_pixels) {
        if (dest.has_page_height_pixels && dest.page_height_pixels_value != src.page_height_pixels_value) {
            return false;
        }
        dest.has_page_height_pixels = true;
        dest.page_height_pixels_value = src.page_height_pixels_value;
    }
    if (src.has_page_count) {
        if (dest.has_page_count && dest.page_count_value != src.page_count_value) {
            return false;
        }
        dest.has_page_count = true;
        dest.page_count_value = src.page_count_value;
    }
    if (src.has_page_bits) {
        if (dest.has_page_bits && dest.page_bits_value != src.page_bits_value) {
            return false;
        }
        dest.has_page_bits = true;
        dest.page_bits_value = src.page_bits_value;
    }
    if (src.has_footer_rows) {
        if (dest.has_footer_rows && dest.footer_rows_value != src.footer_rows_value) {
            return false;
        }
        dest.has_footer_rows = true;
        dest.footer_rows_value = src.footer_rows_value;
    }
    if (src.has_font_size) {
        if (dest.has_font_size && dest.font_size_value != src.font_size_value) {
            return false;
        }
        dest.has_font_size = true;
        dest.font_size_value = src.font_size_value;
    }
    if (src.has_skew_src_width) {
        if (dest.has_skew_src_width && dest.skew_src_width_value != src.skew_src_width_value) {
            return false;
        }
        dest.has_skew_src_width = true;
        dest.skew_src_width_value = src.skew_src_width_value;
    }
    if (src.has_skew_src_height) {
        if (dest.has_skew_src_height && dest.skew_src_height_value != src.skew_src_height_value) {
            return false;
        }
        dest.has_skew_src_height = true;
        dest.skew_src_height_value = src.skew_src_height_value;
    }
    if (src.has_skew_margin_x) {
        if (dest.has_skew_margin_x && dest.skew_margin_x_value != src.skew_margin_x_value) {
            return false;
        }
        dest.has_skew_margin_x = true;
        dest.skew_margin_x_value = src.skew_margin_x_value;
    }
    if (src.has_skew_x_pixels) {
        if (dest.has_skew_x_pixels && dest.skew_x_pixels_value != src.skew_x_pixels_value) {
            return false;
        }
        dest.has_skew_x_pixels = true;
        dest.skew_x_pixels_value = src.skew_x_pixels_value;
    }
    if (src.has_skew_bottom_x) {
        if (dest.has_skew_bottom_x && dest.skew_bottom_x_value != src.skew_bottom_x_value) {
            return false;
        }
        dest.has_skew_bottom_x = true;
        dest.skew_bottom_x_value = src.skew_bottom_x_value;
    }
    return true;
}

static bool frame_bits_to_payload(const u8* frame_data,
                                  u64 frame_bit_count,
                                  const PpmParserState& metadata,
                                  makocode::ByteBuffer& output,
                                  u64& out_bit_count) {
    output.release();
    out_bit_count = 0u;
    if (!frame_data || frame_bit_count == 0u) {
        return false;
    }
    makocode::BitReader reader;
    reader.reset(frame_data, frame_bit_count);
    if (frame_bit_count < 64u) {
        return false;
    }
    u64 header_bits = reader.read_bits(64u);
    if (reader.failed) {
        return false;
    }
    u64 available_bits = (frame_bit_count >= 64u) ? (frame_bit_count - 64u) : 0u;
    u64 payload_bits = header_bits;
    if (metadata.has_bits) {
        if (metadata.bits_value <= available_bits) {
            payload_bits = metadata.bits_value;
        }
    }
    if (payload_bits > available_bits) {
        return false;
    }
    makocode::BitWriter payload_writer;
    for (u64 bit_index = 0u; bit_index < payload_bits; ++bit_index) {
        u8 bit = reader.read_bit();
        if (reader.failed) {
            return false;
        }
        if (!payload_writer.write_bit(bit)) {
            return false;
        }
    }
    if (payload_writer.failed) {
        return false;
    }
    usize payload_bytes = payload_writer.byte_size();
    if (payload_bytes && !output.ensure(payload_bytes)) {
        return false;
    }
    const u8* payload_data = payload_writer.data();
    for (usize i = 0u; i < payload_bytes; ++i) {
        output.data[i] = payload_data ? payload_data[i] : 0u;
    }
    output.size = payload_bytes;
    out_bit_count = payload_bits;
    return true;
}

static bool buffer_append_number(makocode::ByteBuffer& buffer, u64 value) {
    char digits[32];
   u64_to_ascii(value, digits, sizeof(digits));
    return buffer.append_ascii(digits);
}

static bool append_comment_number(makocode::ByteBuffer& buffer,
                                  const char* tag,
                                  u64 value) {
    if (!tag) {
        return false;
    }
    if (!buffer.append_char('#')) {
        return false;
    }
    if (!buffer.append_char(' ')) {
        return false;
    }
    if (!buffer.append_ascii(tag)) {
        return false;
    }
    if (!buffer.append_char(' ')) {
        return false;
    }
    if (!buffer_append_number(buffer, value)) {
        return false;
    }
    return buffer.append_char('\n');
}

static bool buffer_append_zero_padded(makocode::ByteBuffer& buffer,
                                      u64 value,
                                      u32 width) {
    char digits[32];
    u64_to_ascii(value, digits, sizeof(digits));
    usize length = ascii_length(digits);
    if (width > 16u) {
        width = 16u;
    }
    if (width > length) {
        u32 pad = width - (u32)length;
        for (u32 i = 0u; i < pad; ++i) {
            if (!buffer.append_char('0')) {
                return false;
            }
        }
    }
    return buffer.append_ascii(digits);
}

static bool buffer_clone_with_suffix(const makocode::ByteBuffer& base,
                                     const char* suffix,
                                     const char* extension,
                                     makocode::ByteBuffer& out) {
    out.release();
    if (base.size) {
        if (!out.append_bytes(base.data, base.size)) {
            return false;
        }
    }
    if (suffix && suffix[0]) {
        if (!out.append_ascii(suffix)) {
            return false;
        }
    }
    if (extension && extension[0]) {
        if (!out.append_ascii(extension)) {
            return false;
        }
    }
    return out.append_char('\0');
}

static bool format_scale_label(double factor, char* out, usize out_size) {
    if (!out || out_size < 3u) {
        return false;
    }
    if (factor <= 0.0) {
        return false;
    }
    double scaled_double = factor * 100.0;
    u64 scaled = (u64)(scaled_double + 0.5);
    u64 integer_part = scaled / 100u;
    u64 fractional_part = scaled % 100u;
    char integer_digits[32];
    u64_to_ascii(integer_part, integer_digits, sizeof(integer_digits));
    usize integer_len = ascii_length(integer_digits);
    if (integer_len == 0u || integer_len >= out_size) {
        return false;
    }
    if (fractional_part == 0u) {
        if (integer_part < 10u) {
            if (out_size < 3u) {
                return false;
            }
            out[0] = '0';
            out[1] = integer_digits[0];
            out[2] = '\0';
            return true;
        }
        if ((integer_len + 1u) > out_size) {
            return false;
        }
        for (usize i = 0u; i < integer_len; ++i) {
            out[i] = integer_digits[i];
        }
        out[integer_len] = '\0';
        return true;
    }
    if (out_size < (integer_len + 4u)) {
        return false;
    }
    u64 first_digit = fractional_part / 10u;
    u64 second_digit = fractional_part % 10u;
    for (usize i = 0u; i < integer_len; ++i) {
        out[i] = integer_digits[i];
    }
    out[integer_len] = 'p';
    out[integer_len + 1u] = (char)('0' + first_digit);
    out[integer_len + 2u] = (char)('0' + second_digit);
    out[integer_len + 3u] = '\0';
    return true;
}

static bool build_page_base_name(makocode::ByteBuffer& buffer,
                                 u64 test_case_index,
                                 const char* prefix,
                                 const char* scenario_digits,
                                 const char* color_digits,
                                 u64 page_index_one_based,
                                 const char* scale_label) {
    buffer.release();
    if (!prefix || !prefix[0]) {
        return false;
    }
    if (!buffer_append_zero_padded(buffer, test_case_index, 4u) ||
        !buffer.append_ascii("_") ||
        !buffer.append_ascii(prefix) ||
        !buffer.append_ascii("_s") ||
        !buffer.append_ascii(scenario_digits) ||
        !buffer.append_ascii("_c") ||
        !buffer.append_ascii(color_digits) ||
        !buffer.append_ascii("_p") ||
        !buffer_append_zero_padded(buffer, page_index_one_based, 2u)) {
        return false;
    }
    if (scale_label && scale_label[0]) {
        if (!buffer.append_ascii("_x") ||
            !buffer.append_ascii(scale_label)) {
            return false;
        }
    }
    return true;
}

static bool buffer_store_bits_with_count(const u8* data,
                                         u64 bit_count,
                                         makocode::ByteBuffer& output) {
    output.release();
    u64 byte_count = (bit_count + 7u) >> 3u;
    usize total_bytes = (usize)byte_count + (usize)8u;
    if (!output.ensure(total_bytes)) {
        return false;
    }
    write_le_u64(output.data, bit_count);
    output.size = 8u;
    for (u64 i = 0u; i < byte_count; ++i) {
        u8 value = data ? data[i] : 0u;
        if (i == byte_count - 1u) {
            u32 remainder = (u32)(bit_count & 7u);
            if (remainder) {
                u8 mask = (u8)((1u << remainder) - 1u);
                value &= mask;
            }
        }
        output.data[output.size++] = value;
    }
    return true;
}

static bool ppm_scale_integer(const makocode::ByteBuffer& input,
                              u32 factor,
                              makocode::ByteBuffer& output) {
    if (factor == 0u) {
        return false;
    }
    if (factor == 1u) {
        output.release();
        if (input.size) {
            if (!output.ensure(input.size)) {
                return false;
            }
            const u8* source = input.data;
            if (!source) {
                return false;
            }
            for (usize i = 0u; i < input.size; ++i) {
                output.data[i] = source[i];
            }
            output.size = input.size;
        } else {
            output.size = 0u;
        }
        return true;
    }
    if (!input.data || input.size == 0u) {
        return false;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    if (!ascii_equals_token(token, token_length, "P3")) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 width = 0u;
    if (!ascii_to_u64(token, token_length, &width) || width == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 height = 0u;
    if (!ascii_to_u64(token, token_length, &height) || height == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 max_value = 0u;
    if (!ascii_to_u64(token, token_length, &max_value) || max_value != 255u) {
        return false;
    }
    u64 pixel_count = width * height;
    if (pixel_count == 0u) {
        return false;
    }
    makocode::ByteBuffer pixels;
    if (!ppm_read_rgb_pixels(state, pixel_count, pixels)) {
        return false;
    }
    const u8* pixel_data = pixels.data;
    if (!pixel_data) {
        return false;
    }
    u64 scaled_width = width * (u64)factor;
    u64 scaled_height = height * (u64)factor;
    if (scaled_width == 0u || scaled_height == 0u) {
        return false;
    }
    output.release();
    if (!output.append_ascii("P3\n")) {
        return false;
    }
    if (state.has_bytes) {
        if (!append_comment_number(output, "MAKOCODE_BYTES", state.bytes_value)) {
            return false;
        }
    }
    if (state.has_bits) {
        if (!append_comment_number(output, "MAKOCODE_BITS", state.bits_value)) {
            return false;
        }
    }
    if (state.has_ecc_flag) {
        if (!append_comment_number(output, "MAKOCODE_ECC", state.ecc_flag_value)) {
            return false;
        }
    }
    if (state.has_ecc_block_data) {
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_DATA", state.ecc_block_data_value)) {
            return false;
        }
    }
    if (state.has_ecc_parity) {
        if (!append_comment_number(output, "MAKOCODE_ECC_PARITY", state.ecc_parity_value)) {
            return false;
        }
    }
    if (state.has_ecc_block_count) {
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_COUNT", state.ecc_block_count_value)) {
            return false;
        }
    }
    if (state.has_ecc_original_bytes) {
        if (!append_comment_number(output, "MAKOCODE_ECC_ORIGINAL_BYTES", state.ecc_original_bytes_value)) {
            return false;
        }
    }
    if (state.has_color_channels) {
        if (!append_comment_number(output, "MAKOCODE_COLOR_CHANNELS", state.color_channels_value)) {
            return false;
        }
    }
    if (state.has_page_count) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_COUNT", state.page_count_value)) {
            return false;
        }
    }
    if (state.has_page_index) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_INDEX", state.page_index_value)) {
            return false;
        }
    }
    if (state.has_page_bits) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_BITS", state.page_bits_value)) {
            return false;
        }
    }
    if (state.has_page_width_pixels) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_WIDTH_PX", state.page_width_pixels_value)) {
            return false;
        }
    }
    if (state.has_page_height_pixels) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_HEIGHT_PX", state.page_height_pixels_value)) {
            return false;
        }
    }
    if (state.has_footer_rows) {
        if (!append_comment_number(output, "MAKOCODE_FOOTER_ROWS", state.footer_rows_value)) {
            return false;
        }
        if (state.has_font_size) {
            if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", state.font_size_value)) {
                return false;
            }
        }
    } else if (state.has_font_size) {
        if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", state.font_size_value)) {
            return false;
        }
    }
    if (state.has_fiducial_size) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_SIZE", state.fiducial_size_value)) {
            return false;
        }
    }
    if (state.has_fiducial_columns) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_COLUMNS", state.fiducial_columns_value)) {
            return false;
        }
    }
    if (state.has_fiducial_rows) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_ROWS", state.fiducial_rows_value)) {
            return false;
        }
    }
    if (state.has_fiducial_margin) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_MARGIN", state.fiducial_margin_value)) {
            return false;
        }
    }
    if (!buffer_append_number(output, scaled_width) || !output.append_char(' ')) {
        return false;
    }
    if (!buffer_append_number(output, scaled_height) || !output.append_char('\n')) {
        return false;
    }
    if (!output.append_ascii("255\n")) {
        return false;
    }
    u64 src_width = width;
    for (u64 row = 0u; row < scaled_height; ++row) {
        u64 src_row = row / factor;
        if (src_row >= height) {
            return false;
        }
        const u8* src_row_ptr = pixel_data + (usize)(src_row * src_width * 3u);
        for (u64 col = 0u; col < scaled_width; ++col) {
            u64 src_col = col / factor;
            if (src_col >= width) {
                return false;
            }
            const u8* rgb = src_row_ptr + (usize)(src_col * 3u);
            for (u8 channel = 0u; channel < 3u; ++channel) {
                if (channel) {
                    if (!output.append_char(' ')) {
                        return false;
                    }
                }
                if (!buffer_append_number(output, (u64)rgb[channel])) {
                    return false;
                }
            }
            if (!output.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}

static bool ppm_scale_fractional_axes(const makocode::ByteBuffer& input,
                                      double factor_x,
                                      double factor_y,
                                      makocode::ByteBuffer& output) {
    if (factor_x <= 0.0 || factor_y <= 0.0) {
        return false;
    }
    double delta_x = factor_x - 1.0;
    double delta_y = factor_y - 1.0;
    bool near_identity = (delta_x < 0.000001 && delta_x > -0.000001) &&
                         (delta_y < 0.000001 && delta_y > -0.000001);
    if (near_identity) {
        output.release();
        if (input.size) {
            if (!output.ensure(input.size)) {
                return false;
            }
            if (!input.data) {
                return false;
            }
            for (usize i = 0u; i < input.size; ++i) {
                output.data[i] = input.data[i];
            }
            output.size = input.size;
        } else {
            output.size = 0u;
        }
        return true;
    }
    if (!input.data || input.size == 0u) {
        return false;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    if (!ascii_equals_token(token, token_length, "P3")) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 width = 0u;
    if (!ascii_to_u64(token, token_length, &width) || width == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 height = 0u;
    if (!ascii_to_u64(token, token_length, &height) || height == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 max_value = 0u;
    if (!ascii_to_u64(token, token_length, &max_value) || max_value != 255u) {
        return false;
    }
    u64 pixel_count = width * height;
    if (pixel_count == 0u) {
        return false;
    }
    makocode::ByteBuffer pixels;
    if (!ppm_read_rgb_pixels(state, pixel_count, pixels)) {
        return false;
    }
    const u8* pixel_data = pixels.data;
    if (!pixel_data) {
        return false;
    }
    double scaled_width_d = (double)width * factor_x;
    double scaled_height_d = (double)height * factor_y;
    if (scaled_width_d <= 0.0 || scaled_height_d <= 0.0) {
        return false;
    }
    u64 scaled_width = (u64)(scaled_width_d + 0.5);
    u64 scaled_height = (u64)(scaled_height_d + 0.5);
    if (scaled_width == 0u || scaled_height == 0u) {
        return false;
    }
    output.release();
    if (!output.append_ascii("P3\n")) {
        return false;
    }
    if (state.has_bytes) {
        if (!append_comment_number(output, "MAKOCODE_BYTES", state.bytes_value)) {
            return false;
        }
    }
    if (state.has_bits) {
        if (!append_comment_number(output, "MAKOCODE_BITS", state.bits_value)) {
            return false;
        }
    }
    if (state.has_ecc_flag) {
        if (!append_comment_number(output, "MAKOCODE_ECC", state.ecc_flag_value)) {
            return false;
        }
    }
    if (state.has_ecc_block_data) {
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_DATA", state.ecc_block_data_value)) {
            return false;
        }
    }
    if (state.has_ecc_parity) {
        if (!append_comment_number(output, "MAKOCODE_ECC_PARITY", state.ecc_parity_value)) {
            return false;
        }
    }
    if (state.has_ecc_block_count) {
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_COUNT", state.ecc_block_count_value)) {
            return false;
        }
    }
    if (state.has_ecc_original_bytes) {
        if (!append_comment_number(output, "MAKOCODE_ECC_ORIGINAL_BYTES", state.ecc_original_bytes_value)) {
            return false;
        }
    }
    if (state.has_color_channels) {
        if (!append_comment_number(output, "MAKOCODE_COLOR_CHANNELS", state.color_channels_value)) {
            return false;
        }
    }
    if (state.has_page_count) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_COUNT", state.page_count_value)) {
            return false;
        }
    }
    if (state.has_page_index) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_INDEX", state.page_index_value)) {
            return false;
        }
    }
    if (state.has_page_bits) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_BITS", state.page_bits_value)) {
            return false;
        }
    }
    if (state.has_page_width_pixels) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_WIDTH_PX", state.page_width_pixels_value)) {
            return false;
        }
    }
    if (state.has_page_height_pixels) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_HEIGHT_PX", state.page_height_pixels_value)) {
            return false;
        }
    }
    if (state.has_footer_rows) {
        if (!append_comment_number(output, "MAKOCODE_FOOTER_ROWS", state.footer_rows_value)) {
            return false;
        }
        if (state.has_font_size) {
            if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", state.font_size_value)) {
                return false;
            }
        }
    } else if (state.has_font_size) {
        if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", state.font_size_value)) {
            return false;
        }
    }
    if (state.has_fiducial_size) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_SIZE", state.fiducial_size_value)) {
            return false;
        }
    }
    if (state.has_fiducial_columns) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_COLUMNS", state.fiducial_columns_value)) {
            return false;
        }
    }
    if (state.has_fiducial_rows) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_ROWS", state.fiducial_rows_value)) {
            return false;
        }
    }
    if (state.has_fiducial_margin) {
        if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_MARGIN", state.fiducial_margin_value)) {
            return false;
        }
    }
    if (!buffer_append_number(output, scaled_width) || !output.append_char(' ')) {
        return false;
    }
    if (!buffer_append_number(output, scaled_height) || !output.append_char('\n')) {
        return false;
    }
    if (!output.append_ascii("255\n")) {
        return false;
    }
    double inv_factor_x = 1.0 / factor_x;
    double inv_factor_y = 1.0 / factor_y;
    double max_src_x = (width > 0u) ? (double)(width - 1u) : 0.0;
    double max_src_y = (height > 0u) ? (double)(height - 1u) : 0.0;
    for (u64 row = 0u; row < scaled_height; ++row) {
        double src_y = ((double)row + 0.5) * inv_factor_y - 0.5;
        if (src_y < 0.0) {
            src_y = 0.0;
        }
        if (src_y > max_src_y) {
            src_y = max_src_y;
        }
        unsigned y0 = (unsigned)floor(src_y);
        unsigned y1 = (y0 + 1u < height) ? (unsigned)(y0 + 1u) : y0;
        double fy = src_y - (double)y0;
        for (u64 col = 0u; col < scaled_width; ++col) {
            double src_x = ((double)col + 0.5) * inv_factor_x - 0.5;
            if (src_x < 0.0) {
                src_x = 0.0;
            }
            if (src_x > max_src_x) {
                src_x = max_src_x;
            }
            unsigned x0 = (unsigned)floor(src_x);
            unsigned x1 = (x0 + 1u < width) ? (unsigned)(x0 + 1u) : x0;
            double fx = src_x - (double)x0;
            usize idx00 = ((usize)y0 * (usize)width + (usize)x0) * 3u;
            usize idx10 = ((usize)y0 * (usize)width + (usize)x1) * 3u;
            usize idx01 = ((usize)y1 * (usize)width + (usize)x0) * 3u;
            usize idx11 = ((usize)y1 * (usize)width + (usize)x1) * 3u;
            for (u32 channel = 0u; channel < 3u; ++channel) {
                double v00 = (double)pixel_data[idx00 + channel];
                double v10 = (double)pixel_data[idx10 + channel];
                double v01 = (double)pixel_data[idx01 + channel];
                double v11 = (double)pixel_data[idx11 + channel];
                double top = v00 + (v10 - v00) * fx;
                double bottom = v01 + (v11 - v01) * fx;
                double value = top + (bottom - top) * fy;
                if (value < 0.0) {
                    value = 0.0;
                } else if (value > 255.0) {
                    value = 255.0;
                }
                if (channel) {
                    if (!output.append_char(' ')) {
                        return false;
                    }
                }
                if (!buffer_append_number(output, (u64)(value + 0.5))) {
                    return false;
                }
            }
            if (!output.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}

static bool ppm_scale_fractional(const makocode::ByteBuffer& input,
                                 double factor,
                                 makocode::ByteBuffer& output) {
    return ppm_scale_fractional_axes(input, factor, factor, output);
}

static bool ppm_write_metadata_header(const PpmParserState& state,
                                      makocode::ByteBuffer& output) {
    if (!output.append_ascii("P3\n")) {
        return false;
    }
    if (state.has_bytes) {
        if (!append_comment_number(output, "MAKOCODE_BYTES", state.bytes_value)) {
            return false;
        }
    }
    if (state.has_bits) {
        if (!append_comment_number(output, "MAKOCODE_BITS", state.bits_value)) {
            return false;
        }
    }
    if (state.has_ecc_flag) {
        if (!append_comment_number(output, "MAKOCODE_ECC", state.ecc_flag_value)) {
            return false;
        }
    }
    if (state.has_ecc_block_data) {
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_DATA", state.ecc_block_data_value)) {
            return false;
        }
    }
    if (state.has_ecc_parity) {
        if (!append_comment_number(output, "MAKOCODE_ECC_PARITY", state.ecc_parity_value)) {
            return false;
        }
    }
    if (state.has_ecc_block_count) {
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_COUNT", state.ecc_block_count_value)) {
            return false;
        }
    }
    if (state.has_ecc_original_bytes) {
        if (!append_comment_number(output, "MAKOCODE_ECC_ORIGINAL_BYTES", state.ecc_original_bytes_value)) {
            return false;
        }
    }
    if (state.has_color_channels) {
        if (!append_comment_number(output, "MAKOCODE_COLOR_CHANNELS", state.color_channels_value)) {
            return false;
        }
    }
    if (state.has_page_count) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_COUNT", state.page_count_value)) {
            return false;
        }
    }
    if (state.has_page_index) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_INDEX", state.page_index_value)) {
            return false;
        }
    }
    if (state.has_page_bits) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_BITS", state.page_bits_value)) {
            return false;
        }
    }
    if (state.has_page_width_pixels) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_WIDTH_PX", state.page_width_pixels_value)) {
            return false;
        }
    }
    if (state.has_page_height_pixels) {
        if (!append_comment_number(output, "MAKOCODE_PAGE_HEIGHT_PX", state.page_height_pixels_value)) {
            return false;
        }
    }
    if (state.has_footer_rows) {
        if (!append_comment_number(output, "MAKOCODE_FOOTER_ROWS", state.footer_rows_value)) {
            return false;
        }
        if (state.has_font_size) {
            if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", state.font_size_value)) {
                return false;
            }
        }
    } else if (state.has_font_size) {
        if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", state.font_size_value)) {
            return false;
        }
    }
    return true;
}

static bool ppm_write_dimensions(u64 width,
                                 u64 height,
                                 makocode::ByteBuffer& output) {
    if (!buffer_append_number(output, width) || !output.append_char(' ')) {
        return false;
    }
    if (!buffer_append_number(output, height) || !output.append_char('\n')) {
        return false;
    }
    if (!output.append_ascii("255\n")) {
        return false;
    }
    return true;
}

static bool ppm_insert_fiducial_grid(const makocode::ByteBuffer& input,
                                     u32 marker_size,
                                     u32 grid_columns,
                                     u32 grid_rows,
                                     u32 margin_pixels,
                                     makocode::ByteBuffer& output) {
    if (!input.data || input.size == 0u) {
        return false;
    }
    if (marker_size == 0u || grid_columns == 0u || grid_rows == 0u) {
        return false;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    if (!ascii_equals_token(token, token_length, "P3")) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 width = 0u;
    if (!ascii_to_u64(token, token_length, &width) || width == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 height = 0u;
    if (!ascii_to_u64(token, token_length, &height) || height == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 max_value = 0u;
    if (!ascii_to_u64(token, token_length, &max_value) || max_value != 255u) {
        return false;
    }
    u64 pixel_count = width * height;
    if (pixel_count == 0u) {
        return false;
    }
    makocode::ByteBuffer pixels;
    if (!ppm_read_rgb_pixels(state, pixel_count, pixels)) {
        return false;
    }
    u8* pixel_data = pixels.data;
    if (!pixel_data) {
        return false;
    }
    if (width > (u64)0xFFFFFFFFu || height > (u64)0xFFFFFFFFu) {
        return false;
    }
    u32 width_px = (u32)width;
    u32 height_px = (u32)height;
    if (marker_size > width_px || marker_size > height_px) {
        return false;
    }
    double min_x = (margin_pixels < width_px) ? (double)margin_pixels : 0.0;
    double max_x = (width_px > margin_pixels) ? (double)(width_px - 1u - margin_pixels)
                                              : (width_px ? (double)(width_px - 1u) : 0.0);
    double min_y = (margin_pixels < height_px) ? (double)margin_pixels : 0.0;
    double max_y = (height_px > margin_pixels) ? (double)(height_px - 1u - margin_pixels)
                                               : (height_px ? (double)(height_px - 1u) : 0.0);
    if (max_x < min_x) {
        max_x = min_x;
    }
    if (max_y < min_y) {
        max_y = min_y;
    }
    for (u32 grid_row = 0u; grid_row < grid_rows; ++grid_row) {
        double t_y = (grid_rows == 1u) ? 0.5 : ((double)grid_row / (double)(grid_rows - 1u));
        double center_y = min_y + (max_y - min_y) * t_y;
        for (u32 grid_col = 0u; grid_col < grid_columns; ++grid_col) {
            double t_x = (grid_columns == 1u) ? 0.5 : ((double)grid_col / (double)(grid_columns - 1u));
            double center_x = min_x + (max_x - min_x) * t_x;
            int start_x = (int)(center_x - ((double)marker_size - 1.0) * 0.5);
            int start_y = (int)(center_y - ((double)marker_size - 1.0) * 0.5);
            for (u32 dy = 0u; dy < marker_size; ++dy) {
                int pixel_y = start_y + (int)dy;
                if (pixel_y < 0 || pixel_y >= (int)height_px) {
                    continue;
                }
                for (u32 dx = 0u; dx < marker_size; ++dx) {
                    int pixel_x = start_x + (int)dx;
                    if (pixel_x < 0 || pixel_x >= (int)width_px) {
                        continue;
                    }
                    usize index = ((usize)pixel_y * (usize)width_px + (usize)pixel_x) * 3u;
                    pixel_data[index + 0u] = 255u;
                    pixel_data[index + 1u] = 255u;
                    pixel_data[index + 2u] = 255u;
                }
            }
        }
    }
    output.release();
    if (!ppm_write_metadata_header(state, output)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_SIZE", (u64)marker_size)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_COLUMNS", (u64)grid_columns)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_ROWS", (u64)grid_rows)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_FIDUCIAL_MARGIN", (u64)margin_pixels)) {
        return false;
    }
    if (!ppm_write_dimensions(width, height, output)) {
        return false;
    }
    for (u64 row = 0u; row < height; ++row) {
        for (u64 col = 0u; col < width; ++col) {
            usize pixel_index = ((usize)row * (usize)width + (usize)col) * 3u;
            for (u32 channel = 0u; channel < 3u; ++channel) {
                if (channel) {
                    if (!output.append_char(' ')) {
                        return false;
                    }
                }
                if (!buffer_append_number(output, (u64)pixel_data[pixel_index + channel])) {
                    return false;
                }
            }
            if (!output.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}

static bool ppm_measure_dimensions(const makocode::ByteBuffer& input,
                                   u32& width_pixels,
                                   u32& height_pixels) {
    width_pixels = 0u;
    height_pixels = 0u;
    if (!input.data || input.size == 0u) {
        return false;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    if (!ascii_equals_token(token, token_length, "P3")) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 width_value = 0u;
    if (!ascii_to_u64(token, token_length, &width_value) ||
        width_value == 0u ||
        width_value > (u64)0xFFFFFFFFu) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 height_value = 0u;
    if (!ascii_to_u64(token, token_length, &height_value) ||
        height_value == 0u ||
        height_value > (u64)0xFFFFFFFFu) {
        return false;
    }
    width_pixels = (u32)width_value;
    height_pixels = (u32)height_value;
    return true;
}

static bool apply_default_fiducial_grid(const makocode::ByteBuffer& input,
                                        makocode::ByteBuffer& output) {
    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!ppm_measure_dimensions(input, width_pixels, height_pixels)) {
        return false;
    }
    u32 fiducial_marker_size = g_fiducial_defaults.marker_size_pixels;
    if (fiducial_marker_size == 0u) {
        fiducial_marker_size = 1u;
    }
    u32 fiducial_spacing = g_fiducial_defaults.spacing_pixels;
    if (fiducial_spacing == 0u) {
        fiducial_spacing = fiducial_marker_size;
    }
    u32 fiducial_margin = g_fiducial_defaults.margin_pixels;
    double min_x = (fiducial_margin < width_pixels) ? (double)fiducial_margin : 0.0;
    double max_x = (width_pixels > fiducial_margin)
                       ? (double)(width_pixels - 1u - fiducial_margin)
                       : (width_pixels ? (double)(width_pixels - 1u) : 0.0);
    if (max_x < min_x) {
        max_x = min_x;
    }
    double min_y = (fiducial_margin < height_pixels) ? (double)fiducial_margin : 0.0;
    double max_y = (height_pixels > fiducial_margin)
                       ? (double)(height_pixels - 1u - fiducial_margin)
                       : (height_pixels ? (double)(height_pixels - 1u) : 0.0);
    if (max_y < min_y) {
        max_y = min_y;
    }
    double available_width = (max_x >= min_x) ? (max_x - min_x) : 0.0;
    double available_height = (max_y >= min_y) ? (max_y - min_y) : 0.0;
    // Fit the grid into the drawable span so fixtures keep the configured spacing.
    u32 fiducial_columns = 1u;
    if (fiducial_spacing > 0u && available_width > 0.0) {
        double span = available_width / (double)fiducial_spacing;
        if (span < 0.0) {
            span = 0.0;
        }
        u64 additional = (u64)span;
        if (additional > 0xFFFFFFFFull - 1ull) {
            additional = 0xFFFFFFFFull - 1ull;
        }
        fiducial_columns = (u32)(additional + 1ull);
    }
    if (fiducial_columns == 0u) {
        fiducial_columns = 1u;
    }
    u32 fiducial_rows = 1u;
    if (fiducial_spacing > 0u && available_height > 0.0) {
        double span = available_height / (double)fiducial_spacing;
        if (span < 0.0) {
            span = 0.0;
        }
        u64 additional = (u64)span;
        if (additional > 0xFFFFFFFFull - 1ull) {
            additional = 0xFFFFFFFFull - 1ull;
        }
        fiducial_rows = (u32)(additional + 1ull);
    }
    if (fiducial_rows == 0u) {
        fiducial_rows = 1u;
    }
    if (fiducial_columns > 1u) {
        double span = (double)fiducial_spacing * (double)(fiducial_columns - 1u);
        double limit = available_width;
        while (fiducial_columns > 1u && span > limit + 1e-6) {
            --fiducial_columns;
            span = (double)fiducial_spacing * (double)(fiducial_columns - 1u);
        }
    }
    if (fiducial_rows > 1u) {
        double span = (double)fiducial_spacing * (double)(fiducial_rows - 1u);
        double limit = available_height;
        while (fiducial_rows > 1u && span > limit + 1e-6) {
            --fiducial_rows;
            span = (double)fiducial_spacing * (double)(fiducial_rows - 1u);
        }
    }
    return ppm_insert_fiducial_grid(input,
                                    fiducial_marker_size,
                                    fiducial_columns,
                                    fiducial_rows,
                                    fiducial_margin,
                                    output);
}

static bool write_ppm_with_fiducials_to_file(const char* path, const makocode::ByteBuffer& buffer) {
    makocode::ByteBuffer fiducial_buffer;
    if (!apply_default_fiducial_grid(buffer, fiducial_buffer)) {
        return false;
    }
    return write_bytes_to_file(path, fiducial_buffer.data, fiducial_buffer.size);
}

static bool ppm_apply_wavy_ripple(const makocode::ByteBuffer& input,
                                  double amplitude_pixels,
                                  double cycles_y,
                                  double cycles_x,
                                  makocode::ByteBuffer& output) {
    if (!input.data || input.size == 0u) {
        return false;
    }
    if (amplitude_pixels <= 0.0) {
        amplitude_pixels = 1.0;
    }
    if (cycles_y <= 0.0) {
        cycles_y = 1.0;
    }
    if (cycles_x <= 0.0) {
        cycles_x = 1.0;
    }
    PpmParserState state;
    state.data = input.data;
    state.size = input.size;
    const char* token = 0;
    usize token_length = 0u;
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    if (!ascii_equals_token(token, token_length, "P3")) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 width = 0u;
    if (!ascii_to_u64(token, token_length, &width) || width == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 height = 0u;
    if (!ascii_to_u64(token, token_length, &height) || height == 0u) {
        return false;
    }
    if (!ppm_next_token(state, &token, &token_length)) {
        return false;
    }
    u64 max_value = 0u;
    if (!ascii_to_u64(token, token_length, &max_value) || max_value != 255u) {
        return false;
    }
    u64 pixel_count = width * height;
    if (pixel_count == 0u) {
        return false;
    }
    makocode::ByteBuffer pixels;
    if (!ppm_read_rgb_pixels(state, pixel_count, pixels)) {
        return false;
    }
    const u8* pixel_data = pixels.data;
    if (!pixel_data) {
        return false;
    }
    if (width > (u64)0xFFFFFFFFu || height > (u64)0xFFFFFFFFu) {
        return false;
    }
    u32 width_px = (u32)width;
    u32 height_px = (u32)height;
    output.release();
    if (!ppm_write_metadata_header(state, output)) {
        return false;
    }
    u64 amplitude_milli = (amplitude_pixels >= 0.0)
                              ? (u64)(amplitude_pixels * 1000.0 + 0.5)
                              : 0u;
    u64 cycles_y_milli = (cycles_y >= 0.0)
                             ? (u64)(cycles_y * 1000.0 + 0.5)
                             : 0u;
    u64 cycles_x_milli = (cycles_x >= 0.0)
                             ? (u64)(cycles_x * 1000.0 + 0.5)
                             : 0u;
    if (!append_comment_number(output, "MAKOCODE_RIPPLE_AMPLITUDE_MILLIPX", amplitude_milli)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_RIPPLE_CYCLES_Y_MILLI", cycles_y_milli)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_RIPPLE_CYCLES_X_MILLI", cycles_x_milli)) {
        return false;
    }
    if (!ppm_write_dimensions(width, height, output)) {
        return false;
    }
    const double two_pi = 6.283185307179586476925286766559;
    double inv_width = (width_px > 1u) ? 1.0 / (double)(width_px - 1u) : 0.0;
    double inv_height = (height_px > 1u) ? 1.0 / (double)(height_px - 1u) : 0.0;
    double max_x = (width_px > 0u) ? (double)(width_px - 1u) : 0.0;
    double max_y = (height_px > 0u) ? (double)(height_px - 1u) : 0.0;
    for (u32 row = 0u; row < height_px; ++row) {
        double norm_y = (height_px > 1u) ? ((double)row * inv_height) : 0.0;
        double primary_wave_y = sin(two_pi * cycles_y * norm_y);
        double secondary_wave_y = sin(two_pi * cycles_y * 0.5 * norm_y + 0.4);
        for (u32 col = 0u; col < width_px; ++col) {
            double norm_x = (width_px > 1u) ? ((double)col * inv_width) : 0.0;
            double primary_wave_x = sin(two_pi * cycles_x * norm_x + norm_y * 0.35);
            double cross_wave = sin(two_pi * (cycles_x * 0.75) * norm_x + two_pi * (cycles_y * 0.25) * norm_y);
            double dx = amplitude_pixels * (0.6 * primary_wave_y +
                                            0.35 * primary_wave_x +
                                            0.25 * cross_wave +
                                            0.15 * secondary_wave_y * sin(two_pi * cycles_x * 0.33 * norm_x + 0.2));
            double dy = amplitude_pixels * (0.45 * primary_wave_x +
                                            0.30 * sin(two_pi * cycles_y * 0.8 * norm_y + norm_x * 2.1) +
                                            0.20 * cross_wave);
            double src_x = (double)col + dx;
            double src_y = (double)row + dy;
            if (src_x < 0.0) {
                src_x = 0.0;
            }
            if (src_x > max_x) {
                src_x = max_x;
            }
            if (src_y < 0.0) {
                src_y = 0.0;
            }
            if (src_y > max_y) {
                src_y = max_y;
            }
            unsigned x0 = (unsigned)floor(src_x);
            unsigned x1 = (x0 + 1u < width_px) ? (unsigned)(x0 + 1u) : x0;
            unsigned y0 = (unsigned)floor(src_y);
            unsigned y1 = (y0 + 1u < height_px) ? (unsigned)(y0 + 1u) : y0;
            double fx = src_x - (double)x0;
            double fy = src_y - (double)y0;
            usize idx00 = ((usize)y0 * (usize)width_px + (usize)x0) * 3u;
            usize idx10 = ((usize)y0 * (usize)width_px + (usize)x1) * 3u;
            usize idx01 = ((usize)y1 * (usize)width_px + (usize)x0) * 3u;
            usize idx11 = ((usize)y1 * (usize)width_px + (usize)x1) * 3u;
            for (u32 channel = 0u; channel < 3u; ++channel) {
                double v00 = (double)pixel_data[idx00 + channel];
                double v10 = (double)pixel_data[idx10 + channel];
                double v01 = (double)pixel_data[idx01 + channel];
                double v11 = (double)pixel_data[idx11 + channel];
                double top = v00 + (v10 - v00) * fx;
                double bottom = v01 + (v11 - v01) * fx;
                double value = top + (bottom - top) * fy;
                if (value < 0.0) {
                    value = 0.0;
                } else if (value > 255.0) {
                    value = 255.0;
                }
                if (channel) {
                    if (!output.append_char(' ')) {
                        return false;
                    }
                }
                if (!buffer_append_number(output, (u64)(value + 0.5))) {
                    return false;
                }
            }
            if (!output.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}

static bool build_page_filename(makocode::ByteBuffer& buffer,
                                const char* timestamp,
                                u64 page_index,
                                u64 page_count) {
    if (!timestamp || page_index == 0u) {
        return false;
    }
    buffer.release();
    if (!buffer.append_ascii(timestamp)) {
        return false;
    }
    if (page_count > 1u) {
        if (!buffer.append_ascii("_page_")) {
            return false;
        }
        u32 width = 4u;
        u64 count = page_count;
        u32 digits = 1u;
        while (count >= 10u) {
            count /= 10u;
            ++digits;
        }
        if (digits > width) {
            width = digits;
        }
        if (!buffer_append_zero_padded(buffer, page_index, width)) {
            return false;
        }
    }
    if (!buffer.append_ascii(".ppm") || !buffer.append_char('\0')) {
        return false;
    }
    return true;
}

static bool build_frame_bits(const makocode::EncoderContext& encoder,
                             const ImageMappingConfig& mapping,
                             makocode::ByteBuffer& frame_bits,
                             u64& frame_bit_count,
                             u64& payload_bit_count) {
    if (mapping.color_channels == 0u || mapping.color_channels > 3u) {
        return false;
    }
    const u8 sample_bits = bits_per_sample(mapping.color_channels);
    if (sample_bits == 0u) {
        return false;
    }
    const PaletteColor* palette = 0;
    u32 palette_size = 0u;
    if (!palette_for_mode(mapping.color_channels, palette, palette_size)) {
        return false;
    }
    if (palette_size != (1u << sample_bits)) {
        return false;
    }
    (void)palette;
    payload_bit_count = encoder.bit_writer.bit_size();
    usize payload_byte_count = encoder.bit_writer.byte_size();
    makocode::BitWriter frame_writer;
    if (!frame_writer.write_bits(payload_bit_count, 64u)) {
        return false;
    }
    const u8* payload_raw = encoder.bit_writer.data();
    for (usize byte_index = 0u; byte_index < payload_byte_count; ++byte_index) {
        u8 byte = payload_raw ? payload_raw[byte_index] : 0u;
        u64 bits_written = (u64)byte_index * 8u;
        u64 bits_remaining = (payload_bit_count > bits_written) ? (payload_bit_count - bits_written) : 0u;
        if (!bits_remaining) {
            break;
        }
        usize chunk = (bits_remaining >= 8u) ? 8u : (usize)bits_remaining;
        if (!frame_writer.write_bits((u64)byte, chunk)) {
            return false;
        }
    }
    frame_bit_count = frame_writer.bit_size();
    usize frame_bytes = frame_writer.byte_size();
    frame_bits.release();
    if (frame_bytes && !frame_bits.ensure(frame_bytes)) {
        return false;
    }
    const u8* raw = frame_writer.data();
    for (usize i = 0u; i < frame_bytes; ++i) {
        frame_bits.data[i] = raw ? raw[i] : 0u;
    }
    frame_bits.size = frame_bytes;
    if (mapping.color_channels == 3u && frame_bytes) {
        for (usize i = 0u; i < frame_bytes; ++i) {
            u8 rotate = (u8)((i % 3u) + 1u);
            frame_bits.data[i] = rotate_left_u8(frame_bits.data[i], rotate);
        }
    }
    return true;
}

static bool encode_page_to_ppm(const ImageMappingConfig& mapping,
                               const makocode::ByteBuffer& frame_bits,
                               u64 frame_bit_count,
                               u64 bit_offset,
                               u32 width_pixels,
                               u32 height_pixels,
                               u64 page_index,
                               u64 page_count,
                               u64 bits_per_page,
                               u64 payload_bit_count,
                               const makocode::EccSummary* ecc_summary,
                               const char* footer_text,
                               usize footer_length,
                               const FooterLayout& footer_layout,
                               makocode::ByteBuffer& output) {
    if (mapping.color_channels == 0u || mapping.color_channels > 3u) {
        return false;
    }
    const u8 sample_bits = bits_per_sample(mapping.color_channels);
    if (sample_bits == 0u) {
        return false;
    }
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (samples_per_pixel == 0u) {
        return false;
    }
    u64 total_pixels = (u64)width_pixels * (u64)height_pixels;
    if (total_pixels == 0u) {
        return false;
    }
    bool has_footer_text = (footer_text && footer_length > 0u);
    if (has_footer_text && !footer_layout.has_text) {
        return false;
    }
    u32 data_height_pixels = height_pixels;
    if (footer_layout.has_text) {
        if (footer_layout.data_height_pixels == 0u || footer_layout.data_height_pixels > height_pixels) {
            return false;
        }
        data_height_pixels = footer_layout.data_height_pixels;
    }
    if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
        return false;
    }
    u64 expected_bits_per_page = (u64)width_pixels *
                                 (u64)data_height_pixels *
                                 (u64)sample_bits *
                                 (u64)samples_per_pixel;
    if (expected_bits_per_page != bits_per_page) {
        return false;
    }
    u32 footer_rows = height_pixels - data_height_pixels;
    u8 footer_text_rgb[3] = {0u, 0u, 0u};
    u8 footer_background_rgb[3] = {255u, 255u, 255u};
    footer_select_colors(mapping.color_channels, footer_text_rgb, footer_background_rgb);
    output.release();
    if (!output.append_ascii("P3\n")) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_COLOR_CHANNELS", (u64)mapping.color_channels)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_BITS", payload_bit_count)) {
        return false;
    }
    if (ecc_summary && ecc_summary->enabled) {
        if (!append_comment_number(output, "MAKOCODE_ECC", 1u)) {
            return false;
        }
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_DATA", (u64)ecc_summary->block_data_symbols)) {
            return false;
        }
        if (!append_comment_number(output, "MAKOCODE_ECC_PARITY", (u64)ecc_summary->parity_symbols)) {
            return false;
        }
        if (!append_comment_number(output, "MAKOCODE_ECC_BLOCK_COUNT", ecc_summary->block_count)) {
            return false;
        }
        if (!append_comment_number(output, "MAKOCODE_ECC_ORIGINAL_BYTES", ecc_summary->original_bytes)) {
            return false;
        }
    } else {
        if (!append_comment_number(output, "MAKOCODE_ECC", 0u)) {
            return false;
        }
    }
    if (!append_comment_number(output, "MAKOCODE_PAGE_COUNT", page_count)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_PAGE_INDEX", page_index)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_PAGE_BITS", bits_per_page)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_PAGE_WIDTH_PX", (u64)mapping.page_width_pixels)) {
        return false;
    }
    if (!append_comment_number(output, "MAKOCODE_PAGE_HEIGHT_PX", (u64)mapping.page_height_pixels)) {
        return false;
    }
    if (footer_rows) {
        if (!append_comment_number(output, "MAKOCODE_FOOTER_ROWS", (u64)footer_rows)) {
            return false;
        }
        if (footer_layout.has_text) {
            if (!append_comment_number(output, "MAKOCODE_FONT_SIZE", (u64)footer_layout.font_size)) {
                return false;
            }
        }
    }
    if (!buffer_append_number(output, (u64)width_pixels) || !output.append_char(' ')) {
        return false;
    }
    if (!buffer_append_number(output, (u64)height_pixels) || !output.append_char('\n')) {
        return false;
    }
    if (!output.append_ascii("255\n")) {
        return false;
    }
    const u8* frame_data = frame_bits.data;
    u64 bit_cursor = bit_offset;
    for (u32 row = 0u; row < height_pixels; ++row) {
        bool is_footer_row = (row >= data_height_pixels);
        for (u32 column = 0u; column < width_pixels; ++column) {
            u8 rgb[3] = {0u, 0u, 0u};
            if (!is_footer_row) {
                u32 samples_raw[3] = {0u, 0u, 0u};
                for (u8 sample_index = 0u; sample_index < samples_per_pixel; ++sample_index) {
                    u32 sample = 0u;
                    for (u8 bit = 0u; bit < sample_bits; ++bit) {
                        u8 bit_value = 0u;
                        if (bit_cursor < frame_bit_count && frame_data) {
                            usize byte_index = (usize)(bit_cursor >> 3u);
                            u8 mask = (u8)(1u << (bit_cursor & 7u));
                            bit_value = (frame_data[byte_index] & mask) ? 1u : 0u;
                        }
                        sample |= ((u32)bit_value) << bit;
                        ++bit_cursor;
                    }
                    samples_raw[sample_index] = sample;
                }
                if (!map_samples_to_rgb(mapping.color_channels, samples_raw, rgb)) {
                    return false;
                }
            } else {
                rgb[0] = footer_background_rgb[0];
                rgb[1] = footer_background_rgb[1];
                rgb[2] = footer_background_rgb[2];
                if (has_footer_text && footer_is_text_pixel(footer_text, footer_length, footer_layout, column, row)) {
                    rgb[0] = footer_text_rgb[0];
                    rgb[1] = footer_text_rgb[1];
                    rgb[2] = footer_text_rgb[2];
                }
            }
            for (u8 channel = 0u; channel < 3u; ++channel) {
                if (channel) {
                    if (!output.append_char(' ')) {
                        return false;
                    }
                }
                if (!buffer_append_number(output, (u64)rgb[channel])) {
                    return false;
                }
            }
            if (!output.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}
static bool process_fiducial_option(int arg_count,
                                    char** args,
                                    int* arg_index,
                                    FiducialGridDefaults& defaults,
                                    const char* command_name,
                                    bool* handled) {
    if (!handled || !arg_index || !args) {
        return false;
    }
    *handled = false;
    int index = *arg_index;
    if (index < 0 || index >= arg_count) {
        return false;
    }
    const char* arg = args[index];
    if (!arg) {
        return true;
    }
    const char fiducial_prefix[] = "--fiducials=";
    const char* value_text = 0;
    if (ascii_equals_token(arg, ascii_length(arg), "--fiducials")) {
        if ((index + 1) >= arg_count) {
            console_write(2, command_name);
            console_line(2, ": --fiducials requires size,spacing[,margin]");
            return false;
        }
        value_text = args[index + 1];
        if (!value_text) {
            console_write(2, command_name);
            console_line(2, ": --fiducials requires size,spacing[,margin]");
            return false;
        }
        *arg_index = index + 1;
    } else if (ascii_starts_with(arg, fiducial_prefix)) {
        value_text = arg + (sizeof(fiducial_prefix) - 1u);
    } else {
        return true;
    }
    usize total_length = ascii_length(value_text);
    if (total_length == 0u) {
        console_write(2, command_name);
        console_line(2, ": --fiducials requires size,spacing[,margin]");
        return false;
    }
    u64 parsed_values[3];
    usize parsed_count = 0u;
    usize segment_start = 0u;
    for (usize i = 0u; i <= total_length; ++i) {
        bool at_end = (i == total_length);
        char c = at_end ? '\0' : value_text[i];
        if (c == ',' || at_end) {
            if (parsed_count >= 3u) {
                console_write(2, command_name);
                console_line(2, ": --fiducials accepts at most three numeric values");
                return false;
            }
            if (i < segment_start) {
                console_write(2, command_name);
                console_line(2, ": --fiducials parsing failure");
                return false;
            }
            usize segment_length = i - segment_start;
            if (segment_length == 0u) {
                console_write(2, command_name);
                console_line(2, ": --fiducials values must be non-empty");
                return false;
            }
            u64 number_value = 0u;
            if (!ascii_to_u64(value_text + segment_start, segment_length, &number_value)) {
                console_write(2, command_name);
                console_line(2, ": --fiducials values must be unsigned integers");
                return false;
            }
            parsed_values[parsed_count++] = number_value;
            segment_start = i + 1u;
            continue;
        }
        if (c < '0' || c > '9') {
            console_write(2, command_name);
            console_line(2, ": --fiducials values must be numeric");
            return false;
        }
    }
    if (parsed_count < 2u) {
        console_write(2, command_name);
        console_line(2, ": --fiducials requires at least size and spacing");
        return false;
    }
    if (parsed_values[0] == 0u || parsed_values[0] > 0xFFFFFFFFull) {
        console_write(2, command_name);
        console_line(2, ": --fiducials size must be between 1 and 4294967295");
        return false;
    }
    if (parsed_values[1] == 0u || parsed_values[1] > 0xFFFFFFFFull) {
        console_write(2, command_name);
        console_line(2, ": --fiducials spacing must be between 1 and 4294967295");
        return false;
    }
    if (parsed_values[1] < parsed_values[0]) {
        console_write(2, command_name);
        console_line(2, ": --fiducials spacing must be >= size");
        return false;
    }
    defaults.marker_size_pixels = (u32)parsed_values[0];
    defaults.spacing_pixels = (u32)parsed_values[1];
    if (parsed_count >= 3u) {
        if (parsed_values[2] > 0xFFFFFFFFull) {
            console_write(2, command_name);
            console_line(2, ": --fiducials margin must be between 0 and 4294967295");
            return false;
        }
        defaults.margin_pixels = (u32)parsed_values[2];
    }
    *handled = true;
    return true;
}

static bool process_image_mapping_option(int arg_count,
                                         char** args,
                                         int* arg_index,
                                         ImageMappingConfig& config,
                                         const char* command_name,
                                         bool* handled) {
    if (!handled || !arg_index || !args) {
        return false;
    }
    *handled = false;
    int index = *arg_index;
    if (index < 0 || index >= arg_count) {
        return false;
    }
    const char* arg = args[index];
    if (!arg) {
        return true;
    }

    const char color_prefix[] = "--color-channels=";
    const char* value_text = 0;
    usize length = 0u;
    if (ascii_equals_token(arg, ascii_length(arg), "--color-channels")) {
        if ((index + 1) >= arg_count) {
            console_write(2, command_name);
            console_line(2, ": --color-channels requires a value");
            return false;
        }
        value_text = args[index + 1];
        if (!value_text) {
            console_write(2, command_name);
            console_line(2, ": --color-channels requires a value");
            return false;
        }
        length = ascii_length(value_text);
        *arg_index = index + 1;
    } else if (ascii_starts_with(arg, color_prefix)) {
        value_text = arg + (sizeof(color_prefix) - 1u);
        length = ascii_length(value_text);
    }
    if (value_text) {
        if (length == 0u) {
            console_write(2, command_name);
            console_line(2, ": --color-channels requires a value");
            return false;
        }
        u64 value = 0u;
        if (!ascii_to_u64(value_text, length, &value)) {
            console_write(2, command_name);
            console_line(2, ": --color-channels value is not numeric");
            return false;
        }
        if (value == 0u || value > 3u) {
            console_write(2, command_name);
            console_line(2, ": --color-channels must be between 1 and 3");
            return false;
        }
        config.color_channels = (u8)value;
        config.color_set = true;
        *handled = true;
        return true;
    }

    const char width_prefix[] = "--page-width=";
    value_text = 0;
    length = 0u;
    if (ascii_equals_token(arg, ascii_length(arg), "--page-width")) {
        if ((index + 1) >= arg_count) {
            console_write(2, command_name);
            console_line(2, ": --page-width requires a value (pixels)");
            return false;
        }
        value_text = args[index + 1];
        if (!value_text) {
            console_write(2, command_name);
            console_line(2, ": --page-width requires a value (pixels)");
            return false;
        }
        length = ascii_length(value_text);
        *arg_index = index + 1;
    } else if (ascii_starts_with(arg, width_prefix)) {
        value_text = arg + (sizeof(width_prefix) - 1u);
        length = ascii_length(value_text);
    }
    if (value_text) {
        if (length == 0u) {
            console_write(2, command_name);
            console_line(2, ": --page-width requires a value (pixels)");
            return false;
        }
        u64 value = 0u;
        if (!ascii_to_u64(value_text, length, &value) || value == 0u || value > 0xFFFFFFFFu) {
            console_write(2, command_name);
            console_line(2, ": --page-width must be a positive integer number of pixels");
            return false;
        }
        config.page_width_pixels = (u32)value;
        config.page_width_set = true;
        *handled = true;
        return true;
    }

    const char height_prefix[] = "--page-height=";
    value_text = 0;
    length = 0u;
    if (ascii_equals_token(arg, ascii_length(arg), "--page-height")) {
        if ((index + 1) >= arg_count) {
            console_write(2, command_name);
            console_line(2, ": --page-height requires a value (pixels)");
            return false;
        }
        value_text = args[index + 1];
        if (!value_text) {
            console_write(2, command_name);
            console_line(2, ": --page-height requires a value (pixels)");
            return false;
        }
        length = ascii_length(value_text);
        *arg_index = index + 1;
    } else if (ascii_starts_with(arg, height_prefix)) {
        value_text = arg + (sizeof(height_prefix) - 1u);
        length = ascii_length(value_text);
    }
    if (value_text) {
        if (length == 0u) {
            console_write(2, command_name);
            console_line(2, ": --page-height requires a value (pixels)");
            return false;
        }
        u64 value = 0u;
        if (!ascii_to_u64(value_text, length, &value) || value == 0u || value > 0xFFFFFFFFu) {
            console_write(2, command_name);
            console_line(2, ": --page-height must be a positive integer number of pixels");
            return false;
        }
        config.page_height_pixels = (u32)value;
        config.page_height_set = true;
        *handled = true;
        return true;
    }
    return true;
}

static bool read_entire_stdin(makocode::ByteBuffer& buffer) {
    const usize chunk = 4096u;
    usize total = 0u;
    for (;;) {
        if (!buffer.ensure(total + chunk)) {
            return false;
        }
        int read_result = read(0, buffer.data + total, chunk);
        if (read_result < 0) {
            return false;
        }
        if (read_result == 0) {
            break;
        }
        total += (usize)read_result;
        buffer.size = total;
    }
    buffer.size = total;
    return true;
}

static bool read_entire_file(const char* path, makocode::ByteBuffer& buffer) {
    if (!path) {
        return false;
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    buffer.release();
    const usize chunk = 4096u;
    usize total = 0u;
    for (;;) {
        if (!buffer.ensure(total + chunk)) {
            close(fd);
            return false;
        }
        int read_result = read(fd, buffer.data + total, chunk);
        if (read_result < 0) {
            close(fd);
            return false;
        }
        if (read_result > 0 && total == 0u) {
            char debug_msg[64];
            u64_to_ascii((u64)read_result, debug_msg, sizeof(debug_msg));
            console_write(2, "debug read chunk size: ");
            console_line(2, debug_msg);
        }
        if (read_result == 0) {
            break;
        }
        total += (usize)read_result;
        buffer.size = total;
    }
    buffer.size = total;
    close(fd);
    if (buffer.size >= 8u && buffer.data) {
        console_write(2, "debug file first bytes: ");
        for (usize debug_i = 0u; debug_i < 8u && debug_i < buffer.size; ++debug_i) {
            char value_buffer[32];
            u64_to_ascii((u64)(unsigned char)buffer.data[debug_i], value_buffer, sizeof(value_buffer));
            console_write(2, value_buffer);
            if ((debug_i + 1u) < buffer.size && debug_i < 7u) {
                console_write(2, " ");
            }
        }
        console_line(2, "");
    }
    return true;
}

static const char ARCHIVE_MAGIC[8] = {'M', 'K', 'A', 'R', 'C', 'H', '0', '1'};
static const usize ARCHIVE_MAGIC_SIZE = (usize)sizeof(ARCHIVE_MAGIC);
static const usize ARCHIVE_HEADER_SIZE = ARCHIVE_MAGIC_SIZE + 4u;
static const usize MAX_ARCHIVE_PATH_COMPONENTS = 512u;

struct ArchiveBuildContext {
    makocode::ByteBuffer buffer;
    makocode::ByteBuffer path_registry;
    u32 entry_count;

    ArchiveBuildContext()
        : buffer(),
          path_registry(),
          entry_count(0u) {}
};

static bool archive_init(ArchiveBuildContext& ctx) {
    ctx.buffer.release();
    ctx.path_registry.release();
    ctx.entry_count = 0u;
    if (!ctx.buffer.ensure(ARCHIVE_HEADER_SIZE)) {
        return false;
    }
    for (usize i = 0u; i < ARCHIVE_MAGIC_SIZE; ++i) {
        ctx.buffer.data[i] = (u8)ARCHIVE_MAGIC[i];
    }
    write_le_u32(ctx.buffer.data + ARCHIVE_MAGIC_SIZE, 0u);
    ctx.buffer.size = ARCHIVE_HEADER_SIZE;
    return true;
}

static bool archive_append_u8(makocode::ByteBuffer& buffer, u8 value) {
    if (!buffer.ensure(buffer.size + 1u)) {
        return false;
    }
    buffer.data[buffer.size++] = value;
    return true;
}

static bool archive_append_u32(makocode::ByteBuffer& buffer, u32 value) {
    if (!buffer.ensure(buffer.size + 4u)) {
        return false;
    }
    write_le_u32(buffer.data + buffer.size, value);
    buffer.size += 4u;
    return true;
}

static bool archive_append_u64(makocode::ByteBuffer& buffer, u64 value) {
    if (!buffer.ensure(buffer.size + 8u)) {
        return false;
    }
    write_le_u64(buffer.data + buffer.size, value);
    buffer.size += 8u;
    return true;
}

static bool archive_append_bytes(makocode::ByteBuffer& buffer,
                                 const u8* data,
                                 usize length) {
    if (length == 0u) {
        return true;
    }
    if (!buffer.ensure(buffer.size + length)) {
        return false;
    }
    for (usize i = 0u; i < length; ++i) {
        buffer.data[buffer.size + i] = data ? data[i] : 0u;
    }
    buffer.size += length;
    return true;
}

static bool archive_register_path(ArchiveBuildContext& ctx,
                                  const char* path,
                                  usize length,
                                  bool* duplicate_out) {
    if (duplicate_out) {
        *duplicate_out = false;
    }
    if (!path || length == 0u) {
        return false;
    }
    usize cursor = 0u;
    while (cursor < ctx.path_registry.size) {
        const char* existing = (const char*)(ctx.path_registry.data + cursor);
        usize existing_length = ascii_length(existing);
        if (existing_length == length) {
            bool same = true;
            for (usize i = 0u; i < length; ++i) {
                if (existing[i] != path[i]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                if (duplicate_out) {
                    *duplicate_out = true;
                }
                return false;
            }
        }
        cursor += existing_length + 1u;
    }
    if (!ctx.path_registry.ensure(ctx.path_registry.size + length + 1u)) {
        return false;
    }
    u8* dest = ctx.path_registry.data + ctx.path_registry.size;
    for (usize i = 0u; i < length; ++i) {
        dest[i] = (u8)path[i];
    }
    dest[length] = 0u;
    ctx.path_registry.size += length + 1u;
    return true;
}

static bool archive_add_directory(ArchiveBuildContext& ctx,
                                  const char* rel_path) {
    if (!rel_path) {
        return false;
    }
    usize path_len = ascii_length(rel_path);
    if (path_len == 0u || path_len > 0xFFFFFFFFu) {
        return false;
    }
    bool duplicate = false;
    if (!archive_register_path(ctx, rel_path, path_len, &duplicate)) {
        if (duplicate) {
            console_write(2, "encode: duplicate entry path ");
            console_line(2, rel_path);
        }
        return false;
    }
    if (!archive_append_u8(ctx.buffer, 1u)) {
        return false;
    }
    if (!archive_append_u32(ctx.buffer, (u32)path_len)) {
        return false;
    }
    if (!archive_append_bytes(ctx.buffer, (const u8*)rel_path, path_len)) {
        return false;
    }
    ++ctx.entry_count;
    return true;
}

static bool archive_add_file(ArchiveBuildContext& ctx,
                             const char* rel_path,
                             const u8* data,
                             usize length) {
    if (!rel_path) {
        return false;
    }
    usize path_len = ascii_length(rel_path);
    if (path_len == 0u || path_len > 0xFFFFFFFFu) {
        return false;
    }
    bool duplicate = false;
    if (!archive_register_path(ctx, rel_path, path_len, &duplicate)) {
        if (duplicate) {
            console_write(2, "encode: duplicate entry path ");
            console_line(2, rel_path);
        }
        return false;
    }
    if (!archive_append_u8(ctx.buffer, 0u)) {
        return false;
    }
    if (!archive_append_u32(ctx.buffer, (u32)path_len)) {
        return false;
    }
    if (!archive_append_bytes(ctx.buffer, (const u8*)rel_path, path_len)) {
        return false;
    }
    if (!archive_append_u64(ctx.buffer, (u64)length)) {
        return false;
    }
    if (!archive_append_bytes(ctx.buffer, data, length)) {
        return false;
    }
    ++ctx.entry_count;
    return true;
}

static bool archive_finalize(ArchiveBuildContext& ctx) {
    if (ctx.buffer.size < ARCHIVE_HEADER_SIZE) {
        return false;
    }
    write_le_u32(ctx.buffer.data + ARCHIVE_MAGIC_SIZE, ctx.entry_count);
    return true;
}

static const char* find_last_path_component(const char* path) {
    if (!path) {
        return path;
    }
    usize length = ascii_length(path);
    if (length == 0u) {
        return path;
    }
    const char* start = path;
    const char* end = path + length;
    while (end > start) {
        char c = end[-1];
        if (c == '/' || c == '\\') {
            --end;
        } else {
            break;
        }
    }
    const char* component = start;
    const char* cursor = start;
    while (cursor < end) {
        char c = *cursor;
        if (c == '/' || c == '\\') {
            component = cursor + 1;
        }
        ++cursor;
    }
    if (component >= end) {
        return component;
    }
    return component;
}

static bool normalize_store_path(const char* source,
                                 makocode::ByteBuffer& output) {
    output.release();
    if (!source) {
        return false;
    }
    usize length = ascii_length(source);
    if (length == 0u) {
        return false;
    }
    makocode::ByteBuffer scratch;
    if (!scratch.ensure(length)) {
        return false;
    }
    scratch.size = length;
    for (usize i = 0u; i < length; ++i) {
        char c = source[i];
        if (c == '\\') {
            c = '/';
        }
        scratch.data[i] = (u8)c;
    }
    usize component_offsets[MAX_ARCHIVE_PATH_COMPONENTS];
    usize component_lengths[MAX_ARCHIVE_PATH_COMPONENTS];
    usize component_count = 0u;
    usize index = 0u;
    while (index < length) {
        char c = (char)scratch.data[index];
        if (c == '/') {
            ++index;
            continue;
        }
        usize start = index;
        while (index < length) {
            char d = (char)scratch.data[index];
            if (d == '/') {
                break;
            }
            ++index;
        }
        usize segment_length = index - start;
        if (segment_length == 0u) {
            continue;
        }
        if (segment_length == 1u && scratch.data[start] == '.') {
            continue;
        }
        if (segment_length == 2u &&
            scratch.data[start] == '.' &&
            scratch.data[start + 1u] == '.') {
            if (component_count > 0u) {
                --component_count;
            }
            continue;
        }
        if (component_count >= MAX_ARCHIVE_PATH_COMPONENTS) {
            scratch.release();
            return false;
        }
        component_offsets[component_count] = start;
        component_lengths[component_count] = segment_length;
        ++component_count;
    }
    if (component_count == 0u) {
        scratch.release();
        char* resolved = realpath(source, 0);
        if (!resolved) {
            return false;
        }
        const char* base = find_last_path_component(resolved);
        usize base_len = ascii_length(base);
        bool valid_base = (base_len > 0u);
        if (valid_base) {
            if (base_len == 1u && base[0] == '.') {
                valid_base = false;
            } else if (base_len == 2u && base[0] == '.' && base[1u] == '.') {
                valid_base = false;
            } else {
                for (usize i = 0u; i < base_len; ++i) {
                    char c = base[i];
                    if (c == '/' || c == '\\' || c == 0) {
                        valid_base = false;
                        break;
                    }
                }
            }
        }
        if (valid_base && output.ensure(base_len + 1u)) {
            for (usize i = 0u; i < base_len; ++i) {
                char c = base[i];
                if (c == '\\') {
                    c = '/';
                }
                output.data[i] = (u8)c;
            }
            output.data[base_len] = 0u;
            output.size = base_len;
            free(resolved);
            return true;
        }
        free(resolved);
        return false;
    }
    usize result_length = 0u;
    for (usize i = 0u; i < component_count; ++i) {
        result_length += component_lengths[i];
        if ((i + 1u) < component_count) {
            ++result_length;
        }
    }
    if (!output.ensure(result_length + 1u)) {
        scratch.release();
        return false;
    }
    usize cursor = 0u;
    for (usize i = 0u; i < component_count; ++i) {
        if (i) {
            output.data[cursor++] = (u8)'/';
        }
        usize start = component_offsets[i];
        usize seg_len = component_lengths[i];
        for (usize j = 0u; j < seg_len; ++j) {
            output.data[cursor++] = scratch.data[start + j];
        }
    }
    output.data[cursor] = 0u;
    output.size = cursor;
    scratch.release();
    return true;
}

static bool join_rel_path(const char* base,
                          const char* name,
                          makocode::ByteBuffer& out) {
    out.release();
    if (!name) {
        return false;
    }
    usize name_len = ascii_length(name);
    if (name_len == 0u) {
        return false;
    }
    usize base_len = base ? ascii_length(base) : 0u;
    bool include_base = (base_len > 0u);
    usize total = name_len;
    if (include_base) {
        total += base_len;
        char last = base[base_len - 1u];
        if (last != '/' && last != '\\') {
            ++total;
        }
    }
    if (!out.ensure(total + 1u)) {
        return false;
    }
    usize cursor = 0u;
    if (include_base) {
        for (usize i = 0u; i < base_len; ++i) {
            char c = base[i];
            if (c == '\\') {
                c = '/';
            }
            out.data[cursor++] = (u8)c;
        }
        if (cursor && out.data[cursor - 1u] != '/') {
            out.data[cursor++] = (u8)'/';
        }
    }
    for (usize i = 0u; i < name_len; ++i) {
        char c = name[i];
        if (c == '\\') {
            c = '/';
        }
        out.data[cursor++] = (u8)c;
    }
    out.data[cursor] = 0u;
    out.size = cursor;
    return true;
}

static bool join_fs_path(const char* base,
                         const char* name,
                         makocode::ByteBuffer& out) {
    out.release();
    if (!base || !name) {
        return false;
    }
    usize base_len = ascii_length(base);
    usize name_len = ascii_length(name);
    bool need_separator = (base_len > 0u);
    if (need_separator) {
        char last = base[base_len - 1u];
        if (last == '/' || last == '\\') {
            need_separator = false;
        }
    }
    usize total = base_len + name_len + (need_separator ? 1u : 0u);
    if (!out.ensure(total + 1u)) {
        return false;
    }
    usize cursor = 0u;
    for (usize i = 0u; i < base_len; ++i) {
        out.data[cursor++] = (u8)base[i];
    }
    if (need_separator) {
        out.data[cursor++] = (u8)'/';
    }
    for (usize i = 0u; i < name_len; ++i) {
        out.data[cursor++] = (u8)name[i];
    }
    out.data[cursor] = 0u;
    out.size = cursor;
    return true;
}

static bool archive_collect_directory(const char* fs_path,
                                      const char* rel_path,
                                      ArchiveBuildContext& ctx) {
    if (!fs_path || !rel_path) {
        return false;
    }
    if (!archive_add_directory(ctx, rel_path)) {
        return false;
    }
    DIR* dir = opendir(fs_path);
    if (!dir) {
        console_write(2, "encode: failed to open directory ");
        console_line(2, fs_path);
        return false;
    }
    struct dirent* entry = 0;
    while ((entry = readdir(dir)) != (struct dirent*)0) {
        const char* name = entry->d_name;
        if (!name) {
            continue;
        }
        if (name[0] == '.' &&
            (name[1] == '\0' ||
             (name[1] == '.' && name[2] == '\0'))) {
            continue;
        }
        makocode::ByteBuffer child_fs;
        if (!join_fs_path(fs_path, name, child_fs)) {
            closedir(dir);
            return false;
        }
        makocode::ByteBuffer child_rel;
        if (!join_rel_path(rel_path, name, child_rel)) {
            closedir(dir);
            return false;
        }
        struct stat st;
        if (stat((const char*)child_fs.data, &st) != 0) {
            console_write(2, "encode: unable to stat ");
            console_line(2, (const char*)child_fs.data);
            closedir(dir);
            return false;
        }
        if (S_ISDIR(st.st_mode)) {
            if (!archive_collect_directory((const char*)child_fs.data,
                                           (const char*)child_rel.data,
                                           ctx)) {
                closedir(dir);
                return false;
            }
        } else if (S_ISREG(st.st_mode)) {
            makocode::ByteBuffer file_data;
            if (!read_entire_file((const char*)child_fs.data, file_data)) {
                console_write(2, "encode: failed to read ");
                console_line(2, (const char*)child_fs.data);
                closedir(dir);
                return false;
            }
            if (!archive_add_file(ctx,
                                  (const char*)child_rel.data,
                                  file_data.data,
                                  file_data.size)) {
                file_data.release();
                closedir(dir);
                return false;
            }
            file_data.release();
        } else {
            console_write(2, "encode: unsupported entry type for ");
            console_line(2, (const char*)child_rel.data);
            closedir(dir);
            return false;
        }
    }
    closedir(dir);
    return true;
}

static bool is_safe_archive_path(const char* path, usize length) {
    if (!path || length == 0u) {
        return false;
    }
    if (path[0] == '/') {
        return false;
    }
    for (usize i = 0u; i < length; ++i) {
        char c = path[i];
        if (c == '\\' || c == '\0') {
            return false;
        }
    }
    usize index = 0u;
    while (index < length) {
        while (index < length && path[index] == '/') {
            ++index;
        }
        usize start = index;
        while (index < length && path[index] != '/') {
            ++index;
        }
        usize segment_length = index - start;
        if (segment_length == 0u) {
            continue;
        }
        if (segment_length == 1u && path[start] == '.') {
            return false;
        }
        if (segment_length == 2u &&
            path[start] == '.' &&
            path[start + 1u] == '.') {
            return false;
        }
    }
    return true;
}

static bool ensure_directory_exists(const char* path) {
    if (!path || !path[0]) {
        return true;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (mkdir(path, 0755) == 0) {
        return true;
    }
    if (errno == EEXIST) {
        if (stat(path, &st) == 0) {
            return S_ISDIR(st.st_mode);
        }
    }
    return false;
}

static bool ensure_directory_tree(const char* path) {
    if (!path || !path[0]) {
        return true;
    }
    makocode::ByteBuffer temp;
    usize length = ascii_length(path);
    if (!temp.ensure(length + 1u)) {
        return false;
    }
    for (usize i = 0u; i < length; ++i) {
        char c = path[i];
        if (c == '\\') {
            c = '/';
        }
        temp.data[i] = (u8)c;
    }
    temp.data[length] = 0u;
    temp.size = length;
    for (usize i = 1u; i <= length; ++i) {
        char c = (char)temp.data[i];
        if (c == '/' || c == 0) {
            char saved = (char)temp.data[i];
            temp.data[i] = 0u;
            const char* partial = (const char*)temp.data;
            if (partial[0] != 0) {
                if (!ensure_directory_exists(partial)) {
                    temp.data[i] = saved;
                    return false;
                }
            }
            temp.data[i] = (u8)saved;
        }
    }
    return true;
}

static bool ensure_parent_directories(const char* file_path) {
    if (!file_path) {
        return false;
    }
    usize length = ascii_length(file_path);
    if (length == 0u) {
        return false;
    }
    usize i = length;
    while (i > 0u) {
        char c = file_path[i - 1u];
        if (c == '/' || c == '\\') {
            break;
        }
        --i;
    }
    if (i == 0u) {
        return true;
    }
    makocode::ByteBuffer parent;
    if (!parent.ensure(i + 1u)) {
        return false;
    }
    for (usize j = 0u; j < i; ++j) {
        char c = file_path[j];
        if (c == '\\') {
            c = '/';
        }
        parent.data[j] = (u8)c;
    }
    parent.data[i] = 0u;
    parent.size = i;
    return ensure_directory_tree((const char*)parent.data);
}

static bool join_output_path(const char* base_dir,
                             const char* rel_path,
                             makocode::ByteBuffer& out) {
    out.release();
    if (!rel_path) {
        return false;
    }
    usize rel_len = ascii_length(rel_path);
    if (rel_len == 0u) {
        return false;
    }
    bool include_base = false;
    usize base_len = 0u;
    if (base_dir && base_dir[0]) {
        base_len = ascii_length(base_dir);
        if (!(base_len == 1u && base_dir[0] == '.')) {
            include_base = true;
        }
    }
    bool need_separator = false;
    if (include_base) {
        char last = base_dir[base_len - 1u];
        if (last != '/' && last != '\\') {
            need_separator = true;
        }
    }
    usize total = rel_len;
    if (include_base) {
        total += base_len + (need_separator ? 1u : 0u);
    }
    if (!out.ensure(total + 1u)) {
        return false;
    }
    usize cursor = 0u;
    if (include_base) {
        for (usize i = 0u; i < base_len; ++i) {
            char c = base_dir[i];
            if (c == '\\') {
                c = '/';
            }
            out.data[cursor++] = (u8)c;
        }
        if (need_separator) {
            out.data[cursor++] = (u8)'/';
        }
    }
    for (usize i = 0u; i < rel_len; ++i) {
        char c = rel_path[i];
        if (c == '\\') {
            c = '/';
        }
        out.data[cursor++] = (u8)c;
    }
    out.data[cursor] = 0u;
    out.size = cursor;
    return true;
}

static bool unpack_archive_to_directory(const makocode::ByteBuffer& payload,
                                        const char* output_dir) {
    if (!payload.data || payload.size < ARCHIVE_HEADER_SIZE) {
        console_line(2, "decode: payload missing archive header");
        return false;
    }
    for (usize i = 0u; i < ARCHIVE_MAGIC_SIZE; ++i) {
        if (payload.data[i] != (u8)ARCHIVE_MAGIC[i]) {
            console_line(2, "decode: payload is not a makocode archive");
            return false;
        }
    }
    u32 entry_count = read_le_u32(payload.data + ARCHIVE_MAGIC_SIZE);
    usize cursor = ARCHIVE_HEADER_SIZE;
    for (u32 entry_index = 0u; entry_index < entry_count; ++entry_index) {
        if (cursor >= payload.size) {
            console_line(2, "decode: archive truncated before entries completed");
            return false;
        }
        u8 entry_type = payload.data[cursor++];
        if ((payload.size - cursor) < 4u) {
            console_line(2, "decode: archive truncated during path length read");
            return false;
        }
        u32 path_length = read_le_u32(payload.data + cursor);
        cursor += 4u;
        if (path_length == 0u) {
            console_line(2, "decode: archive entry has empty path");
            return false;
        }
        if ((payload.size - cursor) < (usize)path_length) {
            console_line(2, "decode: archive truncated during path read");
            return false;
        }
        makocode::ByteBuffer path_buffer;
        if (!path_buffer.ensure(path_length + 1u)) {
            return false;
        }
        for (u32 i = 0u; i < path_length; ++i) {
            path_buffer.data[i] = payload.data[cursor + i];
        }
        path_buffer.data[path_length] = 0u;
        path_buffer.size = path_length;
        const char* entry_path = (const char*)path_buffer.data;
        if (!is_safe_archive_path(entry_path, path_length)) {
            console_write(2, "decode: unsafe archive path ");
            console_line(2, entry_path);
            return false;
        }
        cursor += (usize)path_length;
        if (entry_type == 1u) {
            makocode::ByteBuffer output_path;
            if (!join_output_path(output_dir, entry_path, output_path)) {
                return false;
            }
            if (!ensure_directory_tree((const char*)output_path.data)) {
                console_write(2, "decode: failed to create directory ");
                console_line(2, (const char*)output_path.data);
                return false;
            }
        } else if (entry_type == 0u) {
            if ((payload.size - cursor) < 8u) {
                console_line(2, "decode: archive truncated during file size read");
                return false;
            }
            u64 file_size = read_le_u64(payload.data + cursor);
            cursor += 8u;
            if ((payload.size - cursor) < file_size) {
                console_line(2, "decode: archive truncated during file data read");
                return false;
            }
            const u8* file_data = payload.data + cursor;
            cursor += (usize)file_size;
            makocode::ByteBuffer output_path;
            if (!join_output_path(output_dir, entry_path, output_path)) {
                return false;
            }
            if (!ensure_parent_directories((const char*)output_path.data)) {
                console_write(2, "decode: failed to prepare directories for ");
                console_line(2, (const char*)output_path.data);
                return false;
            }
            if (!write_bytes_to_file((const char*)output_path.data,
                                     file_data,
                                     (usize)file_size)) {
                console_write(2, "decode: failed to write file ");
                console_line(2, (const char*)output_path.data);
                return false;
            }
        } else {
            console_line(2, "decode: unknown archive entry type");
            return false;
        }
    }
    if (cursor != payload.size) {
        console_line(2, "decode: archive payload has trailing data");
        return false;
    }
    return true;
}

static void write_usage() {
    console_line(1, "MakoCode CLI");
    console_line(1, "Usage:");
    console_line(1, "  makocode encode [options]   (reads payload from file; emits PPM pages)");
    console_line(1, "  makocode decode [options] files... (reads PPM pages; use stdin when no files)");
    console_line(1, "  makocode test   [options]   (verifies two-page encode/decode per color)");
    console_line(1, "  makocode minify             (writes makocode_minified.cpp without comments)");
    console_line(1, "Options:");
    console_line(1, "  --color-channels N (1=Gray, 2=CMY, 3=RGB; default 1)");
    console_line(1, "  --page-width PX    (page width in pixels; default 2480)");
    console_line(1, "  --page-height PX   (page height in pixels; default 3508)");
    console_line(1, "  --fiducials S,D[,M] (marker size, spacing, optional margin; default 4,24,12)");
    console_line(1, "  --input PATH       (encode: repeat to add files or directories)");
    console_line(1, "  --output-dir PATH  (decode: destination directory; default .)");
    console_line(1, "  --ecc RATIO        (Reed-Solomon redundancy; 0 disables, e.g., 0.10)");
    console_line(1, "  --password TEXT    (encrypt payload with ChaCha20-Poly1305 using TEXT)");
    console_line(1, "  --no-filename      (omit payload filename from footer text)");
    console_line(1, "  --no-page-count    (omit page index/total from footer text)");
    console_line(1, "  --title TEXT       (optional footer title; letters, digits, common symbols)");
    console_line(1, "  --font-size PX     (footer font scale in pixels; default 1)");
}

static bool title_char_is_allowed(char c) {
    bool is_digit = (c >= '0' && c <= '9');
    bool is_upper = (c >= 'A' && c <= 'Z');
    bool is_lower = (c >= 'a' && c <= 'z');
    if (is_digit || is_upper || is_lower) {
        return true;
    }
    switch (c) {
        case ' ':
        case '!':
        case '@':
        case '#':
        case '$':
        case '%':
        case '^':
        case '&':
        case '*':
        case '+':
        case '(':
        case ')':
        case '{':
        case '}':
        case '[':
        case ']':
        case ':':
        case '"':
        case ';':
        case '\'':
        case '<':
        case '=':
        case '>':
        case '?':
        case ',':
        case '-':
        case '.':
        case '/':
        case '`':
        case '~':
        case '|':
        case '\\':
        case '_':
            return true;
        default:
            break;
    }
    return false;
}

static usize decimal_digit_count(u64 value) {
    if (value == 0u) {
        return 1u;
    }
    usize digits = 0u;
    while (value) {
        value /= 10u;
        ++digits;
    }
    return digits ? digits : 1u;
}

static usize footer_compute_page_text_length(const PageFooterConfig& footer,
                                             u64 page_index,
                                             u64 page_count) {
    usize length = 0u;
    bool need_separator = false;
    if (footer.has_title && footer.title_text && footer.title_length) {
        if (need_separator) {
            length += 3u; // " | "
        }
        length += footer.title_length;
        need_separator = true;
    }
    if (footer.display_filename && footer.has_filename && footer.filename_text && footer.filename_length) {
        if (need_separator) {
            length += 3u; // " | "
        }
        length += footer.filename_length;
        need_separator = true;
    }
    if (footer.display_page_info) {
        if (page_index == 0u) {
            page_index = 1u;
        }
        if (page_count == 0u) {
            page_count = 1u;
        }
        if (need_separator) {
            length += 3u; // " | "
        }
        length += 5u; // "Page "
        length += decimal_digit_count(page_index);
        length += 1u; // '/'
        length += decimal_digit_count(page_count);
    }
    return length;
}

static usize footer_compute_max_text_length(const PageFooterConfig& footer,
                                            u64 page_count) {
    if (page_count == 0u) {
        page_count = 1u;
    }
    usize max_length = 0u;
    for (u64 index = 1u; index <= page_count; ++index) {
        usize length = footer_compute_page_text_length(footer, index, page_count);
        if (length > max_length) {
            max_length = length;
        }
    }
    return max_length;
}

static bool footer_build_page_text(const PageFooterConfig& footer,
                                   u64 page_index,
                                   u64 page_count,
                                   makocode::ByteBuffer& buffer) {
    if (page_index == 0u) {
        page_index = 1u;
    }
    if (page_count == 0u) {
        page_count = 1u;
    }
    usize length = footer_compute_page_text_length(footer, page_index, page_count);
    if (!buffer.ensure(length + 1u)) {
        return false;
    }
    usize cursor = 0u;
    bool need_separator = false;
    if (footer.has_title && footer.title_text && footer.title_length) {
        if (need_separator) {
            buffer.data[cursor++] = ' ';
            buffer.data[cursor++] = '|';
            buffer.data[cursor++] = ' ';
        }
        for (usize i = 0u; i < footer.title_length; ++i) {
            buffer.data[cursor++] = (u8)footer.title_text[i];
        }
        need_separator = true;
    }
    if (footer.display_filename && footer.has_filename && footer.filename_text && footer.filename_length) {
        if (need_separator) {
            buffer.data[cursor++] = ' ';
            buffer.data[cursor++] = '|';
            buffer.data[cursor++] = ' ';
        }
        for (usize i = 0u; i < footer.filename_length; ++i) {
            buffer.data[cursor++] = (u8)footer.filename_text[i];
        }
        need_separator = true;
    }
    if (footer.display_page_info) {
        if (need_separator) {
            buffer.data[cursor++] = ' ';
            buffer.data[cursor++] = '|';
            buffer.data[cursor++] = ' ';
        }
        buffer.data[cursor++] = 'P';
        buffer.data[cursor++] = 'a';
        buffer.data[cursor++] = 'g';
        buffer.data[cursor++] = 'e';
        buffer.data[cursor++] = ' ';
        char number[32];
        u64_to_ascii(page_index, number, sizeof(number));
        usize digits = 0u;
        while (number[digits]) {
            buffer.data[cursor++] = (u8)number[digits++];
        }
        buffer.data[cursor++] = '/';
        u64_to_ascii(page_count, number, sizeof(number));
        digits = 0u;
        while (number[digits]) {
            buffer.data[cursor++] = (u8)number[digits++];
        }
    }
    if (cursor != length) {
        buffer.size = cursor;
        buffer.data[cursor] = 0u;
        return false;
    }
    buffer.data[length] = 0u;
    buffer.size = length;
    return true;
}

static int command_encode(int arg_count, char** args) {
    ImageMappingConfig mapping;
    PageFooterConfig footer_config;
    double ecc_redundancy = 0.0;
    static const usize MAX_INPUT_ITEMS = 512u;
    const char* input_paths[MAX_INPUT_ITEMS];
    usize input_count = 0u;
    makocode::ByteBuffer title_buffer;
    makocode::ByteBuffer filename_buffer;
    makocode::ByteBuffer password_buffer;
    bool have_password = false;
    for (int i = 0; i < arg_count; ++i) {
        bool handled = false;
        if (!process_image_mapping_option(arg_count, args, &i, mapping, "encode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        handled = false;
        if (!process_fiducial_option(arg_count, args, &i, g_fiducial_defaults, "encode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        const char* arg = args[i];
        if (!arg) {
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--no-filename")) {
            footer_config.display_filename = false;
            continue;
        }
        if (ascii_equals_token(arg, ascii_length(arg), "--no-page-count")) {
            footer_config.display_page_info = false;
            continue;
        }
        const char ecc_prefix[] = "--ecc=";
        const char* ecc_value = 0;
        usize ecc_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--ecc")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --ecc requires a numeric value");
                return 1;
            }
            ecc_value = args[i + 1];
            if (!ecc_value) {
                console_line(2, "encode: --ecc requires a numeric value");
                return 1;
            }
            ecc_length = ascii_length(ecc_value);
            i += 1;
        } else if (ascii_starts_with(arg, ecc_prefix)) {
            ecc_value = arg + (sizeof(ecc_prefix) - 1u);
            ecc_length = ascii_length(ecc_value);
        }
        if (ecc_value) {
            if (ecc_length == 0u) {
                console_line(2, "encode: --ecc requires a numeric value");
                return 1;
            }
            double redundancy_value = 0.0;
            if (!ascii_to_double(ecc_value, ecc_length, &redundancy_value)) {
                console_line(2, "encode: --ecc value is not a valid decimal number");
                return 1;
            }
            if (redundancy_value < 0.0 || redundancy_value > 8.0) {
                console_line(2, "encode: --ecc must be between 0.0 and 8.0");
                return 1;
            }
            ecc_redundancy = redundancy_value;
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
            if (!input_value || input_value[0] == '\0') {
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
        const char title_prefix[] = "--title=";
        const char* title_value = 0;
        usize title_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--title")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "encode: --title requires a non-empty value");
                return 1;
            }
            title_value = args[i + 1];
            if (!title_value) {
                console_line(2, "encode: --title requires a non-empty value");
                return 1;
            }
            title_length = ascii_length(title_value);
            i += 1;
        } else if (ascii_starts_with(arg, title_prefix)) {
            title_value = arg + (sizeof(title_prefix) - 1u);
            title_length = ascii_length(title_value);
        }
        if (title_value) {
            if (title_length == 0u) {
                console_line(2, "encode: --title requires a non-empty value");
                return 1;
            }
            if (!title_buffer.ensure(title_length + 1u)) {
                console_line(2, "encode: failed to allocate title buffer");
                return 1;
            }
            for (usize j = 0u; j < title_length; ++j) {
                char c = title_value[j];
                if (!title_char_is_allowed(c)) {
                    console_line(2, "encode: title supports letters, digits, space, and !@#$%^&*()_+-={}[]:\";'<>?,./`~|\\");
                    return 1;
                }
                title_buffer.data[j] = (u8)c;
            }
            title_buffer.data[title_length] = 0u;
            title_buffer.size = title_length;
            footer_config.has_title = true;
            footer_config.title_length = title_length;
            footer_config.title_text = (const char*)title_buffer.data;
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
            if (!font_value) {
                console_line(2, "encode: --font-size requires a positive integer value");
                return 1;
            }
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
            if (!password_value) {
                console_line(2, "encode: --password requires a non-empty value");
                return 1;
            }
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
        console_write(2, "encode: unknown option: ");
        console_line(2, arg);
        return 1;
    }
    if (input_count == 0u) {
        console_line(2, "encode: at least one --input PATH is required");
        return 1;
    }
    ArchiveBuildContext archive;
    if (!archive_init(archive)) {
        console_line(2, "encode: failed to initialize archive buffer");
        return 1;
    }
    bool single_input = (input_count == 1u);
    makocode::ByteBuffer single_normalized;
    bool have_single_normalized = false;
    for (usize path_index = 0u; path_index < input_count; ++path_index) {
        const char* input_path = input_paths[path_index];
        struct stat path_info;
        if (stat(input_path, &path_info) != 0) {
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
        if (!stored_rel || stored_rel[0] == '\0') {
            console_write(2, "encode: input path resolves to empty relative name ");
            console_line(2, input_path);
            return 1;
        }
        if (S_ISDIR(path_info.st_mode)) {
            if (!archive_collect_directory(input_path, stored_rel, archive)) {
                return 1;
            }
        } else if (S_ISREG(path_info.st_mode)) {
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
        if (single_input && !have_single_normalized) {
            if (!single_normalized.ensure(normalized_path.size + 1u)) {
                console_line(2, "encode: failed to record normalized path");
                return 1;
            }
            for (usize j = 0u; j < normalized_path.size; ++j) {
                single_normalized.data[j] = normalized_path.data[j];
            }
            single_normalized.data[normalized_path.size] = 0u;
            single_normalized.size = normalized_path.size;
            have_single_normalized = true;
        }
    }
    if (archive.entry_count == 0u) {
        console_line(2, "encode: no files or directories to encode");
        return 1;
    }
    if (!archive_finalize(archive)) {
        console_line(2, "encode: failed to finalize archive payload");
        return 1;
    }
    footer_config.filename_text = 0;
    footer_config.filename_length = 0u;
    footer_config.has_filename = false;
    if (footer_config.display_filename) {
        if (single_input && have_single_normalized) {
            const char* base_name = find_last_path_component((const char*)single_normalized.data);
            usize base_length = ascii_length(base_name);
            if (base_length == 0u) {
                console_line(2, "encode: input path yielded empty filename");
                return 1;
            }
            if (!filename_buffer.ensure(base_length + 1u)) {
                console_line(2, "encode: failed to allocate filename buffer");
                return 1;
            }
            for (usize j = 0u; j < base_length; ++j) {
                char c = base_name[j];
                if (!title_char_is_allowed(c)) {
                    console_line(2, "encode: filename contains unsupported characters for footer text");
                    return 1;
                }
                filename_buffer.data[j] = (u8)c;
            }
            filename_buffer.data[base_length] = 0u;
            filename_buffer.size = base_length;
            footer_config.filename_text = (const char*)filename_buffer.data;
            footer_config.filename_length = base_length;
            footer_config.has_filename = true;
        } else {
            const char bundle_label[] = "bundle";
            usize label_length = (usize)(sizeof(bundle_label) - 1u);
            if (!filename_buffer.ensure(label_length + 1u)) {
                console_line(2, "encode: failed to allocate filename buffer");
                return 1;
            }
            for (usize j = 0u; j < label_length; ++j) {
                filename_buffer.data[j] = (u8)bundle_label[j];
            }
            filename_buffer.data[label_length] = 0u;
            filename_buffer.size = label_length;
            footer_config.filename_text = (const char*)filename_buffer.data;
            footer_config.filename_length = label_length;
            footer_config.has_filename = true;
        }
    }
    if (footer_config.has_title && (!footer_config.title_text || footer_config.title_length == 0u)) {
        console_line(2, "encode: title configuration is invalid");
        return 1;
    }
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = ecc_redundancy;
    if (have_password) {
        if (!encoder.set_password((const char*)password_buffer.data, password_buffer.size)) {
            console_line(2, "encode: failed to set encryption password");
            return 1;
        }
    }
    if (!encoder.set_payload(archive.buffer.data, archive.buffer.size)) {
        console_line(2, "encode: failed to set payload");
        return 1;
    }
    if (!encoder.build()) {
        console_line(2, "encode: build failed");
        return 1;
    }
    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    u64 payload_bit_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count, payload_bit_count)) {
        console_line(2, "encode: failed to build frame");
        return 1;
    }
    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!compute_page_dimensions(mapping, width_pixels, height_pixels)) {
        console_line(2, "encode: invalid page dimensions");
        return 1;
    }
    u8 sample_bits = bits_per_sample(mapping.color_channels);
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (sample_bits == 0u || samples_per_pixel == 0u) {
        console_line(2, "encode: unsupported color configuration");
        return 1;
    }
    FooterLayout footer_layout;
    u32 data_height_pixels = height_pixels;
    u64 bits_per_page = 0u;
    u64 page_count = 1u;
    const u32 MAX_FOOTER_LAYOUT_PASSES = 16u;
    bool layout_converged = false;
    for (u32 pass = 0u; pass < MAX_FOOTER_LAYOUT_PASSES; ++pass) {
        u64 text_page_count = footer_config.display_page_info ? page_count : 1u;
        footer_config.max_text_length = footer_compute_max_text_length(footer_config, text_page_count);
        if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
            console_line(2, "encode: footer text does not fit within the page layout");
            return 1;
        }
        data_height_pixels = footer_layout.has_text ? footer_layout.data_height_pixels : height_pixels;
        if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
            console_line(2, "encode: invalid footer configuration");
            return 1;
        }
        bits_per_page = (u64)width_pixels * (u64)data_height_pixels * (u64)sample_bits * (u64)samples_per_pixel;
        if (bits_per_page == 0u) {
            console_line(2, "encode: page capacity is zero");
            return 1;
        }
        u64 new_page_count = (frame_bit_count + bits_per_page - 1u) / bits_per_page;
        if (new_page_count == 0u) {
            new_page_count = 1u;
        }
        if (!footer_config.display_page_info || new_page_count == page_count) {
            page_count = new_page_count;
            layout_converged = true;
            break;
        }
        page_count = new_page_count;
    }
    if (!layout_converged) {
        console_line(2, "encode: footer layout did not converge");
        return 1;
    }
    char timestamp_name[32];
    if (!utc_timestamp_string(timestamp_name, sizeof(timestamp_name))) {
        console_line(2, "encode: failed to construct timestamped filename");
        return 1;
    }
    makocode::ByteBuffer footer_text_buffer;
    const makocode::EccSummary* ecc_summary = &encoder.ecc_info();
    if (page_count == 1u) {
        makocode::ByteBuffer page_output;
        if (!footer_build_page_text(footer_config, 1u, page_count, footer_text_buffer)) {
            console_line(2, "encode: failed to build footer text");
            return 1;
        }
        const char* footer_text = footer_layout.has_text ? (const char*)footer_text_buffer.data : 0;
        usize footer_length = footer_layout.has_text ? footer_text_buffer.size : 0u;
        if (!encode_page_to_ppm(mapping,
                                frame_bits,
                                frame_bit_count,
                                0u,
                                width_pixels,
                                height_pixels,
                                1u,
                                1u,
                                bits_per_page,
                                payload_bit_count,
                                ecc_summary,
                                footer_text,
                                footer_length,
                                footer_layout,
                                page_output)) {
            console_line(2, "encode: failed to format ppm");
            return 1;
        }
        makocode::ByteBuffer fiducial_page;
        if (!apply_default_fiducial_grid(page_output, fiducial_page)) {
            console_line(2, "encode: failed to embed fiducial grid");
            return 1;
        }
        byte_buffer_move(page_output, fiducial_page);
        makocode::ByteBuffer output_name;
        if (!build_page_filename(output_name, timestamp_name, 1u, 1u)) {
            console_line(2, "encode: failed to build output filename");
            return 1;
        }
        if (!write_buffer_to_file((const char*)output_name.data, page_output)) {
            console_line(2, "encode: failed to write ppm file");
            return 1;
        }
        console_write(1, "encode: wrote 1 page (");
        console_write(1, (const char*)output_name.data);
        console_line(1, ")");
    } else {
        makocode::ByteBuffer name_buffer;
        for (u64 page = 0u; page < page_count; ++page) {
            makocode::ByteBuffer page_output;
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
                                    page_count,
                                    bits_per_page,
                                    payload_bit_count,
                                    ecc_summary,
                                    footer_text,
                                    footer_length,
                                    footer_layout,
                                    page_output)) {
                console_line(2, "encode: failed to format ppm page");
                return 1;
            }
            makocode::ByteBuffer fiducial_page;
            if (!apply_default_fiducial_grid(page_output, fiducial_page)) {
                console_line(2, "encode: failed to embed fiducial grid");
                return 1;
            }
            byte_buffer_move(page_output, fiducial_page);
            if (!build_page_filename(name_buffer, timestamp_name, page + 1u, page_count)) {
                console_line(2, "encode: failed to build filename");
                return 1;
            }
            if (!write_buffer_to_file((const char*)name_buffer.data, page_output)) {
                console_line(2, "encode: failed to write ppm file");
                return 1;
            }
        }
        makocode::ByteBuffer sample_name;
        if (!build_page_filename(sample_name, timestamp_name, 1u, page_count)) {
            console_line(2, "encode: failed to summarize filenames");
            return 1;
        }
        char digits[32];
        u64_to_ascii(page_count, digits, sizeof(digits));
        console_write(1, "encode: wrote ");
        console_write(1, digits);
        console_write(1, " pages (");
        console_write(1, (const char*)sample_name.data);
        console_line(1, " ...)");
    }
    return 0;
}

static int command_decode(int arg_count, char** args) {
    ImageMappingConfig mapping;
    static const usize MAX_INPUT_FILES = 256u;
    const char* input_files[MAX_INPUT_FILES];
    usize file_count = 0u;
    makocode::ByteBuffer password_buffer;
    bool have_password = false;
    makocode::ByteBuffer output_dir_buffer;
   const char* output_dir = ".";
   bool have_output_dir = false;
   for (int i = 0; i < arg_count; ++i) {
       bool handled = false;
        if (!process_image_mapping_option(arg_count, args, &i, mapping, "decode", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        const char* arg = args[i];
        if (!arg) {
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
            if (!output_value) {
                console_line(2, "decode: --output-dir requires a non-empty path");
                return 1;
            }
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
            if (!password_value) {
                console_line(2, "decode: --password requires a non-empty value");
                return 1;
            }
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
    makocode::ByteBuffer bitstream;
    u64 bit_count = 0u;
    PpmParserState aggregate_state;
    bool have_metadata = false;
    if (file_count == 0u) {
        makocode::ByteBuffer ppm_stream;
        if (!read_entire_stdin(ppm_stream)) {
            console_line(2, "decode: failed to read stdin");
            return 1;
        }
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        PpmParserState single_state;
        if (!ppm_extract_frame_bits(ppm_stream, mapping, frame_bits, frame_bit_count, single_state)) {
            console_line(2, "decode: invalid ppm input");
            return 1;
        }
        if (!frame_bits_to_payload(frame_bits.data, frame_bit_count, single_state, bitstream, bit_count)) {
            console_line(2, "decode: failed to extract payload bits");
            return 1;
        }
        aggregate_state = single_state;
        have_metadata = true;
    } else {
        makocode::BitWriter frame_aggregator;
        frame_aggregator.reset();
        bool aggregate_initialized = false;
        bool enforce_page_index = true;
        u64 expected_page_index = 1u;
        for (usize file_index = 0u; file_index < file_count; ++file_index) {
            makocode::ByteBuffer ppm_stream;
            console_write(2, "debug reading file: ");
            console_line(2, input_files[file_index]);
            if (!read_entire_file(input_files[file_index], ppm_stream)) {
                console_write(2, "decode: failed to read ");
                console_line(2, input_files[file_index]);
                return 1;
            }
            if (ppm_stream.size >= 8u && ppm_stream.data) {
                console_write(2, "debug read bytes: ");
                for (usize debug_i = 0u; debug_i < 8u && debug_i < ppm_stream.size; ++debug_i) {
                    char value_buffer[32];
                    u64_to_ascii((u64)(unsigned char)ppm_stream.data[debug_i], value_buffer, sizeof(value_buffer));
                    console_write(2, value_buffer);
                    if ((debug_i + 1u) < ppm_stream.size && debug_i < 7u) {
                        console_write(2, " ");
                    }
                }
                console_line(2, "");
            }
            makocode::ByteBuffer page_bits;
            u64 page_bit_count = 0u;
            PpmParserState page_state;
            if (!ppm_extract_frame_bits(ppm_stream, mapping, page_bits, page_bit_count, page_state)) {
                console_write(2, "decode: invalid ppm in ");
                console_line(2, input_files[file_index]);
                return 1;
            }
            if (!aggregate_initialized) {
                if (!merge_parser_state(aggregate_state, page_state)) {
                    console_line(2, "decode: inconsistent metadata");
                    return 1;
                }
                aggregate_initialized = true;
            } else {
                if (!merge_parser_state(aggregate_state, page_state)) {
                    console_line(2, "decode: conflicting metadata between pages");
                    return 1;
                }
            }
            if (enforce_page_index) {
                if (page_state.has_page_index) {
                    if (page_state.page_index_value != expected_page_index) {
                        console_line(2, "decode: unexpected page order");
                        return 1;
                    }
                } else {
                    enforce_page_index = false;
                }
            }
            u64 effective_bits = page_bit_count;
            if (page_state.has_page_bits && page_state.page_bits_value <= effective_bits) {
                effective_bits = page_state.page_bits_value;
            }
            if (!append_bits_from_buffer(frame_aggregator, page_bits.data, effective_bits)) {
                console_line(2, "decode: failed to assemble bitstream");
                return 1;
            }
            ++expected_page_index;
        }
        if (aggregate_state.has_page_count && aggregate_state.page_count_value != (u64)file_count) {
            console_line(2, "decode: page count metadata mismatch");
            return 1;
        }
        const u8* frame_data = frame_aggregator.data();
        u64 frame_bit_total = frame_aggregator.bit_size();
        if (!frame_bits_to_payload(frame_data, frame_bit_total, aggregate_state, bitstream, bit_count)) {
            console_line(2, "decode: failed to extract payload bits");
            return 1;
        }
        have_metadata = true;
    }
    bool ecc_header_repaired = false;
    makocode::EccHeaderInfo bitstream_header;
    bool bitstream_header_present = false;
    bool bitstream_header_valid = false;
    if (bitstream.data && bitstream.size >= makocode::ECC_HEADER_BYTES) {
        bitstream_header_present = makocode::parse_ecc_header(bitstream.data, bitstream.size, bitstream_header);
        bitstream_header_valid = bitstream_header_present && bitstream_header.valid && bitstream_header.enabled;
    }
    bool ecc_metadata_available = have_metadata &&
                                   aggregate_state.has_ecc_flag &&
                                   aggregate_state.ecc_flag_value;
    if (have_metadata &&
        aggregate_state.has_ecc_flag &&
        !aggregate_state.ecc_flag_value) {
        console_line(2, "decode: warning: payload was encoded without ECC protection");
    }
    if (!bitstream_header_valid &&
        ecc_metadata_available &&
        aggregate_state.has_ecc_block_data &&
        aggregate_state.has_ecc_parity &&
        aggregate_state.has_ecc_block_count &&
        aggregate_state.has_ecc_original_bytes &&
        bitstream.data &&
        bitstream.size >= makocode::ECC_HEADER_BYTES &&
        bit_count >= (u64)makocode::ECC_HEADER_BITS) {
        u64 block_data_value = aggregate_state.ecc_block_data_value;
        u64 parity_value = aggregate_state.ecc_parity_value;
        if (block_data_value <= 0xFFFFu && parity_value <= 0xFFFFu) {
            u8 header_bytes[makocode::ECC_HEADER_BYTES];
            if (makocode::build_ecc_header_bytes(header_bytes,
                                                 makocode::ECC_HEADER_BYTES,
                                                 (u16)block_data_value,
                                                 (u16)parity_value,
                                                 aggregate_state.ecc_block_count_value,
                                                 aggregate_state.ecc_original_bytes_value)) {
                bool differs = false;
                for (usize i = 0u; i < makocode::ECC_HEADER_BYTES; ++i) {
                    if (bitstream.data[i] != header_bytes[i]) {
                        differs = true;
                        break;
                    }
                }
                if (differs) {
                    for (usize i = 0u; i < makocode::ECC_HEADER_BYTES; ++i) {
                        bitstream.data[i] = header_bytes[i];
                    }
                    ecc_header_repaired = true;
                }
            }
        }
    } else if (have_metadata &&
               aggregate_state.has_ecc_flag &&
               aggregate_state.ecc_flag_value) {
        console_line(2, "decode: warning: ECC metadata incomplete; header reconstruction skipped");
    }
    if (ecc_header_repaired) {
        console_line(2, "decode: repaired ECC header from metadata");
    }
    makocode::DecoderContext decoder;
    const char* password_ptr = have_password ? (const char*)password_buffer.data : (const char*)0;
    usize password_length = have_password ? password_buffer.size : 0u;
    if (!decoder.parse(bitstream.data, bit_count, password_ptr, password_length)) {
        if (decoder.password_auth_failed()) {
            console_line(2, "decode: decryption failed (password mismatch or corrupted data)");
            return 1;
        }
        if (decoder.ecc_correction_failed()) {
            console_line(2, "decode: ECC could not repair the payload");
        } else {
            console_line(2, "decode: parse failure");
        }
        return 1;
    }
    if (decoder.password_attempt_made() && decoder.password_was_ignored()) {
        console_line(2, "decode: warning: payload was not encrypted; password ignored");
    }
   if (decoder.ecc_correction_failed()) {
        console_line(2, "decode: warning: payload may contain uncorrected errors");
    }
    if (!decoder.has_payload) {
        console_line(2, "decode: no payload recovered");
        return 1;
    }
    if (!unpack_archive_to_directory(decoder.payload, output_dir)) {
        return 1;
    }
    console_write(1, "decode: wrote files to ");
    console_line(1, output_dir);
    return 0;
}

static bool build_payload_frame(const ImageMappingConfig& mapping,
                                usize payload_size,
                                u64 seed,
                                const char* password,
                                usize password_length,
                                makocode::ByteBuffer& payload,
                                makocode::EncoderContext& encoder,
                                makocode::ByteBuffer& frame_bits,
                                u64& frame_bit_count,
                                u64& payload_bit_count) {
    if (!makocode::generate_random_bytes(payload, payload_size, seed)) {
        return false;
    }
    if (!encoder.set_password(password, password_length)) {
        return false;
    }
    if (!encoder.set_payload(payload.data, payload.size)) {
        return false;
    }
    if (!encoder.build()) {
        return false;
    }
    return build_frame_bits(encoder, mapping, frame_bits, frame_bit_count, payload_bit_count);
}

static bool compute_frame_bit_count(const ImageMappingConfig& mapping,
                                    usize payload_size,
                                    u64 seed,
                                    double ecc_redundancy,
                                    const char* password,
                                    usize password_length,
                                    u64& frame_bit_count) {
    makocode::ByteBuffer payload;
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = ecc_redundancy;
    makocode::ByteBuffer frame_bits;
    u64 payload_bits = 0u;
    if (!build_payload_frame(mapping,
                              payload_size,
                              seed,
                              password,
                              password_length,
                              payload,
                              encoder,
                              frame_bits,
                              frame_bit_count,
                              payload_bits)) {
        return false;
    }
    return true;
}

static bool validate_ecc_random_bit_flips(const makocode::ByteBuffer& original_payload,
                                          const makocode::ByteBuffer& compressed_payload,
                                          const makocode::ByteBuffer& encoded_bits,
                                          u64 encoded_bit_count,
                                          const makocode::EccSummary& summary,
                                          u64 seed,
                                          const char* password,
                                          usize password_length) {
    if (!summary.enabled || summary.parity_symbols < 2u) {
        return true;
    }
    if (!encoded_bits.data || encoded_bits.size == 0u || encoded_bit_count == 0u) {
        return true;
    }
    if (original_payload.size == 0u) {
        return true;
    }
    usize byte_count = (usize)((encoded_bit_count + 7u) >> 3u);
    if (byte_count <= makocode::ECC_HEADER_BYTES) {
        return true;
    }
    if (byte_count > encoded_bits.size) {
        return false;
    }
    makocode::ByteBuffer corrupted;
    if (!corrupted.ensure(byte_count)) {
        return false;
    }
    for (usize i = 0u; i < byte_count; ++i) {
        corrupted.data[i] = encoded_bits.data ? encoded_bits.data[i] : 0u;
    }
    corrupted.size = byte_count;
    u64 max_symbol_errors = summary.parity_symbols / 2u;
    if (max_symbol_errors == 0u) {
        return true;
    }
    u64 available_symbols = (u64)(byte_count - makocode::ECC_HEADER_BYTES);
    if (available_symbols == 0u) {
        return true;
    }
    if (max_symbol_errors > available_symbols) {
        max_symbol_errors = available_symbols;
    }
    u64 desired_errors = (max_symbol_errors > 0u) ? 1u : 0u;
    if (desired_errors == 0u) {
        return true;
    }
    usize chosen[4] = {0u, 0u, 0u, 0u};
    u64 prng = seed ? seed : 0x9e3779b97f4a7c15ull;
    for (u64 error_index = 0u; error_index < desired_errors; ++error_index) {
        for (;;) {
            prng = prng * 6364136223846793005ull + 0x9e3779b97f4a7c15ull;
            usize candidate = (usize)((prng >> 24u) % (usize)available_symbols);
            bool duplicate = false;
            for (u64 seen = 0u; seen < error_index; ++seen) {
                if (chosen[seen] == candidate) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                chosen[error_index] = candidate;
                break;
            }
        }
        prng = prng * 6364136223846793005ull + 0x9e3779b97f4a7c15ull;
        u8 bit_mask = (u8)(1u << ((prng >> 11u) & 7u));
        if (bit_mask == 0u) {
            bit_mask = 1u;
        }
        usize byte_offset = makocode::ECC_HEADER_BYTES + chosen[error_index];
        if (byte_offset >= corrupted.size) {
            byte_offset = corrupted.size - 1u;
        }
        corrupted.data[byte_offset] ^= bit_mask;
    }
    makocode::ByteBuffer ecc_stream;
    if (!ecc_stream.ensure(byte_count)) {
        console_line(2, "test: failed to allocate ECC workspace during validation");
        return false;
    }
    for (usize i = 0u; i < byte_count; ++i) {
        ecc_stream.data[i] = corrupted.data[i];
    }
    ecc_stream.size = byte_count;
    if (!makocode::unshuffle_encoded_stream(ecc_stream.data, byte_count)) {
        console_line(2, "test: failed to unshuffle stream during ECC validation");
        return false;
    }
    makocode::EccHeaderInfo header_info;
    if (!makocode::parse_ecc_header(ecc_stream.data, byte_count, header_info) || !header_info.valid || !header_info.enabled) {
        console_line(2, "test: parse_ecc_header failed during ECC validation");
        return false;
    }
    makocode::ByteBuffer repaired_payload;
    if (!makocode::decode_ecc_payload(ecc_stream.data + makocode::ECC_HEADER_BYTES, header_info, repaired_payload)) {
        console_line(2, "test: decode_ecc_payload failed during ECC validation");
        return false;
    }
    if (compressed_payload.size == repaired_payload.size) {
        for (usize i = 0u; i < repaired_payload.size; ++i) {
            if (repaired_payload.data[i] != compressed_payload.data[i]) {
                return false;
            }
        }
    }
    makocode::DecoderContext validator;
    if (!validator.parse(corrupted.data, (usize)encoded_bit_count, password, password_length)) {
        console_line(2, "test: validator parse failed during ECC validation");
        return false;
    }
    if (password && password_length > 0u && validator.password_attempt_made() && validator.password_was_ignored()) {
        console_line(2, "test: validator ignored password during ECC validation");
        return false;
    }
    if (!validator.has_payload || validator.payload.size != original_payload.size) {
        console_line(2, "test: validator payload size mismatch during ECC validation");
        return false;
    }
    for (usize i = 0u; i < original_payload.size; ++i) {
        if (validator.payload.data[i] != original_payload.data[i]) {
            console_line(2, "test: validator payload mismatch during ECC validation");
            return false;
        }
    }
    return true;
}



static void append_number_ascii(u64 value, char buffer[32]) {
    u64_to_ascii(value, buffer, 32);
}

static bool ensure_directory(const char* path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return (st.st_mode & S_IFDIR) != 0;
    }
    if (mkdir(path, 0700) == 0) {
        return true;
    }
    return errno == EEXIST;
}

static bool write_ppm_fixture(const char* path,
                              unsigned width,
                              unsigned height,
                              const unsigned char* pixels)
{
    int fd = creat(path, 0600);
    if (fd < 0) {
        return false;
    }
    if (write(fd, "P5\n", 3) != 3) {
        close(fd);
        return false;
    }
    char number[32];
    u64_to_ascii(width, number, sizeof(number));
    usize len = ascii_length(number);
    if (write(fd, number, len) != (int)len) {
        close(fd);
        return false;
    }
    if (write(fd, " ", 1) != 1) {
        close(fd);
        return false;
    }
    u64_to_ascii(height, number, sizeof(number));
    len = ascii_length(number);
    if (write(fd, number, len) != (int)len) {
        close(fd);
        return false;
    }
    if (write(fd, "\n255\n", 5) != 5) {
        close(fd);
        return false;
    }
    usize total = (usize)width * (usize)height;
    if (write(fd, pixels, total) != (int)total) {
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

static bool write_invalid_maxval_fixture(const char* path)
{
    int fd = creat(path, 0600);
    if (fd < 0) return false;
    if (write(fd, "P5\n2 2\n1023\n", 12) != 12) {
        close(fd);
        return false;
    }
    unsigned char zeros[4] = {0, 0, 0, 0};
    if (write(fd, zeros, 4) != 4) {
        close(fd);
        return false;
    }
    close(fd);
    return true;
}

static bool write_buffer_to_directory(const char* directory,
                                      const makocode::ByteBuffer& filename,
                                      const makocode::ByteBuffer& buffer)
{
    if (!directory || !filename.data || filename.size == 0u) {
        return false;
    }
    makocode::ByteBuffer path;
    path.release();
    if (!path.append_ascii(directory)) {
        return false;
    }
    if (!path.append_char('/')) {
        return false;
    }
    if (!path.append_bytes(filename.data,
                           filename.size ? filename.size - 1u : 0u)) {
        return false;
    }
    if (!path.append_char('\0')) {
        return false;
    }
    return write_buffer_to_file((const char*)path.data, buffer);
}

static bool extract_ppm_pixels(const makocode::ByteBuffer& ppm,
                               unsigned& width,
                               unsigned& height,
                               makocode::ByteBuffer& rgb_out)
{
    if (!ppm.data || ppm.size < 2u) {
        return false;
    }
    const u8* cursor = ppm.data;
    const u8* end = ppm.data + ppm.size;
    if (*cursor != 'P') {
        return false;
    }
    cursor++;
    if (cursor >= end) {
        return false;
    }
    u8 format = *cursor++;
    if (format != '3' && format != '6') {
        return false;
    }
    auto skip_ws = [&](void) {
        while (cursor < end) {
            u8 ch = *cursor;
            if (ch == '#') {
                while (cursor < end && *cursor != '\n') cursor++;
            } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                cursor++;
            } else {
                break;
            }
        }
        return cursor < end;
    };
    auto parse_uint = [&](unsigned& out_value) -> bool {
        if (!skip_ws()) return false;
        unsigned value = 0;
        bool any = false;
        while (cursor < end) {
            u8 ch = *cursor;
            if (ch < '0' || ch > '9') break;
            any = true;
            value = value * 10u + (unsigned)(ch - '0');
            cursor++;
        }
        if (!any) return false;
        out_value = value;
        return true;
    };
    if (!parse_uint(width) || !parse_uint(height)) {
        return false;
    }
    unsigned maxval = 0;
    if (!parse_uint(maxval) || maxval != 255u) {
        return false;
    }
    if (!skip_ws()) {
        return false;
    }
    usize pixel_count = (usize)width * (usize)height;
    usize expected_bytes = pixel_count * 3u;
    if (!rgb_out.ensure(expected_bytes)) {
        return false;
    }
    if (format == '6') {
        if ((usize)(end - cursor) < expected_bytes) {
            return false;
        }
        for (usize i = 0; i < expected_bytes; ++i) {
            rgb_out.data[i] = cursor[i];
        }
        rgb_out.size = expected_bytes;
        return true;
    }
    // P3 ascii
    for (usize i = 0; i < expected_bytes; ++i) {
        if (!skip_ws()) {
            return false;
        }
        unsigned value = 0;
        bool any = false;
        while (cursor < end) {
            u8 ch = *cursor;
            if (ch < '0' || ch > '9') break;
            any = true;
            value = value * 10u + (unsigned)(ch - '0');
            cursor++;
        }
        if (!any) {
            return false;
        }
        if (value > 255u) {
            value = 255u;
        }
        rgb_out.data[i] = (u8)value;
    }
    rgb_out.size = expected_bytes;
    return true;
}

static bool collect_makocode_comments(const makocode::ByteBuffer& ppm,
                                      makocode::ByteBuffer& comments_out)
{
    comments_out.release();
    if (!ppm.data || ppm.size == 0u) {
        return true;
    }
    const u8* cursor = ppm.data;
    const u8* end = ppm.data + ppm.size;
    const char tag[] = "MAKOCODE_";
    usize tag_length = ascii_length(tag);
    while (cursor < end) {
        if (*cursor != '#') {
            ++cursor;
            continue;
        }
        const u8* comment_start = cursor;
        ++cursor;
        const u8* line_start = cursor;
        while (cursor < end && *cursor != '\n' && *cursor != '\r') {
            ++cursor;
        }
        const u8* line_end = cursor;
        const u8* trimmed = line_start;
        while (trimmed < line_end && (*trimmed == ' ' || *trimmed == '\t')) {
            ++trimmed;
        }
        bool keep = (usize)(line_end - trimmed) >= tag_length;
        if (keep) {
            for (usize i = 0u; i < tag_length; ++i) {
                if (trimmed[i] != (u8)tag[i]) {
                    keep = false;
                    break;
                }
            }
        }
        if (keep) {
            usize comment_length = (usize)(line_end - comment_start);
            if (comment_length) {
                if (!comments_out.append_bytes(comment_start, comment_length)) {
                    return false;
                }
            } else {
                if (!comments_out.append_char('#')) {
                    return false;
                }
            }
        }
        if (cursor < end) {
            if (*cursor == '\r') {
                if (keep) {
                    if (!comments_out.append_char('\r')) {
                        return false;
                    }
                }
                ++cursor;
                if (cursor < end && *cursor == '\n') {
                    if (keep) {
                        if (!comments_out.append_char('\n')) {
                            return false;
                        }
                    }
                    ++cursor;
                }
            } else if (*cursor == '\n') {
                if (keep) {
                    if (!comments_out.append_char('\n')) {
                        return false;
                    }
                }
                ++cursor;
            }
        } else if (keep) {
            if (!comments_out.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}

static bool simulate_scan_distortion(const makocode::ByteBuffer& baseline_ppm,
                                     makocode::ByteBuffer& distorted_ppm,
                                     double rotation_degrees,
                                     double skew_pixels_x,
                                     double skew_pixels_y)
{
    unsigned width = 0;
    unsigned height = 0;
    makocode::ByteBuffer rgb_data;
    makocode::ByteBuffer metadata_comments;
    if (!collect_makocode_comments(baseline_ppm, metadata_comments)) {
        return false;
    }
    if (!extract_ppm_pixels(baseline_ppm, width, height, rgb_data)) {
        return false;
    }
    usize pixel_count = (usize)width * (usize)height;
    if (pixel_count == 0u) {
        return false;
    }
    const u8* src_pixels = rgb_data.data;
    u32 rng_state = width * 73856093u ^ height * 19349663u ^ (u32)pixel_count;
    auto rng_next = [&]() -> double {
        rng_state = rng_state * 1664525u + 1013904223u;
        return (double)((int)(rng_state >> 16) - 32768) / 65536.0;
    };
    static const int fixture_bias[4] = {-22, 14, -9, 27};
    static const double channel_bleed[3] = {0.04, -0.07, 0.03};
    makocode::ByteBuffer working_pixels;
    makocode::ByteBuffer skew_pixels;
    usize total_bytes = pixel_count * 3u;
    if (!working_pixels.ensure(total_bytes)) {
        return false;
    }
    for (usize i = 0u; i < total_bytes; ++i) {
        working_pixels.data[i] = 255u;
    }
    bool apply_distortion = (rotation_degrees == 0.0);
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            usize index = ((usize)y * (usize)width + (usize)x) * 3u;
            if (apply_distortion) {
                double base_r = (double)src_pixels[index + 0u];
                double base_g = (double)src_pixels[index + 1u];
                double base_b = (double)src_pixels[index + 2u];
                double average = (base_r + base_g + base_b) / 3.0;
                int fixture_index = (int)(((y & 1u) << 1u) | (x & 1u));
                double fixture_offset = (double)fixture_bias[fixture_index];
                for (u32 channel = 0u; channel < 3u; ++channel) {
                    double base_value = (double)src_pixels[index + channel];
                    double soften = base_value * 0.86 + average * 0.14;
                    double tone_curve = (average - 128.0) * 0.06;
                    double bleed = (base_value - average) * channel_bleed[channel];
                    double jitter = rng_next() * 8.0;
                    double value = soften + tone_curve + bleed + fixture_offset + jitter;
                    if (value < 0.0) {
                        value = 0.0;
                    } else if (value > 255.0) {
                        value = 255.0;
                    }
                    working_pixels.data[index + channel] = (u8)(value + 0.5);
                }
            } else {
                working_pixels.data[index + 0u] = src_pixels[index + 0u];
                working_pixels.data[index + 1u] = src_pixels[index + 1u];
                working_pixels.data[index + 2u] = src_pixels[index + 2u];
            }
        }
    }
    working_pixels.size = total_bytes;

    const u8* final_pixels = working_pixels.data;
    unsigned final_width = width;
    unsigned final_height = height;
    makocode::ByteBuffer rotation_pixels;
    bool emit_rotation_metadata = false;
    bool emit_skew_metadata = false;
    unsigned rotation_source_width = width;
    unsigned rotation_source_height = height;
    double rotation_margin_value = 0.0;
    double skew_margin_value = 0.0;
    double skew_top_value = 0.0;
    double skew_bottom_value = 0.0;
    unsigned skew_source_width = width;
    unsigned skew_source_height = height;
    if (rotation_degrees != 0.0) {
        double radians = rotation_degrees * (3.14159265358979323846 / 180.0);
        double cos_theta = cos(radians);
        double sin_theta = sin(radians);
        double center_x = ((double)width - 1.0) * 0.5;
        double center_y = ((double)height - 1.0) * 0.5;
        double min_x = 0.0;
        double max_x = 0.0;
        double min_y = 0.0;
        double max_y = 0.0;
        for (int i = 0; i < 4; ++i) {
            double corner_x = (i & 1) ? (double)(width - 1u) : 0.0;
            double corner_y = (i & 2) ? (double)(height - 1u) : 0.0;
            double dx = corner_x - center_x;
            double dy = corner_y - center_y;
            double rx = dx * cos_theta - dy * sin_theta;
            double ry = dx * sin_theta + dy * cos_theta;
            if (i == 0) {
                min_x = max_x = rx;
                min_y = max_y = ry;
            } else {
                if (rx < min_x) min_x = rx;
                if (rx > max_x) max_x = rx;
                if (ry < min_y) min_y = ry;
                if (ry > max_y) max_y = ry;
            }
        }
        double margin = 4.0;
        double offset_x = -min_x + margin;
        double offset_y = -min_y + margin;
        double span_x = max_x - min_x;
        double span_y = max_y - min_y;
        unsigned rotated_width = (unsigned)ceil(span_x + margin * 2.0 + 1.0);
        unsigned rotated_height = (unsigned)ceil(span_y + margin * 2.0 + 1.0);
        if (rotated_width == 0u || rotated_height == 0u) {
            return false;
        }
        usize rotated_count = (usize)rotated_width * (usize)rotated_height;
        if (!rotation_pixels.ensure(rotated_count * 3u)) {
            return false;
        }
        for (usize i = 0u; i < rotated_count * 3u; ++i) {
            rotation_pixels.data[i] = 255u;
        }
        for (unsigned ty = 0u; ty < rotated_height; ++ty) {
            double target_y = (double)ty - offset_y;
            for (unsigned tx = 0u; tx < rotated_width; ++tx) {
                double target_x = (double)tx - offset_x;
                double sx = target_x * cos_theta + target_y * sin_theta;
                double sy = -target_x * sin_theta + target_y * cos_theta;
                double src_x = sx + center_x;
                double src_y = sy + center_y;
                if (src_x >= 0.0 && src_x <= (double)(width - 1u) &&
                    src_y >= 0.0 && src_y <= (double)(height - 1u)) {
                    unsigned x0 = (unsigned)floor(src_x);
                    unsigned y0 = (unsigned)floor(src_y);
                    unsigned x1 = (x0 + 1u < width) ? (x0 + 1u) : x0;
                    unsigned y1 = (y0 + 1u < height) ? (y0 + 1u) : y0;
                    double fx = src_x - (double)x0;
                    double fy = src_y - (double)y0;
                    usize idx00 = ((usize)y0 * (usize)width + (usize)x0) * 3u;
                    usize idx10 = ((usize)y0 * (usize)width + (usize)x1) * 3u;
                    usize idx01 = ((usize)y1 * (usize)width + (usize)x0) * 3u;
                    usize idx11 = ((usize)y1 * (usize)width + (usize)x1) * 3u;
                    usize dst_index = ((usize)ty * (usize)rotated_width + (usize)tx) * 3u;
                    for (u32 channel = 0u; channel < 3u; ++channel) {
                        double v00 = (double)final_pixels[idx00 + channel];
                        double v10 = (double)final_pixels[idx10 + channel];
                        double v01 = (double)final_pixels[idx01 + channel];
                        double v11 = (double)final_pixels[idx11 + channel];
                        double top = v00 + (v10 - v00) * fx;
                        double bottom = v01 + (v11 - v01) * fx;
                        double value = top + (bottom - top) * fy;
                        if (value < 0.0) value = 0.0;
                        if (value > 255.0) value = 255.0;
                        rotation_pixels.data[dst_index + channel] = (u8)(value + 0.5);
                    }
                }
            }
        }
        rotation_pixels.size = rotated_count * 3u;
        final_pixels = rotation_pixels.data;
        final_width = rotated_width;
        final_height = rotated_height;
        emit_rotation_metadata = true;
        rotation_source_width = width;
        rotation_source_height = height;
        rotation_margin_value = margin;
    }

    if (skew_pixels_x != 0.0 || skew_pixels_y != 0.0) {
        if (final_width == 0u || final_height == 0u) {
            return false;
        }
        skew_source_width = final_width;
        skew_source_height = final_height;
        double top_shift = skew_pixels_x;
        double bottom_shift = skew_pixels_y;
        double min_shift = (top_shift < bottom_shift) ? top_shift : bottom_shift;
        double max_shift = (top_shift > bottom_shift) ? top_shift : bottom_shift;
        double margin_left = (min_shift < 0.0) ? (-min_shift) : 0.0;
        double margin_right = (max_shift > 0.0) ? max_shift : 0.0;
        double total_extra = margin_left + margin_right;
        unsigned extra_width = (unsigned)ceil(total_extra);
        if (total_extra > 0.0 && extra_width == 0u) {
            extra_width = 1u;
        }
        unsigned sheared_width = final_width + extra_width;
        if (sheared_width == 0u) {
            return false;
        }
        usize sheared_count = (usize)sheared_width * (usize)final_height;
        if (!skew_pixels.ensure(sheared_count * 3u)) {
            return false;
        }
        for (usize i = 0u; i < sheared_count * 3u; ++i) {
            skew_pixels.data[i] = 255u;
        }
        double denom_height = (final_height > 1u) ? (double)(final_height - 1u) : 1.0;
        for (unsigned dy = 0u; dy < final_height; ++dy) {
            double row_factor = (denom_height > 0.0) ? ((double)dy / denom_height) : 0.0;
            double row_shift = top_shift * (1.0 - row_factor) + bottom_shift * row_factor;
            for (unsigned dx = 0u; dx < sheared_width; ++dx) {
                double source_x = (double)dx - margin_left - row_shift;
                if (source_x < 0.0) {
                    source_x = 0.0;
                }
                double max_source_x = (final_width > 0u) ? (double)(final_width - 1u) : 0.0;
                if (source_x > max_source_x) {
                    source_x = max_source_x;
                }
                unsigned nearest = (unsigned)(source_x + 0.5);
                if (nearest >= final_width) {
                    nearest = final_width - 1u;
                }
                usize src_index = ((usize)dy * (usize)final_width + (usize)nearest) * 3u;
                usize dst_index = ((usize)dy * (usize)sheared_width + (usize)dx) * 3u;
                for (u32 channel = 0u; channel < 3u; ++channel) {
                    skew_pixels.data[dst_index + channel] = final_pixels[src_index + channel];
                }
            }
        }
        skew_pixels.size = sheared_count * 3u;
        final_pixels = skew_pixels.data;
        final_width = sheared_width;
        emit_skew_metadata = true;
        skew_margin_value = margin_left;
        skew_top_value = top_shift;
        skew_bottom_value = bottom_shift;
    }

    distorted_ppm.release();
    if (!distorted_ppm.append_ascii("P3\n") ||
        !buffer_append_number(distorted_ppm, (u64)final_width) ||
        !distorted_ppm.append_char(' ') ||
        !buffer_append_number(distorted_ppm, (u64)final_height) ||
        !distorted_ppm.append_ascii("\n255\n")) {
        return false;
    }
    if (metadata_comments.size) {
        if (!distorted_ppm.append_bytes(metadata_comments.data, metadata_comments.size)) {
            return false;
        }
    }
    if (rotation_degrees != 0.0 && emit_rotation_metadata) {
        if (!distorted_ppm.append_ascii("# rotation_src_width ") ||
            !buffer_append_number(distorted_ppm, (u64)rotation_source_width) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# rotation_src_height ") ||
            !buffer_append_number(distorted_ppm, (u64)rotation_source_height) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# rotation_margin ")) {
            return false;
        }
        char margin_text[32];
        format_fixed_3(rotation_margin_value, margin_text, sizeof(margin_text));
        if (!distorted_ppm.append_ascii(margin_text) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# rotation_deg ")) {
            return false;
        }
        char angle_text[32];
        format_fixed_3(rotation_degrees, angle_text, sizeof(angle_text));
        if (!distorted_ppm.append_ascii(angle_text) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
    }
    if (emit_skew_metadata) {
        if (!distorted_ppm.append_ascii("# skew_src_width ") ||
            !buffer_append_number(distorted_ppm, (u64)skew_source_width) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# skew_src_height ") ||
            !buffer_append_number(distorted_ppm, (u64)skew_source_height) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# skew_margin_x ")) {
            return false;
        }
        char margin_text[32];
        format_fixed_3(skew_margin_value, margin_text, sizeof(margin_text));
        if (!distorted_ppm.append_ascii(margin_text) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# skew_x_pixels ")) {
            return false;
        }
        char skew_text[32];
        format_fixed_3(skew_top_value, skew_text, sizeof(skew_text));
        if (!distorted_ppm.append_ascii(skew_text) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
        if (!distorted_ppm.append_ascii("# skew_bottom_x ")) {
            return false;
        }
        char skew_bottom_text[32];
        format_fixed_3(skew_bottom_value, skew_bottom_text, sizeof(skew_bottom_text));
        if (!distorted_ppm.append_ascii(skew_bottom_text) ||
            !distorted_ppm.append_char('\n')) {
            return false;
        }
    }
    usize final_count = (usize)final_width * (usize)final_height;
    for (usize i = 0u; i < final_count; ++i) {
        usize index = i * 3u;
        for (u32 channel = 0u; channel < 3u; ++channel) {
            if (!buffer_append_number(distorted_ppm, (u64)final_pixels[index + channel]) ||
                !distorted_ppm.append_char('\n')) {
                return false;
            }
        }
    }
    return true;
}

static bool verify_fixture(const char* case_name,
                           const char* path,
                           unsigned char expected_average,
                           unsigned char expected_global,
                           unsigned char expected_fill,
                           double sync_white_cut)
{
    makocode::image::ImageBuffer image;
    image.width = 0;
    image.height = 0;
    image.pixels = 0;
    makocode::image::LoadDiagnostics diag;
    diag.message[0] = '\0';
    makocode::image::LoadStatus status =
        makocode::image::load_ppm_grayscale(path, image, &diag);
    if (status != makocode::image::LoadSuccess) {
        console_write(2, "test-scan-basic: fixture load failed for ");
        console_write(2, case_name);
        console_line(2, "");
        if (diag.message[0]) {
            console_write(2, "  detail: ");
            console_line(2, diag.message);
        }
        return false;
    }
    makocode::image::Histogram histogram;
    makocode::image::compute_histogram(image, histogram);
    if (histogram.average != expected_average) {
        console_write(2, "test-scan-basic: average mismatch for ");
        console_write(2, case_name);
        console_write(2, " (expected ");
        char num[32];
        append_number_ascii(expected_average, num);
        console_write(2, num);
        console_write(2, ", got ");
        append_number_ascii(histogram.average, num);
        console_write(2, num);
        console_line(2, ")");
        makocode::image::release(image);
        return false;
    }
    makocode::image::CutLevels levels;
    if (!makocode::image::analyze_cut_levels(histogram, sync_white_cut, levels)) {
        console_write(2, "test-scan-basic: analyze_cut_levels failed for ");
        console_line(2, case_name);
        makocode::image::release(image);
        return false;
    }
    if (levels.global_cut != expected_global || levels.fill_cut != expected_fill) {
        console_write(2, "test-scan-basic: cutlevel mismatch for ");
        console_write(2, case_name);
        console_write(2, " (expected g=");
        char num[32];
        append_number_ascii(expected_global, num);
        console_write(2, num);
        console_write(2, ",f=");
        append_number_ascii(expected_fill, num);
        console_write(2, num);
        console_write(2, " got g=");
        append_number_ascii(levels.global_cut, num);
        console_write(2, num);
        console_write(2, ",f=");
        append_number_ascii(levels.fill_cut, num);
        console_write(2, num);
        console_line(2, ")");
        makocode::image::release(image);
        return false;
    }
    makocode::image::release(image);
    return true;
}

static bool verify_invalid_fixture(const char* path)
{
    makocode::image::ImageBuffer image;
    image.width = 0;
    image.height = 0;
    image.pixels = 0;
    makocode::image::LoadDiagnostics diag;
    diag.message[0] = '\0';
    makocode::image::LoadStatus status =
        makocode::image::load_ppm_grayscale(path, image, &diag);
    if (status == makocode::image::LoadSuccess) {
        console_line(2, "test-scan-basic: expected failure for invalid fixture");
        makocode::image::release(image);
        return false;
    }
    return true;
}

struct SyntheticEntry {
    unsigned value;
    u64 count;
};

static bool verify_synthetic_case(const char* name,
                                  const SyntheticEntry* entries,
                                  usize entry_count,
                                  u8 expected_average,
                                  u8 expected_global,
                                  u8 expected_fill,
                                  double sync_white_cut)
{
    makocode::image::Histogram histogram;
    for (int i = 0; i < 256; ++i) {
        histogram.bins[i] = 0;
    }
    histogram.average = 0;
    u64 total_pixels = 0;
    u64 total_value = 0;
    for (usize i = 0; i < entry_count; ++i) {
        unsigned value = entries[i].value;
        if (value > 255u) {
            console_write(2, "test-scan-basic: histogram value out of range for ");
            console_line(2, name);
            return false;
        }
        histogram.bins[value] = entries[i].count;
        total_pixels += entries[i].count;
        total_value += (u64)value * entries[i].count;
    }
    if (!total_pixels) {
        console_write(2, "test-scan-basic: histogram empty for ");
        console_line(2, name);
        return false;
    }
    histogram.average = (u8)((total_value + (total_pixels >> 1u)) / total_pixels);
    if (histogram.average != expected_average) {
        console_write(2, "test-scan-basic: synthetic average mismatch for ");
        console_write(2, name);
        console_write(2, " (expected ");
        char num[32];
        append_number_ascii(expected_average, num);
        console_write(2, num);
        console_write(2, ", got ");
        append_number_ascii(histogram.average, num);
        console_write(2, num);
        console_line(2, ")");
        return false;
    }
    makocode::image::CutLevels levels;
    if (!makocode::image::analyze_cut_levels(histogram, sync_white_cut, levels)) {
        console_write(2, "test-scan-basic: analyze_cut_levels failed for synthetic case ");
        console_line(2, name);
        return false;
    }
    if (levels.global_cut != expected_global || levels.fill_cut != expected_fill) {
        console_write(2, "test-scan-basic: synthetic cutlevel mismatch for ");
        console_write(2, name);
        console_write(2, " (expected g=");
        char num[32];
        append_number_ascii(expected_global, num);
        console_write(2, num);
        console_write(2, ",f=");
        append_number_ascii(expected_fill, num);
        console_write(2, num);
        console_write(2, " got g=");
        append_number_ascii(levels.global_cut, num);
        console_write(2, num);
        console_write(2, ",f=");
        append_number_ascii(levels.fill_cut, num);
        console_write(2, num);
        console_line(2, ")");
        return false;
    }
    return true;
}

static bool write_mask_debug_fixture(const char* fixture_dir,
                                     const char* filename,
                                     unsigned width,
                                     unsigned height,
                                     const makocode::ByteBuffer& mask)
{
    usize expected = (usize)width * (usize)height;
    if (mask.size < expected) {
        return false;
    }
    makocode::ByteBuffer path_buffer;
    if (!path_buffer.append_ascii(fixture_dir) ||
        !path_buffer.append_char('/') ||
        !path_buffer.append_ascii(filename) ||
        !path_buffer.append_char('\0')) {
        path_buffer.release();
        return false;
    }
    const char* path = (const char*)path_buffer.data;
    bool result = write_ppm_fixture(path, width, height, (const unsigned char*)mask.data);
    path_buffer.release();
    return result;
}

static double abs_double(double value)
{
    return (value < 0.0) ? -value : value;
}

static void normalize_components(double& x, double& y)
{
    double length = sqrt(x * x + y * y);
    if (length <= 0.0) {
        x = 0.0;
        y = 0.0;
    } else {
        x /= length;
        y /= length;
    }
}

static double compute_angle_deg(double x, double y)
{
    static const double kRadToDeg = 180.0 / 3.14159265358979323846;
    double radians = atan2(y, x);
    double degrees = radians * kRadToDeg;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

static double wrap_angle_deg(double value)
{
    while (value <= -180.0) {
        value += 360.0;
    }
    while (value > 180.0) {
        value -= 360.0;
    }
    return value;
}

static bool generate_rotated_page(unsigned base_width,
                                  unsigned base_height,
                                  unsigned border_thickness,
                                  double scale,
                                  double angle_degrees,
                                  makocode::image::ImageBuffer& image_out,
                                  double expected_corners[4][2])
{
    makocode::image::release(image_out);
    if (!base_width || !base_height || scale <= 0.0) {
        return false;
    }
    const double base_w = (double)base_width;
    const double base_h = (double)base_height;
    const double base_cx = base_w * 0.5;
    const double base_cy = base_h * 0.5;
    const double radians = angle_degrees * (3.14159265358979323846 / 180.0);
    const double cos_theta = cos(radians);
    const double sin_theta = sin(radians);

    double transformed[4][2];
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
    for (int i = 0; i < 4; ++i) {
        double corner_x = (i & 1) ? base_w : 0.0;
        double corner_y = (i & 2) ? base_h : 0.0;
        double dx = corner_x - base_cx;
        double dy = corner_y - base_cy;
        double sx = dx * scale;
        double sy = dy * scale;
        double rx = sx * cos_theta - sy * sin_theta;
        double ry = sx * sin_theta + sy * cos_theta;
        transformed[i][0] = rx;
        transformed[i][1] = ry;
        if (i == 0) {
            min_x = max_x = rx;
            min_y = max_y = ry;
        } else {
            if (rx < min_x) min_x = rx;
            if (rx > max_x) max_x = rx;
            if (ry < min_y) min_y = ry;
            if (ry > max_y) max_y = ry;
        }
    }

    const double margin = 4.0;
    double width_span = max_x - min_x;
    double height_span = max_y - min_y;
    double offset_x = -min_x + margin;
    double offset_y = -min_y + margin;
    double width_extent = width_span + 2.0 * margin + 1.0;
    double height_extent = height_span + 2.0 * margin + 1.0;
    unsigned out_width = (unsigned)ceil(width_extent);
    unsigned out_height = (unsigned)ceil(height_extent);
    if (!out_width) out_width = 1u;
    if (!out_height) out_height = 1u;

    usize total_pixels = (usize)out_width * (usize)out_height;
    if (!total_pixels) {
        return false;
    }
    unsigned char* pixels = (unsigned char*)malloc(total_pixels);
    if (!pixels) {
        return false;
    }
    for (usize i = 0; i < total_pixels; ++i) {
        pixels[i] = 255u;
    }

    double inv_scale = 1.0 / scale;
    for (unsigned ty = 0u; ty < out_height; ++ty) {
        double target_y = (double)ty - offset_y;
        for (unsigned tx = 0u; tx < out_width; ++tx) {
            double target_x = (double)tx - offset_x;
            double sx = target_x * cos_theta + target_y * sin_theta;
            double sy = -target_x * sin_theta + target_y * cos_theta;
            sx *= inv_scale;
            sy *= inv_scale;
            double bx = sx + base_cx;
            double by = sy + base_cy;
            if (bx >= -0.5 && bx <= base_w + 0.5 &&
                by >= -0.5 && by <= base_h + 0.5) {
                double dist_left = bx;
                double dist_right = base_w - bx;
                double dist_top = by;
                double dist_bottom = base_h - by;
                if (dist_left < 0.0) dist_left = 0.0;
                if (dist_right < 0.0) dist_right = 0.0;
                if (dist_top < 0.0) dist_top = 0.0;
                if (dist_bottom < 0.0) dist_bottom = 0.0;
                double min_dist = dist_left;
                if (dist_right < min_dist) min_dist = dist_right;
                if (dist_top < min_dist) min_dist = dist_top;
                if (dist_bottom < min_dist) min_dist = dist_bottom;
                if (min_dist <= (double)border_thickness) {
                    pixels[(usize)ty * (usize)out_width + (usize)tx] = 0u;
                }
            }
        }
    }

    image_out.width = out_width;
    image_out.height = out_height;
    image_out.pixels = pixels;

    for (int i = 0; i < 4; ++i) {
        expected_corners[i][0] = transformed[i][0] + offset_x;
        expected_corners[i][1] = transformed[i][1] + offset_y;
    }
    return true;
}

static bool verify_corner_detection_case(const char* name,
                                         unsigned base_width,
                                         unsigned base_height,
                                         unsigned border_thickness,
                                         double scale,
                                         double rotation_degrees,
                                         double corner_tolerance,
                                         double vector_tolerance,
                                         double metric_tolerance)
{
    makocode::image::ImageBuffer image;
    image.width = 0u;
    image.height = 0u;
    image.pixels = 0;
    double expected_corners[4][2];
    if (!generate_rotated_page(base_width,
                               base_height,
                               border_thickness,
                               scale,
                               rotation_degrees,
                               image,
                               expected_corners)) {
        console_write(2, "test-scan-basic: failed to generate rotated page for ");
        console_line(2, name);
        makocode::image::release(image);
        return false;
    }

    makocode::image::CornerDetectionConfig config;
    config.logical_width = base_width;
    config.logical_height = base_height;
    config.cross_half = 3u;
    config.cross_trim = 0.75;

    makocode::image::CornerDetectionResult result;
    if (!makocode::image::find_corners(image, 128u, config, result) || !result.valid) {
        console_write(2, "test-scan-basic: find_corners failed for ");
        console_line(2, name);
        makocode::image::release(image);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        double dx = abs_double(result.corners[i][0] - expected_corners[i][0]);
        double dy = abs_double(result.corners[i][1] - expected_corners[i][1]);
        if (dx > corner_tolerance || dy > corner_tolerance) {
            console_write(2, "test-scan-basic: corner mismatch for ");
            console_write(2, name);
            console_write(2, " corner=");
            char num[32];
            append_number_ascii((unsigned)(i + 1), num);
            console_write(2, num);
            console_write(2, " dx=");
            format_fixed_3(dx, num, sizeof(num));
            console_write(2, num);
            console_write(2, " dy=");
            format_fixed_3(dy, num, sizeof(num));
            console_write(2, num);
            console_line(2, "");
            ok = false;
            break;
        }
    }

    if (ok) {
        double expected_hx = ((expected_corners[1][0] - expected_corners[0][0]) +
                              (expected_corners[3][0] - expected_corners[2][0])) * 0.5;
        double expected_hy = ((expected_corners[1][1] - expected_corners[0][1]) +
                              (expected_corners[3][1] - expected_corners[2][1])) * 0.5;
        double expected_vx = ((expected_corners[2][0] - expected_corners[0][0]) +
                              (expected_corners[3][0] - expected_corners[1][0])) * 0.5;
        double expected_vy = ((expected_corners[2][1] - expected_corners[0][1]) +
                              (expected_corners[3][1] - expected_corners[1][1])) * 0.5;
        normalize_components(expected_hx, expected_hy);
        normalize_components(expected_vx, expected_vy);

        double diff_hx = abs_double(result.pixelhx - expected_hx);
        double diff_hy = abs_double(result.pixelhy - expected_hy);
        double diff_vx = abs_double(result.pixelvx - expected_vx);
        double diff_vy = abs_double(result.pixelvy - expected_vy);
        if (diff_hx > vector_tolerance || diff_hy > vector_tolerance ||
            diff_vx > vector_tolerance || diff_vy > vector_tolerance) {
            console_write(2, "test-scan-basic: pixel vector mismatch for ");
            console_line(2, name);
            ok = false;
        }
        if (ok) {
            double expected_h_angle = compute_angle_deg(expected_hx, -expected_hy);
            double expected_v_angle = compute_angle_deg(expected_vx, -expected_vy);
            double expected_skew = wrap_angle_deg(expected_h_angle + expected_v_angle + 90.0) * 0.5;
            double expected_perp = expected_h_angle - expected_v_angle;
            if (abs_double(result.skew_degrees - expected_skew) > metric_tolerance ||
                abs_double(result.perpendicularity_degrees - expected_perp) > metric_tolerance) {
                console_write(2, "test-scan-basic: angle metrics mismatch for ");
                console_line(2, name);
                char num[32];
                console_write(2, "  expected_skew=");
                format_fixed_3(expected_skew, num, sizeof(num));
                console_write(2, num);
                console_write(2, " actual_skew=");
                format_fixed_3(result.skew_degrees, num, sizeof(num));
                console_line(2, num);
                console_write(2, "  expected_perp=");
                format_fixed_3(expected_perp, num, sizeof(num));
                console_write(2, num);
                console_write(2, " actual_perp=");
                format_fixed_3(result.perpendicularity_degrees, num, sizeof(num));
                console_line(2, num);
                ok = false;
            }
        }
        if (ok) {
            double expected_h_component = ((expected_corners[1][0] + expected_corners[3][0]) -
                                           (expected_corners[0][0] + expected_corners[0][0])) * 0.5;
            double expected_v_component = ((expected_corners[2][1] + expected_corners[3][1]) -
                                           (expected_corners[0][1] + expected_corners[1][1])) * 0.5;
            double expected_hpixel = expected_h_component / (double)base_width;
            double expected_vpixel = expected_v_component / (double)base_height;
            if (abs_double(result.hpixel - expected_hpixel) > metric_tolerance ||
                abs_double(result.vpixel - expected_vpixel) > metric_tolerance) {
                console_write(2, "test-scan-basic: pixel size mismatch for ");
                console_line(2, name);
                char num[32];
                console_write(2, "  expected_h=");
                format_fixed_3(expected_hpixel, num, sizeof(num));
                console_write(2, num);
                console_write(2, " actual_h=");
                format_fixed_3(result.hpixel, num, sizeof(num));
                console_line(2, num);
                console_write(2, "  expected_v=");
                format_fixed_3(expected_vpixel, num, sizeof(num));
                console_write(2, num);
                console_write(2, " actual_v=");
                format_fixed_3(result.vpixel, num, sizeof(num));
                console_line(2, num);
                for (int ci = 0; ci < 4; ++ci) {
                    console_write(2, "  corner ");
                    append_number_ascii((unsigned)(ci + 1), num);
                    console_write(2, num);
                    console_write(2, " expected=(");
                    format_fixed_3(expected_corners[ci][0], num, sizeof(num));
                    console_write(2, num);
                    console_write(2, ",");
                    format_fixed_3(expected_corners[ci][1], num, sizeof(num));
                    console_write(2, num);
                    console_write(2, ") actual=(");
                    format_fixed_3(result.corners[ci][0], num, sizeof(num));
                    console_write(2, num);
                    console_write(2, ",");
                    format_fixed_3(result.corners[ci][1], num, sizeof(num));
                    console_write(2, num);
                    console_line(2, ")");
                }
                ok = false;
            }
            if (ok) {
                double half_scale = (double)config.cross_half * 0.5;
                unsigned expected_chalf = (unsigned)(expected_hpixel * half_scale);
                unsigned expected_chalf_v = (unsigned)(expected_vpixel * half_scale);
                unsigned expected_chalf_val = expected_chalf < expected_chalf_v ? expected_chalf : expected_chalf_v;
                if (result.chalf != (int)expected_chalf_val) {
                    console_write(2, "test-scan-basic: chalf mismatch for ");
                    console_line(2, name);
                    ok = false;
                }
            }
            if (ok) {
                double core = (double)config.cross_half - config.cross_trim;
                if (core < 0.0) core = 0.0;
                unsigned expected_cf_h = (unsigned)(expected_hpixel * core);
                unsigned expected_cf_v = (unsigned)(expected_vpixel * core);
                unsigned expected_cf = expected_cf_h < expected_cf_v ? expected_cf_h : expected_cf_v;
                if (result.chalf_fine != (int)expected_cf) {
                    console_write(2, "test-scan-basic: chalf_fine mismatch for ");
                    console_line(2, name);
                    ok = false;
                }
            }
        }
    }



    makocode::image::release(image);
    if (!ok) {
        return false;
    }

    console_write(1, "test-scan-basic: corner detection ");
    console_line(1, name);
    return true;
}

static bool verify_border_dirt_cleanup(const char* fixture_dir)
{
    const unsigned width = 16u;
    const unsigned height = 10u;
    usize total = (usize)width * (usize)height;
    makocode::image::ImageBuffer image;
    image.width = width;
    image.height = height;
    image.pixels = (unsigned char*)malloc(total);
    if (!image.pixels) {
        console_line(2, "test-scan-basic: allocation failure for dirt fixture");
        return false;
    }
    for (usize i = 0; i < total; ++i) {
        image.pixels[i] = 255u;
    }

    for (unsigned y = 3u; y + 3u < height; ++y) {
        for (unsigned x = 4u; x + 4u < width; ++x) {
            image.pixels[(usize)y * (usize)width + (usize)x] = 32u;
        }
    }

    const unsigned border_specks[][2] = {
        {0u, 1u},
        {width - 1u, 2u},
        {width / 2u, 0u},
        {width - 2u, height - 1u},
        {1u, height - 1u},
        {0u, height / 2u}
    };
    const usize speck_count = sizeof(border_specks) / sizeof(border_specks[0]);
    for (usize i = 0; i < speck_count; ++i) {
        unsigned sx = border_specks[i][0];
        unsigned sy = border_specks[i][1];
        image.pixels[(usize)sy * (usize)width + (usize)sx] = 12u;
    }

    unsigned data_x = width / 2u;
    unsigned data_y = height / 2u;
    image.pixels[(usize)data_y * (usize)width + (usize)data_x] = 16u;

    makocode::ByteBuffer mask;
    if (!remove_border_dirt(image, 200u, &mask)) {
        console_line(2, "test-scan-basic: remove_border_dirt failed");
        mask.release();
        makocode::image::release(image);
        return false;
    }

    bool ok = true;
    for (usize i = 0; i < speck_count; ++i) {
        unsigned sx = border_specks[i][0];
        unsigned sy = border_specks[i][1];
        unsigned char value = image.pixels[(usize)sy * (usize)width + (usize)sx];
        if (value != 255u) {
            console_write(2, "test-scan-basic: border speck not cleared at ");
            char num[32];
            console_write(2, "(");
            append_number_ascii(sx, num);
            console_write(2, num);
            console_write(2, ",");
            append_number_ascii(sy, num);
            console_write(2, num);
            console_line(2, ")");
            ok = false;
            break;
        }
    }

    unsigned char inner_value =
        image.pixels[(usize)data_y * (usize)width + (usize)data_x];
    if (inner_value != 16u) {
        console_line(2, "test-scan-basic: data region altered during dirt removal");
        ok = false;
    }

    u64 checksum = 0u;
    if (mask.size != total) {
        console_line(2, "test-scan-basic: mask size mismatch");
        ok = false;
    } else {
        for (usize i = 0; i < mask.size; ++i) {
            checksum += (u64)mask.data[i];
        }
    }

    const u64 expected_checksum = 1530u;
    if (ok && checksum != expected_checksum) {
        console_write(2, "test-scan-basic: mask checksum mismatch (got ");
        char num[32];
        append_number_ascii(checksum, num);
        console_write(2, num);
        console_write(2, ")");
        console_line(2, "");
        ok = false;
    }

    if (ok) {
        if (!write_mask_debug_fixture(fixture_dir,
                                      "border_mask_debug.pgm",
                                      width,
                                      height,
                                      mask)) {
            console_line(2, "test-scan-basic: failed to write mask debug fixture");
            ok = false;
        }
    }

    mask.release();
    makocode::image::release(image);
    return ok;
}

static int command_test_border_dirt(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    const char* base_dir = "test";
    const char* fixture_dir = "test/fixtures";
    if (!ensure_directory(base_dir)) {
        console_line(2, "test-border-dirt: failed to create test directory");
        return 1;
    }
    if (!ensure_directory(fixture_dir)) {
        console_line(2, "test-border-dirt: failed to create fixture directory");
        return 1;
    }

    ImageMappingConfig mapping;
    PageFooterConfig footer_config;
    if (!mapping.page_width_set) {
        mapping.page_width_pixels = 64u;
        mapping.page_width_set = true;
    }
    if (!mapping.page_height_set) {
        mapping.page_height_pixels = 64u;
        mapping.page_height_set = true;
    }
    mapping.color_channels = 1u;
    mapping.color_set = true;

    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!compute_page_dimensions(mapping, width_pixels, height_pixels)) {
        console_line(2, "test-border-dirt: invalid page dimensions");
        return 1;
    }
    u8 sample_bits = bits_per_sample(mapping.color_channels);
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (sample_bits == 0u || samples_per_pixel == 0u) {
        console_line(2, "test-border-dirt: unsupported color configuration");
        return 1;
    }
    FooterLayout footer_layout;
    if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
        console_line(2, "test-border-dirt: footer layout computation failed");
        return 1;
    }
    u32 data_height_pixels = footer_layout.data_height_pixels ? footer_layout.data_height_pixels : height_pixels;
    if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
        console_line(2, "test-border-dirt: footer configuration invalid");
        return 1;
    }
    u64 bits_per_page = (u64)width_pixels * (u64)data_height_pixels * (u64)sample_bits * (u64)samples_per_pixel;
    if (bits_per_page == 0u) {
        console_line(2, "test-border-dirt: page capacity is zero");
        return 1;
    }

    auto write_artifact = [&](const char* filename,
                              const makocode::ByteBuffer& buffer) -> bool {
        makocode::ByteBuffer path;
        if (!path.append_ascii(base_dir) ||
            !path.append_char('/') ||
            !path.append_ascii(filename) ||
            !path.append_char('\0')) {
            path.release();
            return false;
        }
        bool ok = write_buffer_to_file((const char*)path.data, buffer);
        path.release();
        return ok;
    };

    usize max_payload_size = (usize)((bits_per_page) / 8u) + 256u;
    if (max_payload_size < 32u) {
        max_payload_size = 32u;
    }
    if (max_payload_size > (1u << 20u)) {
        max_payload_size = (1u << 20u);
    }
    usize best_size = 0u;
    usize left = 1u;
    usize right = max_payload_size;
    while (left <= right) {
        usize mid = left + (right - left) / 2u;
        u64 mid_bits = 0u;
        if (!compute_frame_bit_count(mapping, mid, (u64)mid, 0.0, (const char*)0, 0u, mid_bits)) {
            console_line(2, "test-border-dirt: failed to evaluate payload size");
            return 1;
        }
        u64 mid_pages = (mid_bits + bits_per_page - 1u) / bits_per_page;
        if (mid_pages <= 1u && mid_bits > 0u) {
            best_size = mid;
            left = mid + 1u;
        } else {
            if (mid == 0u) {
                break;
            }
            right = mid - 1u;
        }
    }
    if (best_size == 0u) {
        console_line(2, "test-border-dirt: unable to locate payload size");
        return 1;
    }

    makocode::ByteBuffer payload;
    if (!generate_random_bytes(payload, best_size, 0xB0FDEF11ull)) {
        console_line(2, "test-border-dirt: failed to generate payload");
        return 1;
    }
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = 0.0;
    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    u64 payload_bit_count = 0u;
    if (!build_payload_frame(mapping,
                             payload.size,
                             0xB0FDEF11ull,
                             (const char*)0,
                             0u,
                             payload,
                             encoder,
                             frame_bits,
                             frame_bit_count,
                             payload_bit_count)) {
        console_line(2, "test-border-dirt: failed to build payload frame");
        return 1;
    }

    u64 page_count = (frame_bit_count + bits_per_page - 1u) / bits_per_page;
    if (page_count != 1u) {
        console_line(2, "test-border-dirt: expected single page");
        return 1;
    }

    if (!write_artifact("1013_border_payload.bin", payload)) {
        console_line(2, "test-border-dirt: failed to write payload artifact");
        return 1;
    }
    console_write(1, "test-border-dirt:   artifact 1013.1 payload -> ");
    console_write(1, base_dir);
    console_write(1, "/1013_border_payload.bin");
    console_line(1, "");

    makocode::ByteBuffer page_output;
    if (!encode_page_to_ppm(mapping,
                            frame_bits,
                            frame_bit_count,
                            0u,
                            width_pixels,
                            height_pixels,
                            1u,
                            page_count,
                            bits_per_page,
                            payload_bit_count,
                            &encoder.ecc_info(),
                            0,
                            0u,
                            footer_layout,
                            page_output)) {
        console_line(2, "test-border-dirt: failed to encode page");
        return 1;
    }
    if (!write_artifact("1013_border_encoded.ppm", page_output)) {
        console_line(2, "test-border-dirt: failed to write encoded artifact");
        return 1;
    }
    console_write(1, "test-border-dirt:   artifact 1013.2 encoded -> ");
    console_write(1, base_dir);
    console_write(1, "/1013_border_encoded.ppm");
    console_line(1, "");

    unsigned ppm_width = 0;
    unsigned ppm_height = 0;
    makocode::ByteBuffer rgb_data;
    if (!extract_ppm_pixels(page_output, ppm_width, ppm_height, rgb_data)) {
        console_line(2, "test-border-dirt: failed to parse encoded page");
        return 1;
    }
    usize pixel_count = (usize)ppm_width * (usize)ppm_height;
    usize rgb_bytes = pixel_count * 3u;
    makocode::ByteBuffer rgb_dirty;
    makocode::ByteBuffer rgb_clean;
    if (rgb_bytes) {
        if (!rgb_dirty.ensure(rgb_bytes) || !rgb_clean.ensure(rgb_bytes)) {
            console_line(2, "test-border-dirt: allocation failure for rgb copies");
            return 1;
        }
        rgb_dirty.size = rgb_bytes;
        rgb_clean.size = rgb_bytes;
        const u8* rgb_source = rgb_data.data;
        if (!rgb_source) {
            console_line(2, "test-border-dirt: missing rgb source data");
            return 1;
        }
        for (usize i = 0u; i < rgb_bytes; ++i) {
            u8 value = rgb_source[i];
            rgb_dirty.data[i] = value;
            rgb_clean.data[i] = value;
        }
    }
    makocode::ByteBuffer grayscale;
    if (!grayscale.ensure(pixel_count)) {
        console_line(2, "test-border-dirt: allocation failure for grayscale buffer");
        return 1;
    }
    grayscale.size = pixel_count;
    for (usize i = 0u, p = 0u; i < pixel_count; ++i, p += 3u) {
        unsigned char r = rgb_data.data[p + 0u];
        unsigned char g = rgb_data.data[p + 1u];
        unsigned char b = rgb_data.data[p + 2u];
        grayscale.data[i] = (unsigned char)((r + g + b) / 3u);
    }

    const unsigned specks[][2] = {
        {1u, 0u},
        {ppm_width - 2u, 0u},
        {ppm_width - 1u, ppm_height / 2u},
        {0u, ppm_height / 3u},
        {ppm_width / 2u, ppm_height - 1u},
        {3u, ppm_height - 1u}
    };
    usize speck_indices[sizeof(specks) / sizeof(specks[0])];
    usize speck_total = 0u;
    for (usize i = 0u; i < sizeof(specks)/sizeof(specks[0]); ++i) {
        unsigned sx = specks[i][0];
        unsigned sy = specks[i][1];
        if (sx < ppm_width && sy < ppm_height) {
            usize index = (usize)sy * (usize)ppm_width + (usize)sx;
            grayscale.data[index] = 0u;
            usize rgb_index = index * 3u;
            if (rgb_index + 2u < rgb_dirty.size) {
                rgb_dirty.data[rgb_index + 0u] = 0u;
                rgb_dirty.data[rgb_index + 1u] = 0u;
                rgb_dirty.data[rgb_index + 2u] = 0u;
            }
            if (speck_total < sizeof(speck_indices)/sizeof(speck_indices[0])) {
                speck_indices[speck_total++] = index;
            }
        }
    }

    makocode::ByteBuffer dirty_image;
    if (!dirty_image.ensure(pixel_count)) {
        console_line(2, "test-border-dirt: allocation failure for dirty copy");
        return 1;
    }
    dirty_image.size = pixel_count;
    for (usize i = 0u; i < pixel_count; ++i) {
        dirty_image.data[i] = grayscale.data[i];
    }

    makocode::image::ImageBuffer image_buf;
    image_buf.width = ppm_width;
    image_buf.height = ppm_height;
    image_buf.pixels = grayscale.data;

    makocode::ByteBuffer mask;
    if (!remove_border_dirt(image_buf, 200u, &mask)) {
        console_line(2, "test-border-dirt: remove_border_dirt failed");
        mask.release();
        return 1;
    }
    if (mask.size != pixel_count) {
        console_line(2, "test-border-dirt: mask size mismatch");
        mask.release();
        return 1;
    }

    for (usize i = 0u; i < speck_total; ++i) {
        usize index = speck_indices[i];
        if (index >= pixel_count) {
            continue;
        }
        if (image_buf.pixels[index] != 255u) {
            mask.release();
            console_line(2, "test-border-dirt: speck not cleared by removal");
            return 1;
        }
        usize rgb_index = index * 3u;
        if (rgb_index + 2u < rgb_clean.size) {
            unsigned char cleaned_value = image_buf.pixels[index];
            rgb_clean.data[rgb_index + 0u] = cleaned_value;
            rgb_clean.data[rgb_index + 1u] = cleaned_value;
            rgb_clean.data[rgb_index + 2u] = cleaned_value;
        }
    }

    auto write_case_fixture = [&](const char* suffix,
                                  const unsigned char* data) -> bool {
        makocode::ByteBuffer path;
        if (!path.append_ascii(base_dir) ||
            !path.append_char('/') ||
            !path.append_ascii("1013_border") ||
            !path.append_ascii(suffix) ||
            !path.append_char('\0')) {
            path.release();
            return false;
        }
        bool ok = write_ppm_fixture((const char*)path.data, ppm_width, ppm_height, data);
        path.release();
        return ok;
    };

    if (!write_case_fixture("_dirty.pgm", dirty_image.data) ||
        !write_case_fixture("_clean.pgm", image_buf.pixels) ||
        !write_case_fixture("_mask.pgm", mask.data)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to write fixtures");
        return 1;
    }

    auto make_ppm_from_rgb = [&](const makocode::ByteBuffer& source,
                                 makocode::ByteBuffer& ppm) -> bool {
        if (source.size < rgb_bytes) {
            return false;
        }
        ppm.release();
        if (!ppm.append_ascii("P3\n")) {
            return false;
        }
        if (!append_comment_number(ppm, "MAKOCODE_COLOR_CHANNELS", (u64)mapping.color_channels) ||
            !append_comment_number(ppm, "MAKOCODE_BITS", payload_bit_count) ||
            !append_comment_number(ppm, "MAKOCODE_ECC", 0u) ||
            !append_comment_number(ppm, "MAKOCODE_PAGE_COUNT", page_count) ||
            !append_comment_number(ppm, "MAKOCODE_PAGE_INDEX", 1u) ||
            !append_comment_number(ppm, "MAKOCODE_PAGE_BITS", bits_per_page) ||
            !append_comment_number(ppm, "MAKOCODE_PAGE_WIDTH_PX", (u64)mapping.page_width_pixels) ||
            !append_comment_number(ppm, "MAKOCODE_PAGE_HEIGHT_PX", (u64)mapping.page_height_pixels)) {
            return false;
        }
        u32 footer_rows = height_pixels - data_height_pixels;
        if (footer_rows) {
            if (!append_comment_number(ppm, "MAKOCODE_FOOTER_ROWS", (u64)footer_rows)) {
                return false;
            }
        }
        if (!buffer_append_number(ppm, (u64)ppm_width) ||
            !ppm.append_char(' ') ||
            !buffer_append_number(ppm, (u64)ppm_height) ||
            !ppm.append_char('\n')) {
            return false;
        }
        if (!ppm.append_ascii("255\n")) {
            return false;
        }
        for (usize i = 0u; i < pixel_count; ++i) {
            usize rgb_index = i * 3u;
            for (u32 channel = 0u; channel < 3u; ++channel) {
                if (channel) {
                    if (!ppm.append_char(' ')) {
                        return false;
                    }
                }
                if (!buffer_append_number(ppm, (u64)source.data[rgb_index + channel])) {
                    return false;
                }
            }
            if (!ppm.append_char('\n')) {
                return false;
            }
        }
        return true;
    };

    makocode::ByteBuffer dirty_ppm;
    makocode::ByteBuffer cleaned_ppm;
    if (!make_ppm_from_rgb(rgb_dirty, dirty_ppm) ||
        !make_ppm_from_rgb(rgb_clean, cleaned_ppm)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to marshal ppm fixtures");
        return 1;
    }
    if (!write_artifact("1013_border_dirty.ppm", dirty_ppm) ||
        !write_artifact("1013_border_clean.ppm", cleaned_ppm)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to write scan artifacts");
        return 1;
    }
    console_write(1, "test-border-dirt:   artifact 1013.3 dirty -> ");
    console_write(1, base_dir);
    console_write(1, "/1013_border_dirty.ppm");
    console_line(1, "");
    console_write(1, "test-border-dirt:   artifact 1013.4 cleaned -> ");
    console_write(1, base_dir);
    console_write(1, "/1013_border_clean.ppm");
    console_line(1, "");

    makocode::ByteBuffer dirty_frame_bits;
    u64 dirty_frame_bit_count = 0u;
    PpmParserState dirty_state;
    bool dirty_frame_ok =
        ppm_extract_frame_bits(dirty_ppm, mapping, dirty_frame_bits, dirty_frame_bit_count, dirty_state);
    if (dirty_frame_ok) {
        dirty_state.has_bits = true;
        dirty_state.bits_value = payload_bit_count;
        dirty_state.has_page_count = true;
        dirty_state.page_count_value = 1u;
        dirty_state.has_page_index = true;
        dirty_state.page_index_value = 1u;
        u64 dirty_effective_bits = frame_bit_count;
        if (dirty_effective_bits == 0u || dirty_effective_bits > dirty_frame_bit_count) {
            dirty_effective_bits = dirty_frame_bit_count;
        }
        makocode::ByteBuffer dirty_payload_bits;
        u64 dirty_payload_bit_count = 0u;
        bool dirty_payload_ok = frame_bits_to_payload(dirty_frame_bits.data,
                                                      dirty_effective_bits,
                                                      dirty_state,
                                                      dirty_payload_bits,
                                                      dirty_payload_bit_count);
        bool dirty_payload_match = false;
        if (dirty_payload_ok) {
            makocode::DecoderContext dirty_decoder;
            if (dirty_decoder.parse(dirty_payload_bits.data,
                                    dirty_payload_bit_count,
                                    (const char*)0,
                                    0u) &&
                dirty_decoder.has_payload &&
                dirty_decoder.payload.size == payload.size) {
                dirty_payload_match = true;
                for (usize i = 0u; i < payload.size; ++i) {
                    if (dirty_decoder.payload.data[i] != payload.data[i]) {
                        dirty_payload_match = false;
                        break;
                    }
                }
            }
        }
        if (dirty_payload_match) {
            mask.release();
            console_line(2, "test-border-dirt: dirty image decoded without cleanup");
            return 1;
        }
    }

    makocode::ByteBuffer clean_frame_bits;
    u64 clean_frame_bit_count = 0u;
    PpmParserState clean_state;
    if (!ppm_extract_frame_bits(cleaned_ppm, mapping, clean_frame_bits, clean_frame_bit_count, clean_state)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to parse cleaned ppm");
        return 1;
    }
    clean_state.has_bits = true;
    clean_state.bits_value = payload_bit_count;
    clean_state.has_page_count = true;
    clean_state.page_count_value = 1u;
    clean_state.has_page_index = true;
    clean_state.page_index_value = 1u;
    u64 clean_effective_bits = frame_bit_count;
    if (clean_effective_bits == 0u || clean_effective_bits > clean_frame_bit_count) {
        clean_effective_bits = clean_frame_bit_count;
    }
    makocode::ByteBuffer clean_payload_bits;
    u64 clean_payload_bit_count = 0u;
    if (!frame_bits_to_payload(clean_frame_bits.data,
                               clean_effective_bits,
                               clean_state,
                               clean_payload_bits,
                               clean_payload_bit_count)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to reassemble cleaned payload bits");
        return 1;
    }
    makocode::DecoderContext clean_decoder;
    if (!clean_decoder.parse(clean_payload_bits.data,
                             clean_payload_bit_count,
                             (const char*)0,
                             0u)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to decode cleaned payload");
        return 1;
    }
    if (!clean_decoder.has_payload || clean_decoder.payload.size != payload.size) {
        mask.release();
        console_line(2, "test-border-dirt: cleaned payload size mismatch");
        return 1;
    }
    for (usize i = 0u; i < payload.size; ++i) {
        if (clean_decoder.payload.data[i] != payload.data[i]) {
            mask.release();
            console_line(2, "test-border-dirt: cleaned payload mismatch");
            return 1;
        }
    }
    if (!write_artifact("1013_border_payload_decoded.bin", clean_decoder.payload)) {
        mask.release();
        console_line(2, "test-border-dirt: failed to write decoded payload artifact");
        return 1;
    }
    console_write(1, "test-border-dirt:   artifact 1013.5 decoded -> ");
    console_write(1, base_dir);
    console_write(1, "/1013_border_payload_decoded.bin");
    console_line(1, "");

    const u64 expected_checksum = 378165u;
    u64 checksum = 0u;
    for (usize i = 0u; i < mask.size; ++i) {
        checksum += (u64)mask.data[i];
    }
    if (checksum != expected_checksum) {
        console_write(2, "test-border-dirt: mask checksum mismatch (got ");
        char digits[32];
        append_number_ascii(checksum, digits);
        console_write(2, digits);
        console_line(2, ")");
        mask.release();
        return 1;
    }

    mask.release();

    if (!verify_border_dirt_cleanup(fixture_dir)) {
        console_line(2, "test-border-dirt: verification failed");
        return 1;
    }

    console_line(1, "test-border-dirt: cleanup verified");
    return 0;
}

static int command_test_scan_basic(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    const char* base_dir = "test";
    const char* fixture_dir = "test/fixtures";
    if (!ensure_directory(base_dir)) {
        console_line(2, "test-scan-basic: failed to create test directory");
        return 1;
    }
    if (!ensure_directory(fixture_dir)) {
        console_line(2, "test-scan-basic: failed to create fixture directory");
        return 1;
    }

    const unsigned char clean_pixels[4] = {0, 64, 128, 255};
    const char* clean_path = "test/fixtures/clean_gray.ppm";
    if (!write_ppm_fixture(clean_path, 2, 2, clean_pixels)) {
        console_line(2, "test-scan-basic: failed to generate clean fixture");
        return 1;
    }
    const unsigned char noisy_pixels[6] = {30, 200, 80, 140, 10, 220};
    const char* noisy_path = "test/fixtures/noisy_gray.ppm";
    if (!write_ppm_fixture(noisy_path, 3, 2, noisy_pixels)) {
        console_line(2, "test-scan-basic: failed to generate noisy fixture");
        return 1;
    }
    const char* invalid_path = "test/fixtures/wrong_depth.ppm";
    if (!write_invalid_maxval_fixture(invalid_path)) {
        console_line(2, "test-scan-basic: failed to generate invalid fixture");
        return 1;
    }

    const double sync_white_cut = 0.10;

    if (!verify_fixture("clean_gray", clean_path, 112, 17, 86, sync_white_cut)) {
        return 1;
    }
    console_line(1, "test-scan-basic: fixture clean_gray");
    if (!verify_fixture("noisy_gray", noisy_path, 113, 32, 94, sync_white_cut)) {
        return 1;
    }
    console_line(1, "test-scan-basic: fixture noisy_gray");
    if (!verify_invalid_fixture(invalid_path)) {
        return 1;
    }
    console_line(1, "test-scan-basic: fixture wrong_depth");

    const SyntheticEntry bimodal_entries[] = {
        {64, 500},
        {192, 500}
    };
    if (!verify_synthetic_case("bimodal",
                               bimodal_entries,
                               sizeof(bimodal_entries)/sizeof(*bimodal_entries),
                               128, 77, 128, sync_white_cut)) {
        return 1;
    }
    console_line(1, "test-scan-basic: synthetic bimodal");
    const SyntheticEntry skewed_entries[] = {
        {30, 800},
        {200, 200}
    };
    if (!verify_synthetic_case("skewed",
                               skewed_entries,
                               sizeof(skewed_entries)/sizeof(*skewed_entries),
                               64, 47, 115, sync_white_cut)) {
        return 1;
    }
    console_line(1, "test-scan-basic: synthetic skewed");
    const SyntheticEntry low_contrast_entries[] = {
        {118, 2},
        {119, 2},
        {120, 2},
        {121, 2},
        {122, 1},
        {123, 1}
    };
    if (!verify_synthetic_case("low_contrast",
                               low_contrast_entries,
                               sizeof(low_contrast_entries)/sizeof(*low_contrast_entries),
                               120, 118, 120, sync_white_cut)) {
        return 1;
    }
    console_line(1, "test-scan-basic: synthetic low_contrast");

    if (!verify_corner_detection_case("corner-rotation-scale-a",
                                      160u,
                                      200u,
                                      4u,
                                      1.20,
                                      12.0,
                                      1.5,
                                      0.02,
                                      0.30)) {
        return 1;
    }
    if (!verify_corner_detection_case("corner-rotation-scale-b",
                                      160u,
                                      200u,
                                      4u,
                                      0.85,
                                      -9.0,
                                      1.5,
                                      0.02,
                                      0.30)) {
        return 1;
    }
    if (!verify_corner_detection_case("corner-rotation-scale-c",
                                      160u,
                                      200u,
                                      4u,
                                      1.00,
                                      27.0,
                                      2.0,
                                      0.02,
                                      0.40)) {
        return 1;
    }

    if (!verify_border_dirt_cleanup(fixture_dir)) {
        return 1;
    }
    console_line(1, "test-scan-basic: border dirt removal");

    console_line(1, "test-scan-basic: all fixtures passed");
    return 0;
}

static bool verify_footer_title_roundtrip(const ImageMappingConfig& base_mapping) {
    console_line(1, "test: footer title roundtrip");
    ImageMappingConfig mapping = base_mapping;
    if (!mapping.color_set) {
        mapping.color_channels = 1u;
    }
    if (mapping.color_channels == 0u || mapping.color_channels > 3u) {
        mapping.color_channels = 1u;
    }
    mapping.color_set = true;
    static const char footer_title[] = "MAKOCODE FOOTER TITLE WITH DUST IGNORED 2025-11-09 >>>";
    usize title_length = ascii_length(footer_title);
    if (title_length == 0u) {
        console_line(2, "test: footer title roundtrip invalid title length");
        return false;
    }
    u32 font_size = 1u;
    u32 glyph_width = FOOTER_BASE_GLYPH_WIDTH * font_size;
    u32 glyph_height = FOOTER_BASE_GLYPH_HEIGHT * font_size;
    u32 char_spacing = font_size;
    u32 text_pixel_width = (u32)title_length * glyph_width;
    if (title_length > 1u) {
        text_pixel_width += (u32)(title_length - 1u) * char_spacing;
    }
    u32 horizontal_margin = font_size * 4u;
    u32 desired_width = text_pixel_width + horizontal_margin * 2u;
    if (desired_width < 128u) {
        desired_width = 128u;
    }
    mapping.page_width_pixels = desired_width;
    mapping.page_width_set = true;
    u32 footer_height_estimate = glyph_height + font_size * 2u;
    if (footer_height_estimate < glyph_height + 2u) {
        footer_height_estimate = glyph_height + 2u;
    }
    u32 desired_data_rows = 48u;
    mapping.page_height_pixels = desired_data_rows + footer_height_estimate;
    mapping.page_height_set = true;
    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!compute_page_dimensions(mapping, width_pixels, height_pixels)) {
        console_line(2, "test: footer title roundtrip invalid page dimensions");
        return false;
    }
    const char* base_dir = "test";
    if (!ensure_directory(base_dir)) {
        console_line(2, "test: footer title roundtrip failed to prepare test directory");
        return false;
    }
    auto write_artifact = [&](const char* filename,
                              const makocode::ByteBuffer& buffer) -> bool {
        makocode::ByteBuffer path;
        if (!path.append_ascii(base_dir) ||
            !path.append_char('/') ||
            !path.append_ascii(filename) ||
            !path.append_char('\0')) {
            path.release();
            return false;
        }
        bool ok = write_buffer_to_file((const char*)path.data, buffer);
        path.release();
        return ok;
    };
    PageFooterConfig footer_config;
    footer_config.font_size = font_size;
    footer_config.display_page_info = false;
    footer_config.display_filename = false;
    footer_config.has_title = true;
    footer_config.title_text = footer_title;
    footer_config.title_length = title_length;
    footer_config.max_text_length = footer_compute_max_text_length(footer_config, 1u);
    FooterLayout footer_layout;
    if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
        console_line(2, "test: footer title roundtrip footer layout failed");
        return false;
    }
    u8 sample_bits = bits_per_sample(mapping.color_channels);
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (sample_bits == 0u || samples_per_pixel == 0u) {
        console_line(2, "test: footer title roundtrip unsupported color configuration");
        return false;
    }
    u32 data_height_pixels = footer_layout.has_text ? footer_layout.data_height_pixels : height_pixels;
    if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
        console_line(2, "test: footer title roundtrip invalid data height");
        return false;
    }
    u64 bits_per_page = (u64)width_pixels *
                        (u64)data_height_pixels *
                        (u64)sample_bits *
                        (u64)samples_per_pixel;
    if (bits_per_page == 0u) {
        console_line(2, "test: footer title roundtrip page capacity is zero");
        return false;
    }
    usize max_payload_size = (usize)(bits_per_page / 8u);
    if (max_payload_size < 32u) {
        max_payload_size = 32u;
    }
    if (max_payload_size > (1u << 20u)) {
        max_payload_size = (1u << 20u);
    }
    auto fill_payload_buffer = [&](makocode::ByteBuffer& buffer, usize size) -> bool {
        buffer.release();
        if (!buffer.ensure(size)) {
            return false;
        }
        u32 prng_local = 0x1234567u ^ (u32)size;
        for (usize i = 0u; i < size; ++i) {
            prng_local = prng_local * 1664525u + 1013904223u;
            buffer.data[i] = (u8)(prng_local >> 24u);
        }
        buffer.size = size;
        return true;
    };
    auto compute_bits_for_size = [&](usize candidate, u64& out_bits) -> bool {
        makocode::ByteBuffer temp_payload;
        if (!fill_payload_buffer(temp_payload, candidate)) {
            return false;
        }
        makocode::EncoderContext temp_encoder;
        temp_encoder.config.ecc_redundancy = 0.0;
        if (!temp_encoder.set_payload(temp_payload.data, temp_payload.size)) {
            return false;
        }
        if (!temp_encoder.build()) {
            return false;
        }
        makocode::ByteBuffer temp_frame;
        u64 temp_payload_bits = 0u;
        if (!build_frame_bits(temp_encoder, mapping, temp_frame, out_bits, temp_payload_bits)) {
            return false;
        }
        return true;
    };
    usize best_size = 0u;
    usize left = 1u;
    usize right = max_payload_size;
    while (left <= right) {
        usize mid = left + (right - left) / 2u;
        u64 mid_bits = 0u;
        if (!compute_bits_for_size(mid, mid_bits)) {
            console_line(2, "test: footer title roundtrip failed to evaluate payload size");
            return false;
        }
        u64 mid_pages = (mid_bits + bits_per_page - 1u) / bits_per_page;
        if (mid_pages <= 1u && mid_bits > 0u) {
            best_size = mid;
            left = mid + 1u;
        } else {
            if (mid == 0u) {
                break;
            }
            if (mid_pages > 1u) {
                if (mid == 0u) {
                    break;
                }
                right = mid - 1u;
            } else {
                left = mid + 1u;
            }
        }
    }
    if (best_size == 0u) {
        console_line(2, "test: footer title roundtrip unable to size payload");
        return false;
    }
    makocode::ByteBuffer payload_original;
    if (!fill_payload_buffer(payload_original, best_size)) {
        console_line(2, "test: footer title roundtrip failed to allocate payload buffer");
        return false;
    }
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = 0.0;
    if (!encoder.set_payload(payload_original.data, payload_original.size)) {
        console_line(2, "test: footer title roundtrip failed to set payload");
        return false;
    }
    if (!encoder.build()) {
        console_line(2, "test: footer title roundtrip encoder build failed");
        return false;
    }
    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    u64 payload_bit_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count, payload_bit_count)) {
        console_line(2, "test: footer title roundtrip failed to build frame bits");
        return false;
    }
    if (frame_bit_count == 0u || frame_bit_count > bits_per_page) {
        console_line(2, "test: footer title roundtrip page capacity mismatch");
        return false;
    }
    const makocode::EccSummary& ecc_summary = encoder.ecc_info();
    makocode::ByteBuffer ppm_buffer;
    if (!encode_page_to_ppm(mapping,
                            frame_bits,
                            frame_bit_count,
                            0u,
                            width_pixels,
                            height_pixels,
                            1u,
                            1u,
                            bits_per_page,
                            payload_bit_count,
                            &ecc_summary,
                            footer_config.title_text,
                            footer_config.title_length,
                            footer_layout,
                            ppm_buffer)) {
        console_line(2, "test: footer title roundtrip failed to encode page");
        return false;
    }
    makocode::ByteBuffer extracted_bits;
    u64 extracted_bit_count = 0u;
    PpmParserState page_state;
    if (!ppm_extract_frame_bits(ppm_buffer, mapping, extracted_bits, extracted_bit_count, page_state)) {
        console_line(2, "test: footer title roundtrip failed to extract frame bits");
        return false;
    }
    if (!page_state.has_bits) {
        page_state.has_bits = true;
        page_state.bits_value = payload_bit_count;
    }
    if (!page_state.has_page_count) {
        page_state.has_page_count = true;
        page_state.page_count_value = 1u;
    }
    if (!page_state.has_page_index) {
        page_state.has_page_index = true;
        page_state.page_index_value = 1u;
    }
    u64 effective_bits = extracted_bit_count;
    if (page_state.has_page_bits && page_state.page_bits_value <= effective_bits) {
        effective_bits = page_state.page_bits_value;
    }
    makocode::ByteBuffer payload_bits;
    u64 payload_bits_count = 0u;
    if (!frame_bits_to_payload(extracted_bits.data, effective_bits, page_state, payload_bits, payload_bits_count)) {
        console_line(2, "test: footer title roundtrip failed to convert frame bits");
        return false;
    }
    makocode::DecoderContext decoder;
    if (!decoder.parse(payload_bits.data, payload_bits_count, (const char*)0, 0u)) {
        console_line(2, "test: footer title roundtrip decoder parse failed");
        return false;
    }
    if (!decoder.has_payload || decoder.payload.size != payload_original.size) {
        console_line(2, "test: footer title roundtrip payload size mismatch");
        return false;
    }
    for (usize i = 0u; i < decoder.payload.size; ++i) {
        if (decoder.payload.data[i] != payload_original.data[i]) {
            console_line(2, "test: footer title roundtrip payload mismatch");
            return false;
        }
    }
    if (!write_artifact("1022_footer_title_payload.bin", payload_original)) {
        console_line(2, "test: footer title roundtrip failed to write payload artifact");
        return false;
    }
    console_write(1, "test:   footer payload -> ");
    console_write(1, base_dir);
    console_write(1, "/1022_footer_title_payload.bin");
    console_line(1, "");
    if (!write_artifact("1022_footer_title_encoded.ppm", ppm_buffer)) {
        console_line(2, "test: footer title roundtrip failed to write encoded artifact");
        return false;
    }
    console_write(1, "test:   footer encoded -> ");
    console_write(1, base_dir);
    console_write(1, "/1022_footer_title_encoded.ppm");
    console_line(1, "");
    if (!write_artifact("1022_footer_title_decoded.bin", decoder.payload)) {
        console_line(2, "test: footer title roundtrip failed to write decoded artifact");
        return false;
    }
    console_write(1, "test:   footer decoded -> ");
    console_write(1, base_dir);
    console_write(1, "/1022_footer_title_decoded.bin");
    console_line(1, "");
    console_line(1, "test: footer title roundtrip ok");
    return true;
}

static int command_test_payload_gray_100k(int arg_count, char** args) {
    (void)arg_count;
    (void)args;

    const char* base_dir = "test";
    if (!ensure_directory(base_dir)) {
        console_line(2, "test-100kb: failed to create test directory");
        return 1;
    }

    const usize payload_size = 100u * 1024u;
    makocode::ByteBuffer original_payload;
    if (!original_payload.ensure(payload_size)) {
        console_line(2, "test-100kb: failed to allocate payload buffer");
        return 1;
    }
    for (usize i = 0u; i < payload_size; ++i) {
        original_payload.data[i] = (u8)(i & 0xFFu);
    }
    original_payload.size = payload_size;
    console_line(1, "test-100kb: generated 100 KiB grayscale payload");

    ImageMappingConfig mapping;
    mapping.color_channels = 1u;
    mapping.color_set = true;

    PageFooterConfig footer_config;
    u8 sample_bits = bits_per_sample(mapping.color_channels);
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (sample_bits == 0u || samples_per_pixel == 0u) {
        console_line(2, "test-100kb: unsupported color configuration");
        return 1;
    }

    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = 0.0;
    if (!encoder.set_payload(original_payload.data, original_payload.size)) {
        console_line(2, "test-100kb: failed to set encoder payload");
        return 1;
    }
    if (!encoder.build()) {
        console_line(2, "test-100kb: encoder build failed");
        return 1;
    }

    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    u64 payload_bit_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count, payload_bit_count)) {
        console_line(2, "test-100kb: failed to build frame bits");
        return 1;
    }

    u64 required_bits = (frame_bit_count > 0u) ? frame_bit_count : 1u;
    double width_root = sqrt((double)required_bits);
    if (width_root < 1.0) {
        width_root = 1.0;
    }
    u64 width_candidate = (u64)ceil(width_root);
    if (width_candidate == 0u || width_candidate > 0xFFFFFFFFu) {
        console_line(2, "test-100kb: computed width out of range");
        return 1;
    }
    u64 height_candidate = (required_bits + width_candidate - 1u) / width_candidate;
    if (height_candidate == 0u || height_candidate > 0xFFFFFFFFu) {
        console_line(2, "test-100kb: computed height out of range");
        return 1;
    }

    mapping.page_width_pixels = (u32)width_candidate;
    mapping.page_width_set = true;
    mapping.page_height_pixels = (u32)height_candidate;
    mapping.page_height_set = true;

    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!compute_page_dimensions(mapping, width_pixels, height_pixels)) {
        console_line(2, "test-100kb: invalid page dimensions");
        return 1;
    }

    FooterLayout footer_layout;
    if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
        console_line(2, "test-100kb: footer layout computation failed");
        return 1;
    }

    u32 data_height_pixels = footer_layout.has_text ? footer_layout.data_height_pixels : height_pixels;
    if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
        console_line(2, "test-100kb: invalid data height");
        return 1;
    }

    u64 bits_per_page = (u64)width_pixels *
                        (u64)data_height_pixels *
                        (u64)sample_bits *
                        (u64)samples_per_pixel;
    if (bits_per_page == 0u) {
        console_line(2, "test-100kb: page capacity is zero");
        return 1;
    }

    char frame_bits_digits[32];
    char width_digits[32];
    char height_digits[32];
    char capacity_digits[32];
    u64_to_ascii(frame_bit_count, frame_bits_digits, sizeof(frame_bits_digits));
    u64_to_ascii((u64)width_pixels, width_digits, sizeof(width_digits));
    u64_to_ascii((u64)height_pixels, height_digits, sizeof(height_digits));
    u64_to_ascii(bits_per_page, capacity_digits, sizeof(capacity_digits));
    console_write(1, "test-100kb: frame_bits=");
    console_write(1, frame_bits_digits);
    console_write(1, " width=");
    console_write(1, width_digits);
    console_write(1, " height=");
    console_write(1, height_digits);
    console_write(1, " capacity=");
    console_write(1, capacity_digits);
    console_line(1, "");

    if (frame_bit_count > bits_per_page) {
        console_line(2, "test-100kb: payload exceeds single-page capacity");
        return 1;
    }

    const makocode::EccSummary& ecc_summary = encoder.ecc_info();
    makocode::ByteBuffer ppm_buffer;
    if (!encode_page_to_ppm(mapping,
                            frame_bits,
                            frame_bit_count,
                            0u,
                            width_pixels,
                            height_pixels,
                            1u,
                            1u,
                            bits_per_page,
                            payload_bit_count,
                            &ecc_summary,
                            footer_config.title_text,
                            footer_config.title_length,
                            footer_layout,
                            ppm_buffer)) {
        console_line(2, "test-100kb: failed to encode ppm page");
        return 1;
    }

    makocode::ByteBuffer extracted_bits;
    u64 extracted_bit_count = 0u;
    PpmParserState page_state;
    if (!ppm_extract_frame_bits(ppm_buffer, mapping, extracted_bits, extracted_bit_count, page_state)) {
        console_line(2, "test-100kb: failed to extract frame bits");
        return 1;
    }
    if (!page_state.has_bits) {
        page_state.has_bits = true;
        page_state.bits_value = payload_bit_count;
    }
    if (!page_state.has_page_bits || page_state.page_bits_value > extracted_bit_count) {
        page_state.has_page_bits = true;
        page_state.page_bits_value = payload_bit_count;
    }
    if (!page_state.has_page_count) {
        page_state.has_page_count = true;
        page_state.page_count_value = 1u;
    }
    if (!page_state.has_page_index) {
        page_state.has_page_index = true;
        page_state.page_index_value = 1u;
    }

    u64 effective_bits = extracted_bit_count;
    if (page_state.has_page_bits && page_state.page_bits_value <= effective_bits) {
        effective_bits = page_state.page_bits_value;
    }

    makocode::ByteBuffer payload_bits;
    u64 payload_bits_count = 0u;
    if (!frame_bits_to_payload(extracted_bits.data, effective_bits, page_state, payload_bits, payload_bits_count)) {
        console_line(2, "test-100kb: failed to convert frame bits back to payload stream");
        return 1;
    }
    if (payload_bits_count != payload_bit_count) {
        console_line(2, "test-100kb: payload bit count mismatch");
        return 1;
    }

    makocode::DecoderContext decoder;
    if (!decoder.parse(payload_bits.data, payload_bits_count, (const char*)0, 0u)) {
        console_line(2, "test-100kb: decoder parse failed");
        return 1;
    }
    if (!decoder.has_payload || decoder.payload.size != original_payload.size) {
        console_line(2, "test-100kb: decoded payload size mismatch");
        return 1;
    }
    for (usize i = 0u; i < original_payload.size; ++i) {
        if (decoder.payload.data[i] != original_payload.data[i]) {
            console_line(2, "test-100kb: decoded payload mismatch");
            return 1;
        }
    }

    auto write_artifact = [&](const char* filename,
                              const makocode::ByteBuffer& buffer) -> bool {
        makocode::ByteBuffer path;
        if (!path.append_ascii(base_dir) ||
            !path.append_char('/') ||
            !path.append_ascii(filename) ||
            !path.append_char('\0')) {
            path.release();
            return false;
        }
        bool ok = write_buffer_to_file((const char*)path.data, buffer);
        path.release();
        return ok;
    };

    if (!write_artifact("2001_payload_gray_100k.bin", original_payload)) {
        console_line(2, "test-100kb: failed to write payload artifact");
        return 1;
    }
    console_write(1, "test-100kb:   payload -> ");
    console_write(1, base_dir);
    console_write(1, "/2001_payload_gray_100k.bin");
    console_line(1, "");

    if (!write_artifact("2001_payload_gray_100k_encoded.ppm", ppm_buffer)) {
        console_line(2, "test-100kb: failed to write encoded artifact");
        return 1;
    }
    console_write(1, "test-100kb:   encoded -> ");
    console_write(1, base_dir);
    console_write(1, "/2001_payload_gray_100k_encoded.ppm");
    console_line(1, "");

    if (!write_artifact("2001_payload_gray_100k_decoded.bin", decoder.payload)) {
        console_line(2, "test-100kb: failed to write decoded artifact");
        return 1;
    }
    console_write(1, "test-100kb:   decoded -> ");
    console_write(1, base_dir);
    console_write(1, "/2001_payload_gray_100k_decoded.bin");
    console_line(1, "");

    console_line(1, "test-100kb: roundtrip ok");
    return 0;
}

static int run_test_payload_100k_wavy(u8 color_channels,
                                      const char* test_label,
                                      const char* payload_bin_name,
                                      const char* encoded_ppm_name,
                                      const char* ripple_ppm_name,
                                      const char* decoded_bin_name) {
    auto log_prefix = [&](int fd) {
        console_write(fd, test_label);
        console_write(fd, ": ");
    };
    auto log_line = [&](int fd, const char* message) {
        log_prefix(fd);
        console_line(fd, message);
    };

    const char* base_dir = "test";
    if (!ensure_directory(base_dir)) {
        log_line(2, "failed to create test directory");
        return 1;
    }

    const usize payload_size = 100u * 1024u;
    makocode::ByteBuffer original_payload;
    if (!original_payload.ensure(payload_size)) {
        log_line(2, "failed to allocate payload buffer");
        return 1;
    }
    for (usize i = 0u; i < payload_size; ++i) {
        original_payload.data[i] = (u8)(i & 0xFFu);
    }
    original_payload.size = payload_size;
    char color_digits[8];
    u64_to_ascii((u64)color_channels, color_digits, sizeof(color_digits));
    log_line(1, "generated 100 KiB payload");
    log_prefix(1);
    console_write(1, "color=");
    console_line(1, color_digits);

    ImageMappingConfig mapping;
    mapping.color_channels = color_channels;
    mapping.color_set = true;

    PageFooterConfig footer_config;
    u8 sample_bits = bits_per_sample(mapping.color_channels);
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (sample_bits == 0u || samples_per_pixel == 0u) {
        log_line(2, "unsupported color configuration");
        return 1;
    }

    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = 0.0;
    if (!encoder.set_payload(original_payload.data, original_payload.size)) {
        log_line(2, "failed to set encoder payload");
        return 1;
    }
    if (!encoder.build()) {
        log_line(2, "encoder build failed");
        return 1;
    }

    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    u64 payload_bit_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count, payload_bit_count)) {
        log_line(2, "failed to build frame bits");
        return 1;
    }

    u64 required_bits = (frame_bit_count > 0u) ? frame_bit_count : 1u;
    double width_root = sqrt((double)required_bits);
    if (width_root < 1.0) {
        width_root = 1.0;
    }
    u64 width_candidate = (u64)ceil(width_root);
    if (width_candidate == 0u || width_candidate > 0xFFFFFFFFu) {
        log_line(2, "computed width out of range");
        return 1;
    }
    u64 height_candidate = (required_bits + width_candidate - 1u) / width_candidate;
    if (height_candidate == 0u || height_candidate > 0xFFFFFFFFu) {
        log_line(2, "computed height out of range");
        return 1;
    }

    mapping.page_width_pixels = (u32)width_candidate;
    mapping.page_width_set = true;
    mapping.page_height_pixels = (u32)height_candidate;
    mapping.page_height_set = true;

    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!compute_page_dimensions(mapping, width_pixels, height_pixels)) {
        log_line(2, "invalid page dimensions");
        return 1;
    }

    FooterLayout footer_layout;
    if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
        log_line(2, "footer layout computation failed");
        return 1;
    }

    u32 data_height_pixels = footer_layout.has_text ? footer_layout.data_height_pixels : height_pixels;
    if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
        log_line(2, "invalid data height");
        return 1;
    }

    u64 bits_per_page = (u64)width_pixels *
                        (u64)data_height_pixels *
                        (u64)sample_bits *
                        (u64)samples_per_pixel;
    if (bits_per_page == 0u) {
        log_line(2, "page capacity is zero");
        return 1;
    }
    if (frame_bit_count > bits_per_page) {
        log_line(2, "payload exceeds single-page capacity");
        return 1;
    }

    const makocode::EccSummary& ecc_summary = encoder.ecc_info();
    makocode::ByteBuffer ppm_buffer;
    if (!encode_page_to_ppm(mapping,
                             frame_bits,
                             frame_bit_count,
                             0u,
                             width_pixels,
                             height_pixels,
                             0u,
                             1u,
                             bits_per_page,
                             payload_bit_count,
                             &ecc_summary,
                             (const char*)0,
                             0u,
                             footer_layout,
                             ppm_buffer)) {
        log_line(2, "failed to encode baseline PPM");
        return 1;
    }
    log_line(1, "encoded baseline page");

    makocode::ByteBuffer fiducial_ppm;
    if (!apply_default_fiducial_grid(ppm_buffer, fiducial_ppm)) {
        log_line(2, "failed to embed fiducial grid");
        return 1;
    }
    log_line(1, "applied fiducial markers to baseline page");

    const double ripple_scale_factor = 3.0;
    makocode::ByteBuffer ripple_source_ppm;
    if (!ppm_scale_fractional_axes(fiducial_ppm,
                                   ripple_scale_factor,
                                   ripple_scale_factor,
                                   ripple_source_ppm)) {
        log_line(2, "failed to scale fiducial PPM for ripple distortion");
        return 1;
    }
    log_line(1, "scaled fiducial page 3x for ripple distortion");

    const double ripple_amplitude = 1.8;
    const double ripple_cycles_y = 6.5;
    const double ripple_cycles_x = 9.0;
    makocode::ByteBuffer ripple_ppm;
    if (!ppm_apply_wavy_ripple(ripple_source_ppm,
                               ripple_amplitude,
                               ripple_cycles_y,
                               ripple_cycles_x,
                               ripple_ppm)) {
        log_line(2, "failed to apply ripple distortion");
        return 1;
    }
    log_line(1, "applied uneven ripple distortion");

    auto write_artifact = [&](const char* filename,
                              const makocode::ByteBuffer& buffer) -> bool {
        makocode::ByteBuffer path;
        if (!path.append_ascii(base_dir) ||
            !path.append_char('/') ||
            !path.append_ascii(filename) ||
            !path.append_char('\0')) {
            path.release();
            return false;
        }
        bool ok = write_buffer_to_file((const char*)path.data, buffer);
        path.release();
        return ok;
    };

    if (!write_artifact(payload_bin_name, original_payload)) {
        log_line(2, "failed to write payload artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  payload -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_write(1, payload_bin_name);
    console_line(1, "");

    if (!write_artifact(encoded_ppm_name, fiducial_ppm)) {
        log_line(2, "failed to write fiducial artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  fiducial -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_write(1, encoded_ppm_name);
    console_line(1, "");

    if (!write_artifact(ripple_ppm_name, ripple_ppm)) {
        log_line(2, "failed to write ripple artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  ripple -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_write(1, ripple_ppm_name);
    console_line(1, "");

    makocode::ByteBuffer ripple_bits;
    u64 ripple_bit_count = 0u;
    PpmParserState ripple_state;
    if (!ppm_extract_frame_bits(ripple_ppm, mapping, ripple_bits, ripple_bit_count, ripple_state)) {
        log_line(2, "failed to extract frame bits from ripple scan");
        return 1;
    }
    if (!ripple_state.has_bits) {
        ripple_state.has_bits = true;
        ripple_state.bits_value = payload_bit_count;
    }
    if (!ripple_state.has_page_bits || ripple_state.page_bits_value > ripple_bit_count) {
        ripple_state.has_page_bits = true;
        ripple_state.page_bits_value = payload_bit_count;
    }
    if (!ripple_state.has_page_count) {
        ripple_state.has_page_count = true;
        ripple_state.page_count_value = 1u;
    }
    if (!ripple_state.has_page_index) {
        ripple_state.has_page_index = true;
        ripple_state.page_index_value = 1u;
    }

    u64 ripple_effective_bits = ripple_bit_count;
    if (ripple_state.has_page_bits && ripple_state.page_bits_value <= ripple_effective_bits) {
        ripple_effective_bits = ripple_state.page_bits_value;
    }

    makocode::ByteBuffer ripple_payload_bits;
    u64 ripple_payload_bit_count = 0u;
    if (!frame_bits_to_payload(ripple_bits.data,
                               ripple_effective_bits,
                               ripple_state,
                               ripple_payload_bits,
                               ripple_payload_bit_count)) {
        log_line(2, "failed to convert ripple frame bits to payload stream");
        return 1;
    }

    makocode::DecoderContext decoder;
    if (!decoder.parse(ripple_payload_bits.data, ripple_payload_bit_count, (const char*)0, 0u)) {
        log_line(2, "decoder parse failed for ripple scan");
        return 1;
    }
    if (!decoder.has_payload) {
        log_line(2, "decoder produced no payload");
        return 1;
    }

    bool payload_matches = decoder.payload.size == original_payload.size;
    if (payload_matches) {
        for (usize i = 0u; i < original_payload.size; ++i) {
            if (decoder.payload.data[i] != original_payload.data[i]) {
                payload_matches = false;
                break;
            }
        }
    }

    if (!write_artifact(decoded_bin_name, decoder.payload)) {
        log_line(2, "failed to write decoded payload artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  decoded -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_write(1, decoded_bin_name);
    console_line(1, "");

    if (!payload_matches) {
        log_line(2, "decoded payload mismatch under ripple distortion");
        return 1;
    }

    log_line(1, "ripple roundtrip ok");
    return 0;
}

static int command_test_payload_gray_100k_wavy(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_wavy(1u,
                                      "test-100kb-wavy",
                                      "2006_payload_gray_100k_wavy.bin",
                                      "2006_payload_gray_100k_wavy_encoded.ppm",
                                      "2006_payload_gray_100k_wavy_scan.ppm",
                                      "2006_payload_gray_100k_wavy_decoded.bin");
}

static int command_test_payload_color2_100k_wavy(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_wavy(2u,
                                      "test-100kb-wavy-c2",
                                      "2007_payload_color2_100k_wavy.bin",
                                      "2007_payload_color2_100k_wavy_encoded.ppm",
                                      "2007_payload_color2_100k_wavy_scan.ppm",
                                      "2007_payload_color2_100k_wavy_decoded.bin");
}

static int command_test_payload_color3_100k_wavy(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_wavy(3u,
                                      "test-100kb-wavy-c3",
                                      "2008_payload_color3_100k_wavy.bin",
                                      "2008_payload_color3_100k_wavy_encoded.ppm",
                                      "2008_payload_color3_100k_wavy_scan.ppm",
                                      "2008_payload_color3_100k_wavy_decoded.bin");
}

static int run_test_payload_100k_scaled(u8 color_channels,
                                        const char* artifact_prefix,
                                        const char* test_label,
                                        double scale_factor_x,
                                        double scale_factor_y) {
    const char* base_dir = "test";
    auto log_prefix = [&](int fd) {
        console_write(fd, test_label);
        console_write(fd, ": ");
    };
    auto log_line = [&](int fd, const char* message) {
        log_prefix(fd);
        console_line(fd, message);
    };

    if (!ensure_directory(base_dir)) {
        log_line(2, "failed to create test directory");
        return 1;
    }

    char scale_x_label[16];
    char scale_y_label[16];
    if (!format_scale_label(scale_factor_x, scale_x_label, sizeof(scale_x_label)) ||
        !format_scale_label(scale_factor_y, scale_y_label, sizeof(scale_y_label))) {
        log_line(2, "failed to format scale labels");
        return 1;
    }
    char scale_label[32];
    if (ascii_compare(scale_x_label, scale_y_label) == 0) {
        usize len = ascii_length(scale_x_label);
        if ((len + 1u) > sizeof(scale_label)) {
            log_line(2, "scale label buffer too small");
            return 1;
        }
        for (usize i = 0u; i <= len; ++i) {
            scale_label[i] = scale_x_label[i];
        }
    } else {
        usize len_x = ascii_length(scale_x_label);
        usize len_y = ascii_length(scale_y_label);
        const char separator[] = "by";
        usize sep_len = sizeof(separator) - 1u;
        if ((len_x + sep_len + len_y + 1u) > sizeof(scale_label)) {
            log_line(2, "anisotropic scale label buffer too small");
            return 1;
        }
        usize cursor = 0u;
        for (usize i = 0u; i < len_x; ++i) {
            scale_label[cursor++] = scale_x_label[i];
        }
        for (usize i = 0u; i < sep_len; ++i) {
            scale_label[cursor++] = separator[i];
        }
        for (usize i = 0u; i < len_y; ++i) {
            scale_label[cursor++] = scale_y_label[i];
        }
        scale_label[cursor] = '\0';
    }
    auto to_display_label = [](const char* source,
                               char* destination,
                               usize destination_size) -> bool {
        if (!source || !destination || destination_size == 0u) {
            return false;
        }
        usize length = ascii_length(source);
        if ((length + 1u) > destination_size) {
            return false;
        }
        for (usize i = 0u; i < length; ++i) {
            char c = source[i];
            destination[i] = (c == 'p') ? '.' : c;
        }
        destination[length] = '\0';
        return true;
    };

    const usize payload_size = 100u * 1024u;
    makocode::ByteBuffer original_payload;
    if (!original_payload.ensure(payload_size)) {
        log_line(2, "failed to allocate payload buffer");
        return 1;
    }
    for (usize i = 0u; i < payload_size; ++i) {
        original_payload.data[i] = (u8)(i & 0xFFu);
    }
    original_payload.size = payload_size;

    char channel_digits[8];
    u64_to_ascii((u64)color_channels, channel_digits, sizeof(channel_digits));
    log_prefix(1);
    console_write(1, "generated 100 KiB payload (color channel ");
    console_write(1, channel_digits);
    console_line(1, ")");

    ImageMappingConfig mapping;
    mapping.color_channels = color_channels;
    mapping.color_set = true;

    PageFooterConfig footer_config;
    u8 sample_bits = bits_per_sample(mapping.color_channels);
    u8 samples_per_pixel = color_mode_samples_per_pixel(mapping.color_channels);
    if (sample_bits == 0u || samples_per_pixel == 0u) {
        log_line(2, "unsupported color configuration");
        return 1;
    }

    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = 0.0;
    if (!encoder.set_payload(original_payload.data, original_payload.size)) {
        log_line(2, "failed to set encoder payload");
        return 1;
    }
    if (!encoder.build()) {
        log_line(2, "encoder build failed");
        return 1;
    }

    makocode::ByteBuffer frame_bits;
    u64 frame_bit_count = 0u;
    u64 payload_bit_count = 0u;
    if (!build_frame_bits(encoder, mapping, frame_bits, frame_bit_count, payload_bit_count)) {
        log_line(2, "failed to build frame bits");
        return 1;
    }

    u64 required_bits = (frame_bit_count > 0u) ? frame_bit_count : 1u;
    double width_root = sqrt((double)required_bits);
    if (width_root < 1.0) {
        width_root = 1.0;
    }
    u64 width_candidate = (u64)ceil(width_root);
    if (width_candidate == 0u || width_candidate > 0xFFFFFFFFu) {
        log_line(2, "computed width out of range");
        return 1;
    }
    u64 height_candidate = (required_bits + width_candidate - 1u) / width_candidate;
    if (height_candidate == 0u || height_candidate > 0xFFFFFFFFu) {
        log_line(2, "computed height out of range");
        return 1;
    }

    mapping.page_width_pixels = (u32)width_candidate;
    mapping.page_width_set = true;
    mapping.page_height_pixels = (u32)height_candidate;
    mapping.page_height_set = true;

    u32 width_pixels = 0u;
    u32 height_pixels = 0u;
    if (!compute_page_dimensions(mapping, width_pixels, height_pixels)) {
        log_line(2, "invalid page dimensions");
        return 1;
    }

    FooterLayout footer_layout;
    if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
        log_line(2, "footer layout computation failed");
        return 1;
    }

    u32 data_height_pixels = footer_layout.has_text ? footer_layout.data_height_pixels : height_pixels;
    if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
        log_line(2, "invalid data height");
        return 1;
    }

    u64 bits_per_page = (u64)width_pixels *
                        (u64)data_height_pixels *
                        (u64)sample_bits *
                        (u64)samples_per_pixel;
    if (bits_per_page == 0u) {
        log_line(2, "page capacity is zero");
        return 1;
    }

    char frame_bits_digits[32];
    char width_digits[32];
    char height_digits[32];
    char capacity_digits[32];
    u64_to_ascii(frame_bit_count, frame_bits_digits, sizeof(frame_bits_digits));
    u64_to_ascii((u64)width_pixels, width_digits, sizeof(width_digits));
    u64_to_ascii((u64)height_pixels, height_digits, sizeof(height_digits));
    u64_to_ascii(bits_per_page, capacity_digits, sizeof(capacity_digits));
    log_prefix(1);
    console_write(1, "frame_bits=");
    console_write(1, frame_bits_digits);
    console_write(1, " width=");
    console_write(1, width_digits);
    console_write(1, " height=");
    console_write(1, height_digits);
    console_write(1, " capacity=");
    console_write(1, capacity_digits);
    console_line(1, "");

    if (frame_bit_count > bits_per_page) {
        log_line(2, "payload exceeds single-page capacity");
        return 1;
    }

    const makocode::EccSummary& ecc_summary = encoder.ecc_info();
    makocode::ByteBuffer ppm_buffer;
    if (!encode_page_to_ppm(mapping,
                             frame_bits,
                             frame_bit_count,
                             0u,
                             width_pixels,
                             height_pixels,
                             0u,
                             1u,
                             bits_per_page,
                             payload_bit_count,
                             &ecc_summary,
                             (const char*)0,
                             0u,
                             footer_layout,
                             ppm_buffer)) {
        log_line(2, "failed to encode baseline PPM");
        return 1;
    }
    log_line(1, "encoded baseline page");

    makocode::ByteBuffer extracted_bits;
    u64 extracted_bit_count = 0u;
    PpmParserState page_state;
    if (!ppm_extract_frame_bits(ppm_buffer, mapping, extracted_bits, extracted_bit_count, page_state)) {
        log_line(2, "failed to extract frame bits from baseline page");
        return 1;
    }
    if (!page_state.has_bits) {
        page_state.has_bits = true;
        page_state.bits_value = payload_bit_count;
    }
    if (!page_state.has_page_bits || page_state.page_bits_value > extracted_bit_count) {
        page_state.has_page_bits = true;
        page_state.page_bits_value = payload_bit_count;
    }
    if (!page_state.has_page_count) {
        page_state.has_page_count = true;
        page_state.page_count_value = 1u;
    }
    if (!page_state.has_page_index) {
        page_state.has_page_index = true;
        page_state.page_index_value = 1u;
    }
    u64 effective_bits = extracted_bit_count;
    if (page_state.has_page_bits && page_state.page_bits_value <= effective_bits) {
        effective_bits = page_state.page_bits_value;
    }

    makocode::ByteBuffer payload_bits;
    u64 payload_bits_count = 0u;
    if (!frame_bits_to_payload(extracted_bits.data,
                               effective_bits,
                               page_state,
                               payload_bits,
                               payload_bits_count)) {
        log_line(2, "failed to convert baseline frame bits");
        return 1;
    }
    if (payload_bits_count != payload_bit_count) {
        log_line(2, "baseline payload bit count mismatch");
        return 1;
    }

    makocode::DecoderContext decoder;
    if (!decoder.parse(payload_bits.data, payload_bits_count, (const char*)0, 0u)) {
        log_line(2, "baseline decoder parse failed");
        return 1;
    }
    if (!decoder.has_payload || decoder.payload.size != original_payload.size) {
        log_line(2, "baseline decoded payload size mismatch");
        return 1;
    }
    for (usize i = 0u; i < original_payload.size; ++i) {
        if (decoder.payload.data[i] != original_payload.data[i]) {
            log_line(2, "baseline decoded payload mismatch");
            return 1;
        }
    }

    makocode::ByteBuffer fiducial_ppm_for_artifact;
    if (!apply_default_fiducial_grid(ppm_buffer, fiducial_ppm_for_artifact)) {
        log_line(2, "failed to embed fiducial grid for baseline artifact");
        return 1;
    }

    makocode::ByteBuffer scaled_ppm;
    if (!ppm_scale_fractional_axes(ppm_buffer, scale_factor_x, scale_factor_y, scaled_ppm)) {
        log_line(2, "failed to scale PPM");
        return 1;
    }
    makocode::ByteBuffer scaled_ppm_with_fiducials;
    if (!ppm_scale_fractional_axes(fiducial_ppm_for_artifact, scale_factor_x, scale_factor_y, scaled_ppm_with_fiducials)) {
        log_line(2, "failed to scale fiducial PPM");
        return 1;
    }
    char scale_x_display[16];
    char scale_y_display[16];
    if (!to_display_label(scale_x_label, scale_x_display, sizeof(scale_x_display)) ||
        !to_display_label(scale_y_label, scale_y_display, sizeof(scale_y_display))) {
        log_line(2, "failed to prepare scale display labels");
        return 1;
    }
    log_prefix(1);
    console_write(1, "scaled page with fractional factors x=");
    console_write(1, scale_x_display);
    console_write(1, " y=");
    console_write(1, scale_y_display);
    console_line(1, "");

    makocode::ByteBuffer scaled_bits;
    u64 scaled_bit_count = 0u;
    PpmParserState scaled_state;
    if (!ppm_extract_frame_bits(scaled_ppm, mapping, scaled_bits, scaled_bit_count, scaled_state)) {
        log_line(2, "failed to extract frame bits from scaled page");
        return 1;
    }
    if (!scaled_state.has_bits) {
        scaled_state.has_bits = true;
        scaled_state.bits_value = payload_bit_count;
    }
    if (!scaled_state.has_page_bits || scaled_state.page_bits_value > scaled_bit_count) {
        scaled_state.has_page_bits = true;
        scaled_state.page_bits_value = payload_bit_count;
    }
    if (!scaled_state.has_page_count) {
        scaled_state.has_page_count = true;
        scaled_state.page_count_value = 1u;
    }
    if (!scaled_state.has_page_index) {
        scaled_state.has_page_index = true;
        scaled_state.page_index_value = 1u;
    }
    u64 scaled_effective_bits = scaled_bit_count;
    if (scaled_state.has_page_bits && scaled_state.page_bits_value <= scaled_effective_bits) {
        scaled_effective_bits = scaled_state.page_bits_value;
    }
    makocode::ByteBuffer scaled_payload_bits;
    u64 scaled_payload_bit_count = 0u;
    if (!frame_bits_to_payload(scaled_bits.data,
                               scaled_effective_bits,
                               scaled_state,
                               scaled_payload_bits,
                               scaled_payload_bit_count)) {
        log_line(2, "failed to convert scaled frame bits");
        return 1;
    }
    if (scaled_payload_bit_count != payload_bit_count) {
        log_line(2, "scaled payload bit count mismatch");
        return 1;
    }

    makocode::DecoderContext scaled_decoder;
    if (!scaled_decoder.parse(scaled_payload_bits.data, scaled_payload_bit_count, (const char*)0, 0u)) {
        log_line(2, "scaled decoder parse failed");
        return 1;
    }
    if (!scaled_decoder.has_payload || scaled_decoder.payload.size != original_payload.size) {
        log_line(2, "scaled decoded payload size mismatch");
        return 1;
    }
    for (usize i = 0u; i < original_payload.size; ++i) {
        if (scaled_decoder.payload.data[i] != original_payload.data[i]) {
            log_line(2, "scaled decoded payload mismatch");
            return 1;
        }
    }

    auto write_artifact = [&](const char* filename,
                              const makocode::ByteBuffer& buffer) -> bool {
        makocode::ByteBuffer path;
        if (!path.append_ascii(base_dir) ||
            !path.append_char('/') ||
            !path.append_ascii(filename) ||
            !path.append_char('\0')) {
            path.release();
            return false;
        }
        bool ok = write_buffer_to_file((const char*)path.data, buffer);
        path.release();
        return ok;
    };

    makocode::ByteBuffer payload_name;
    if (!payload_name.append_ascii(artifact_prefix) ||
        !payload_name.append_ascii(".bin") ||
        !payload_name.append_char('\0')) {
        log_line(2, "failed to build payload filename");
        return 1;
    }
    if (!write_artifact((const char*)payload_name.data, original_payload)) {
        log_line(2, "failed to write payload artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  payload -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_line(1, (const char*)payload_name.data);

    makocode::ByteBuffer encoded_name;
    if (!encoded_name.append_ascii(artifact_prefix) ||
        !encoded_name.append_ascii("_encoded.ppm") ||
        !encoded_name.append_char('\0')) {
        log_line(2, "failed to build baseline encoded filename");
        return 1;
    }
    if (!write_artifact((const char*)encoded_name.data, fiducial_ppm_for_artifact)) {
        log_line(2, "failed to write baseline encoded artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  encoded -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_line(1, (const char*)encoded_name.data);

    makocode::ByteBuffer decoded_name;
    if (!decoded_name.append_ascii(artifact_prefix) ||
        !decoded_name.append_ascii("_decoded.bin") ||
        !decoded_name.append_char('\0')) {
        log_line(2, "failed to build baseline decoded filename");
        return 1;
    }
    if (!write_artifact((const char*)decoded_name.data, decoder.payload)) {
        log_line(2, "failed to write baseline decoded artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  decoded -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_line(1, (const char*)decoded_name.data);

    makocode::ByteBuffer scaled_encoded_name;
    if (!scaled_encoded_name.append_ascii(artifact_prefix) ||
        !scaled_encoded_name.append_ascii("_encoded_x") ||
        !scaled_encoded_name.append_ascii(scale_label) ||
        !scaled_encoded_name.append_ascii(".ppm") ||
        !scaled_encoded_name.append_char('\0')) {
        log_line(2, "failed to build scaled encoded filename");
        return 1;
    }
    if (!write_artifact((const char*)scaled_encoded_name.data, scaled_ppm_with_fiducials)) {
        log_line(2, "failed to write scaled encoded artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  scaled encoded -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_line(1, (const char*)scaled_encoded_name.data);

    makocode::ByteBuffer scaled_decoded_name;
    if (!scaled_decoded_name.append_ascii(artifact_prefix) ||
        !scaled_decoded_name.append_ascii("_decoded_x") ||
        !scaled_decoded_name.append_ascii(scale_label) ||
        !scaled_decoded_name.append_ascii(".bin") ||
        !scaled_decoded_name.append_char('\0')) {
        log_line(2, "failed to build scaled decoded filename");
        return 1;
    }
    if (!write_artifact((const char*)scaled_decoded_name.data, scaled_decoder.payload)) {
        log_line(2, "failed to write scaled decoded artifact");
        return 1;
    }
    log_prefix(1);
    console_write(1, "  scaled decoded -> ");
    console_write(1, base_dir);
    console_write(1, "/");
    console_line(1, (const char*)scaled_decoded_name.data);

    log_line(1, "baseline and scaled roundtrips ok");
    return 0;
}

static int command_test_payload_gray_100k_scaled(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_scaled(1u,
                                        "2002_payload_gray_100k_scaled",
                                        "test-100kb-scale",
                                        2.5,
                                        2.5);
}

static int command_test_payload_color2_100k_scaled(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_scaled(2u,
                                        "2003_payload_color2_100k_scaled",
                                        "test-100kb-scale-c2",
                                        2.5,
                                        2.5);
}

static int command_test_payload_color3_100k_scaled(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_scaled(3u,
                                        "2004_payload_color3_100k_scaled",
                                        "test-100kb-scale-c3",
                                        2.5,
                                        2.5);
}

static int command_test_payload_gray_100k_stretch_h26_v24(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_scaled(1u,
                                        "2005_payload_gray_100k_stretch_h26_v24",
                                        "test-100kb-stretch-h26-v24",
                                        2.6,
                                        2.4);
}

static int command_test_payload_gray_100k_stretch_h24_v26(int arg_count, char** args) {
    (void)arg_count;
    (void)args;
    return run_test_payload_100k_scaled(1u,
                                        "2006_payload_gray_100k_stretch_h24_v26",
                                        "test-100kb-stretch-h24-v26",
                                        2.4,
                                        2.6);
}

static int command_test_payload(int arg_count, char** args) {
    ImageMappingConfig mapping;
    PageFooterConfig footer_config;
    double ecc_redundancy = 0.0;
    makocode::ByteBuffer password_buffer;
    bool have_password = false;

    const char* test_output_dir = "test";
    if (!ensure_directory(test_output_dir)) {
        console_line(2, "test: failed to create test directory");
        return 1;
    }

    for (int i = 0; i < arg_count; ++i) {
        bool handled = false;
        if (!process_image_mapping_option(arg_count, args, &i, mapping, "test", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        handled = false;
        if (!process_fiducial_option(arg_count, args, &i, g_fiducial_defaults, "test", &handled)) {
            return 1;
        }
        if (handled) {
            continue;
        }
        const char* arg = args[i];
        if (!arg) {
            continue;
        }
        const char ecc_prefix[] = "--ecc=";
        const char* ecc_value = 0;
        usize ecc_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--ecc")) {
            if ((i + 1) >= arg_count) {
                console_line(2, "test: --ecc requires a numeric value");
                return 1;
            }
            ecc_value = args[i + 1];
            if (!ecc_value) {
                console_line(2, "test: --ecc requires a numeric value");
                return 1;
            }
            ecc_length = ascii_length(ecc_value);
            i += 1;
        } else if (ascii_starts_with(arg, ecc_prefix)) {
            ecc_value = arg + (sizeof(ecc_prefix) - 1u);
            ecc_length = ascii_length(ecc_value);
        }
        if (ecc_value) {
            if (ecc_length == 0u) {
                console_line(2, "test: --ecc requires a numeric value");
                return 1;
            }
            double redundancy_value = 0.0;
            if (!ascii_to_double(ecc_value, ecc_length, &redundancy_value)) {
                console_line(2, "test: --ecc value is not a valid decimal number");
                return 1;
            }
            if (redundancy_value < 0.0 || redundancy_value > 8.0) {
                console_line(2, "test: --ecc must be between 0.0 and 8.0");
                return 1;
            }
            ecc_redundancy = redundancy_value;
            continue;
        }
        const char password_prefix[] = "--password=";
        const char* password_value = 0;
        usize password_length = 0u;
        if (ascii_equals_token(arg, ascii_length(arg), "--password")) {
            if (have_password) {
                console_line(2, "test: password specified multiple times");
                return 1;
            }
            if ((i + 1) >= arg_count) {
                console_line(2, "test: --password requires a non-empty value");
                return 1;
            }
            password_value = args[i + 1];
            if (!password_value) {
                console_line(2, "test: --password requires a non-empty value");
                return 1;
            }
            password_length = ascii_length(password_value);
            i += 1;
        } else if (ascii_starts_with(arg, password_prefix)) {
            if (have_password) {
                console_line(2, "test: password specified multiple times");
                return 1;
            }
            password_value = arg + (sizeof(password_prefix) - 1u);
            password_length = ascii_length(password_value);
        }
        if (password_value) {
            if (password_length == 0u) {
                console_line(2, "test: --password requires a non-empty value");
                return 1;
            }
            if (!password_buffer.ensure(password_length)) {
                console_line(2, "test: failed to allocate password buffer");
                return 1;
            }
            for (usize j = 0u; j < password_length; ++j) {
                password_buffer.data[j] = (u8)password_value[j];
            }
            password_buffer.size = password_length;
            have_password = true;
            continue;
        }
        console_write(2, "test: unknown option: ");
        console_line(2, arg);
        return 1;
    }
    if (!mapping.page_width_set) {
        mapping.page_width_pixels = 64u;
        mapping.page_width_set = true;
    }
    if (!mapping.page_height_set) {
        mapping.page_height_pixels = 64u;
        mapping.page_height_set = true;
    }
    if (!verify_footer_title_roundtrip(mapping)) {
        return 1;
    }
    makocode::ByteBuffer default_password_buffer;
    const char* base_password = (const char*)0;
    usize base_password_length = 0u;
    if (have_password) {
        base_password = (const char*)password_buffer.data;
        base_password_length = password_buffer.size;
    } else {
        const char default_password_text[] = "suite-password";
        usize default_length = sizeof(default_password_text) - 1u;
        if (!default_password_buffer.ensure(default_length)) {
            console_line(2, "test: failed to allocate default password buffer");
            return 1;
        }
        for (usize i = 0u; i < default_length; ++i) {
            default_password_buffer.data[i] = (u8)default_password_text[i];
        }
        default_password_buffer.size = default_length;
        base_password = (const char*)default_password_buffer.data;
        base_password_length = default_length;
    }
    struct TestScenario {
        bool use_password;
        bool use_ecc;
        double ecc_redundancy;
        double rotation_degrees;
        bool rotate_scaled_only;
        double skew_pixels_x;
        double skew_pixels_y;
    };
    TestScenario scenarios[6];
    usize scenario_count = 0u;
    double enabled_ecc_redundancy = (ecc_redundancy > 0.0) ? ecc_redundancy : 0.5;
    for (int password_flag = 0; password_flag < 2; ++password_flag) {
        for (int ecc_flag = 0; ecc_flag < 2; ++ecc_flag) {
            TestScenario& scenario = scenarios[scenario_count++];
            scenario.use_password = (password_flag != 0);
            scenario.use_ecc = (ecc_flag != 0);
            scenario.ecc_redundancy = scenario.use_ecc ? enabled_ecc_redundancy : 0.0;
            scenario.rotation_degrees = 0.0;
            scenario.rotate_scaled_only = false;
            scenario.skew_pixels_x = 0.0;
            scenario.skew_pixels_y = 0.0;
        }
    }
    TestScenario& rotation_case = scenarios[scenario_count++];
    rotation_case.use_password = false;
    rotation_case.use_ecc = false;
    rotation_case.ecc_redundancy = 0.0;
    rotation_case.rotation_degrees = 3.0;
    rotation_case.rotate_scaled_only = true;
    rotation_case.skew_pixels_x = 0.0;
    rotation_case.skew_pixels_y = 0.0;
    TestScenario& skew_case = scenarios[scenario_count++];
    skew_case.use_password = false;
    skew_case.use_ecc = true;
    skew_case.ecc_redundancy = enabled_ecc_redundancy;
    skew_case.rotation_degrees = 0.0;
    skew_case.rotate_scaled_only = false;
    skew_case.skew_pixels_x = 3.0;
    skew_case.skew_pixels_y = 0.0;
    static const u8 color_options[3] = {1u, 2u, 3u};
    int total_runs = 0;
    u64 artifact_serial = 0u;
    bool two_page_test_done = false;
    for (usize scenario_index = 0u; scenario_index < scenario_count; ++scenario_index) {
        const TestScenario& scenario = scenarios[scenario_index];
        const char* scenario_password = scenario.use_password ? base_password : (const char*)0;
        usize scenario_password_length = scenario.use_password ? base_password_length : 0u;
        char digits_scenario[8];
        u64_to_ascii((u64)(scenario_index + 1u), digits_scenario, sizeof(digits_scenario));
        console_write(1, "test: scenario ");
        console_write(1, digits_scenario);
        console_write(1, " password=");
        console_write(1, scenario.use_password ? "yes" : "no");
        console_write(1, " ecc=");
        console_write(1, scenario.use_ecc ? "yes" : "no");
        console_write(1, " redundancy=");
        char redundancy_digits[32];
        format_fixed_3(scenario.ecc_redundancy, redundancy_digits, sizeof(redundancy_digits));
        console_write(1, redundancy_digits);
        console_write(1, " rotation=");
        char rotation_digits[32];
        format_fixed_3(scenario.rotation_degrees, rotation_digits, sizeof(rotation_digits));
        console_write(1, rotation_digits);
        console_write(1, " skew_x=");
        char skew_x_digits[32];
        format_fixed_3(scenario.skew_pixels_x, skew_x_digits, sizeof(skew_x_digits));
        console_line(1, skew_x_digits);
        for (usize color_index = 0; color_index < 3u; ++color_index) {
            ImageMappingConfig run_mapping = mapping;
            if (run_mapping.color_set) {
                if (run_mapping.color_channels != color_options[color_index]) {
                    continue;
                }
            } else {
                run_mapping.color_channels = color_options[color_index];
            }
            if (scenario.skew_pixels_x != 0.0 || scenario.skew_pixels_y != 0.0) {
                if (run_mapping.page_width_pixels <= (0xFFFFFFFFu / 3u)) {
                    run_mapping.page_width_pixels *= 3u;
                }
                if (run_mapping.page_height_pixels <= (0xFFFFFFFFu / 3u)) {
                    run_mapping.page_height_pixels *= 3u;
                }
            }
            char digits_color[8];
            u64_to_ascii((u64)run_mapping.color_channels, digits_color, sizeof(digits_color));
            console_write(1, "test:  color=");
            console_line(1, digits_color);
        u32 width_pixels = 0u;
        u32 height_pixels = 0u;
        if (!compute_page_dimensions(run_mapping, width_pixels, height_pixels)) {
            console_line(2, "test: invalid page dimensions");
            return 1;
        }
        u8 sample_bits = bits_per_sample(run_mapping.color_channels);
        u8 samples_per_pixel = color_mode_samples_per_pixel(run_mapping.color_channels);
        if (sample_bits == 0u || samples_per_pixel == 0u) {
            console_line(2, "test: unsupported color configuration");
            return 1;
        }
        FooterLayout footer_layout;
        if (!compute_footer_layout(width_pixels, height_pixels, footer_config, footer_layout)) {
            console_line(2, "test: footer layout computation failed");
            return 1;
        }
        u32 data_height_pixels = footer_layout.data_height_pixels ? footer_layout.data_height_pixels : height_pixels;
        if (data_height_pixels == 0u || data_height_pixels > height_pixels) {
            console_line(2, "test: footer configuration invalid");
            return 1;
        }
        u64 bits_per_page = (u64)width_pixels * (u64)data_height_pixels * (u64)sample_bits * (u64)samples_per_pixel;
        if (bits_per_page == 0u) {
            console_line(2, "test: page capacity is zero");
            return 1;
        }
        bool require_two_pages = (!two_page_test_done &&
                                  !scenario.use_password &&
                                  !scenario.use_ecc &&
                                  run_mapping.color_channels == 1u);
        u64 target_pages = require_two_pages ? 2u : 1u;
        if (require_two_pages) {
            two_page_test_done = true;
        }
        usize max_payload_size = (usize)((bits_per_page * 2u) / 8u) + 1024u;
        if (max_payload_size < 32u) {
            max_payload_size = 32u;
        }
        if (max_payload_size > (1u << 22u)) {
            max_payload_size = (1u << 22u);
        }
        const char* password_ptr = scenario_password;
        usize password_length = scenario_password_length;
        usize best_size = 0u;
        if (target_pages == 1u) {
            usize left = 1u;
            usize right = max_payload_size;
            bool found = false;
            while (left <= right) {
                usize mid = left + (right - left) / 2u;
                u64 seed = ((u64)run_mapping.color_channels << 32u) | (u64)mid;
                u64 mid_bits = 0u;
                if (!compute_frame_bit_count(run_mapping,
                                             mid,
                                             seed,
                                             scenario.ecc_redundancy,
                                             password_ptr,
                                             password_length,
                                             mid_bits)) {
                    console_line(2, "test: failed to evaluate payload size");
                    return 1;
                }
                u64 mid_pages = (mid_bits + bits_per_page - 1u) / bits_per_page;
                if (mid_pages <= 1u && mid_bits > 0u) {
                    found = true;
                    best_size = mid;
                    left = mid + 1u;
                } else {
                    if (mid == 0u) {
                        break;
                    }
                    if (mid_pages > 1u) {
                        if (mid == 0u) {
                            break;
                        }
                        right = mid - 1u;
                    } else {
                        left = mid + 1u;
                    }
                }
            }
            if (!found || best_size == 0u) {
                console_line(2, "test: unable to locate single-page payload");
                return 1;
            }
            u64 confirm_bits = 0u;
            u64 confirm_seed = ((u64)run_mapping.color_channels << 32u) | (u64)best_size;
            if (!compute_frame_bit_count(run_mapping,
                                         best_size,
                                         confirm_seed,
                                         scenario.ecc_redundancy,
                                         password_ptr,
                                         password_length,
                                         confirm_bits)) {
                console_line(2, "test: failed to confirm single-page payload size");
                return 1;
            }
            u64 confirm_pages = (confirm_bits + bits_per_page - 1u) / bits_per_page;
            if (confirm_pages != 1u || confirm_bits == 0u) {
                console_line(2, "test: single-page payload confirmation failed");
                return 1;
            }
        } else {
            usize left = 1u;
            usize right = max_payload_size;
            bool found = false;
            while (left <= right) {
                usize mid = left + (right - left) / 2u;
                u64 seed = ((u64)run_mapping.color_channels << 32u) | (u64)mid;
                u64 mid_bits = 0u;
                if (!compute_frame_bit_count(run_mapping,
                                             mid,
                                             seed,
                                             scenario.ecc_redundancy,
                                             password_ptr,
                                             password_length,
                                             mid_bits)) {
                    console_line(2, "test: failed to evaluate payload size");
                    return 1;
                }
                u64 mid_pages = (mid_bits + bits_per_page - 1u) / bits_per_page;
                if (mid_pages >= 2u) {
                    found = true;
                    best_size = mid;
                    if (mid == 0u) {
                        break;
                    }
                    right = mid - 1u;
                } else {
                    left = mid + 1u;
                }
            }
            if (!found || best_size == 0u) {
                console_line(2, "test: unable to locate two-page payload");
                return 1;
            }
            u64 confirm_bits = 0u;
            u64 confirm_seed = ((u64)run_mapping.color_channels << 32u) | (u64)best_size;
            if (!compute_frame_bit_count(run_mapping,
                                         best_size,
                                         confirm_seed,
                                         scenario.ecc_redundancy,
                                         password_ptr,
                                         password_length,
                                         confirm_bits)) {
                console_line(2, "test: failed to confirm two-page payload size");
                return 1;
            }
            u64 confirm_pages = (confirm_bits + bits_per_page - 1u) / bits_per_page;
            if (confirm_pages != 2u) {
                console_line(2, "test: two-page payload confirmation failed");
                return 1;
            }
        }
        makocode::ByteBuffer payload;
        makocode::EncoderContext encoder;
        encoder.config.ecc_redundancy = scenario.ecc_redundancy;
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        u64 payload_bit_count = 0u;
        u64 final_seed = ((u64)run_mapping.color_channels << 32u) | (u64)best_size;
        if (!build_payload_frame(run_mapping,
                                 best_size,
                                 final_seed,
                                 password_ptr,
                                 password_length,
                                 payload,
                                 encoder,
                                 frame_bits,
                                 frame_bit_count,
                                 payload_bit_count)) {
            console_line(2, "test: failed to build payload frame");
            return 1;
        }
        u64 page_count = (frame_bit_count + bits_per_page - 1u) / bits_per_page;
        if (page_count != target_pages) {
            console_line(2, "test: unexpected page count");
            return 1;
        }
        u64 scenario_anchor_serial = ++artifact_serial;
        makocode::BitWriter aggregate_writer;
        aggregate_writer.reset();
        PpmParserState aggregate_state;
        bool aggregate_initialized = false;
        makocode::BitWriter scan_writer;
        scan_writer.reset();
        PpmParserState scan_state;
        bool scan_initialized = false;
        bool scan_pages_present = false;
        makocode::ByteBuffer name_buffer;
        const makocode::EccSummary* ecc_summary = &encoder.ecc_info();
        for (u64 page = 0u; page < page_count; ++page) {
            u64 page_serial = scenario_anchor_serial;
            makocode::ByteBuffer page_output;
            u64 bit_offset = page * bits_per_page;
            if (!encode_page_to_ppm(run_mapping,
                                    frame_bits,
                                    frame_bit_count,
                                    bit_offset,
                                    width_pixels,
                                    height_pixels,
                                    page + 1u,
                                    page_count,
                                    bits_per_page,
                                    payload_bit_count,
                                    ecc_summary,
                                    0,
                                    0u,
                                    footer_layout,
                                    page_output)) {
                console_line(2, "test: failed to format ppm page");
                return 1;
            }
            makocode::ByteBuffer page_bits_buffer;
            u64 page_bit_count = 0u;
            PpmParserState page_state;
            if (!ppm_extract_frame_bits(page_output, run_mapping, page_bits_buffer, page_bit_count, page_state)) {
                console_line(2, "test: failed to read back ppm page");
                return 1;
            }
            if (!aggregate_initialized) {
                if (!merge_parser_state(aggregate_state, page_state)) {
                    console_line(2, "test: inconsistent metadata during aggregation");
                    return 1;
                }
                aggregate_initialized = true;
            } else {
                if (!merge_parser_state(aggregate_state, page_state)) {
                    console_line(2, "test: metadata mismatch between pages");
                    return 1;
                }
            }
            u64 effective_page_bits = page_bit_count;
            if (page_state.has_page_bits && page_state.page_bits_value <= effective_page_bits) {
                effective_page_bits = page_state.page_bits_value;
            }
            if (!append_bits_from_buffer(aggregate_writer, page_bits_buffer.data, effective_page_bits)) {
                console_line(2, "test: failed to combine page bits");
                return 1;
            }
            makocode::ByteBuffer data_base;
            if (!build_page_base_name(data_base,
                                      page_serial,
                                      "data",
                                      digits_scenario,
                                      digits_color,
                                      page + 1u,
                                      (const char*)0)) {
                console_line(2, "test: failed to build data base name");
                return 1;
            }
            makocode::ByteBuffer original_bits_payload;
            if (!buffer_store_bits_with_count(page_bits_buffer.data,
                                              effective_page_bits,
                                              original_bits_payload)) {
                console_line(2, "test: failed to marshal original data bits");
                return 1;
            }
            makocode::ByteBuffer data_bin_name;
            if (!buffer_clone_with_suffix(data_base, (const char*)0, ".bin", data_bin_name) ||
                !write_buffer_to_directory(test_output_dir, data_bin_name, original_bits_payload)) {
                console_line(2, "test: failed to write original data bits");
                return 1;
            }
            console_write(1, "test:   data bits ");
            console_line(1, (const char*)data_bin_name.data);
            makocode::ByteBuffer encoded_name;
            if (!buffer_clone_with_suffix(data_base, "_encoded", ".ppm", encoded_name) ||
                !write_buffer_to_directory(test_output_dir, encoded_name, page_output)) {
                console_line(2, "test: failed to write encoded page");
                return 1;
            }
            console_write(1, "test:   data encoded ");
            console_line(1, (const char*)encoded_name.data);
            double page_rotation = scenario.rotate_scaled_only ? 0.0 : scenario.rotation_degrees;
            makocode::ByteBuffer scan_output;
            if (simulate_scan_distortion(page_output,
                                         scan_output,
                                         page_rotation,
                                         scenario.skew_pixels_x,
                                         scenario.skew_pixels_y)) {
                makocode::ByteBuffer scan_base;
                if (!build_page_base_name(scan_base,
                                          page_serial,
                                          "scan",
                                          digits_scenario,
                                          digits_color,
                                          page + 1u,
                                          (const char*)0)) {
                    console_line(2, "test: failed to build scan base name");
                    return 1;
                }
                const char* scan_suffix = (page_rotation != 0.0) ? "_rotated" : (const char*)0;
                makocode::ByteBuffer scan_name;
                if (!buffer_clone_with_suffix(scan_base, scan_suffix, ".ppm", scan_name) ||
                    !write_buffer_to_directory(test_output_dir, scan_name, scan_output)) {
                    console_line(2, "test: failed to write simulated scan");
                    return 1;
                }
                console_write(1, "test:   scan observed ");
                console_line(1, (const char*)scan_name.data);
                makocode::ByteBuffer scan_bits;
                u64 scan_bit_count = 0u;
                PpmParserState scan_page_state;
                if (!ppm_extract_frame_bits(scan_output, run_mapping, scan_bits, scan_bit_count, scan_page_state)) {
                    console_line(2, "test: failed to parse simulated scan");
                    return 1;
                }
                if (!scan_initialized) {
                    if (!merge_parser_state(scan_state, scan_page_state)) {
                        console_line(2, "test: simulated scan metadata mismatch");
                        return 1;
                    }
                    scan_initialized = true;
                } else {
                    if (!merge_parser_state(scan_state, scan_page_state)) {
                        console_line(2, "test: simulated scan metadata mismatch between pages");
                        return 1;
                    }
                }
                u64 scan_effective_bits = scan_bit_count;
                if (scan_page_state.has_page_bits && scan_page_state.page_bits_value <= scan_effective_bits) {
                    scan_effective_bits = scan_page_state.page_bits_value;
                }
                if (scan_effective_bits != effective_page_bits) {
                    if (scan_effective_bits > effective_page_bits) {
                        scan_effective_bits = effective_page_bits;
                    } else {
                        console_line(2, "test: simulated scan bit count mismatch");
                        char detail[32];
                        console_write(2, "  expected=");
                        append_number_ascii(effective_page_bits, detail);
                        console_line(2, detail);
                        console_write(2, "  observed=");
                        append_number_ascii(scan_effective_bits, detail);
                        console_line(2, detail);
                        return 1;
                    }
                }
                for (u64 bit_index = 0u; bit_index < effective_page_bits; ++bit_index) {
                    usize byte_index = (usize)(bit_index >> 3u);
                    u8 mask = (u8)(1u << (bit_index & 7u));
                    u8 original_bit = (page_bits_buffer.data[byte_index] & mask) ? 1u : 0u;
                    u8 scanned_bit = (scan_bits.data[byte_index] & mask) ? 1u : 0u;
                    if (original_bit != scanned_bit) {
                        console_line(2, "test: simulated scan bit mismatch");
                        return 1;
                    }
                }
                makocode::ByteBuffer decoded_bits_payload;
                if (!buffer_store_bits_with_count(scan_bits.data, scan_effective_bits, decoded_bits_payload)) {
                    console_line(2, "test: failed to marshal decoded scan bits");
                    return 1;
                }
                makocode::ByteBuffer decoded_bin_name;
                if (!buffer_clone_with_suffix(data_base, "_decoded", ".bin", decoded_bin_name) ||
                    !write_buffer_to_directory(test_output_dir, decoded_bin_name, decoded_bits_payload)) {
                    console_line(2, "test: failed to write decoded data bits");
                    return 1;
                }
                console_write(1, "test:   data decoded ");
                console_line(1, (const char*)decoded_bin_name.data);
                if (!append_bits_from_buffer(scan_writer, scan_bits.data, scan_effective_bits)) {
                    console_line(2, "test: failed to combine simulated scan bits");
                    return 1;
                }
                scan_pages_present = true;
            }
        }
        if (aggregate_state.has_page_count && aggregate_state.page_count_value != page_count) {
            console_line(2, "test: page count metadata mismatch");
            return 1;
        }
        const u8* aggregate_data = aggregate_writer.data();
        u64 aggregate_bits = aggregate_writer.bit_size();
        if (aggregate_bits == 0u) {
            console_line(2, "test: empty aggregate bitstream");
            return 1;
        }
        makocode::ByteBuffer roundtrip_bits;
        u64 roundtrip_count = 0u;
        if (!frame_bits_to_payload(aggregate_data, aggregate_bits, aggregate_state, roundtrip_bits, roundtrip_count)) {
            console_line(2, "test: failed to reconstruct payload bits");
            return 1;
        }
        makocode::DecoderContext decoder;
        if (!decoder.parse(roundtrip_bits.data, roundtrip_count, password_ptr, password_length)) {
            console_line(2, "test: decode failed");
            return 1;
        }
        if (!decoder.has_payload || decoder.payload.size != payload.size) {
            console_line(2, "test: payload size mismatch");
            return 1;
        }
        for (usize i = 0u; i < payload.size; ++i) {
            if (decoder.payload.data[i] != payload.data[i]) {
                console_line(2, "test: payload mismatch");
                return 1;
            }
        }
        if (!scan_pages_present) {
            console_line(2, "test: missing simulated scan artifacts");
            return 1;
        }
        if (scan_state.has_page_count && scan_state.page_count_value != page_count) {
            console_line(2, "test: simulated scan page count mismatch");
            return 1;
        }
        const u8* scan_data = scan_writer.data();
        u64 scan_total_bits = scan_writer.bit_size();
        if (scan_total_bits == 0u) {
            console_line(2, "test: simulated scan aggregate bitstream empty");
            return 1;
        }
        makocode::ByteBuffer scan_payload_bits;
        u64 scan_payload_bit_count = 0u;
        if (!frame_bits_to_payload(scan_data, scan_total_bits, scan_state, scan_payload_bits, scan_payload_bit_count)) {
            console_line(2, "test: failed to reconstruct payload from simulated scans");
            return 1;
        }
        makocode::DecoderContext scan_decoder;
        if (!scan_decoder.parse(scan_payload_bits.data, scan_payload_bit_count, password_ptr, password_length)) {
            console_line(2, "test: simulated scan decode failed");
            return 1;
        }
        if (!scan_decoder.has_payload || scan_decoder.payload.size != payload.size) {
            console_line(2, "test: simulated scan payload size mismatch");
            return 1;
        }
        for (usize i = 0u; i < payload.size; ++i) {
            if (scan_decoder.payload.data[i] != payload.data[i]) {
                console_line(2, "test: simulated scan payload mismatch");
                return 1;
            }
        }
        makocode::ByteBuffer compressed_snapshot;
        if (!encoder.encode_payload(compressed_snapshot)) {
            console_line(2, "test: failed to recompute compressed payload for ECC validation");
            return 1;
        }
        const makocode::ByteBuffer& encoded_stream = encoder.bit_writer.buffer;
        u64 encoded_stream_bits = (u64)encoder.bit_writer.bit_size();
        if (!validate_ecc_random_bit_flips(payload,
                                            compressed_snapshot,
                                            encoded_stream,
                                            encoded_stream_bits,
                                            *ecc_summary,
                                            final_seed ^ (u64)page_count,
                                            password_ptr,
                                            password_length)) {
            console_line(2, "test: ECC random flip validation failed");
            return 1;
        }
        if (!scenario.use_password && !scenario.use_ecc && run_mapping.color_channels == 1u) {
            struct ScaleScenario {
                double factor;
                bool use_integer;
            };
            static const ScaleScenario scale_cases[] = {
                {2.0, true},
                {3.0, true},
                {2.25, false},
                {2.5, false}
            };
            usize scale_case_count = sizeof(scale_cases) / sizeof(scale_cases[0]);
            for (usize scale_index = 0u; scale_index < scale_case_count; ++scale_index) {
                const ScaleScenario& scale_case = scale_cases[scale_index];
                double scale_factor_value = scale_case.factor;
                bool use_integer_scaler = scale_case.use_integer;
                char scale_label[16];
                if (!format_scale_label(scale_factor_value, scale_label, sizeof(scale_label))) {
                    console_line(2, "test: failed to format scale label");
                    return 1;
                }
                u32 integer_factor = (u32)(scale_factor_value + 0.5);
                makocode::BitWriter scaled_writer;
                scaled_writer.reset();
                PpmParserState scaled_state;
                bool scaled_state_initialized = false;
                makocode::BitWriter scan_scaled_writer;
                scan_scaled_writer.reset();
                PpmParserState scan_scaled_state;
                bool scan_scaled_initialized = false;
                bool scan_scaled_pages_present = false;
                u64 scaled_anchor_serial = ++artifact_serial;
                for (u64 page = 0u; page < page_count; ++page) {
                    makocode::ByteBuffer baseline_page;
                    u64 bit_offset = page * bits_per_page;
                    if (!encode_page_to_ppm(run_mapping,
                                            frame_bits,
                                            frame_bit_count,
                                            bit_offset,
                                            width_pixels,
                                            height_pixels,
                                            page + 1u,
                                            page_count,
                                            bits_per_page,
                                            payload_bit_count,
                                            ecc_summary,
                                            0,
                                            0u,
                                            footer_layout,
                                            baseline_page)) {
                        console_line(2, "test: failed to regenerate baseline page for scaling");
                        return 1;
                    }
                    makocode::ByteBuffer baseline_page_with_fid;
                    if (!apply_default_fiducial_grid(baseline_page, baseline_page_with_fid)) {
                        console_line(2, "test: failed to embed fiducial grid before scaling");
                        return 1;
                    }
                    makocode::ByteBuffer scaled_page;
                    makocode::ByteBuffer scaled_page_for_artifact;
                    if (use_integer_scaler) {
                        if (!ppm_scale_integer(baseline_page, integer_factor, scaled_page)) {
                            console_line(2, "test: failed to scale ppm page");
                            return 1;
                        }
                        if (!ppm_scale_integer(baseline_page_with_fid, integer_factor, scaled_page_for_artifact)) {
                            console_line(2, "test: failed to scale fiducial ppm page");
                            return 1;
                        }
                    } else {
                        if (!ppm_scale_fractional(baseline_page, scale_factor_value, scaled_page)) {
                            console_line(2, "test: failed to scale ppm page fractionally");
                            return 1;
                        }
                        if (!ppm_scale_fractional(baseline_page_with_fid, scale_factor_value, scaled_page_for_artifact)) {
                            console_line(2, "test: failed to scale fiducial ppm page fractionally");
                            return 1;
                        }
                    }
                    makocode::ByteBuffer scaled_bits;
                    u64 scaled_bit_count = 0u;
                    PpmParserState scaled_page_state;
                    if (!ppm_extract_frame_bits(scaled_page, run_mapping, scaled_bits, scaled_bit_count, scaled_page_state)) {
                        console_line(2, "test: scaled ppm parse failed");
                        return 1;
                    }
                    u64 scaled_effective_bits = scaled_bit_count;
                    if (scaled_page_state.has_page_bits && scaled_page_state.page_bits_value <= scaled_effective_bits) {
                        scaled_effective_bits = scaled_page_state.page_bits_value;
                    }
                    makocode::ByteBuffer scaled_base;
                    u64 scaled_serial = scaled_anchor_serial;
                    if (!build_page_base_name(scaled_base,
                                              scaled_serial,
                                              "scaled",
                                              digits_scenario,
                                              digits_color,
                                              page + 1u,
                                              scale_label)) {
                        console_line(2, "test: failed to build scaled scan base");
                        return 1;
                    }
                    makocode::ByteBuffer scaled_original_payload;
                    if (!buffer_store_bits_with_count(scaled_bits.data,
                                                      scaled_effective_bits,
                                                      scaled_original_payload)) {
                        console_line(2, "test: failed to marshal scaled original bits");
                        return 1;
                    }
                    makocode::ByteBuffer scaled_bin_name;
                    if (!buffer_clone_with_suffix(scaled_base, (const char*)0, ".bin", scaled_bin_name) ||
                        !write_buffer_to_directory(test_output_dir, scaled_bin_name, scaled_original_payload)) {
                        console_line(2, "test: failed to write scaled original bits");
                        return 1;
                    }
                    console_write(1, "test:   scaled bits ");
                    console_line(1, (const char*)scaled_bin_name.data);
                    makocode::ByteBuffer scaled_encoded_name;
                    if (!buffer_clone_with_suffix(scaled_base, "_encoded", ".ppm", scaled_encoded_name) ||
                        !write_buffer_to_directory(test_output_dir, scaled_encoded_name, scaled_page_for_artifact)) {
                        console_line(2, "test: failed to write scaled scan page");
                        return 1;
                    }
                    console_write(1, "test:   scaled encoded ");
                    console_line(1, (const char*)scaled_encoded_name.data);
                    double scaled_rotation = (scenario.rotate_scaled_only && use_integer_scaler && integer_factor == 3u) ? scenario.rotation_degrees : 0.0;
                    makocode::ByteBuffer scan_scaled;
                    if (simulate_scan_distortion(scaled_page,
                                                 scan_scaled,
                                                 scaled_rotation,
                                                 scenario.skew_pixels_x,
                                                 scenario.skew_pixels_y)) {
                        const char* scaled_suffix = (scaled_rotation != 0.0) ? "_rotated" : (const char*)0;
                        makocode::ByteBuffer scan_scaled_name;
                        if (!buffer_clone_with_suffix(scaled_base, scaled_suffix, ".ppm", scan_scaled_name) ||
                            !write_buffer_to_directory(test_output_dir, scan_scaled_name, scan_scaled)) {
                            console_line(2, "test: failed to write simulated scan for scaled page");
                            return 1;
                        }
                        console_write(1, "test:   scan observed ");
                        console_line(1, (const char*)scan_scaled_name.data);
                        makocode::ByteBuffer scan_scaled_bits;
                        u64 scan_scaled_bit_count = 0u;
                        PpmParserState scan_scaled_page_state;
                        if (!ppm_extract_frame_bits(scan_scaled, run_mapping, scan_scaled_bits, scan_scaled_bit_count, scan_scaled_page_state)) {
                            console_line(2, "test: failed to parse simulated scan for scaled page");
                            return 1;
                        }
                        if (!scan_scaled_initialized) {
                            if (!merge_parser_state(scan_scaled_state, scan_scaled_page_state)) {
                                console_line(2, "test: scaled simulated scan metadata mismatch");
                                return 1;
                            }
                            scan_scaled_initialized = true;
                        } else {
                            if (!merge_parser_state(scan_scaled_state, scan_scaled_page_state)) {
                                console_line(2, "test: scaled simulated scan metadata mismatch between pages");
                                return 1;
                            }
                        }
                        u64 scan_scaled_effective = scan_scaled_bit_count;
                        if (scan_scaled_page_state.has_page_bits && scan_scaled_page_state.page_bits_value <= scan_scaled_effective) {
                            scan_scaled_effective = scan_scaled_page_state.page_bits_value;
                        }
                        if (scan_scaled_effective != scaled_effective_bits) {
                            console_line(2, "test: scaled simulated scan bit count mismatch");
                            return 1;
                        }
                        for (u64 bit_index = 0u; bit_index < scaled_effective_bits; ++bit_index) {
                            usize byte_index = (usize)(bit_index >> 3u);
                            u8 mask = (u8)(1u << (bit_index & 7u));
                            u8 original_bit = (scaled_bits.data[byte_index] & mask) ? 1u : 0u;
                            u8 scanned_bit = (scan_scaled_bits.data[byte_index] & mask) ? 1u : 0u;
                            if (original_bit != scanned_bit) {
                                console_line(2, "test: scaled simulated scan bit mismatch");
                                return 1;
                            }
                        }
                        makocode::ByteBuffer scan_scaled_decoded_payload;
                        if (!buffer_store_bits_with_count(scan_scaled_bits.data,
                                                           scan_scaled_effective,
                                                           scan_scaled_decoded_payload)) {
                            console_line(2, "test: failed to marshal scaled decoded bits");
                            return 1;
                        }
                        makocode::ByteBuffer scan_scaled_bin_name;
                        if (!buffer_clone_with_suffix(scaled_base, "_decoded", ".bin", scan_scaled_bin_name) ||
                            !write_buffer_to_directory(test_output_dir, scan_scaled_bin_name, scan_scaled_decoded_payload)) {
                            console_line(2, "test: failed to write scaled decoded bits");
                            return 1;
                        }
                        console_write(1, "test:   scaled decoded ");
                        console_line(1, (const char*)scan_scaled_bin_name.data);
                        if (!append_bits_from_buffer(scan_scaled_writer, scan_scaled_bits.data, scan_scaled_effective)) {
                            console_line(2, "test: failed to combine scaled simulated scan bits");
                            return 1;
                        }
                        scan_scaled_pages_present = true;
                    }
                    if (!scaled_state_initialized) {
                        if (!merge_parser_state(scaled_state, scaled_page_state)) {
                            console_line(2, "test: scaled metadata mismatch");
                            return 1;
                        }
                        scaled_state_initialized = true;
                    } else {
                        if (!merge_parser_state(scaled_state, scaled_page_state)) {
                            console_line(2, "test: scaled metadata mismatch between pages");
                            return 1;
                        }
                    }
                    if (!append_bits_from_buffer(scaled_writer, scaled_bits.data, scaled_effective_bits)) {
                        console_line(2, "test: failed to assemble scaled bitstream");
                        return 1;
                    }
                }
                if (scaled_state.has_page_count && scaled_state.page_count_value != page_count) {
                    console_line(2, "test: scaled page count metadata mismatch");
                    return 1;
                }
                const u8* scaled_data = scaled_writer.data();
                u64 scaled_total_bits = scaled_writer.bit_size();
                if (scaled_total_bits == 0u) {
                    console_line(2, "test: scaled aggregate bitstream empty");
                    return 1;
                }
                makocode::ByteBuffer scaled_payload_bits;
                u64 scaled_payload_bit_count = 0u;
                if (!frame_bits_to_payload(scaled_data, scaled_total_bits, scaled_state, scaled_payload_bits, scaled_payload_bit_count)) {
                    console_line(2, "test: failed to reconstruct payload from scaled pages");
                    return 1;
                }
                makocode::DecoderContext scaled_decoder;
                if (!scaled_decoder.parse(scaled_payload_bits.data, scaled_payload_bit_count, password_ptr, password_length)) {
                    console_line(2, "test: scaled decode failed");
                    return 1;
                }
                if (!scaled_decoder.has_payload || scaled_decoder.payload.size != payload.size) {
                    console_line(2, "test: scaled payload size mismatch");
                    return 1;
                }
                for (usize i = 0u; i < payload.size; ++i) {
                    if (scaled_decoder.payload.data[i] != payload.data[i]) {
                        console_line(2, "test: scaled payload mismatch");
                        return 1;
                    }
                }
                if (!scan_scaled_pages_present) {
                    console_line(2, "test: missing simulated scans for scaled pages");
                    return 1;
                }
                if (scan_scaled_state.has_page_count && scan_scaled_state.page_count_value != page_count) {
                    console_line(2, "test: scaled simulated scan page count mismatch");
                    return 1;
                }
                const u8* scan_scaled_data = scan_scaled_writer.data();
                u64 scan_scaled_total_bits = scan_scaled_writer.bit_size();
                if (scan_scaled_total_bits == 0u) {
                    console_line(2, "test: scaled simulated scan aggregate bitstream empty");
                    return 1;
                }
                makocode::ByteBuffer scan_scaled_payload_bits;
                u64 scan_scaled_payload_bit_count = 0u;
                if (!frame_bits_to_payload(scan_scaled_data,
                                           scan_scaled_total_bits,
                                           scan_scaled_state,
                                           scan_scaled_payload_bits,
                                           scan_scaled_payload_bit_count)) {
                    console_line(2, "test: failed to reconstruct payload from scaled simulated scans");
                    return 1;
                }
                makocode::DecoderContext scan_scaled_decoder;
                if (!scan_scaled_decoder.parse(scan_scaled_payload_bits.data,
                                               scan_scaled_payload_bit_count,
                                               password_ptr,
                                               password_length)) {
                    console_line(2, "test: scaled simulated scan decode failed");
                    return 1;
                }
                if (!scan_scaled_decoder.has_payload || scan_scaled_decoder.payload.size != payload.size) {
                    console_line(2, "test: scaled simulated scan payload size mismatch");
                    return 1;
                }
                for (usize i = 0u; i < payload.size; ++i) {
                    if (scan_scaled_decoder.payload.data[i] != payload.data[i]) {
                        console_line(2, "test: scaled simulated scan payload mismatch");
                        return 1;
                    }
                }
            }
        }
        name_buffer.release();
        if (scenario_anchor_serial == 0u) {
            console_line(2, "test: payload missing anchor serial");
            return 1;
        }
        u64 payload_serial = scenario_anchor_serial;
        if (!buffer_append_zero_padded(name_buffer, payload_serial, 4u) ||
            !name_buffer.append_ascii("_payload_s") ||
            !name_buffer.append_ascii(digits_scenario) ||
            !name_buffer.append_ascii("_c") ||
            !name_buffer.append_ascii(digits_color) ||
            !name_buffer.append_ascii(".bin") ||
            !name_buffer.append_char('\0')) {
            console_line(2, "test: failed to build payload filename");
            return 1;
        }
        if (!write_buffer_to_directory(test_output_dir, name_buffer, payload)) {
            console_line(2, "test: failed to write payload file");
            return 1;
        }
        console_write(1, "test:   payload ");
        console_line(1, (const char*)name_buffer.data);
        name_buffer.release();
        if (!buffer_append_zero_padded(name_buffer, payload_serial, 4u) ||
            !name_buffer.append_ascii("_payload_s") ||
            !name_buffer.append_ascii(digits_scenario) ||
            !name_buffer.append_ascii("_c") ||
            !name_buffer.append_ascii(digits_color) ||
            !name_buffer.append_ascii("_decoded.bin") ||
            !name_buffer.append_char('\0')) {
            console_line(2, "test: failed to build decoded filename");
            return 1;
        }
        if (!write_buffer_to_directory(test_output_dir, name_buffer, decoder.payload)) {
            console_line(2, "test: failed to write decoded payload");
            return 1;
        }
        console_write(1, "test:   payload decoded ");
        console_line(1, (const char*)name_buffer.data);
        ++total_runs;
        if (mapping.color_set) {
            break;
        }
    }
    }
    char digits_runs[16];
    u64_to_ascii((u64)total_runs, digits_runs, sizeof(digits_runs));
    console_write(1, "test: completed runs=");
    console_write(1, digits_runs);
    console_line(1, "");
    console_line(1, "test: artifacts saved for all combinations");
    return 0;
}

static int command_test(int arg_count, char** args) {
    struct TestSuiteEntry {
        const char* name;
        int (*fn)(int, char**);
        bool forward_args;
    };
    /* Test suite summary:
       1) scan-basic                      - validates histogram analytics and dirt removal on small fixtures.
       2) border-dirt                     - exercises case 13 border speck encode -> dirty -> clean -> decode roundtrip.
       3) payload-100kb                   - performs a 100 KiB encode/ppm/decode roundtrip without distortions.
       4) payload-100kb-wavy (commented)  - runs a 100 KiB encode with fiducials, ripple distortion, and decode comparison.
       5) payload-100kb-wavy-c2 (commented) - extends the ripple distortion roundtrip to color channel 2.
       6) payload-100kb-wavy-c3 (commented) - extends the ripple distortion roundtrip to color channel 3.
       7) payload-100kb-scaled            - expands the 100 KiB roundtrip with a 2.5x fractional scale decode validation for color channel 1.
       8) payload-100kb-scaled-c2         - repeats the scaled roundtrip for color channel 2.
       9) payload-100kb-scaled-c3         - repeats the scaled roundtrip for color channel 3.
      10) payload-100kb-stretch-h26-v24   - validates fractional scaling with horizontal 2.6x and vertical 2.4x for grayscale pages.
      11) payload-100kb-stretch-h24-v26   - validates fractional scaling with horizontal 2.4x and vertical 2.6x for grayscale pages.
      12) payload-suite                   - runs exhaustive payload encode/decode scenarios across colors/password/ECC. */
    const TestSuiteEntry suites[] = {
        {"scan-basic", command_test_scan_basic, false},
        {"border-dirt", command_test_border_dirt, false},
        {"payload-100kb", command_test_payload_gray_100k, false},
        // {"payload-100kb-wavy", command_test_payload_gray_100k_wavy, false},
        // {"payload-100kb-wavy-c2", command_test_payload_color2_100k_wavy, false},
        // {"payload-100kb-wavy-c3", command_test_payload_color3_100k_wavy, false},
        {"payload-100kb-scaled", command_test_payload_gray_100k_scaled, false},
        {"payload-100kb-scaled-c2", command_test_payload_color2_100k_scaled, false},
        {"payload-100kb-scaled-c3", command_test_payload_color3_100k_scaled, false},
        {"payload-100kb-stretch-h26-v24", command_test_payload_gray_100k_stretch_h26_v24, false},
        {"payload-100kb-stretch-h24-v26", command_test_payload_gray_100k_stretch_h24_v26, false},
        {"payload-suite", command_test_payload, true}
    };
    usize suite_count = sizeof(suites) / sizeof(suites[0]);
    for (usize i = 0u; i < suite_count; ++i) {
        const TestSuiteEntry& entry = suites[i];
        console_write(1, "test-suite: ");
        console_line(1, entry.name);
        int result = entry.forward_args ? entry.fn(arg_count, args)
                                        : entry.fn(0, (char**)0);
        if (result != 0) {
            console_write(2, "test-suite: failure in ");
            console_line(2, entry.name);
            return result;
        }
    }
    console_line(1, "test-suite: complete");
    return 0;
}

static bool read_file_into_buffer(const char* path, makocode::ByteBuffer& buffer) {
    if (!path) {
        return false;
    }
    buffer.release();
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    u8 chunk[4096];
    while (1) {
        int read_result = read(fd, chunk, sizeof(chunk));
        if (read_result < 0) {
            close(fd);
            buffer.release();
            return false;
        }
        if (read_result == 0) {
            break;
        }
        if (!buffer.append_bytes(chunk, (usize)read_result)) {
            close(fd);
            buffer.release();
            return false;
        }
    }
    close(fd);
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

static int command_minify(int arg_count, char** args) {
    (void)args;
    if (arg_count > 0) {
        console_line(2, "minify: this command does not accept arguments");
        return 1;
    }
    makocode::ByteBuffer source;
    if (!read_file_into_buffer("makocode.cpp", source)) {
        console_line(2, "minify: failed to read makocode.cpp");
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
                } else if (previous != '\n') {
                    if (!minified.push('\n')) {
                        minified.release();
                        console_line(2, "minify: failed to compact whitespace");
                        return 1;
                    }
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
    usize start_index = 0u;
    while (start_index < stripped.size &&
           (stripped.data[start_index] == ' ' || stripped.data[start_index] == '\n')) {
        start_index += 1u;
    }
    usize end_index = stripped.size;
    while (end_index > start_index &&
           (stripped.data[end_index - 1u] == ' ' || stripped.data[end_index - 1u] == '\n')) {
        end_index -= 1u;
    }
    if (start_index > 0u && end_index > start_index) {
        for (usize i = start_index; i < end_index; ++i) {
            stripped.data[i - start_index] = stripped.data[i];
        }
    }
    stripped.size = (end_index > start_index) ? (end_index - start_index) : 0u;
    if (!write_bytes_to_file("makocode_minified.cpp", stripped.data, stripped.size)) {
        console_line(2, "minify: failed to write makocode_minified.cpp");
        return 1;
    }
    console_line(1, "minify: wrote makocode_minified.cpp");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        write_usage();
        return 0;
    }
    if (ascii_compare(argv[1], "encode") == 0) {
        return command_encode(argc - 2, argv + 2);
    }
    if (ascii_compare(argv[1], "decode") == 0) {
        return command_decode(argc - 2, argv + 2);
    }
    if (ascii_compare(argv[1], "test-scan-basic") == 0) {
        return command_test_scan_basic(argc - 2, argv + 2);
    }
    if (ascii_compare(argv[1], "test-border-dirt") == 0) {
        return command_test_border_dirt(argc - 2, argv + 2);
    }
    if (ascii_compare(argv[1], "test") == 0) {
        return command_test(argc - 2, argv + 2);
    }
    if (ascii_compare(argv[1], "minify") == 0) {
        return command_minify(argc - 2, argv + 2);
    }
    write_usage();
    return 0;
}
