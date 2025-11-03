/*
    makocode.cpp
    ------------
    Prototypical single-translation-unit implementation of the MakoCode encoder/decoder.
    The code is intentionally free of #include directives so it does not depend on any
    headers or external libraries. Every construct required to build the program lives
    inside this one file.

    This file is a starting point distilled from NOTES.md. It is not feature-complete,
    but it establishes the overall architecture and core utilities that later stages
    of the project can extend. The primary goals are:
        * Define the bit-level plumbing that the format requires.
        * Lay out the section model (address table, metadata, fiducials, payload).
        * Provide a simple CLI that performs a round-trip self-test using stubbed
          hash/ECC implementations so the infrastructure can be validated early.
        * Document assumptions directly alongside the code so future contributors
          keep the specification in view while implementing missing pieces.

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
};

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

struct SectionAddresses {
    u64 entries[8];

    SectionAddresses() {
        for (usize i = 0; i < 8u; ++i) {
            entries[i] = 0u;
        }
    }
};

struct HashDigest {
    u64 value;
};

struct HashBuilder {
    u64 state;

    HashBuilder() : state(0xcbf29ce484222325ull) {}

    void absorb_byte(u8 byte) {
        const u64 prime = 0x100000001b3ull;
        state ^= (u64)byte;
        state *= prime;
    }

    void absorb_bits(const u8* data, usize bit_count) {
        if (!data) {
            return;
        }
        usize full_bytes = bit_count >> 3u;
        usize remaining_bits = bit_count & 7u;
        for (usize i = 0; i < full_bytes; ++i) {
            absorb_byte(data[i]);
        }
        if (remaining_bits) {
            u8 last = 0u;
            for (usize bit = 0; bit < remaining_bits; ++bit) {
                u8 bit_value = (u8)((data[full_bytes] >> bit) & 1u);
                last |= (bit_value << bit);
            }
            absorb_byte(last);
        }
    }

    HashDigest finish() const {
        HashDigest digest;
        digest.value = state;
        return digest;
    }
};

struct AddressSizer {
    u8 address_size_size;
    u8 address_size_bits;
    u64 max_addressable_bits;

    AddressSizer() : address_size_size(0u), address_size_bits(0u), max_addressable_bits(0u) {}

    void compute(u64 total_bits) {
        u64 required = total_bits + 1u;
        u8 logical_size = 0u;
        u64 span = 64u;
        while (span < required && logical_size < 63u) {
            ++logical_size;
            if (span >= (1ull << 63)) {
                span = ~0ull;
                break;
            }
            span <<= 1;
        }
        if (logical_size > 63u) {
            logical_size = 63u;
        }
        address_size_bits = (u8)(6u + logical_size);
        u8 bits_needed = 0u;
        u64 max_representable = 0u;
        while (max_representable < logical_size && bits_needed < 6u) {
            ++bits_needed;
            max_representable = (1ull << bits_needed) - 1ull;
        }
        if (bits_needed < 3u) {
            bits_needed = 3u;
        }
        address_size_size = (u8)(bits_needed - 3u);
        if (address_size_size > 3u) {
            address_size_size = 3u;
        }
        if (address_size_bits >= 64u) {
            max_addressable_bits = ~0ull;
        } else {
            max_addressable_bits = (1ull << address_size_bits);
        }
    }
};

struct SectionLayout {
    AddressSizer address_sizer;
    SectionAddresses addresses;
    HashDigest address_hash;
    HashDigest index_hash;

    SectionLayout() : address_sizer(), addresses(), address_hash(), index_hash() {}

    void plan(u64 metadata_bits, u64 fiducial_bits, u64 data_bits) {
        u64 current = 0u;
        addresses.entries[0] = current;
        current += (u64)(2u);
        current += (u64)(address_sizer.address_size_size + 3u);
        addresses.entries[1] = current;
        current += ((u64)8u * (u64)(address_sizer.address_size_bits));
        addresses.entries[2] = current;
        current += 64u;
        addresses.entries[3] = current;
        current += 64u;
        u64 padding = (8u - (current & 7u)) & 7u;
        current += padding;
        addresses.entries[4] = current;
        current += metadata_bits;
        addresses.entries[5] = current;
        current += fiducial_bits;
        addresses.entries[6] = current;
        current += data_bits;
        addresses.entries[7] = current;
    }

    void bake_hashes(const BitWriter& writer) {
        HashBuilder builder;
        builder.absorb_bits(writer.data(), writer.bit_size());
        address_hash = builder.finish();
        HashBuilder index_builder;
        for (usize i = 0; i < 8u; ++i) {
            u64 value = addresses.entries[i];
            for (usize byte = 0; byte < sizeof(u64); ++byte) {
                u8 component = (u8)((value >> (byte * 8u)) & 0xFFu);
                index_builder.absorb_byte(component);
            }
        }
        index_hash = index_builder.finish();
    }
};

struct MetadataEntry {
    u32 key;
    ByteBuffer value;

    MetadataEntry() : key(0u), value() {}
};

struct MetadataBuilder {
    MetadataEntry entries[16];
    usize used;

    MetadataBuilder() : entries(), used(0u) {}

    MetadataEntry* push(u32 key) {
        if (used >= (usize)16) {
            return 0;
        }
        MetadataEntry* entry = &entries[used++];
        entry->key = key;
        entry->value.release();
        return entry;
    }

    void reset() {
        for (usize i = 0; i < used; ++i) {
            entries[i].value.release();
        }
        used = 0u;
    }
};

struct EncoderConfig {
    u32 metadata_key_count;
    u32 fiducial_density;
    u32 ecc_rate;
    u32 max_parallelism;

    EncoderConfig()
        : metadata_key_count(0u),
          fiducial_density(0u),
          ecc_rate(0u),
          max_parallelism(1u) {}
};

struct EncoderContext {
    EncoderConfig config;
    MetadataBuilder metadata;
    ByteBuffer payload_bytes;
    BitWriter bit_writer;
    SectionLayout layout;
    bool configured;

    EncoderContext() : config(), metadata(), payload_bytes(), bit_writer(), layout(), configured(false) {}

    void reset() {
        metadata.reset();
        payload_bytes.release();
        bit_writer.reset();
        layout = SectionLayout();
        configured = false;
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

    bool append_metadata_u64(u32 key, u64 value) {
        MetadataEntry* entry = metadata.push(key);
        if (!entry) {
            return false;
        }
        if (!entry->value.ensure(sizeof(u64))) {
            return false;
        }
        for (usize byte = 0; byte < sizeof(u64); ++byte) {
            entry->value.push((u8)((value >> (byte * 8u)) & 0xFFu));
        }
        return true;
    }

    bool append_metadata_text(u32 key, const char* text) {
        MetadataEntry* entry = metadata.push(key);
        if (!entry) {
            return false;
        }
        usize len = ascii_length(text);
        if (!entry->value.ensure(len)) {
            return false;
        }
        for (usize i = 0; i < len; ++i) {
            entry->value.push((u8)text[i]);
        }
        return true;
    }

    bool encode_metadata(BitWriter& writer) {
        u8 key_size_size = 0u;
        u8 key_size_bits = 0u;
        if (metadata.used == 0u) {
            key_size_size = 0u;
            key_size_bits = 0u;
        } else {
            key_size_size = 1u;
            key_size_bits = 7u;
        }
        if (!writer.write_bits((u64)key_size_size, 2u)) {
            return false;
        }
        if (!writer.write_bits((u64)key_size_bits, (usize)(key_size_size + 1u))) {
            return false;
        }
        if (!writer.write_bits((u64)metadata.used, 6u)) {
            return false;
        }
        for (usize index = 0; index < metadata.used; ++index) {
            MetadataEntry& entry = metadata.entries[index];
            if (!writer.write_bits((u64)entry.key, 7u)) {
                return false;
            }
            u64 bit_length = (u64)entry.value.size * 8u;
            if (!writer.write_bits(bit_length, 16u)) {
                return false;
            }
            for (usize i = 0; i < entry.value.size; ++i) {
                if (!writer.write_bits((u64)entry.value.data[i], 8u)) {
                    return false;
                }
            }
        }
        return true;
    }

    bool encode_payload(BitWriter& writer) {
        u64 byte_count = (u64)payload_bytes.size;
        if (!writer.write_bits(byte_count, 64u)) {
            return false;
        }
        for (usize i = 0; i < payload_bytes.size; ++i) {
            if (!writer.write_bits((u64)payload_bytes.data[i], 8u)) {
                return false;
            }
        }
        return true;
    }

    bool encode_address_section(BitWriter& writer) {
        u8 size_size = layout.address_sizer.address_size_size;
        u8 size_bits = layout.address_sizer.address_size_bits;
        if (!writer.write_bits((u64)size_size, 2u)) {
            return false;
        }
        u64 size_value = (u64)(layout.address_sizer.address_size_bits - 6u);
        if (!writer.write_bits(size_value, (usize)(size_size + 3u))) {
            return false;
        }
        for (usize i = 0; i < 8u; ++i) {
            if (!writer.write_bits(layout.addresses.entries[i], size_bits)) {
                return false;
            }
        }
        return true;
    }

    bool build() {
        if (!payload_bytes.data) {
            return false;
        }
        bit_writer.reset();
        BitWriter metadata_writer;
        if (!encode_metadata(metadata_writer)) {
            return false;
        }
        metadata_writer.align_to_byte();
        u64 metadata_bits = metadata_writer.bit_size();
        BitWriter payload_writer;
        if (!encode_payload(payload_writer)) {
            return false;
        }
        payload_writer.align_to_byte();
        u64 payload_bits = payload_writer.bit_size();
        u64 fiducial_bits = 128u;
        layout.address_sizer.compute(metadata_bits + payload_bits + fiducial_bits + 512u);
        layout.plan(metadata_bits, fiducial_bits, payload_bits);
        if (!encode_address_section(bit_writer)) {
            return false;
        }
        if (!bit_writer.write_bits(0u, 64u)) {
            return false;
        }
        if (!bit_writer.write_bits(0u, 64u)) {
            return false;
        }
        if (!bit_writer.align_to_byte()) {
            return false;
        }
        for (usize i = 0; i < metadata_writer.byte_size(); ++i) {
            if (!bit_writer.write_bits((u64)metadata_writer.data()[i], 8u)) {
                return false;
            }
        }
        for (usize i = 0; i < (fiducial_bits >> 3u); ++i) {
            u8 pattern = (u8)(i & 1u ? 0xAAu : 0x55u);
            if (!bit_writer.write_bits((u64)pattern, 8u)) {
                return false;
            }
        }
        for (usize i = 0; i < payload_writer.byte_size(); ++i) {
            if (!bit_writer.write_bits((u64)payload_writer.data()[i], 8u)) {
                return false;
            }
        }
        layout.bake_hashes(bit_writer);
        configured = true;
        return true;
    }
};

struct DecoderContext {
    BitReader reader;
    SectionLayout layout;
    ByteBuffer payload;
    bool has_payload;

    DecoderContext() : reader(), layout(), payload(), has_payload(false) {}

    void reset() {
        reader = BitReader();
        layout = SectionLayout();
        payload.release();
        has_payload = false;
    }

    bool parse(const u8* data, usize size_in_bits) {
        reader.reset(data, size_in_bits);
        u8 size_size = (u8)reader.read_bits(2u);
        u8 size_bits = (u8)reader.read_bits((usize)(size_size + 3u));
        size_bits = (u8)(size_bits + 6u);
        layout.address_sizer.address_size_size = size_size;
        layout.address_sizer.address_size_bits = size_bits;
        for (usize i = 0; i < 8u; ++i) {
            layout.addresses.entries[i] = reader.read_bits(size_bits);
        }
        layout.address_sizer.max_addressable_bits = layout.addresses.entries[7];
        HashBuilder builder;
        builder.absorb_bits(data, layout.addresses.entries[4]);
        layout.address_hash = builder.finish();
        reader.cursor = layout.addresses.entries[4];
        MetadataBuilder metadata;
        u8 key_size_size = (u8)reader.read_bits(2u);
        u8 key_size_bits = (u8)reader.read_bits((usize)(key_size_size + 1u));
        (void)key_size_bits;
        u64 entry_count = reader.read_bits(6u);
        for (u64 entry = 0; entry < entry_count; ++entry) {
            u64 key = reader.read_bits(7u);
            MetadataEntry* slot = metadata.push((u32)key);
            if (!slot) {
                return false;
            }
            u64 length_bits = reader.read_bits(16u);
            u64 length_bytes = (length_bits + 7u) >> 3u;
            if (!slot->value.ensure((usize)length_bytes)) {
                return false;
            }
            for (u64 byte_index = 0u; byte_index < length_bytes; ++byte_index) {
                u8 value = (u8)reader.read_bits(8u);
                slot->value.push(value);
            }
        }
        reader.cursor = layout.addresses.entries[6];
        u64 payload_size = reader.read_bits(64u);
        if (!payload.ensure((usize)payload_size)) {
            return false;
        }
        payload.size = (usize)payload_size;
        for (usize i = 0; i < payload.size; ++i) {
            payload.data[i] = (u8)reader.read_bits(8u);
        }
        has_payload = true;
        return true;
    }
};

} // namespace makocode

struct PpmParserState {
    const u8* data;
    usize size;
    usize cursor;
    bool has_bytes;
    u64 bytes_value;
    bool has_bits;
    u64 bits_value;

    PpmParserState()
        : data(0),
          size(0u),
          cursor(0u),
          has_bytes(false),
          bytes_value(0u),
          has_bits(false),
          bits_value(0u) {}
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
    const usize bytes_tag_len = (usize)sizeof(bytes_tag) - 1u;
    const usize bits_tag_len = (usize)sizeof(bits_tag) - 1u;
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

static bool ppm_to_bitstream(const makocode::ByteBuffer& input,
                             makocode::ByteBuffer& output,
                             u64& out_bit_count) {
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
    if (pixel_count > (USIZE_MAX_VALUE / 3u)) {
        return false;
    }
    u64 component_count = pixel_count * 3u;
    if (!output.ensure((usize)component_count)) {
        return false;
    }
    for (u64 component = 0u; component < component_count; ++component) {
        if (!ppm_next_token(state, &token, &token_length)) {
            return false;
        }
        u64 value = 0u;
        if (!ascii_to_u64(token, token_length, &value) || value > 255u) {
            return false;
        }
        output.data[component] = (u8)value;
    }
    output.size = (usize)component_count;
    u64 byte_count = component_count;
    if (state.has_bytes) {
        byte_count = state.bytes_value;
        if (byte_count > component_count) {
            return false;
        }
    }
    output.size = (usize)byte_count;
    if (state.has_bits) {
        if (state.bits_value > (byte_count * 8u)) {
            return false;
        }
        out_bit_count = state.bits_value;
    } else {
        out_bit_count = byte_count * 8u;
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

static void write_usage() {
    console_line(1, "MakoCode prototype");
    console_line(1, "Usage:");
    console_line(1, "  makocode encode   (reads raw bytes from stdin, emits bitstream to stdout)");
    console_line(1, "  makocode decode   (reads bitstream from stdin, emits payload to stdout)");
    console_line(1, "  makocode selftest (runs an encode->decode loop and reports status)");
}

static int command_encode() {
    makocode::ByteBuffer input;
    if (!read_entire_stdin(input)) {
        console_line(2, "encode: failed to read stdin");
        return 1;
    }
    makocode::EncoderContext encoder;
    if (!encoder.set_payload(input.data, input.size)) {
        console_line(2, "encode: failed to set payload");
        return 1;
    }
    encoder.append_metadata_text(4u, "1");
    encoder.append_metadata_text(5u, "0");
    encoder.append_metadata_text(6u, "0");
    encoder.append_metadata_u64(7u, 1u);
    if (!encoder.build()) {
        console_line(2, "encode: build failed");
        return 1;
    }
    u64 bit_count = encoder.bit_writer.bit_size();
    usize byte_count = encoder.bit_writer.byte_size();
    u64 pixel_count = (byte_count + 2u) / 3u;
    if (pixel_count == 0u) {
        pixel_count = 1u;
    }
    u64 max_width = 64u;
    u64 width = (pixel_count < max_width) ? pixel_count : max_width;
    if (width == 0u) {
        width = 1u;
    }
    u64 height = (pixel_count + width - 1u) / width;
    u64 total_pixels = width * height;
    char digits_bytes[32];
    char digits_bits[32];
    char digits_width[32];
    char digits_height[32];
    u64_to_ascii((u64)byte_count, digits_bytes, sizeof(digits_bytes));
    u64_to_ascii(bit_count, digits_bits, sizeof(digits_bits));
    u64_to_ascii(width, digits_width, sizeof(digits_width));
    u64_to_ascii(height, digits_height, sizeof(digits_height));
    console_line(1, "P3");
    console_write(1, "# MAKOCODE_BYTES ");
    console_write(1, digits_bytes);
    console_write(1, "\n");
    console_write(1, "# MAKOCODE_BITS ");
    console_write(1, digits_bits);
    console_write(1, "\n");
    console_write(1, digits_width);
    console_write(1, " ");
    console_write(1, digits_height);
    console_write(1, "\n");
    console_line(1, "255");
    const u8* raw = encoder.bit_writer.data();
    usize byte_index = 0u;
    for (u64 pixel = 0u; pixel < total_pixels; ++pixel) {
        for (u64 channel = 0u; channel < 3u; ++channel) {
            if (channel > 0u) {
                console_write(1, " ");
            }
            u8 value = 0u;
            if (byte_index < byte_count && raw) {
                value = raw[byte_index];
                ++byte_index;
            }
            char digits_channel[4];
            u64_to_ascii((u64)value, digits_channel, sizeof(digits_channel));
            console_write(1, digits_channel);
        }
        console_write(1, "\n");
    }
    return 0;
}

static int command_decode() {
    makocode::ByteBuffer ppm_stream;
    if (!read_entire_stdin(ppm_stream)) {
        console_line(2, "decode: failed to read stdin");
        return 1;
    }
    makocode::ByteBuffer bitstream;
    u64 bit_count = 0u;
    if (!ppm_to_bitstream(ppm_stream, bitstream, bit_count)) {
        console_line(2, "decode: invalid ppm input");
        return 1;
    }
    makocode::DecoderContext decoder;
    if (!decoder.parse(bitstream.data, bit_count)) {
        console_line(2, "decode: parse failure");
        return 1;
    }
    if (decoder.has_payload && decoder.payload.size) {
        write(1, decoder.payload.data, decoder.payload.size);
    }
    return 0;
}

static int command_selftest() {
    const char* sample = "MakoCode prototype self-test payload.";
    makocode::EncoderContext encoder;
    if (!encoder.set_payload((const u8*)sample, ascii_length(sample))) {
        console_line(2, "selftest: failed to allocate payload");
        return 1;
    }
    encoder.append_metadata_text(4u, "0");
    encoder.append_metadata_text(5u, "1");
    encoder.append_metadata_text(6u, "0");
    encoder.append_metadata_u64(7u, 1u);
    if (!encoder.build()) {
        console_line(2, "selftest: encode failed");
        return 1;
    }
    makocode::DecoderContext decoder;
    if (!decoder.parse(encoder.bit_writer.data(), encoder.bit_writer.bit_size())) {
        console_line(2, "selftest: decode failed");
        return 1;
    }
    if (!decoder.has_payload) {
        console_line(2, "selftest: payload missing");
        return 1;
    }
    bool match = true;
    if ((usize)ascii_length(sample) != decoder.payload.size) {
        match = false;
    } else {
        for (usize i = 0; i < decoder.payload.size; ++i) {
            if (decoder.payload.data[i] != (u8)sample[i]) {
                match = false;
                break;
            }
        }
    }
    if (!match) {
        console_line(2, "selftest: round-trip mismatch");
        return 1;
    }
    char digits[32];
    u64_to_ascii(encoder.bit_writer.bit_size(), digits, sizeof(digits));
    console_write(1, "selftest: ok (bitstream bits = ");
    console_write(1, digits);
    console_line(1, ")");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        write_usage();
        return 0;
    }
    if (ascii_compare(argv[1], "encode") == 0) {
        return command_encode();
    }
    if (ascii_compare(argv[1], "decode") == 0) {
        return command_decode();
    }
    if (ascii_compare(argv[1], "selftest") == 0) {
        return command_selftest();
    }
    write_usage();
    return 0;
}
