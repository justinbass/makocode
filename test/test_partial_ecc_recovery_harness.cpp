#define main makocode_program_main
#include "../makocode.cpp"
#undef main

using namespace makocode;

struct PartialArchiveCheck {
    bool found_first_file;
    bool second_file_intact;
    const u8* expected_second_file;
    usize expected_second_size;
};

int main() {
    BitWriter archive_writer;
    static const char first_path[] = "00-first.txt";
    static const char first_data[] = "recovered before the damaged block\n";
    static const char second_path[] = "99-large.bin";
    const usize second_size = 65536u;
    ByteBuffer second_data;
    if (!second_data.ensure(second_size) ||
        !MetadataTile::write_compact_u64(archive_writer, 2u) ||
        !archive_writer.write_bit(0u) ||
        !MetadataTile::write_compact_u64(archive_writer, sizeof(first_path) - 1u)) {
        fprintf(stderr, "partial ECC harness: archive setup failed\n");
        return 1;
    }
    for (usize i = 0u; i < sizeof(first_path) - 1u; ++i) {
        if (!archive_writer.write_bits((u8)first_path[i], 8u)) return 1;
    }
    if (!MetadataTile::write_compact_u64(archive_writer, sizeof(first_data) - 1u)) return 1;
    for (usize i = 0u; i < sizeof(first_data) - 1u; ++i) {
        if (!archive_writer.write_bits((u8)first_data[i], 8u)) return 1;
    }
    if (!archive_writer.write_bit(0u) ||
        !MetadataTile::write_compact_u64(archive_writer, sizeof(second_path) - 1u)) return 1;
    for (usize i = 0u; i < sizeof(second_path) - 1u; ++i) {
        if (!archive_writer.write_bits((u8)second_path[i], 8u)) return 1;
    }
    if (!MetadataTile::write_compact_u64(archive_writer, second_size)) return 1;
    u32 random_state = 0x9E3779B9u;
    for (usize i = 0u; i < second_size; ++i) {
        random_state ^= random_state << 13u;
        random_state ^= random_state >> 17u;
        random_state ^= random_state << 5u;
        second_data.data[i] = (u8)random_state;
        if (!archive_writer.write_bits(second_data.data[i], 8u)) return 1;
    }
    second_data.size = second_size;
    if (!archive_writer.align_to_byte()) return 1;

    ByteBuffer compressed;
    if (!lzma_compress(archive_writer.data(), archive_writer.byte_size(), compressed, true)) {
        fprintf(stderr, "partial ECC harness: compression failed\n");
        return 1;
    }
    ByteBuffer compressed_with_properties;
    if (!compressed_with_properties.ensure(compressed.size + LZMA_PROPS_SIZE)) return 1;
    memcpy(compressed_with_properties.data, LZMA_FIXED_PROPERTIES, LZMA_PROPS_SIZE);
    memcpy(compressed_with_properties.data + LZMA_PROPS_SIZE, compressed.data, compressed.size);
    compressed_with_properties.size = compressed.size + LZMA_PROPS_SIZE;
    byte_buffer_move(compressed, compressed_with_properties);
    BitWriter encoded_writer;
    EccSummary summary;
    const double redundancy = 0.05;
    if (!encode_payload_with_ecc(compressed, redundancy, encoded_writer, summary)) {
        fprintf(stderr, "partial ECC harness: ECC encode failed\n");
        return 1;
    }
    usize header_span = ecc_header_span_for_bytes(ECC_HEADER_TOTAL_BYTES + encoded_writer.byte_size());
    ByteBuffer stream;
    if (!stream.ensure(header_span + encoded_writer.byte_size())) return 1;
    stream.size = header_span + encoded_writer.byte_size();
    if (header_span != ECC_HEADER_TOTAL_BYTES ||
        !build_ecc_header_bytes(stream.data,
                                summary.block_data_symbols,
                                summary.parity_symbols,
                                summary.block_count,
                                summary.original_bytes)) {
        fprintf(stderr, "partial ECC harness: ECC header setup failed\n");
        return 1;
    }
    memcpy(stream.data + header_span, encoded_writer.data(), encoded_writer.byte_size());
    usize block_total = (usize)(summary.block_data_symbols + summary.parity_symbols);
    usize final_block = header_span + (usize)(summary.block_count - 1u) * block_total;
    u16 damage_count = summary.parity_symbols;
    for (u16 i = 0u; i < damage_count; ++i) {
        stream.data[final_block + summary.block_data_symbols - damage_count + i] ^=
            (u8)(0xA5u + i * 17u);
    }
    if (!fisher_yates_shuffle(stream.data + header_span, stream.size - header_span)) return 1;

    DecoderContext decoder;
    bool parsed = decoder.parse(stream.data,
                                (u64)stream.size * 8u,
                                (const char*)0,
                                0u);
    if (!parsed || !decoder.has_payload || !decoder.partial_payload ||
        decoder.ecc_stats.unrecoverable_blocks == 0u) {
        fprintf(stderr, "partial ECC harness: parsed=%d payload=%d partial=%d bad_blocks=%llu\n",
                parsed ? 1 : 0,
                decoder.has_payload ? 1 : 0,
                decoder.partial_payload ? 1 : 0,
                (unsigned long long)decoder.ecc_stats.unrecoverable_blocks);
        fprintf(stderr, "partial ECC harness: decode did not return a partial payload\n");
        return 1;
    }
    PartialArchiveCheck check = {false,
                                 false,
                                 second_data.data,
                                 second_data.size};
    auto check_partial_archive_entry = [](u8 type,
                                          const char* path,
                                          usize path_size,
                                          const u8* data,
                                          usize data_size,
                                          u64,
                                          void* context) -> bool {
        PartialArchiveCheck* archive_check = (PartialArchiveCheck*)context;
        static const char expected_path[] = "00-first.txt";
        static const char expected_data[] = "recovered before the damaged block\n";
        if (type == 0u && path_size == sizeof(expected_path) - 1u &&
            memcmp(path, expected_path, path_size) == 0) {
            archive_check->found_first_file =
                data_size == sizeof(expected_data) - 1u &&
                memcmp(data, expected_data, data_size) == 0;
        } else if (type == 0u && path_size == sizeof("99-large.bin") - 1u &&
                   memcmp(path, "99-large.bin", path_size) == 0) {
            archive_check->second_file_intact =
                data_size == archive_check->expected_second_size &&
                memcmp(data, archive_check->expected_second_file, data_size) == 0;
        }
        return true;
    };
    if (!archive_parse(decoder.payload,
                       check_partial_archive_entry,
                       &check,
                       (u64*)0,
                       true) ||
        !check.found_first_file || check.second_file_intact) {
        fprintf(stderr, "partial ECC harness: expected the earlier file and partial later data\n");
        return 1;
    }
    puts("partial ECC harness: recovered a complete archive entry before the damaged block");
    return 0;
}
