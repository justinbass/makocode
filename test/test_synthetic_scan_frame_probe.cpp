#define main makocode_cli_main
#include "../makocode.cpp"
#undef main

#include <stdio.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: synthetic_scan_frame_probe SOURCE.ppm DAMAGED.ppm\n");
        return 2;
    }

    makocode::ByteBuffer image[2];
    makocode::ByteBuffer frame[2];
    makocode::ByteBuffer payload[2];
    PpmParserState metadata[2];
    u64 frame_bits[2] = {0u, 0u};
    u64 payload_bits[2] = {0u, 0u};
    u64 length_header[2] = {0u, 0u};
    u64 lzma_output_size[2] = {0u, 0u};
    u8 lzma_props[2][LZMA_PROPS_SIZE] = {};
    for (int i = 0; i < 2; ++i) {
        if (!read_entire_file(argv[i + 1], image[i])) {
            fprintf(stderr, "frame probe: failed to read image %d\n", i);
            return 1;
        }
        ImageMappingConfig mapping;
        bool retry_without_subgrid = false;
        if (!ppm_extract_frame_bits(image[i], mapping, frame[i], frame_bits[i],
                                    metadata[i], false, &retry_without_subgrid)) {
            fprintf(stderr, "frame probe: extraction failed for image %d\n", i);
            return 1;
        }
        makocode::BitReader reader;
        reader.reset(frame[i].data, frame_bits[i]);
        length_header[i] = reader.read_bits(64u);
        if (reader.failed ||
            !frame_bits_to_payload(frame[i].data, frame_bits[i], metadata[i],
                                   payload[i], payload_bits[i])) {
            fprintf(stderr, "frame probe: payload framing failed for image %d\n", i);
            return 1;
        }
        makocode::ByteBuffer unshuffled;
        if (!unshuffled.ensure(payload[i].size)) return 1;
        unshuffled.size = payload[i].size;
        memcpy(unshuffled.data, payload[i].data, payload[i].size);
        if (!makocode::unshuffle_encoded_stream(unshuffled.data,
                                                unshuffled.size) ||
            unshuffled.size < LZMA_PROPS_SIZE + 8u) return 1;
        memcpy(lzma_props[i], unshuffled.data, LZMA_PROPS_SIZE);
        lzma_output_size[i] = read_le_u64(unshuffled.data + LZMA_PROPS_SIZE);
    }

    u64 mismatches = 0u;
    u64 first_mismatch = frame_bits[0];
    for (u64 bit = 0u; bit < frame_bits[0]; ++bit) {
        u8 source_bit = (frame[0].data[bit >> 3u] >> (bit & 7u)) & 1u;
        u8 damaged_bit = (frame[1].data[bit >> 3u] >> (bit & 7u)) & 1u;
        if (source_bit != damaged_bit) {
            if (mismatches == 0u) first_mismatch = bit;
            ++mismatches;
        }
    }
    printf("source_frame_bits=%llu damaged_frame_bits=%llu "
           "source_header_bits=%llu damaged_header_bits=%llu "
           "frame_mismatches=%llu first_mismatch=%llu "
           "lzma_props=%02x%02x%02x%02x%02x/%02x%02x%02x%02x%02x "
           "lzma_output_size=%llu/%llu "
           "damaged_affine=(%.6f,%.6f,%.3f,%.3f)\n",
           (unsigned long long)frame_bits[0],
           (unsigned long long)frame_bits[1],
           (unsigned long long)length_header[0],
           (unsigned long long)length_header[1],
           (unsigned long long)mismatches,
           (unsigned long long)first_mismatch,
           lzma_props[0][0], lzma_props[0][1], lzma_props[0][2],
           lzma_props[0][3], lzma_props[0][4],
           lzma_props[1][0], lzma_props[1][1], lzma_props[1][2],
           lzma_props[1][3], lzma_props[1][4],
           (unsigned long long)lzma_output_size[0],
           (unsigned long long)lzma_output_size[1],
           metadata[1].affine_transform.a00,
           metadata[1].affine_transform.a11,
           metadata[1].affine_transform.tx,
           metadata[1].affine_transform.ty);

    if (frame_bits[0] != 721u || frame_bits[1] != 721u ||
        !metadata[0].has_bits || !metadata[1].has_bits ||
        metadata[0].bits_value != metadata[1].bits_value ||
        length_header[0] != 528u || length_header[1] != 528u ||
        payload_bits[0] != 528u || payload_bits[1] != 528u ||
        !metadata[1].has_affine_transform ||
        fabs(metadata[1].affine_transform.a00 - 2.269230769) > 0.005 ||
        fabs(metadata[1].affine_transform.tx - 1.153846154) > 0.005 ||
        fabs(metadata[1].affine_transform.ty - 2.653846154) > 0.005 ||
        memcmp(lzma_props[0], lzma_props[1], LZMA_PROPS_SIZE) != 0 ||
        lzma_output_size[0] != 68u || lzma_output_size[1] != 580u ||
        mismatches != 1u || first_mismatch != 569u) {
        fprintf(stderr, "frame probe: failure differs from sampling regression\n");
        return 1;
    }
    return 0;
}
