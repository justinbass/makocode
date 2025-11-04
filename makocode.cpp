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
struct tm;
extern "C" long  time(long* tloc);
extern "C" struct tm* gmtime(const long* timep);
extern "C" unsigned long strftime(char* s, unsigned long max, const char* format, const struct tm* tm);

#ifndef O_RDONLY
#define O_RDONLY 0
#endif

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
    bool configured;

    EccSummary ecc_summary;

    EncoderContext() : config(), payload_bytes(), bit_writer(), configured(false), ecc_summary() {}

    void reset() {
        payload_bytes.release();
        bit_writer.reset();
        configured = false;
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
        if (compressed.size == 0u) {
            if (!bit_writer.align_to_byte()) {
                return false;
            }
            configured = true;
            return true;
        }
        double redundancy = (config.ecc_redundancy > 0.0) ? config.ecc_redundancy : 0.0;
        if (redundancy > 0.0) {
            if (!encode_payload_with_ecc(compressed, redundancy, bit_writer, ecc_summary)) {
                return false;
            }
        } else {
            for (usize i = 0u; i < compressed.size; ++i) {
                u8 byte = compressed.data ? compressed.data[i] : 0u;
                if (!bit_writer.write_bits((u64)byte, 8u)) {
                    return false;
                }
            }
            ecc_summary.original_bytes = compressed.size;
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

    DecoderContext() : payload(), has_payload(false), ecc_failed(false) {}

    void reset() {
        payload.release();
        has_payload = false;
        ecc_failed = false;
    }

    bool parse(u8* data, usize size_in_bits) {
        payload.release();
        has_payload = false;
        ecc_failed = false;
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
                ecc_failed = true;
                return false;
            }
            if (header.original_bytes == 0u) {
                has_payload = true;
                return true;
            }
            u64 bit_total = header.original_bytes * 8u;
            if (bit_total > (u64)USIZE_MAX_VALUE) {
                ecc_failed = true;
                return false;
            }
            if (!lzw_decompress(compressed.data, (usize)bit_total, payload)) {
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
        if (!lzw_decompress(data, size_in_bits, payload)) {
            return false;
        }
        has_payload = true;
        return true;
    }

    bool ecc_correction_failed() const {
        return ecc_failed;
    }
};

} // namespace makocode

static const u32 DEFAULT_PAGE_WIDTH_PIXELS  = 2480u; // matches prior A4 default
static const u32 DEFAULT_PAGE_HEIGHT_PIXELS = 3508u; // matches prior A4 default

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
    {'0', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
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
    {'g', {"00000", "00000", "01111", "10001", "10001", "01111", "00001"}},
    {'h', {"10000", "10000", "11110", "10001", "10001", "10001", "10001"}},
    {'i', {"00100", "00000", "01100", "00100", "00100", "00100", "01110"}},
    {'j', {"00010", "00000", "00110", "00010", "00010", "00010", "01100"}},
    {'k', {"10000", "10000", "10010", "10100", "11000", "10100", "10010"}},
    {'l', {"01100", "00100", "00100", "00100", "00100", "00100", "01110"}},
    {'m', {"00000", "00000", "11010", "10101", "10101", "10101", "10101"}},
    {'n', {"00000", "00000", "11110", "10001", "10001", "10001", "10001"}},
    {'o', {"00000", "00000", "01110", "10001", "10001", "10001", "01110"}},
    {'p', {"00000", "00000", "11110", "10001", "10001", "11110", "10000"}},
    {'q', {"00000", "00000", "01111", "10001", "10001", "01111", "00001"}},
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
    for (u32 palette_index = 0u; palette_index < palette_size; ++palette_index) {
        const PaletteColor& candidate = palette[palette_index];
        if (candidate.r == rgb[0] &&
            candidate.g == rgb[1] &&
            candidate.b == rgb[2]) {
            if (mode == 1u) {
                if (palette_index > 1u) {
                    return false;
                }
                samples[0] = palette_index ^ 1u;
            } else {
                samples[0] = palette_index;
            }
            return true;
        }
    }
    return false;
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

static bool write_buffer_to_file(const char* path, const makocode::ByteBuffer& buffer) {
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
          font_size_value(0u) {}
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
    const usize bytes_tag_len = (usize)sizeof(bytes_tag) - 1u;
    const usize bits_tag_len = (usize)sizeof(bits_tag) - 1u;
    const usize ecc_tag_len = (usize)sizeof(ecc_tag) - 1u;
    const usize ecc_block_tag_len = (usize)sizeof(ecc_block_tag) - 1u;
    const usize ecc_parity_tag_len = (usize)sizeof(ecc_parity_tag) - 1u;
    const usize ecc_block_count_tag_len = (usize)sizeof(ecc_block_count_tag) - 1u;
    const usize ecc_original_tag_len = (usize)sizeof(ecc_original_tag) - 1u;
    const usize color_tag_len = (usize)sizeof(color_tag) - 1u;
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
    u64 pixel_count = width * height;
    if (pixel_count == 0u) {
        return false;
    }
    u64 footer_rows = 0u;
    if (state.has_footer_rows) {
        footer_rows = state.footer_rows_value;
        if (footer_rows > height) {
            return false;
        }
    }
    u64 data_height = height - footer_rows;
    if (data_height == 0u) {
        return false;
    }
    u64 data_pixels = width * data_height;
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
    for (u64 pixel = 0u; pixel < pixel_count; ++pixel) {
        u64 r_value = 0u;
        u64 g_value = 0u;
        u64 b_value = 0u;
        if (!ppm_next_token(state, &token, &token_length)) {
            return false;
        }
        if (!ascii_to_u64(token, token_length, &r_value) || r_value > 255u) {
            return false;
        }
        if (!ppm_next_token(state, &token, &token_length)) {
            return false;
        }
        if (!ascii_to_u64(token, token_length, &g_value) || g_value > 255u) {
            return false;
        }
        if (!ppm_next_token(state, &token, &token_length)) {
            return false;
        }
        if (!ascii_to_u64(token, token_length, &b_value) || b_value > 255u) {
            return false;
        }
        u8 rgb[3];
        rgb[0] = (u8)r_value;
        rgb[1] = (u8)g_value;
        rgb[2] = (u8)b_value;
        u32 samples_raw[3] = {0u, 0u, 0u};
        if (!map_rgb_to_samples(color_mode, rgb, samples_raw)) {
            return false;
        }
        if (pixel < data_pixels) {
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
static bool process_image_mapping_option(const char* arg,
                                         ImageMappingConfig& config,
                                         const char* command_name,
                                         bool* handled) {
    if (!handled) {
        return false;
    }
    *handled = false;
    if (!arg) {
        return true;
    }
    const char color_prefix[] = "--color-channels=";
    if (ascii_starts_with(arg, color_prefix)) {
        const char* value_text = arg + (sizeof(color_prefix) - 1u);
        usize length = ascii_length(value_text);
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
    if (ascii_starts_with(arg, width_prefix)) {
        const char* value_text = arg + (sizeof(width_prefix) - 1u);
        usize length = ascii_length(value_text);
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
    if (ascii_starts_with(arg, height_prefix)) {
        const char* value_text = arg + (sizeof(height_prefix) - 1u);
        usize length = ascii_length(value_text);
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

static void write_usage() {
    console_line(1, "MakoCode CLI");
    console_line(1, "Usage:");
    console_line(1, "  makocode encode [options]   (reads payload from file; emits PPM pages)");
    console_line(1, "  makocode decode [options] files... (reads PPM pages; use stdin when no files)");
    console_line(1, "  makocode test   [options]   (verifies two-page encode/decode per color)");
    console_line(1, "Options:");
    console_line(1, "  --color-channels=N (1=Gray, 2=CMY, 3=RGB; default 1)");
    console_line(1, "  --page-width=PX    (page width in pixels; default 2480)");
    console_line(1, "  --page-height=PX   (page height in pixels; default 3508)");
    console_line(1, "  --input=FILE       (payload input path; required for encode)");
    console_line(1, "  --ecc=RATIO        (Reed-Solomon redundancy; 0 disables, e.g., 0.10)");
    console_line(1, "  --no-filename      (omit payload filename from footer text)");
    console_line(1, "  --no-page-count    (omit page index/total from footer text)");
    console_line(1, "  --title=TEXT       (optional footer title; letters, digits, common symbols)");
    console_line(1, "  --font-size=PX     (footer font scale in pixels; default 1)");
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
    const char* input_path = 0;
    makocode::ByteBuffer title_buffer;
    makocode::ByteBuffer filename_buffer;
    for (int i = 0; i < arg_count; ++i) {
        const char* arg = args[i];
        bool handled = false;
        if (!process_image_mapping_option(arg, mapping, "encode", &handled)) {
            return 1;
        }
        if (!handled) {
            if (ascii_equals_token(arg, ascii_length(arg), "--no-filename")) {
                footer_config.display_filename = false;
                continue;
            }
            if (ascii_equals_token(arg, ascii_length(arg), "--no-page-count")) {
                footer_config.display_page_info = false;
                continue;
            }
            const char ecc_prefix[] = "--ecc=";
            if (ascii_starts_with(arg, ecc_prefix)) {
                const char* value_text = arg + (sizeof(ecc_prefix) - 1u);
                usize length = ascii_length(value_text);
                if (length == 0u) {
                    console_line(2, "encode: --ecc requires a numeric value");
                    return 1;
                }
                double redundancy_value = 0.0;
                if (!ascii_to_double(value_text, length, &redundancy_value)) {
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
            if (ascii_starts_with(arg, input_prefix)) {
                const char* value_text = arg + (sizeof(input_prefix) - 1u);
                if (!value_text || value_text[0] == '\0') {
                    console_line(2, "encode: --input requires a file path");
                    return 1;
                }
                input_path = value_text;
                continue;
            }
            const char title_prefix[] = "--title=";
            if (ascii_starts_with(arg, title_prefix)) {
                const char* value_text = arg + (sizeof(title_prefix) - 1u);
                usize length = ascii_length(value_text);
                if (length == 0u) {
                    console_line(2, "encode: --title requires a non-empty value");
                    return 1;
                }
                if (!title_buffer.ensure(length + 1u)) {
                    console_line(2, "encode: failed to allocate title buffer");
                    return 1;
                }
                for (usize j = 0u; j < length; ++j) {
                    char c = value_text[j];
                    if (!title_char_is_allowed(c)) {
                        console_line(2, "encode: title supports letters, digits, space, and !@#$%^&*()_+-={}[]:\";'<>?,./`~|\\");
                        return 1;
                    }
                    title_buffer.data[j] = (u8)c;
                }
                title_buffer.data[length] = 0u;
                title_buffer.size = length;
                footer_config.has_title = true;
                footer_config.title_length = length;
                footer_config.title_text = (const char*)title_buffer.data;
                continue;
            }
            const char font_prefix[] = "--font-size=";
            if (ascii_starts_with(arg, font_prefix)) {
                const char* value_text = arg + (sizeof(font_prefix) - 1u);
                usize length = ascii_length(value_text);
                if (length == 0u) {
                    console_line(2, "encode: --font-size requires a positive integer value");
                    return 1;
                }
                u64 value = 0u;
                if (!ascii_to_u64(value_text, length, &value) || value == 0u || value > 2048u) {
                    console_line(2, "encode: --font-size must be between 1 and 2048");
                    return 1;
                }
                footer_config.font_size = (u32)value;
                continue;
            }
            console_write(2, "encode: unknown option: ");
            console_line(2, arg);
            return 1;
        }
    }
    if (!input_path) {
        console_line(2, "encode: --input=FILE is required");
        return 1;
    }
    makocode::ByteBuffer payload;
    if (!read_entire_file(input_path, payload)) {
        console_write(2, "encode: failed to read ");
        console_line(2, input_path);
        return 1;
    }
    const char* base_name = input_path;
    const char* scan = input_path;
    while (scan && *scan) {
        if (*scan == '/' || *scan == '\\') {
            base_name = scan + 1;
        }
        ++scan;
    }
    usize base_length = ascii_length(base_name);
    if (base_length == 0u) {
        console_line(2, "encode: input filename is empty");
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
    footer_config.has_filename = (base_length > 0u);
    if (footer_config.has_title && (!footer_config.title_text || footer_config.title_length == 0u)) {
        console_line(2, "encode: title configuration is invalid");
        return 1;
    }
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = ecc_redundancy;
    if (!encoder.set_payload(payload.data, payload.size)) {
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
    for (int i = 0; i < arg_count; ++i) {
        bool handled = false;
        if (!process_image_mapping_option(args[i], mapping, "decode", &handled)) {
            return 1;
        }
        if (!handled) {
            if (file_count >= MAX_INPUT_FILES) {
                console_line(2, "decode: too many input files");
                return 1;
            }
            input_files[file_count++] = args[i];
        }
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
            if (!append_bits_from_buffer(frame_aggregator, page_bits.data, page_bit_count)) {
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
    bool ecc_metadata_available = have_metadata &&
                                   aggregate_state.has_ecc_flag &&
                                   aggregate_state.ecc_flag_value;
    if (have_metadata &&
        aggregate_state.has_ecc_flag &&
        !aggregate_state.ecc_flag_value) {
        console_line(2, "decode: warning: payload was encoded without ECC protection");
    }
    if (ecc_metadata_available &&
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
    if (!decoder.parse(bitstream.data, bit_count)) {
        if (decoder.ecc_correction_failed()) {
            console_line(2, "decode: ECC could not repair the payload");
        } else {
            console_line(2, "decode: parse failure");
        }
        return 1;
    }
    if (decoder.ecc_correction_failed()) {
        console_line(2, "decode: warning: payload may contain uncorrected errors");
    }
    if (decoder.has_payload && decoder.payload.size) {
        write(1, decoder.payload.data, decoder.payload.size);
    }
    return 0;
}

static bool build_payload_frame(const ImageMappingConfig& mapping,
                                usize payload_size,
                                u64 seed,
                                makocode::ByteBuffer& payload,
                                makocode::EncoderContext& encoder,
                                makocode::ByteBuffer& frame_bits,
                                u64& frame_bit_count,
                                u64& payload_bit_count) {
    if (!makocode::generate_random_bytes(payload, payload_size, seed)) {
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
                                    u64& frame_bit_count) {
    makocode::ByteBuffer payload;
    makocode::EncoderContext encoder;
    encoder.config.ecc_redundancy = ecc_redundancy;
    makocode::ByteBuffer frame_bits;
    u64 payload_bits = 0u;
    if (!build_payload_frame(mapping, payload_size, seed, payload, encoder, frame_bits, frame_bit_count, payload_bits)) {
        return false;
    }
    return true;
}

static bool validate_ecc_random_bit_flips(const makocode::ByteBuffer& original_payload,
                                          const makocode::ByteBuffer& compressed_payload,
                                          const makocode::ByteBuffer& encoded_bits,
                                          u64 encoded_bit_count,
                                          const makocode::EccSummary& summary,
                                          u64 seed) {
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
    makocode::EccHeaderInfo header_info;
    if (!makocode::parse_ecc_header(corrupted.data, byte_count, header_info) || !header_info.valid || !header_info.enabled) {
        return false;
    }
    makocode::ByteBuffer repaired_payload;
    if (!makocode::decode_ecc_payload(corrupted.data + makocode::ECC_HEADER_BYTES, header_info, repaired_payload)) {
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
    if (!validator.parse(corrupted.data, (usize)encoded_bit_count)) {
        return false;
    }
    if (!validator.has_payload || validator.payload.size != original_payload.size) {
        return false;
    }
    for (usize i = 0u; i < original_payload.size; ++i) {
        if (validator.payload.data[i] != original_payload.data[i]) {
            return false;
        }
    }
    return true;
}



static int command_test(int arg_count, char** args) {
    ImageMappingConfig mapping;
    PageFooterConfig footer_config;
    double ecc_redundancy = 0.0;
    for (int i = 0; i < arg_count; ++i) {
        const char* arg = args[i];
        if (!arg) {
            continue;
        }
        bool handled = false;
        if (!process_image_mapping_option(arg, mapping, "test", &handled)) {
            return 1;
        }
        if (!handled) {
            const char ecc_prefix[] = "--ecc=";
            if (ascii_starts_with(arg, ecc_prefix)) {
                const char* value_text = arg + (sizeof(ecc_prefix) - 1u);
                usize length = ascii_length(value_text);
                if (length == 0u) {
                    console_line(2, "test: --ecc requires a numeric value");
                    return 1;
                }
                double redundancy_value = 0.0;
                if (!ascii_to_double(value_text, length, &redundancy_value)) {
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
            console_write(2, "test: unknown option: ");
            console_line(2, arg);
            return 1;
        }
    }
    if (!mapping.page_width_set) {
        mapping.page_width_pixels = 64u;
        mapping.page_width_set = true;
    }
    if (!mapping.page_height_set) {
        mapping.page_height_pixels = 64u;
        mapping.page_height_set = true;
    }
    static const u8 color_options[3] = {1u, 2u, 3u};
    int total_runs = 0;
    for (usize color_index = 0; color_index < 3u; ++color_index) {
        ImageMappingConfig run_mapping = mapping;
        if (run_mapping.color_set) {
            if (run_mapping.color_channels != color_options[color_index]) {
                continue;
            }
        } else {
            run_mapping.color_channels = color_options[color_index];
        }
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
        usize max_payload_size = (usize)((bits_per_page * 2u) / 8u) + 1024u;
        if (max_payload_size < 32u) {
            max_payload_size = 32u;
        }
        if (max_payload_size > (1u << 22u)) {
            max_payload_size = (1u << 22u);
        }
        usize low_size = 0u;
        usize high_size = 1u;
        u64 high_bits = 0u;
        while (true) {
            u64 seed = ((u64)run_mapping.color_channels << 32u) | (u64)high_size;
            if (!compute_frame_bit_count(run_mapping, high_size, seed, ecc_redundancy, high_bits)) {
                console_line(2, "test: failed to evaluate payload size");
                return 1;
            }
            if (high_bits > bits_per_page) {
                break;
            }
            low_size = high_size;
            if (high_size >= max_payload_size) {
                console_line(2, "test: unable to construct two-page payload");
                return 1;
            }
            high_size *= 2u;
            if (high_size > max_payload_size) {
                high_size = max_payload_size;
            }
            if (high_size == low_size) {
                ++high_size;
            }
        }
        usize left = (low_size == 0u) ? 1u : (low_size + 1u);
        usize right = high_size;
        usize best_size = high_size;
        u64 best_bits = high_bits;
        while (left <= right) {
            usize mid = left + (right - left) / 2u;
            u64 seed = ((u64)run_mapping.color_channels << 32u) | (u64)mid;
            u64 mid_bits = 0u;
            if (!compute_frame_bit_count(run_mapping, mid, seed, ecc_redundancy, mid_bits)) {
                console_line(2, "test: failed to evaluate payload size");
                return 1;
            }
            if (mid_bits > bits_per_page) {
                best_size = mid;
                best_bits = mid_bits;
                if (mid == 0u) {
                    break;
                }
                if (mid == left) {
                    break;
                }
                right = mid - 1u;
            } else {
                left = mid + 1u;
            }
        }
        if (best_bits <= bits_per_page || best_bits > bits_per_page * 2u) {
            console_line(2, "test: could not find payload yielding exactly two pages");
            return 1;
        }
        makocode::ByteBuffer payload;
        makocode::EncoderContext encoder;
        encoder.config.ecc_redundancy = ecc_redundancy;
        makocode::ByteBuffer frame_bits;
        u64 frame_bit_count = 0u;
        u64 payload_bit_count = 0u;
        u64 final_seed = ((u64)run_mapping.color_channels << 32u) | (u64)best_size;
        if (!build_payload_frame(run_mapping, best_size, final_seed, payload, encoder, frame_bits, frame_bit_count, payload_bit_count)) {
            console_line(2, "test: failed to build payload frame");
            return 1;
        }
        u64 page_count = (frame_bit_count + bits_per_page - 1u) / bits_per_page;
        if (page_count != 2u) {
            console_line(2, "test: unexpected page count");
            return 1;
        }
        makocode::BitWriter aggregate_writer;
        aggregate_writer.reset();
        PpmParserState aggregate_state;
        bool aggregate_initialized = false;
        makocode::ByteBuffer name_buffer;
        const makocode::EccSummary* ecc_summary = &encoder.ecc_info();
        for (u64 page = 0u; page < page_count; ++page) {
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
            if (!append_bits_from_buffer(aggregate_writer, page_bits_buffer.data, page_bit_count)) {
                console_line(2, "test: failed to combine page bits");
                return 1;
            }
            name_buffer.release();
            char digits_color[8];
            u64_to_ascii((u64)run_mapping.color_channels, digits_color, sizeof(digits_color));
            if (!name_buffer.append_ascii("encoded_c") ||
                !name_buffer.append_ascii(digits_color) ||
                !name_buffer.append_ascii("_p") ||
                !buffer_append_zero_padded(name_buffer, page + 1u, 2u) ||
                !name_buffer.append_ascii(".ppm") ||
                !name_buffer.append_char('\0')) {
                console_line(2, "test: failed to build encoded filename");
                return 1;
            }
            if (!write_buffer_to_file((const char*)name_buffer.data, page_output)) {
                console_line(2, "test: failed to write encoded page");
                return 1;
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
        if (!decoder.parse(roundtrip_bits.data, roundtrip_count)) {
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
                                            final_seed ^ (u64)page_count)) {
            return 1;
        }
        char digits_color[8];
        u64_to_ascii((u64)run_mapping.color_channels, digits_color, sizeof(digits_color));
        name_buffer.release();
        if (!name_buffer.append_ascii("payload_c") ||
            !name_buffer.append_ascii(digits_color) ||
            !name_buffer.append_ascii(".bin") ||
            !name_buffer.append_char('\0')) {
            console_line(2, "test: failed to build payload filename");
            return 1;
        }
        if (!write_buffer_to_file((const char*)name_buffer.data, payload)) {
            console_line(2, "test: failed to write payload file");
            return 1;
        }
        name_buffer.release();
        if (!name_buffer.append_ascii("decoded_c") ||
            !name_buffer.append_ascii(digits_color) ||
            !name_buffer.append_ascii(".bin") ||
            !name_buffer.append_char('\0')) {
            console_line(2, "test: failed to build decoded filename");
            return 1;
        }
        if (!write_buffer_to_file((const char*)name_buffer.data, decoder.payload)) {
            console_line(2, "test: failed to write decoded payload");
            return 1;
        }
        ++total_runs;
        if (mapping.color_set) {
            break;
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
    if (ascii_compare(argv[1], "test") == 0) {
        return command_test(argc - 2, argv + 2);
    }
    write_usage();
    return 0;
}
