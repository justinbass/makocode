/*
 * Shared MakoCode declarations.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#ifndef MAKOCODE_COMMON_HPP
#define MAKOCODE_COMMON_HPP
#include <stddef.h>
#include <string.h>
#include <math.h>
#include "makocode_platform.hpp"
#define LZMA_PROPS_SIZE 5
#define FOOTER_GLYPH_BLANK_ROW "00000"
#define FOOTER_GLYPH_SIMPLE(symbol, r0, r1, r2, r3, r4, r5, r6) {symbol, {r0, r1, r2, r3, r4, r5, r6, "00000"}}
#define FOOTER_GLYPH_CUSTOM(symbol, r0, r1, r2, r3, r4, r5, r6, r7) {symbol, {r0, r1, r2, r3, r4, r5, r6, r7}}


typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long       usize;
typedef unsigned long long  u64;
typedef long long           i64;
static const usize USIZE_MAX_VALUE = (usize)~(usize)0;

static const u64 U64_MAX_VALUE = (u64)~(u64)0;

namespace makocode {
namespace image {
enum LoadStatus {
    LoadSuccess = 0,
    LoadFileOpenFailed,
    LoadDecodeError,
    LoadUnsupportedFormat,
    LoadOutOfMemory
};
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
struct ImageBuffer {
    unsigned width;
    unsigned height;
    unsigned char* pixels;
};
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
struct Histogram {
    unsigned long long bins[256];
    unsigned char average;
};
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
struct CutLevels {
    unsigned char global_cut;
    unsigned char fill_cut;
};
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
struct LoadDiagnostics {
    char message[128];
};
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
static const int kMaxIterations = 32;
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
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
} // namespace image

} // namespace makocode

namespace makocode {
namespace image {
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
} // namespace image

} // namespace makocode

usize ascii_length(const char* text);

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
            makocode_platform_free(data);
            data = 0;
        }
        size = 0;
        capacity = 0;
    }

    bool reserve(usize new_capacity) {
        if (new_capacity > (USIZE_MAX_VALUE / 2u)) {
            return false;
        }
        u8* new_data = (u8*)makocode_platform_malloc(new_capacity);
        if (!new_data) {
            return false;
        }
        for (usize i = 0; i < size; ++i) {
            new_data[i] = data[i];
        }
        makocode_platform_free(data);
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
                return false;
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
        return append_bytes((const u8*)text, ascii_length(text));
    }

    bool append_char(char c) {
        return append_bytes((const u8*)&c, 1u);
    }
};

struct SecretBuffer : ByteBuffer {
    ~SecretBuffer() {
        volatile u8* clear = data;
        for (usize i = 0u; i < capacity; ++i) clear[i] = 0u;
    }
};
} // namespace makocode

namespace makocode {
struct FloodQueue {
    usize* data;
    usize head;
    usize tail;

    FloodQueue() : data(0), head(0), tail(0) {}

    bool initialize(usize max_elements) {
        data = (usize*)makocode_platform_malloc(max_elements * sizeof(usize));
        if (!data) {
            head = tail = 0;
            return false;
        }
        head = tail = 0;
        return true;
    }

    void release() {
        if (data) {
            makocode_platform_free(data);
            data = 0;
        }
        head = tail = 0;
    }

    void reset() {
        head = tail = 0;
    }

    bool push(usize value) {
        usize next = head + 1u;
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
        return true;
    }
};
} // namespace makocode

namespace makocode {
static const usize ENCRYPTION_SALT_BYTES = (usize)16;
} // namespace makocode

namespace makocode {
static const usize ENCRYPTION_NONCE_BYTES = (usize)12;
} // namespace makocode

namespace makocode {
static const usize ENCRYPTION_TAG_BYTES = (usize)16;
} // namespace makocode

namespace makocode {
static const u32 ENCRYPTION_PBKDF2_ITERATIONS = 60000u;
} // namespace makocode

namespace makocode {
static const u32 SHA256_INITIAL_STATE[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};
} // namespace makocode

namespace makocode {
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
} // namespace makocode

namespace makocode {
struct Sha256State {
    u32 h[8];
    u8 buffer[64];
    u64 bit_length;
    u32 buffer_used;

    Sha256State() : h(), buffer(), bit_length(0u), buffer_used(0u) {}
};
} // namespace makocode

namespace makocode {
typedef unsigned __int128 u128;
} // namespace makocode

namespace makocode {
static const u128 PCG64_MULTIPLIER = (((u128)0x2360ED051FC65DA4ull) << 64u) | (u128)0x4385DF649FCCF645ull;
} // namespace makocode

namespace makocode {
static const u128 PCG64_INCREMENT = (((u128)0x5851F42D4C957F2Dull) << 64u) | (u128)0x14057B7EF767814Full;
} // namespace makocode

namespace makocode {
struct Pcg64Generator {
    u128 state;
    Pcg64Generator() : state(0u) {}
    void seed(u64 seed_value) { state = 0u; (void)next(); state += (u128)seed_value; (void)next(); }
    u64 next() {
        u128 oldstate = state;
        state = oldstate * PCG64_MULTIPLIER + PCG64_INCREMENT;
        u64 xorshifted = (u64)(((oldstate >> 64u) ^ oldstate) >> 64u);
        u32 rotate = (u32)(oldstate >> 122u) & 63u;
        return (xorshifted >> rotate) | (xorshifted << ((64u - rotate) & 63u));
    }
};
} // namespace makocode

namespace makocode {
struct Poly1305State {
    u64 r0, r1, r2, r3, r4;
    u64 s1, s2, s3, s4;
    u64 h0, h1, h2, h3, h4;

    Poly1305State()
        : r0(0u), r1(0u), r2(0u), r3(0u), r4(0u),
          s1(0u), s2(0u), s3(0u), s4(0u),
          h0(0u), h1(0u), h2(0u), h3(0u), h4(0u) {}
};
} // namespace makocode

namespace makocode {
static const usize STEALTH_CONTAINER_PREFIX_BYTES =
    ENCRYPTION_SALT_BYTES + ENCRYPTION_NONCE_BYTES;
} // namespace makocode

namespace makocode {
static const usize STEALTH_VISIBLE_DESCRIPTOR_PLAINTEXT_BYTES = 16u;
} // namespace makocode

namespace makocode {
static const usize STEALTH_VISIBLE_DESCRIPTOR_BYTES =
    STEALTH_VISIBLE_DESCRIPTOR_PLAINTEXT_BYTES + ENCRYPTION_TAG_BYTES;
} // namespace makocode

namespace makocode {
static const usize STEALTH_HIDDEN_DESCRIPTOR_PLAINTEXT_BYTES = 32u;
} // namespace makocode

namespace makocode {
static const usize STEALTH_HIDDEN_DESCRIPTOR_BYTES =
    STEALTH_HIDDEN_DESCRIPTOR_PLAINTEXT_BYTES + ENCRYPTION_TAG_BYTES;
} // namespace makocode

namespace makocode {
static const usize STEALTH_CONTAINER_HEADER_BYTES = STEALTH_CONTAINER_PREFIX_BYTES +
                                                    STEALTH_VISIBLE_DESCRIPTOR_BYTES +
                                                    STEALTH_HIDDEN_DESCRIPTOR_BYTES;
} // namespace makocode

namespace makocode {
struct BitWriter {
    ByteBuffer buffer;
    usize bit_position;
    bool  failed;

    BitWriter() : buffer(), bit_position(0u), failed(false) {}

    ~BitWriter() {
        reset();
    }

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
            buffer.push(0u);
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
} // namespace makocode

namespace makocode {
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
} // namespace makocode

namespace makocode {
static const usize LZMA_HEADER_BYTES = (usize)LZMA_PROPS_SIZE + 8u;
} // namespace makocode

namespace makocode {
static const u8 LZMA_FIXED_PROPERTIES[LZMA_PROPS_SIZE] = {93u, 0u, 0u, 0u, 8u};
} // namespace makocode

namespace makocode {
static const u64 LZMA_SIZE_FROM_HEADER = U64_MAX_VALUE;
} // namespace makocode

namespace makocode {
static const u16 RS_FIELD_PRIMITIVE = 0x11d;
} // namespace makocode

namespace makocode {
static const u16 RS_FIELD_SIZE = 255u;
} // namespace makocode

namespace makocode {
static const usize RS_POLY_CAPACITY = 256u;
} // namespace makocode

namespace makocode {
class PaletteSymbolPasswordContext;
struct EncoderConfig {
    u32 metadata_key_count;
    u32 fiducial_density;
    double ecc_redundancy;
    u16 overlay_erasure_budget;
    u16 unknown_error_budget;
    u32 palette_size;
    u32 parity_step;
    bool omit_palette_symbol_bootstrap;
    const char* password;
    usize password_length;
    const PaletteSymbolPasswordContext* password_context;
    u32 max_parallelism;

    EncoderConfig()
        : metadata_key_count(0u),
          fiducial_density(0u),
          ecc_redundancy(0.0),
          overlay_erasure_budget(0u),
          unknown_error_budget(0u),
          palette_size(0u),
          parity_step(4u),
          omit_palette_symbol_bootstrap(false),
          password(0),
          password_length(0u),
          password_context(0),
          max_parallelism(1u) {}
};
} // namespace makocode

namespace makocode {
struct EccSummary {
    bool enabled;
    u16 block_data_symbols;
    u16 parity_symbols;
    double redundancy;
    u64 original_bytes;
    u64 block_count;
    u16 overlay_erasure_budget;
    u16 unknown_error_budget;
    bool palette_symbol_stream;
    u32 metadata_ecc_minimum_parity_bytes;

    EccSummary()
        : enabled(false),
          block_data_symbols(0u),
          parity_symbols(0u),
          redundancy(0.0),
          original_bytes(0u),
          block_count(0u),
          overlay_erasure_budget(0u),
          unknown_error_budget(0u),
          palette_symbol_stream(false),
          metadata_ecc_minimum_parity_bytes(2u) {}
};
} // namespace makocode

namespace makocode {
struct EccDecodeStats {
    u64 corrected_symbols;
    u64 total_parity_symbols;
    u64 total_blocks;
    u64 blocks_with_errors;
    u64 unrecoverable_blocks;
    u32 failed_block_errors;
    u32 failed_block_erasures;
    u32 failed_block_stage;
    u64 failed_block_index;

    EccDecodeStats()
        : corrected_symbols(0u),
          total_parity_symbols(0u),
          total_blocks(0u),
          blocks_with_errors(0u),
          unrecoverable_blocks(0u),
          failed_block_errors(0u),
          failed_block_erasures(0u),
          failed_block_stage(0u),
          failed_block_index(0u) {}
};
} // namespace makocode

namespace makocode {
struct PaletteSymbolProfile {
    u32 palette_size;
    u64 field_order;
    u32 field_characteristic;
    u32 field_degree;
    u32 palette_digit_degree;
    u32 field_value_mask;
    u32 field_reduction;
    u32 block_length;
    u32 symbol_bits;
    u8 field_polynomial[32];

    PaletteSymbolProfile()
        : palette_size(0u), field_order(0u), field_characteristic(0u),
          field_degree(0u), palette_digit_degree(0u), field_value_mask(0u),
          field_reduction(0u), block_length(0u), symbol_bits(0u),
          field_polynomial{} {}
};

static const u32 PALETTE_SYMBOL_MAX_BLOCK_LENGTH = 1024u;

class PaletteSymbolPasswordContext {
public:
    PaletteSymbolPasswordContext();
    ~PaletteSymbolPasswordContext();
    PaletteSymbolPasswordContext(const PaletteSymbolPasswordContext&) = delete;
    PaletteSymbolPasswordContext& operator=(const PaletteSymbolPasswordContext&) = delete;

    bool derive(const char* password,
                usize password_length,
                const u8 salt[ENCRYPTION_SALT_BYTES] = 0);
    bool derive_salted(const PaletteSymbolPasswordContext& locator_context,
                       const u8 salt[ENCRYPTION_SALT_BYTES]);
    const u8* shuffle_key() const;
    bool mask(const PaletteSymbolProfile& profile,
              ByteBuffer& symbols,
              bool unmask,
              u64 first_symbol_index = 0u) const;
    bool available() const;

private:
    u8 key_[32];
    bool available_;
};

struct PaletteSymbolHeaderRecovery {
    bool available;
    bool password_protected;
    u64 byte_count;
    u64 block_count;
    u16 data_symbols_per_block;
    u16 parity_symbols;
    u16 overlay_erasure_budget;
    u16 unknown_error_budget;

    PaletteSymbolHeaderRecovery()
        : available(false), password_protected(false), byte_count(0u), block_count(0u),
          data_symbols_per_block(0u), parity_symbols(0u),
          overlay_erasure_budget(0u), unknown_error_budget(0u) {}
};

static const u64 PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED = 0x4D616B6F76300005ull;
static const u32 PALETTE_SYMBOL_BOOTSTRAP_SYMBOLS = 48u;
static const u32 PALETTE_SYMBOL_BOOTSTRAP_DATA_SYMBOLS = 32u;
static const u32 PALETTE_SYMBOL_PAGE_SALT_SYMBOLS = 48u;
static const u32 PALETTE_SYMBOL_PAGE_SALT_DATA_SYMBOLS = 16u;
static const usize PALETTE_SYMBOL_PAGE_SALT_BYTES = 16u;

struct PaletteSymbolPageSalt {
    bool available;
    u8 salt[PALETTE_SYMBOL_PAGE_SALT_BYTES];
    u32 page_index;
    u32 page_count;

    PaletteSymbolPageSalt()
        : available(false), salt{}, page_index(0u), page_count(0u) {}
};

bool palette_symbol_profile(u32 palette_size, PaletteSymbolProfile& profile);
bool palette_symbol_encode_page_salt(const PaletteSymbolProfile& profile,
                                     const u8 salt[PALETTE_SYMBOL_PAGE_SALT_BYTES],
                                     u64 page_index,
                                     u64 page_count,
                                     ByteBuffer& encoded_symbols);
bool palette_symbol_decode_page_salt(const PaletteSymbolProfile& profile,
                                     const ByteBuffer& encoded_symbols,
                                     const ByteBuffer& erased_symbols,
                                     u64 expected_page_index,
                                     u64 expected_page_count,
                                     PaletteSymbolPageSalt& page_salt,
                                     EccDecodeStats* stats = 0);
bool palette_symbol_parity_for_redundancy(const PaletteSymbolProfile& profile,
                                          double redundancy,
                                          u32& parity_symbols,
                                          u32 parity_step = 4u);
bool palette_symbol_to_dots(const PaletteSymbolProfile& profile,
                            u32 value,
                            u8 dots[8]);
bool palette_dots_to_symbol(const PaletteSymbolProfile& profile,
                            const u8 dots[8],
                            u32& value);
bool palette_bytes_to_symbols(const PaletteSymbolProfile& profile,
                              const u8* bytes,
                              usize byte_count,
                              ByteBuffer& symbols);
bool palette_symbols_to_bytes(const PaletteSymbolProfile& profile,
                              const ByteBuffer& symbols,
                              usize byte_count,
                              ByteBuffer& bytes);
u64 palette_symbol_count_for_bytes(const PaletteSymbolProfile& profile,
                                   usize byte_count);
bool palette_rs_encode(const PaletteSymbolProfile& profile,
                       const ByteBuffer& data_symbols,
                       u32 block_length,
                       u32 data_symbols_per_block,
                       ByteBuffer& code_symbols);
bool palette_rs_decode(const PaletteSymbolProfile& profile,
                       const ByteBuffer& code_symbols,
                       u32 block_length,
                       u32 data_symbols_per_block,
                       const ByteBuffer& erased_symbols,
                       ByteBuffer& data_symbols,
                       EccDecodeStats* stats = 0);
bool palette_symbol_password_mask(const PaletteSymbolProfile& profile,
                                  ByteBuffer& symbols,
                                  const char* password,
                                  usize password_length,
                                  bool unmask,
                                  u64 first_symbol_index = 0u);
bool palette_symbol_selector_mask(u64 seed,
                                 u64 block_index,
                                 u32 block_length,
                                 u32 selected_count,
                                 u8* mask_out,
                                 const u8* password_shuffle_key = 0);
u64 palette_symbol_interleaved_offset(u64 block_index,
                                      u32 position,
                                      u64 block_count,
                                      u32 final_block_length);
bool palette_symbol_encode_stream(const PaletteSymbolProfile& profile,
                                  const u8* bytes,
                                  usize byte_count,
                                  double redundancy,
                                  u16 overlay_erasure_budget,
                                  u16 unknown_error_budget,
                                  const char* password,
                                  usize password_length,
                                  ByteBuffer& encoded_symbols,
                                  EccSummary& summary,
                                  bool omit_bootstrap = false,
                                  u32 parity_step = 4u,
                                  const PaletteSymbolPasswordContext* password_context = 0);
bool palette_symbol_decode_stream(const PaletteSymbolProfile& profile,
                                  const ByteBuffer& encoded_symbols,
                                  const ByteBuffer& erased_symbols,
                                  const char* password,
                                  usize password_length,
                                  ByteBuffer& bytes,
                                  EccDecodeStats* stats,
                                  bool* password_protected = 0,
                                  const PaletteSymbolHeaderRecovery* header_recovery = 0,
                                  bool allow_partial = false,
                                  bool bootstrap_omitted = false,
                                  const PaletteSymbolPasswordContext* password_context = 0);
} // namespace makocode

namespace makocode {
bool lzma_compress(const u8* input,
                          usize length,
                          ByteBuffer& output,
                          bool include_uncompressed_size = true);
} // namespace makocode

namespace makocode {
struct EncoderContext {
    EncoderConfig config;
    ByteBuffer payload_bytes;
    BitWriter bit_writer;
    bool configured;
    bool payload_is_preencoded;

    EccSummary ecc_summary;

    EncoderContext()
        : config(),
          payload_bytes(),
          bit_writer(),
          configured(false),
          payload_is_preencoded(false),
          ecc_summary() {}

    ~EncoderContext() {
        reset();
    }

    void reset() {
        payload_bytes.release();
        bit_writer.reset();
        configured = false;
        payload_is_preencoded = false;
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
        return lzma_compress(payload_bytes.data, payload_bytes.size, output, true);
    }

    bool build() {
        bit_writer.reset();
        ecc_summary = EccSummary();
        if (config.palette_size < 2u) return false;
        ecc_summary.overlay_erasure_budget = config.overlay_erasure_budget;
        ByteBuffer compressed;
        if (!payload_is_preencoded) {
            if (!encode_payload(compressed)) {
                return false;
            }
        }
        const ByteBuffer* payload_source = payload_is_preencoded ? &payload_bytes : &compressed;
        double redundancy = (config.ecc_redundancy > 0.0) ? config.ecc_redundancy : 0.0;
        PaletteSymbolProfile profile;
        ByteBuffer symbols;
        if (!palette_symbol_profile(config.palette_size, profile) ||
            !palette_symbol_encode_stream(profile,
                                          payload_source->data,
                                          payload_source->size,
                                          redundancy,
                                          config.overlay_erasure_budget,
                                          config.unknown_error_budget,
                                          config.password,
                                          config.password_length,
                                          symbols,
                                          ecc_summary,
                                          config.omit_palette_symbol_bootstrap,
                                          config.parity_step,
                                          config.password_context)) {
            return false;
        }
        for (usize symbol_index = 0u; symbol_index < symbols.size / 4u; ++symbol_index) {
            const u8* value = symbols.data + symbol_index * 4u;
            u32 symbol = (u32)value[0] | ((u32)value[1] << 8u) |
                         ((u32)value[2] << 16u) | ((u32)value[3] << 24u);
            if (!bit_writer.write_bits((u64)symbol, 32u)) return false;
        }
        bit_writer.align_to_byte();
        configured = true;
        return true;
    }

    const EccSummary& ecc_info() const {
        return ecc_summary;
    }
};
} // namespace makocode

u16 read_le_u16(const u8* ptr);

u64 read_le_u64(const u8* ptr);

void write_le_u16(u8* ptr, u16 value);

void write_le_u64(u8* ptr, u64 value);

namespace makocode {
struct ReedSolomonTables {
    bool initialized;
    u8 exp_table[512];
    u8 log_table[256];
    u8 product[256][256];
};
} // namespace makocode

namespace makocode {
struct EncodedPayloadMetadata {
    bool encryption_present;
    u8 encryption_salt[ENCRYPTION_SALT_BYTES];
    u8 encryption_nonce[ENCRYPTION_NONCE_BYTES];
    u8 encryption_tag[ENCRYPTION_TAG_BYTES];
    bool lzma_present;
    u8 lzma_header[LZMA_HEADER_BYTES];

    EncodedPayloadMetadata()
        : encryption_present(false), encryption_salt(), encryption_nonce(),
          encryption_tag(), lzma_present(false), lzma_header() {}
};
} // namespace makocode

namespace makocode {
bool decode_stealth_container(const u8* data,
                                    usize data_length,
                                    const char* password,
                                    usize password_length,
                                    ByteBuffer& archive_out,
                                    bool& is_container,
                                    usize* container_bytes_out = 0,
                                    usize* authenticated_body_end_out = 0,
                                    bool decompress_archive = true);
} // namespace makocode

namespace makocode {
bool lzma_decompress(const u8* input,
                            usize bit_count,
                            ByteBuffer& output,
                            u64 external_uncompressed_size = LZMA_SIZE_FROM_HEADER,
                            bool properties_embedded = true,
                            usize* consumed_bytes_out = 0,
                            bool allow_partial = false);
} // namespace makocode

namespace makocode {
struct DecoderContext {
    ByteBuffer payload;
    bool has_payload;
    bool ecc_failed;
    bool partial_payload;
    bool password_attempted;
    bool password_failed;
    bool password_not_encrypted;
    EccDecodeStats ecc_stats;
    EncodedPayloadMetadata encoded_metadata;

    DecoderContext()
        : payload(),
          has_payload(false),
          ecc_failed(false),
          partial_payload(false),
          password_attempted(false),
          password_failed(false),
          password_not_encrypted(false),
          ecc_stats() {}

    ~DecoderContext() {
        reset();
    }

    void reset() {
        payload.release();
        has_payload = false;
        ecc_failed = false;
        partial_payload = false;
        password_attempted = false;
        password_failed = false;
        password_not_encrypted = false;
        ecc_stats = EccDecodeStats();
        encoded_metadata = EncodedPayloadMetadata();
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

    const EccDecodeStats& ecc_statistics() const {
        return ecc_stats;
    }
};
} // namespace makocode

struct PaletteColor {
    u8 r;
    u8 g;
    u8 b;
};

static const u32 MAX_CUSTOM_PALETTE_COLORS = 16u;

static const usize MAX_CUSTOM_PALETTE_TEXT = 256u;

static const u8 CUSTOM_PALETTE_SYNTHETIC_COLOR_MODE = 3u;

struct ImageMappingConfig {
    u8  color_channels;
    u32 page_width_pixels;
    bool page_width_set;
    u32 page_height_pixels;
    bool page_height_set;
    char palette_text[MAX_CUSTOM_PALETTE_TEXT];
    usize palette_text_length;
    bool palette_set;
    PaletteColor custom_palette[MAX_CUSTOM_PALETTE_COLORS];
    u8 custom_palette_ids[MAX_CUSTOM_PALETTE_COLORS];
    u32 custom_palette_count;
    u32 custom_palette_base;
    bool custom_palette_valid;
    u64 symbol_placement_seed;
    const makocode::PaletteSymbolPasswordContext* symbol_placement_password_context;
    const makocode::PaletteSymbolPasswordContext* symbol_payload_password_context;
    const u8* symbol_placement_salt;
    bool scatter_payload;
    bool recover_palette;
    const char* recover_limit_text;
    usize recover_limit_length;

    ImageMappingConfig()
        : color_channels(1u),
          page_width_pixels(0u),
          page_width_set(false),
          page_height_pixels(0u),
          page_height_set(false),
          palette_text_length(0u),
          palette_set(false),
          custom_palette_count(0u),
          custom_palette_base(0u),
          custom_palette_valid(false),
          symbol_placement_seed(makocode::PALETTE_SYMBOL_DEFAULT_PLACEMENT_SEED),
          symbol_placement_password_context(0),
          symbol_payload_password_context(0),
          symbol_placement_salt(0),
          scatter_payload(false),
          recover_palette(false),
          recover_limit_text("4"),
          recover_limit_length(1u) {
        palette_text[0] = '\0';
        for (u32 i = 0u; i < MAX_CUSTOM_PALETTE_COLORS; ++i) {
            custom_palette[i].r = 0u;
            custom_palette[i].g = 0u;
            custom_palette[i].b = 0u;
            custom_palette_ids[i] = 0u;
        }
    }
};

static const u32 FOOTER_BASE_GLYPH_WIDTH  = 5u;

static const u32 FOOTER_BASE_GLYPH_HEIGHT = 8u;

struct PageFooterConfig {
    const char* text;
    usize text_length;
    const char* pages;
    usize pages_length;
    bool over_barcode;
    u32 font_size;
    usize max_text_length;
    PageFooterConfig()
        : text(0),
          text_length(0u),
          pages(0),
          pages_length(0u),
          over_barcode(false),
          font_size(1u),
          max_text_length(0u) {}
};

struct FiducialGridDefaults {
    u32 spacing_pixels = 24u;
};

static const u32 FIDUCIAL_MARKER_SIZE_PIXELS = 2u;

static const u32 CURRENT_TILE_SCHEMA_VERSION = 0u;

static const u8 OVERWRITE_MASK_PIXEL = 1u;

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
    u32 chars_per_line;
    u32 line_count;
    bool over_barcode;

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
          text_pixel_width(0u),
          chars_per_line(0u),
          line_count(0u),
          over_barcode(false) {}
};

struct AffineTransform {
    // 2x2 matrix (row-major) and translation
    double a00, a01, a10, a11, tx, ty;

    AffineTransform()
        : a00(1.0), a01(0.0), a10(0.0), a11(1.0), tx(0.0), ty(0.0) {}
};

namespace MetadataTile {
using namespace makocode;
static thread_local const PaletteColor* g_decode_palette_override = 0;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_SIDE = 48u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_TAG_BITS = MAX_CUSTOM_PALETTE_COLORS + 1u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_MARKER_WIDTH = 8u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_TAG_ROWS = 5u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_HEADER_ROWS = 6u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_LENGTH_ROW = 5u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_MAX_METADATA_SQUARE_SIDE = TILE_SIDE - TILE_HEADER_ROWS;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const usize TILE_PAYLOAD_MODULE_CAPACITY =
        (usize)TILE_MAX_METADATA_SQUARE_SIDE * (usize)TILE_MAX_METADATA_SQUARE_SIDE;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const usize TILE_MAX_ACTIVE_CELLS = TILE_PAYLOAD_MODULE_CAPACITY +
                                               (usize)TILE_TAG_BITS + 32u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 TILE_SCHEMA_VERSION = CURRENT_TILE_SCHEMA_VERSION;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
static const u32 ASCII_LOW_NIBBLE_MARKER = 0xD1BF3F45u;
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
struct Values {
        u32 schema_version;
        u64 encoded_tag;
        u32 encoded_metadata_size;
        u32 metadata_bit_count;
        u32 metadata_ecc_bytes;
        double metadata_ecc_redundancy;
        u32 metadata_ecc_minimum_parity_bytes;
        u32 metadata_length_prefix_bits;
        u32 metadata_outer_length_bits;
        u32 metadata_symbol_count;
        u32 metadata_square_side;
        u32 metadata_square_x0;
        u32 metadata_square_y0;
        u32 metadata_tag_y0;
        u32 metadata_tile_width;
        u32 metadata_tile_height;
        u8 encoded_metadata[4096];
        u32 decoded_metadata_size;
        u8 decoded_metadata[4096];
        u64 page_bits;
        u64 payload_bytes;
        u64 page_count;
        u64 page_index;
        u32 page_width_pixels;
        u32 page_height_pixels;
        u32 footer_rows;
        u32 fiducial_spacing_pixels;
        bool ecc_enabled;
        u16 ecc_block_data;
        u16 ecc_parity;
        u64 ecc_block_count;
        u64 ecc_original_bytes;
        u32 fiducial_columns;
        u32 fiducial_rows;
        bool overwrite_layout;
        bool overlay_selector_present;
        u64 overlay_selector_seed;
        u16 unknown_parity_budget;
        u16 overlay_erasure_budget;
        bool palette_background_first;
        bool palette_is_custom;
        bool scatter_payload;
        u32 palette_count;
        PaletteColor palette[MAX_CUSTOM_PALETTE_COLORS];

        Values()
            : schema_version(TILE_SCHEMA_VERSION),
              encoded_tag(0u),
              encoded_metadata_size(0u),
              metadata_bit_count(0u),
              metadata_ecc_bytes(0u),
              metadata_ecc_redundancy(0.0),
              metadata_ecc_minimum_parity_bytes(2u),
              metadata_length_prefix_bits(0u),
              metadata_outer_length_bits(0u),
              metadata_symbol_count(0u),
              metadata_square_side(0u),
              metadata_square_x0(0u),
              metadata_square_y0(0u),
              metadata_tag_y0(0u),
              metadata_tile_width(0u),
              metadata_tile_height(0u),
              encoded_metadata(),
              decoded_metadata_size(0u),
              decoded_metadata(),
              page_bits(0u),
              payload_bytes(0u),
              page_count(0u),
              page_index(0u),
              page_width_pixels(0u),
              page_height_pixels(0u),
              footer_rows(0u),
              fiducial_spacing_pixels(24u),
              ecc_enabled(false),
              ecc_block_data(0u),
              ecc_parity(0u),
              ecc_block_count(0u),
              ecc_original_bytes(0u),
              fiducial_columns(0u),
              fiducial_rows(0u),
              overwrite_layout(true),
              overlay_selector_present(false),
              overlay_selector_seed(0u),
              unknown_parity_budget(0u),
              overlay_erasure_budget(0u),
              palette_background_first(false),
              palette_is_custom(false),
              scatter_payload(false),
              palette_count(0u),
              palette() {}
    };
struct PayloadRankMapCache {
    bool configured;
    u32 width_pixels;
    u32 data_height_pixels;
    bool placement_valid;
    u32 placement_x0;
    u32 placement_y0;
    u32 placement_offset_x;
    u32 placement_offset_y;
    u32 metadata_length_prefix_bits;
    u32 metadata_symbol_count;
    u32 metadata_square_side;
    u32 metadata_square_x0;
    u32 metadata_square_y0;
    bool include_metadata_overwrites;
    u64 payload_pixel_count;
    ByteBuffer reserved_mask;
    ByteBuffer base_rank_map;
    ByteBuffer page_rank_map;
    ByteBuffer rank_to_pixel;

    PayloadRankMapCache()
        : configured(false),
          width_pixels(0u),
          data_height_pixels(0u),
          placement_valid(false),
          placement_x0(0u),
          placement_y0(0u),
          placement_offset_x(0u),
          placement_offset_y(0u),
          metadata_length_prefix_bits(0u),
          metadata_symbol_count(0u),
          metadata_square_side(0u),
          metadata_square_x0(0u),
          metadata_square_y0(0u),
          include_metadata_overwrites(false),
          payload_pixel_count(0u),
          reserved_mask(),
          base_rank_map(),
          page_rank_map(),
          rank_to_pixel() {}
};
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
struct Placement {
        bool valid;
        u32 x0;
        u32 y0;
        u32 offset_x;
        u32 offset_y;

        Placement() : valid(false), x0(0u), y0(0u), offset_x(0u), offset_y(0u) {}
    };
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
struct AffineParams {
        double center_x;
        double center_y;
        double pitch_pixels; // pixels per module
        double pitch_x_pixels;
        double pitch_y_pixels;
        double angle_rad;
    };
} // namespace MetadataTile

struct GlyphPattern {
    char symbol;
    const char* rows[FOOTER_BASE_GLYPH_HEIGHT];
};

static const GlyphPattern FOOTER_GLYPHS[] = {
    FOOTER_GLYPH_SIMPLE(' ', "00000", "00000", "00000", "00000", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('!', "00100", "00100", "00100", "00100", "00100", "00000", "00100"),
    FOOTER_GLYPH_SIMPLE('"', "01010", "01010", "00000", "00000", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('#', "01010", "01010", "11111", "01010", "11111", "01010", "01010"),
    FOOTER_GLYPH_SIMPLE('$', "00100", "01111", "10100", "01110", "00101", "11110", "00100"),
    FOOTER_GLYPH_SIMPLE('%', "11001", "11001", "00010", "00100", "01000", "10011", "10011"),
    FOOTER_GLYPH_SIMPLE('&', "01100", "10010", "10100", "01000", "10101", "10010", "01101"),
    FOOTER_GLYPH_SIMPLE('\'', "00100", "00100", "00000", "00000", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('(', "00010", "00100", "01000", "01000", "01000", "00100", "00010"),
    FOOTER_GLYPH_SIMPLE(')', "01000", "00100", "00010", "00010", "00010", "00100", "01000"),
    FOOTER_GLYPH_SIMPLE('*', "00000", "00100", "10101", "01110", "10101", "00100", "00000"),
    FOOTER_GLYPH_SIMPLE('+', "00000", "00100", "00100", "11111", "00100", "00100", "00000"),
    FOOTER_GLYPH_SIMPLE(',', "00000", "00000", "00000", "00000", "00100", "00100", "01000"),
    FOOTER_GLYPH_SIMPLE('-', "00000", "00000", "11111", "00000", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('.', "00000", "00000", "00000", "00000", "00000", "00100", "00000"),
    FOOTER_GLYPH_SIMPLE('/', "00001", "00010", "00100", "01000", "10000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('0', "01110", "10001", "11001", "10101", "10011", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('1', "00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    FOOTER_GLYPH_SIMPLE('2', "01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    FOOTER_GLYPH_SIMPLE('3', "01110", "10001", "00001", "00110", "00001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('4', "00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    FOOTER_GLYPH_SIMPLE('5', "11111", "10000", "11110", "00001", "00001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('6', "01110", "10000", "11110", "10001", "10001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('7', "11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    FOOTER_GLYPH_SIMPLE('8', "01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('9', "01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    FOOTER_GLYPH_SIMPLE(':', "00000", "00100", "00000", "00000", "00100", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE(';', "00000", "00100", "00000", "00000", "00100", "00100", "01000"),
    FOOTER_GLYPH_SIMPLE('<', "00010", "00100", "01000", "10000", "01000", "00100", "00010"),
    FOOTER_GLYPH_SIMPLE('=', "00000", "11111", "00000", "11111", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('>', "01000", "00100", "00010", "00001", "00010", "00100", "01000"),
    FOOTER_GLYPH_SIMPLE('?', "01110", "10001", "00010", "00100", "00100", "00000", "00100"),
    FOOTER_GLYPH_SIMPLE('@', "01110", "10001", "10111", "10101", "10111", "10000", "01110"),
    FOOTER_GLYPH_SIMPLE('[', "01110", "01000", "01000", "01000", "01000", "01000", "01110"),
    FOOTER_GLYPH_SIMPLE('\\', "10000", "01000", "00100", "00010", "00001", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE(']', "01110", "00010", "00010", "00010", "00010", "00010", "01110"),
    FOOTER_GLYPH_SIMPLE('^', "00100", "01010", "10001", "00000", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('_', "00000", "00000", "00000", "00000", "00000", "11111", "00000"),
    FOOTER_GLYPH_SIMPLE('`', "00100", "00010", "00000", "00000", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('{', "00011", "00100", "00100", "01000", "00100", "00100", "00011"),
    FOOTER_GLYPH_SIMPLE('|', "00100", "00100", "00100", "00100", "00100", "00100", "00100"),
    FOOTER_GLYPH_SIMPLE('}', "11000", "00100", "00100", "00010", "00100", "00100", "11000"),
    FOOTER_GLYPH_SIMPLE('~', "00000", "00000", "01001", "10110", "00000", "00000", "00000"),
    FOOTER_GLYPH_SIMPLE('A', "01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('B', "11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    FOOTER_GLYPH_SIMPLE('C', "01110", "10001", "10000", "10000", "10000", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('D', "11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    FOOTER_GLYPH_SIMPLE('E', "11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    FOOTER_GLYPH_SIMPLE('F', "11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    FOOTER_GLYPH_SIMPLE('G', "01110", "10001", "10000", "10000", "10011", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('H', "10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('I', "01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    FOOTER_GLYPH_SIMPLE('J', "00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    FOOTER_GLYPH_SIMPLE('K', "10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    FOOTER_GLYPH_SIMPLE('L', "10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    FOOTER_GLYPH_SIMPLE('M', "10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('N', "10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('O', "01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('P', "11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    FOOTER_GLYPH_SIMPLE('Q', "01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    FOOTER_GLYPH_SIMPLE('R', "11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    FOOTER_GLYPH_SIMPLE('S', "01110", "10001", "10000", "01110", "00001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('T', "11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    FOOTER_GLYPH_SIMPLE('U', "10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('V', "10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    FOOTER_GLYPH_SIMPLE('W', "10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    FOOTER_GLYPH_SIMPLE('X', "10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('Y', "10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    FOOTER_GLYPH_SIMPLE('Z', "11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    FOOTER_GLYPH_SIMPLE('a', "00000", "00000", "01110", "00001", "01111", "10001", "01111"),
    FOOTER_GLYPH_SIMPLE('b', "10000", "10000", "11110", "10001", "10001", "10001", "11110"),
    FOOTER_GLYPH_SIMPLE('c', "00000", "00000", "01110", "10001", "10000", "10001", "01110"),
    FOOTER_GLYPH_SIMPLE('d', "00001", "00001", "01111", "10001", "10001", "10001", "01111"),
    FOOTER_GLYPH_SIMPLE('e', "00000", "00000", "01110", "10001", "11111", "10000", "01110"),
    FOOTER_GLYPH_SIMPLE('f', "00110", "01001", "01000", "11110", "01000", "01000", "01000"),
    FOOTER_GLYPH_CUSTOM('g', "00000", "01110", "10001", "10001", "01111", "00001", "01110", "00000"),
    FOOTER_GLYPH_SIMPLE('h', "10000", "10000", "11110", "10001", "10001", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('i', "00100", "00000", "01100", "00100", "00100", "00100", "01110"),
    FOOTER_GLYPH_CUSTOM('j', "00010", "00000", "00110", "00010", "00010", "00010", "01100", "00000"),
    FOOTER_GLYPH_SIMPLE('k', "10000", "10000", "10010", "10100", "11000", "10100", "10010"),
    FOOTER_GLYPH_SIMPLE('l', "01100", "00100", "00100", "00100", "00100", "00100", "01110"),
    FOOTER_GLYPH_SIMPLE('m', "00000", "00000", "11010", "10101", "10101", "10101", "10101"),
    FOOTER_GLYPH_SIMPLE('n', "00000", "00000", "11110", "10001", "10001", "10001", "10001"),
    FOOTER_GLYPH_SIMPLE('o', "00000", "00000", "01110", "10001", "10001", "10001", "01110"),
    FOOTER_GLYPH_CUSTOM('p', "00000", "00000", "11110", "10001", "10001", "11110", "10000", "10000"),
    FOOTER_GLYPH_CUSTOM('q', "00000", "00000", "01110", "10001", "10001", "01111", "00001", "00001"),
    FOOTER_GLYPH_SIMPLE('r', "00000", "00000", "10110", "11001", "10000", "10000", "10000"),
    FOOTER_GLYPH_SIMPLE('s', "00000", "00000", "01110", "10000", "01110", "00001", "11110"),
    FOOTER_GLYPH_SIMPLE('t', "01000", "01000", "11110", "01000", "01000", "01001", "00110"),
    FOOTER_GLYPH_SIMPLE('u', "00000", "00000", "10001", "10001", "10001", "10011", "01101"),
    FOOTER_GLYPH_SIMPLE('v', "00000", "00000", "10001", "10001", "10001", "01010", "00100"),
    FOOTER_GLYPH_SIMPLE('w', "00000", "00000", "10001", "10001", "10101", "11011", "10001"),
    FOOTER_GLYPH_SIMPLE('x', "00000", "00000", "10001", "01010", "00100", "01010", "10001"),
    FOOTER_GLYPH_CUSTOM('y', "00000", "00000", "10001", "10001", "01010", "00100", "00100", "01000"),
    FOOTER_GLYPH_SIMPLE('z', "00000", "00000", "11111", "00010", "00100", "01000", "11111")
};

static const usize FOOTER_GLYPH_COUNT = (usize)(sizeof(FOOTER_GLYPHS) / sizeof(FOOTER_GLYPHS[0]));

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
    {255u, 0u, 0u},     // red
    {0u, 255u, 0u},     // green
    {0u, 0u, 255u},     // blue
    {0u, 255u, 255u},   // cyan
    {255u, 0u, 255u},   // magenta
    {255u, 255u, 0u},   // yellow
    {0u, 0u, 0u}        // black
};

enum NamedPaletteColorId {
    NAMED_COLOR_WHITE = 0u,
    NAMED_COLOR_CYAN = 1u,
    NAMED_COLOR_MAGENTA = 2u,
    NAMED_COLOR_YELLOW = 3u,
    NAMED_COLOR_BLACK = 4u,
    NAMED_COLOR_INVALID = 0xFFu
};

static const char* NAMED_COLOR_LABELS[] = {
    "White",
    "Cyan",
    "Magenta",
    "Yellow",
    "Black"
};

static const PaletteColor NAMED_COLOR_VALUES[] = {
    {255u, 255u, 255u},
    {0u, 255u, 255u},
    {255u, 0u, 255u},
    {255u, 255u, 0u},
    {0u, 0u, 0u}
};

struct PaletteRecoveryCandidate {
    u32 color_count;
    PaletteColor colors[MAX_CUSTOM_PALETTE_COLORS];
};

struct PpmParserState {
    bool has_encoded_tile;
    MetadataTile::Values encoded_tile;
    const u8* data;
    usize size;
    usize cursor;
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
    bool has_palette_text;
    char palette_text[MAX_CUSTOM_PALETTE_TEXT];
    usize palette_text_length;
    bool has_palette_base;
    u64 palette_base_value;
    bool has_page_symbols;
    u64 page_symbols_value;
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
    bool has_rotation_degrees;
    double rotation_degrees_value;
    bool has_rotation_width;
    u64 rotation_width_value;
    bool has_rotation_height;
    u64 rotation_height_value;
    bool has_rotation_margin;
    double rotation_margin_value;
    bool has_affine_transform;
    AffineTransform affine_transform;
    bool affine_fit_covers_page;
    bool affine_from_previous_page;
    bool has_skew_src_width;
    u64 skew_src_width_value;
    bool has_skew_src_height;
    u64 skew_src_height_value;
    bool has_skew_y_pixels;
    double skew_y_pixels_value;
    bool has_fiducial_spacing;
    u64 fiducial_spacing_value;
    bool has_fiducial_columns;
    u64 fiducial_columns_value;
    bool has_fiducial_rows;
    u64 fiducial_rows_value;

    PpmParserState()
        : has_encoded_tile(false),
          encoded_tile(),
          data(0),
          size(0u),
          cursor(0u),
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
          has_palette_text(false),
          palette_text_length(0u),
          has_palette_base(false),
          palette_base_value(0u),
          has_page_symbols(false),
          page_symbols_value(0u),
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
          has_rotation_degrees(false),
          rotation_degrees_value(0.0),
          has_rotation_width(false),
          rotation_width_value(0u),
          has_rotation_height(false),
          rotation_height_value(0u),
          has_rotation_margin(false),
          rotation_margin_value(0.0),
          has_affine_transform(false),
          affine_transform(),
          affine_fit_covers_page(false),
          affine_from_previous_page(false),
          has_skew_src_width(false),
          skew_src_width_value(0u),
          has_skew_src_height(false),
          skew_src_height_value(0u),
          has_skew_y_pixels(false),
          skew_y_pixels_value(0.0),
          has_fiducial_spacing(false),
          fiducial_spacing_value(0u),
          has_fiducial_columns(false),
          fiducial_columns_value(0u),
          has_fiducial_rows(false),
          fiducial_rows_value(0u) {
        palette_text[0] = '\0';
    }
};

struct CenteredDataRectangle {
    u32 x0;
    u32 y0;
    u32 width;
    u32 height;

    CenteredDataRectangle() : x0(0u), y0(0u), width(0u), height(0u) {}
};

struct PageCapacityResult {
    u32 width_pixels;
    u32 height_pixels;
    u32 data_height_pixels;
    u64 bits_per_page;
    u64 usable_data_pixels;
    FooterLayout footer_layout;

    PageCapacityResult()
        : width_pixels(0u),
          height_pixels(0u),
          data_height_pixels(0u),
          bits_per_page(0u),
          usable_data_pixels(0u),
          footer_layout() {}
};

static const usize MAX_ARCHIVE_PATH_COMPONENTS = 512u;

struct ArchiveBuildContext {
    makocode::ByteBuffer buffer;
    makocode::BitWriter entries;
    makocode::ByteBuffer path_registry;
    u64 entry_count;
    bool raw_stream;

    ArchiveBuildContext()
        : buffer(),
          entries(),
          path_registry(),
          entry_count(0u),
          raw_stream(false) {}

    ~ArchiveBuildContext() {
        buffer.release();
        entries.reset();
        path_registry.release();
        entry_count = 0u;
        raw_stream = false;
    }
};

typedef bool (*ArchiveEntryCallback)(u8,
                                     const char*,
                                     usize,
                                     const u8*,
                                     usize,
                                     u64,
                                     void*);

struct EccFillCandidate {
    double ratio;
    double requested_ratio;
    u16 block_data;
    u16 parity;
    u64 block_count;
    u64 payload_bits;
    u64 frame_bits;
    u64 bits_per_page;
    u64 page_count;
    u16 overlay_budget;
};

namespace MetadataTile {
using namespace makocode;
bool match_tile_tag_affine(const u8* pixel_data_rgb,
                                     u32 width_pixels,
                                     u32 height_pixels,
                                     u32 data_height_pixels,
                                     const AffineParams& affine,
                                     u64& tag_out,
                                     bool& inverted_out,
                                     double& confidence_out,
                                     bool allow_incomplete_palette = false,
                                     bool tag_only = false,
                                     bool sample_scaled_cells = false,
                                     u32* tag_y_out = 0,
                                     u32* metadata_bit_count_out = 0,
                                     u32 max_marker_errors = 0xFFFFFFFFu,
                                     bool ignore_marker = false);
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
bool decode_tile(const u8* pixel_data_rgb,
                            u32 width_pixels,
                            u32 height_pixels,
                            u32 data_height_pixels,
                            const Placement& placement,
                            Values& out_values,
                            ByteBuffer& out_palette_text);
} // namespace MetadataTile

bool archive_is_well_formed(const makocode::ByteBuffer& archive);

bool archive_init(ArchiveBuildContext& ctx);

bool ascii_to_fraction(const char* text,
                              usize length,
                              u64& numerator,
                              u64& denominator);

bool ppm_write_metadata_header(const PpmParserState& state,
                                      makocode::ByteBuffer& output);

void apply_metadata_tile_metadata(PpmParserState& state,
                                         const MetadataTile::Values& values,
                                         const makocode::ByteBuffer& palette_text);

bool validate_overwrite_budget(const ImageMappingConfig& mapping,
                                           const makocode::ByteBuffer& frame_bits,
                                           u64 frame_bit_count,
                                           u32 width_pixels,
                                           u32 height_pixels,
                                           u32 data_height_pixels,
                                           u64 bits_per_page,
                                           u64 page_count,
                                           const makocode::EccSummary& ecc_summary,
                                           double metadata_ecc_redundancy,
                                           u16 unknown_parity_budget,
                                           u16 overlay_erasure_budget,
                                           u16* minimum_parity_required_out = 0,
                                           bool include_metadata_tile = true);

bool archive_add_file(ArchiveBuildContext& ctx,
                             const char* rel_path,
                             const u8* data,
                             usize length);

bool compute_footer_layout(u32 page_width_pixels,
                                  u32 page_height_pixels,
                                  const PageFooterConfig& footer,
                                  FooterLayout& layout);

bool is_power_of_two_u32(u32 value);

usize footer_compute_max_text_length(const PageFooterConfig& footer,
                                            u64 page_count);

namespace makocode {
bool constant_time_equal(const u8* a, const u8* b, usize length);
} // namespace makocode

bool read_entire_stdin(makocode::ByteBuffer& buffer);

namespace MetadataTile {
using namespace makocode;
bool build_active_guard_mask(const u8* active_mask,
                             usize active_mask_size,
                             u8* guard_mask,
                             usize guard_mask_size);
bool build_payload_rank_map(u32 width_pixels,
                                       u32 data_height_pixels,
                                       const Placement& placement,
                                       const Values& values,
                                       const u8* reserved_mask,
                                       usize reserved_mask_size,
                                       ByteBuffer& rank_map,
                                       u64& payload_pixel_count,
                                       bool include_metadata_overwrites = false,
                                       u64 symbol_placement_seed = 0u,
                                       PayloadRankMapCache* cache = 0,
                                       bool scatter_payload = false,
                                       const u8* password_shuffle_key = 0,
                                       const u8* symbol_placement_salt = 0,
                                       const u8* salted_password_shuffle_key = 0);
bool permute_payload_rank_map(u64 pixel_count,
                              const Values& values,
                              u64 symbol_placement_seed,
                              ByteBuffer& rank_map,
                              u64 payload_pixel_count,
                              bool scatter_payload = false,
                              const u8* password_shuffle_key = 0,
                              const u8* symbol_placement_salt = 0,
                              const u8* salted_password_shuffle_key = 0);
} // namespace MetadataTile

bool arguments_request_help(int arg_count, char** args);

namespace MetadataTile {
using namespace makocode;
Placement compute_center_tile_placement(u32 width_pixels,
                                                   u32 data_height_pixels);
} // namespace MetadataTile

namespace MetadataTile {
using namespace makocode;
u32 palette_tag_x0(u32 palette_count);
} // namespace MetadataTile

bool path_contains_substring(const char* path, const char* needle);

bool archive_append_bytes(makocode::BitWriter& writer,
                                 const u8* data,
                                 usize length);

namespace makocode {
bool stealth_derive_base_key(const char* password,
                                    usize password_length,
                                    const u8 salt[ENCRYPTION_SALT_BYTES],
                                    u8 base_key[32]);
} // namespace makocode

bool image_mapping_build_custom_palette(ImageMappingConfig& config,
                                               const char* command_name);

bool join_output_path(const char* base_dir,
                             const char* rel_path,
                             makocode::ByteBuffer& out);

void byte_buffer_move(makocode::ByteBuffer& dest, makocode::ByteBuffer& src);

bool ensure_parent_directories(const char* file_path);

bool ascii_starts_with(const char* text, const char* prefix);

bool encode_page_to_ppm(const ImageMappingConfig& mapping,
                               const makocode::ByteBuffer& frame_bits,
                               u64 frame_bit_count,
                               u64 bit_offset,
                               u32 width_pixels,
                               u32 height_pixels,
                               u64 page_index,
                               u64 bits_per_page,
                               const makocode::EccSummary* ecc_summary,
                               double metadata_ecc_redundancy,
                               const char* footer_text,
                               usize footer_length,
                               const FooterLayout& footer_layout,
                               makocode::ByteBuffer& output,
                               u16 unknown_parity_budget = 0u,
                               u16 overlay_erasure_budget = 0u,
                               bool include_metadata_tile = true,
                               MetadataTile::Values* metadata_values_out = 0,
                               MetadataTile::PayloadRankMapCache* rank_map_cache = 0,
                               u64 page_count = 1u);

namespace MetadataTile {
using namespace makocode;
bool write_compact_u64(BitWriter& writer, u64 value);
} // namespace MetadataTile

bool decode_named_palette_color(const char* token,
                                       usize length,
                                       u8& color_id,
                                       PaletteColor& color);

bool is_safe_archive_path(const char* path, usize length);

bool write_buffer_to_file(const char* path,
                                 const makocode::ByteBuffer& buffer,
                                 makocode::ByteBuffer* debug_csv = 0,
                                 u64 debug_page = 1u,
                                 u32 data_height_pixels = 0u,
                                 const u8* fiducial_rgb = 0,
                                 bool embed_fiducials = true);

bool append_bits_from_buffer(makocode::BitWriter& writer,
                                    const u8* data,
                                    u64 bit_count);

bool metadata_schema_is_current(u32 schema_version);

void write_minify_help();

namespace MetadataTile {
using namespace makocode;
double rgb_luminance_u8(u8 r, u8 g, u8 b);
} // namespace MetadataTile

bool archive_finalize(ArchiveBuildContext& ctx);

bool ppm_consume_binary_separator(PpmParserState& state);

bool ascii_to_u64(const char* text, usize length, u64* out_value);

bool ppm_next_token(PpmParserState& state, const char** out_start, usize* out_length);

bool build_page_filename(makocode::ByteBuffer& buffer,
                                const char* timestamp,
                                u64 page_index,
                                u64 page_count);

bool compute_bits_per_page(const ImageMappingConfig& mapping,
                                  u32 width_pixels,
                                  u32 height_pixels,
                                  u32 data_height_pixels,
                                  u64& bits_per_page);

bool footer_char_is_allowed(char c);

bool ppm_extract_frame_bits(const makocode::ByteBuffer& input,
                                   const ImageMappingConfig& overrides,
                                   makocode::ByteBuffer& frame_bits,
                                   u64& frame_bit_count,
                                   PpmParserState& metadata_out,
                                   bool force_disable_fiducial_subgrid,
                                   bool* retry_without_subgrid_out = 0,
                                   makocode::ByteBuffer* debug_fiducial_csv = 0,
                                   u64 debug_page = 1u,
                                   makocode::ByteBuffer* frame_erasure_bits = 0,
                                   u64* overlay_selector_seed_out = 0,
                                   makocode::ByteBuffer* overlay_selector_counts_out = 0,
                                   u16* unknown_parity_budget_out = 0,
                                   u16* overlay_erasure_budget_out = 0,
                                   bool* color_rotation_out = 0,
                                   const AffineTransform* previous_page_affine_hint = 0,
                                   bool force_tile_center_sampling = false,
                                   u64* page_capacity_bits_out = 0,
                                   const MetadataTile::Values* known_tile_override = 0,
                                   const u8* known_rgb_pixels_override = 0,
                                   u32 known_rgb_width_override = 0u,
                                   u32 known_rgb_height_override = 0u,
                                   u32 small_scan_geometry_variant = 0u,
                                   bool detect_uniform_blots = true,
                                   bool known_tile_is_present = true,
                                   bool metadata_center_probe_only = false,
                                   bool metadata_summary_only = false,
                                   u64 page_capacity_override = 0u,
                                   makocode::PaletteSymbolPageSalt* page_salt_out = 0);

bool palette_colors_equal(const PaletteColor& a, const PaletteColor& b);

bool find_best_ecc_fill(usize payload_bytes,
                               double min_ratio,
                               double max_ratio,
                               const ImageMappingConfig& mapping,
                               const PageFooterConfig& footer_config,
                               u32 width_pixels,
                               u32 height_pixels,
                               bool fit_width,
                               bool fit_height,
                               double structural_erasure_fraction,
                               double overlay_fraction,
                               u16 required_unknown_errors,
                               bool omit_bootstrap,
                               double& best_ratio,
                               double& best_requested_ratio,
                               u16& best_block_data,
                               u16& best_parity,
                               u64& best_frame_bits,
                               u64& best_bits_needed,
                               u64& best_page_count);

bool archive_collect_directory(const char* fs_path,
                                      const char* rel_path,
                                      ArchiveBuildContext& ctx);

u8 color_mode_samples_per_pixel(u8 mode);

bool merge_parser_state(PpmParserState& dest, const PpmParserState& src);

bool build_frame_bits(const makocode::EncoderContext& encoder,
                             const ImageMappingConfig& mapping,
                             makocode::ByteBuffer& frame_bits,
                             u64& frame_bit_count);

bool recover_metadata_free_single_page(const makocode::ByteBuffer& input,
                                              const ImageMappingConfig& mapping_overrides,
                                              bool metadata_absent,
                                              makocode::ByteBuffer& frame_bits_out,
                                              makocode::ByteBuffer& frame_erasures_out,
                                              u64& frame_bit_count_out,
                                              u64& page_capacity_bits_out,
                                              PpmParserState& state_out,
                                              MetadataTile::Values& values_out,
                                              makocode::PaletteSymbolPageSalt* page_salt_out = 0);

void write_decode_help();

bool unpack_archive_to_directory(const makocode::ByteBuffer& payload,
                                        const char* output_dir,
                                        bool allow_partial = false);

void u64_to_ascii(u64 value, char* buffer, usize capacity);

bool mapping_has_custom_palette(const ImageMappingConfig& mapping);

u8 bits_per_sample(u8 mode);

bool ascii_equals_token(const char* text, usize length, const char* keyword);

bool join_fs_path(const char* base,
                         const char* name,
                         makocode::ByteBuffer& out);

bool process_image_mapping_option(int arg_count,
                                         char** args,
                                         int* arg_index,
                                         ImageMappingConfig& config,
                                         const char* command_name,
                                         bool* handled);

bool apply_default_fiducial_grid(const makocode::ByteBuffer& input,
                                        makocode::ByteBuffer& output,
                                        u32 data_height_pixels = 0u,
                                        const u8* fiducial_rgb = 0,
                                        makocode::ByteBuffer* debug_csv = 0,
                                        u64 debug_page = 1u);

namespace makocode {
bool stealth_fill_random(ByteBuffer& output,
                         usize size);
bool read_password_from_terminal(SecretBuffer& output,
                                 const char* prompt,
                                 bool confirm);
} // namespace makocode

namespace MetadataTile {
using namespace makocode;
u32 tag_marker_x0();
} // namespace MetadataTile

bool read_entire_file(const char* path, makocode::ByteBuffer& buffer);

u32 custom_palette_group_bit_count(u32 palette_count);

bool utc_timestamp_string(char* buffer, usize capacity);

namespace makocode {
bool stealth_encrypt_blob(const u8* plaintext,
                                 usize plaintext_length,
                                 const u8 base_key[32],
                                 const u8 nonce_seed[12],
                                 const u8* aad,
                                 usize aad_length,
                                 const char* domain,
                                 ByteBuffer& output);
} // namespace makocode

bool footer_build_page_text(const PageFooterConfig& footer,
                                   u64 page_index,
                                   u64 page_count,
                                   makocode::ByteBuffer& buffer);

void console_line(int fd, const char* text);

void format_fixed_3(double value, char* buffer, usize capacity);

u8 rotate_right_u8(u8 value, u8 amount);

u32 read_le_u32(const u8* ptr);

bool compute_fiducial_marker_mask(u32 width_pixels,
                                        u32 height_pixels,
                                        u32 data_height_pixels,
                                        u64& reserved_data_pixels,
                                        makocode::ByteBuffer* mask_out,
                                        bool use_grid_override = false,
                                        u32 columns_override = 0u,
                                        u32 rows_override = 0u,
                                        u32 spacing_override = 0u);

bool ppm_write_dimensions(u64 width,
                                 u64 height,
                                 makocode::ByteBuffer& output);



bool evaluate_ecc_fill_candidate(usize payload_bytes,
                                        double requested_redundancy,
                                        const ImageMappingConfig& mapping,
                                        const PageFooterConfig& footer_template,
                                        u32 width_pixels,
                                        u32 height_pixels,
                                        bool fit_width,
                                        bool fit_height,
                                        double structural_erasure_fraction,
                                        double overlay_fraction,
                                        EccFillCandidate& candidate,
                                        u32 metadata_ecc_minimum_parity_bytes = 2u,
                                        u16 required_unknown_errors = 0u,
                                        u32 parity_step = 4u,
                                        bool omit_bootstrap = false);

bool write_all_fd(int fd, const u8* data, usize length);

namespace makocode {
void sha256_init(Sha256State& state);
} // namespace makocode

namespace makocode {
bool append_affine_csv_row(makocode::ByteBuffer& csv,
                                  u64 page,
                                  bool has_affine,
                                  double a00,
                                  double a01,
                                  double a10,
                                  double a11,
                                  double tx,
                                  double ty);
} // namespace makocode

bool write_bytes_to_file(const char* path, const u8* data, usize length);

bool normalize_store_path(const char* source,
                                 makocode::ByteBuffer& output);

bool ppm_read_rgb_pixels(PpmParserState& state,
                                u64 pixel_count,
                                makocode::ByteBuffer& pixel_buffer);

int ascii_compare(const char* a, const char* b);

void write_encode_help();

const GlyphPattern* footer_lookup_glyph(char c);

namespace makocode {
void sha256_update(Sha256State& state, const u8* data, usize length);
} // namespace makocode

bool compute_page_layout(const ImageMappingConfig& mapping,
                                PageFooterConfig& footer_config,
                                u64 frame_bit_count,
                                u32 width_pixels,
                                u32 height_pixels,
                                FooterLayout& footer_layout,
                                u32& data_height_pixels,
                                u64& bits_per_page,
                                u64& page_count,
                                const makocode::EccSummary* ecc_summary = 0,
                                double metadata_ecc_redundancy = 0.0,
                                u16 unknown_parity_budget = 0u,
                                u16 overlay_erasure_budget = 0u);

u8 rotate_left_u8(u8 value, u8 amount);

bool frame_bits_to_payload(const u8*, u64, const PpmParserState&,
                                  makocode::ByteBuffer&, u64&, u64* = 0,
                                  const u8* = 0, u64 = 0,
                                  makocode::ByteBuffer* = 0);

void fiducial_select_color(const ImageMappingConfig& mapping,
                                  u8* fiducial_rgb);

void console_write(int fd, const char* text);

bool ensure_directory_tree(const char* path);

bool process_fiducial_spacing_option(int arg_count,
                                            char** args,
                                            int* arg_index,
                                            FiducialGridDefaults& defaults,
                                            const char* command_name,
                                            bool* handled);

void write_usage();

namespace makocode {
void sha256_finalize(Sha256State& state, u8 digest[32]);
} // namespace makocode

bool archive_parse(const makocode::ByteBuffer& archive,
                          ArchiveEntryCallback callback,
                          void* callback_context,
                          u64* entry_count_out,
                          bool allow_partial = false);

bool join_rel_path(const char* base,
                          const char* name,
                          makocode::ByteBuffer& out);

bool fit_page_dimensions(const ImageMappingConfig& mapping,
                                const PageFooterConfig& footer_template,
                                u64 frame_bit_count,
                                bool fit_width,
                                bool fit_height,
                                u32& width_pixels,
                                u32& height_pixels,
                                const makocode::EccSummary* ecc_summary = 0,
                                double metadata_ecc_redundancy = 0.0,
                                u16 unknown_parity_budget = 0u,
                                u16 overlay_erasure_budget = 0u);

bool image_mapping_set_palette_text(ImageMappingConfig& config,
                                           const char* text,
                                           usize length,
                                           const char* command_name);

extern FiducialGridDefaults g_fiducial_defaults;

extern bool g_encode_metadata_disabled;

#undef FOOTER_GLYPH_SIMPLE
#undef FOOTER_GLYPH_CUSTOM
#undef FOOTER_GLYPH_BLANK_ROW
#undef LZMA_PROPS_SIZE
#endif
