/*
 * Checkpoint creation and restoration commands.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_common.hpp"
struct CheckpointChunkSource {
    usize path_offset;
    usize offset;
    usize length;
    usize bundle_offset;
    u8 source_kind;
    CheckpointChunkSource() : path_offset(0u), offset(0u), length(0u), bundle_offset(0u), source_kind(0u) {}
};

struct CheckpointChunkRef {
    u8 id[32];
    usize length;
    u8 kind;
    usize bundle_offset;
    CheckpointChunkRef() : id(), length(0u), kind(0u), bundle_offset(0u) {}
};

struct CheckpointEntry {
    u8 type;
    char* path;
    usize path_length;
    u64 file_size;
    CheckpointChunkRef* chunks;
    usize chunk_count;
    usize chunk_capacity;
    bool manifest_complete;
    CheckpointEntry() : type(0u), path(0), path_length(0u), file_size(0u), chunks(0), chunk_count(0u), chunk_capacity(0u), manifest_complete(true) {}
};

struct CheckpointIndexEntry {
    u8 used;
    u8 id[32];
    CheckpointChunkSource source;
};

struct CheckpointChunkIndex {
    CheckpointIndexEntry* entries;
    usize capacity;
    usize count;
    makocode::ByteBuffer path_pool;

    CheckpointChunkIndex() : entries(0), capacity(0u), count(0u), path_pool() {}
    ~CheckpointChunkIndex() { release(); }

    void release() {
        makocode_platform_free(entries);
        entries = 0;
        capacity = 0u;
        count = 0u;
        path_pool.release();
    }

    static u64 hash_id(const u8 id[32]) {
        u64 value = 1469598103934665603ull;
        for (usize i = 0u; i < 32u; ++i) {
            value ^= id[i];
            value *= 1099511628211ull;
        }
        return value;
    }

    usize find_slot(const u8 id[32]) const {
        if (!capacity) return USIZE_MAX_VALUE;
        usize slot = (usize)(hash_id(id) & (u64)(capacity - 1u));
        for (usize probes = 0u; probes < capacity; ++probes) {
            if (!entries[slot].used || memcmp(entries[slot].id, id, 32u) == 0) return slot;
            slot = (slot + 1u) & (capacity - 1u);
        }
        return USIZE_MAX_VALUE;
    }

    bool grow() {
        usize new_capacity = capacity ? capacity * 2u : 64u;
        if (new_capacity <= capacity || new_capacity > USIZE_MAX_VALUE / sizeof(CheckpointIndexEntry)) return false;
        CheckpointIndexEntry* resized = (CheckpointIndexEntry*)makocode_platform_malloc(new_capacity * sizeof(CheckpointIndexEntry));
        if (!resized) return false;
        memset(resized, 0, new_capacity * sizeof(CheckpointIndexEntry));
        CheckpointIndexEntry* previous = entries;
        usize previous_capacity = capacity;
        entries = resized;
        capacity = new_capacity;
        count = 0u;
        for (usize i = 0u; i < previous_capacity; ++i) {
            if (!previous[i].used) continue;
            usize slot = find_slot(previous[i].id);
            entries[slot] = previous[i];
            ++count;
        }
        makocode_platform_free(previous);
        return true;
    }

    CheckpointIndexEntry* find(const u8 id[32]) {
        usize slot = find_slot(id);
        return slot == USIZE_MAX_VALUE || !entries[slot].used ? 0 : &entries[slot];
    }

    bool insert(const u8 id[32],
                u8 source_kind,
                const char* path,
                usize offset,
                usize length,
                usize bundle_offset) {
        if (!capacity || (count + 1u) * 10u >= capacity * 7u) {
            if (!grow()) return false;
        }
        usize slot = find_slot(id);
        if (slot == USIZE_MAX_VALUE) return false;
        if (entries[slot].used) return true;
        CheckpointChunkSource source;
        source.source_kind = source_kind;
        source.offset = offset;
        source.length = length;
        source.bundle_offset = bundle_offset;
        if (source_kind == 1u && path) {
            source.path_offset = path_pool.size;
            usize path_length = ascii_length(path);
            if (!path_pool.ensure(path_pool.size + path_length + 1u)) return false;
            memcpy(path_pool.data + path_pool.size, path, path_length);
            path_pool.data[path_pool.size + path_length] = 0u;
            path_pool.size += path_length + 1u;
        }
        entries[slot].used = 1u;
        memcpy(entries[slot].id, id, 32u);
        entries[slot].source = source;
        ++count;
        return true;
    }

    const char* source_path(const CheckpointIndexEntry& entry) const {
        if (entry.source.source_kind != 1u || entry.source.path_offset >= path_pool.size) return 0;
        return (const char*)(path_pool.data + entry.source.path_offset);
    }
};

struct CheckpointEntryList {
    CheckpointEntry* entries;
    usize count;
    usize capacity;
    CheckpointEntryList() : entries(0), count(0u), capacity(0u) {}
    ~CheckpointEntryList() { release(); }
    void release() {
        for (usize i = 0u; i < count; ++i) {
            makocode_platform_free(entries[i].path);
            makocode_platform_free(entries[i].chunks);
        }
        makocode_platform_free(entries);
        entries = 0;
        count = 0u;
        capacity = 0u;
    }
    bool push(const CheckpointEntry& entry) {
        if (count == capacity) {
            usize next = capacity ? capacity * 2u : 32u;
            if (next <= capacity || next > USIZE_MAX_VALUE / sizeof(CheckpointEntry)) return false;
            CheckpointEntry* resized = (CheckpointEntry*)makocode_platform_realloc(entries, next * sizeof(CheckpointEntry));
            if (!resized) return false;
            entries = resized;
            capacity = next;
        }
        entries[count++] = entry;
        return true;
    }
};

static const u8 CHECKPOINT_MAGIC[8] = {'M', 'K', 'C', 'P', '0', '\r', '\n', 0u};
static const u64 CHECKPOINT_DEFAULT_CHUNK_SIZE = 4ull * 1024ull * 1024ull;
static const u64 CHECKPOINT_MIN_CHUNK_SIZE = 256u;
static const u64 CHECKPOINT_MAX_CHUNK_SIZE = 0x40000000ull;

static void checkpoint_chunk_id(const u8* data, usize length, u8 digest[32]) {
    MetadataTile::Sha256State state;
    MetadataTile::sha256_init(state);
    if (length) MetadataTile::sha256_update(state, data, length);
    MetadataTile::sha256_finalize(state, digest);
}

static void checkpoint_path_id(const char* path, usize length, u8 digest[32]) {
    MetadataTile::Sha256State state;
    MetadataTile::sha256_init(state);
    if (length) MetadataTile::sha256_update(state, (const u8*)path, length);
    MetadataTile::sha256_finalize(state, digest);
}

static usize checkpoint_chunk_end(const u8* data, usize start, usize length, u64 target) {
    if (start >= length) return length;
    u64 minimum = target / 2u;
    if (minimum < 64u) minimum = 64u;
    u64 maximum = target > U64_MAX_VALUE / 2u ? U64_MAX_VALUE : target * 2u;
    u64 threshold = 1u;
    while (threshold <= target / 2u && threshold < (1ull << 62u)) threshold <<= 1u;
    u64 mask = threshold - 1u;
    u64 rolling = 0u;
    usize cursor = start;
    while (cursor < length) {
        u64 value = (u64)data[cursor] + 1u;
        value *= 0x9E3779B97F4A7C15ull;
        value ^= value >> 30u;
        value *= 0xBF58476D1CE4E5B9ull;
        value ^= value >> 27u;
        value *= 0x94D049BB133111EBull;
        value ^= value >> 31u;
        rolling = (rolling << 1u) + value;
        ++cursor;
        u64 chunk_length = (u64)(cursor - start);
        if (chunk_length >= minimum && ((rolling & mask) == 0u || chunk_length >= maximum)) break;
    }
    return cursor;
}

static bool checkpoint_append_u64(makocode::ByteBuffer& buffer, u64 value) {
    u8 bytes[8];
    write_le_u64(bytes, value);
    return buffer.append_bytes(bytes, sizeof(bytes));
}

static bool checkpoint_read_u64(const u8* data, usize size, usize& cursor, u64& value) {
    if (cursor > size || size - cursor < 8u) return false;
    value = read_le_u64(data + cursor);
    cursor += 8u;
    return true;
}

static bool checkpoint_parse_size(const char* text, u64& value_out) {
    if (!text || !*text) return false;
    usize length = ascii_length(text);
    u64 multiplier = 1u;
    usize digits = length;
    char suffix = text[length - 1u];
    if (suffix == 'k' || suffix == 'K') { multiplier = 1024u; --digits; }
    else if (suffix == 'm' || suffix == 'M') { multiplier = 1024u * 1024u; --digits; }
    else if (suffix == 'g' || suffix == 'G') { multiplier = 1024u * 1024u * 1024u; --digits; }
    if (!digits) return false;
    u64 value = 0u;
    for (usize i = 0u; i < digits; ++i) {
        char c = text[i];
        if (c < '0' || c > '9') return false;
        u64 digit = (u64)(c - '0');
        if (value > (U64_MAX_VALUE - digit) / 10u) return false;
        value = value * 10u + digit;
    }
    if (value > U64_MAX_VALUE / multiplier) return false;
    value *= multiplier;
    if (value < CHECKPOINT_MIN_CHUNK_SIZE || value > CHECKPOINT_MAX_CHUNK_SIZE) return false;
    value_out = value;
    return true;
}

static bool checkpoint_scan_tree(const char* fs_path,
                                 const char* relative_path,
                                 bool snapshot_input,
                                 u64 chunk_size,
                                 CheckpointChunkIndex& base_chunks,
                                 CheckpointChunkIndex& bundle_chunks,
                                 CheckpointChunkIndex& entry_paths,
                                 makocode::ByteBuffer& bundle,
                                 u64& entry_count) {
    MakoPlatformFileInfo info;
    if (makocode_platform_stat(fs_path, &info) != 0) {
        console_write(2, "checkpoint: unable to stat ");
        console_line(2, fs_path);
        return false;
    }
    if (info.is_directory) {
        if (snapshot_input) {
            const char* path = relative_path ? relative_path : "";
            usize path_length = ascii_length(path);
            u8 path_id[32];
            checkpoint_path_id(path, path_length, path_id);
            if (!path_length || !is_safe_archive_path(path, path_length) || entry_paths.find(path_id)) {
                console_write(2, "checkpoint: invalid or duplicate path ");
                console_line(2, path);
                return false;
            }
            if (!entry_paths.insert(path_id, 0u, 0, 0u, 0u, 0u)) return false;
            if (!bundle.append_char((char)1u) ||
                !checkpoint_append_u64(bundle, (u64)path_length) ||
                !bundle.append_bytes((const u8*)path, path_length)) return false;
            ++entry_count;
        }
        MakoPlatformDirectory* directory = makocode_platform_open_directory(fs_path);
        if (!directory) {
            console_write(2, "checkpoint: failed to open directory ");
            console_line(2, fs_path);
            return false;
        }
        MakoPlatformDirectoryEntry entry;
        int read_result = 0;
        while ((read_result = makocode_platform_read_directory(directory, &entry)) > 0) {
            const char* name = entry.name;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;
            makocode::ByteBuffer child_fs;
            makocode::ByteBuffer child_relative;
            if (!join_fs_path(fs_path, name, child_fs) ||
                !join_rel_path(relative_path, name, child_relative) ||
                !checkpoint_scan_tree((const char*)child_fs.data,
                                      (const char*)child_relative.data,
                                      snapshot_input,
                                      chunk_size,
                                      base_chunks,
                                      bundle_chunks,
                                      entry_paths,
                                      bundle,
                                      entry_count)) {
                makocode_platform_close_directory(directory);
                return false;
            }
        }
        makocode_platform_close_directory(directory);
        return read_result == 0;
    }
    if (!info.is_regular) {
        console_write(2, "checkpoint: unsupported entry type for ");
        console_line(2, relative_path ? relative_path : fs_path);
        return false;
    }
    makocode::ByteBuffer file_data;
    if (!read_entire_file(fs_path, file_data)) {
        console_write(2, "checkpoint: failed to read ");
        console_line(2, fs_path);
        return false;
    }
    usize cursor = 0u;
    makocode::ByteBuffer refs;
    while (cursor < file_data.size) {
        usize end = checkpoint_chunk_end(file_data.data, cursor, file_data.size, chunk_size);
        if (end <= cursor) return false;
        usize chunk_length = end - cursor;
        u8 id[32];
        checkpoint_chunk_id(file_data.data + cursor, chunk_length, id);
        if (!snapshot_input) {
            if (!base_chunks.insert(id, 0u, 0, 0u, 0u, 0u)) return false;
        } else {
            CheckpointChunkRef ref;
            memcpy(ref.id, id, sizeof(ref.id));
            ref.length = chunk_length;
            ref.kind = base_chunks.find(id) ? 0u : bundle_chunks.find(id) ? 2u : 1u;
            if (ref.kind == 1u && !bundle_chunks.insert(id, 0u, 0, 0u, 0u, 0u)) return false;
            if (!refs.ensure(refs.size + sizeof(ref))) return false;
            memcpy(refs.data + refs.size, &ref, sizeof(ref));
            refs.size += sizeof(ref);
        }
        cursor = end;
    }
    if (snapshot_input) {
        const char* path = relative_path ? relative_path : "";
        usize path_length = ascii_length(path);
        u8 path_id[32];
        checkpoint_path_id(path, path_length, path_id);
        if (!path_length || !is_safe_archive_path(path, path_length) || entry_paths.find(path_id)) {
            console_write(2, "checkpoint: invalid or duplicate path ");
            console_line(2, path);
            return false;
        }
        if (!entry_paths.insert(path_id, 0u, 0, 0u, 0u, 0u)) return false;
        if (!bundle.append_char((char)0u) ||
            !checkpoint_append_u64(bundle, (u64)path_length) ||
            !bundle.append_bytes((const u8*)path, path_length) ||
            !checkpoint_append_u64(bundle, (u64)file_data.size) ||
            !checkpoint_append_u64(bundle, (u64)(refs.size / sizeof(CheckpointChunkRef)))) return false;
        cursor = 0u;
        CheckpointChunkRef* ref_list = (CheckpointChunkRef*)refs.data;
        for (usize i = 0u; i < refs.size / sizeof(CheckpointChunkRef); ++i) {
            const CheckpointChunkRef& ref = ref_list[i];
            if (!bundle.append_bytes(ref.id, sizeof(ref.id)) ||
                !checkpoint_append_u64(bundle, (u64)ref.length) ||
                !bundle.append_char((char)ref.kind)) return false;
            if (ref.kind == 1u && !bundle.append_bytes(file_data.data + cursor, ref.length)) return false;
            cursor += ref.length;
        }
        ++entry_count;
    }
    return true;
}

static void checkpoint_append_hex(makocode::ByteBuffer& output, const u8* data, usize length) {
    static const char digits[] = "0123456789abcdef";
    for (usize i = 0u; i < length; ++i) {
        output.append_char(digits[data[i] >> 4u]);
        output.append_char(digits[data[i] & 15u]);
    }
}

static bool checkpoint_entry_add_ref(CheckpointEntry& entry, const CheckpointChunkRef& ref) {
    if (entry.chunk_count == entry.chunk_capacity) {
        usize next = entry.chunk_capacity ? entry.chunk_capacity * 2u : 8u;
        if (next <= entry.chunk_capacity || next > USIZE_MAX_VALUE / sizeof(CheckpointChunkRef)) return false;
        CheckpointChunkRef* resized = (CheckpointChunkRef*)makocode_platform_realloc(entry.chunks, next * sizeof(CheckpointChunkRef));
        if (!resized) return false;
        entry.chunks = resized;
        entry.chunk_capacity = next;
    }
    entry.chunks[entry.chunk_count++] = ref;
    return true;
}

static bool checkpoint_scan_base_for_restore(const char* fs_path,
                                             u64 chunk_size,
                                             CheckpointChunkIndex& chunks) {
    MakoPlatformFileInfo info;
    if (makocode_platform_stat(fs_path, &info) != 0) return false;
    if (info.is_directory) {
        MakoPlatformDirectory* directory = makocode_platform_open_directory(fs_path);
        if (!directory) return false;
        MakoPlatformDirectoryEntry entry;
        int read_result = 0;
        while ((read_result = makocode_platform_read_directory(directory, &entry)) > 0) {
            const char* name = entry.name;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) continue;
            makocode::ByteBuffer child;
            if (!join_fs_path(fs_path, name, child) ||
                !checkpoint_scan_base_for_restore((const char*)child.data, chunk_size, chunks)) {
                makocode_platform_close_directory(directory);
                return false;
            }
        }
        makocode_platform_close_directory(directory);
        return read_result == 0;
    }
    if (!info.is_regular) return true;
    makocode::ByteBuffer file_data;
    if (!read_entire_file(fs_path, file_data)) return false;
    usize cursor = 0u;
    while (cursor < file_data.size) {
        usize end = checkpoint_chunk_end(file_data.data, cursor, file_data.size, chunk_size);
        if (end <= cursor) return false;
        u8 id[32];
        checkpoint_chunk_id(file_data.data + cursor, end - cursor, id);
        if (!chunks.find(id) && !chunks.insert(id, 1u, fs_path, cursor, end - cursor, 0u)) return false;
        cursor = end;
    }
    return true;
}

static bool checkpoint_parse_bundle(const makocode::ByteBuffer& bundle,
                                   u64& chunk_size,
                                   CheckpointEntryList& entries,
                                   CheckpointChunkIndex& bundled_chunks,
                                   bool& incomplete) {
    incomplete = false;
    if (bundle.size < sizeof(CHECKPOINT_MAGIC) + 16u ||
        memcmp(bundle.data, CHECKPOINT_MAGIC, sizeof(CHECKPOINT_MAGIC)) != 0) {
        console_line(2, "checkpoint: invalid bundle header");
        return false;
    }
    usize cursor = sizeof(CHECKPOINT_MAGIC);
    u64 entry_count = 0u;
    if (!checkpoint_read_u64(bundle.data, bundle.size, cursor, chunk_size) ||
        !checkpoint_read_u64(bundle.data, bundle.size, cursor, entry_count) ||
        chunk_size < CHECKPOINT_MIN_CHUNK_SIZE || chunk_size > CHECKPOINT_MAX_CHUNK_SIZE) {
        console_line(2, "checkpoint: invalid bundle settings");
        return false;
    }
    u64 parsed_entries = 0u;
    for (u64 entry_index = 0u; entry_index < entry_count && cursor < bundle.size; ++entry_index) {
        CheckpointEntry entry;
        entry.type = bundle.data[cursor++];
        u64 path_length = 0u;
        if (entry.type > 1u || !checkpoint_read_u64(bundle.data, bundle.size, cursor, path_length) ||
            path_length == 0u || path_length > (u64)(bundle.size - cursor)) {
            incomplete = true;
            break;
        }
        if (path_length > (u64)(USIZE_MAX_VALUE - 1u)) { incomplete = true; break; }
        entry.path = (char*)makocode_platform_malloc((usize)path_length + 1u);
        if (!entry.path) return false;
        memcpy(entry.path, bundle.data + cursor, (usize)path_length);
        entry.path[path_length] = 0;
        entry.path_length = (usize)path_length;
        cursor += (usize)path_length;
        if (!is_safe_archive_path(entry.path, entry.path_length)) {
            makocode_platform_free(entry.path);
            incomplete = true;
            break;
        }
        if (entry.type == 0u) {
            u64 file_size = 0u;
            u64 chunk_count = 0u;
            if (!checkpoint_read_u64(bundle.data, bundle.size, cursor, file_size) ||
                !checkpoint_read_u64(bundle.data, bundle.size, cursor, chunk_count)) {
                entry.manifest_complete = false;
                if (!entries.push(entry)) return false;
                incomplete = true;
                break;
            }
            entry.file_size = file_size;
            u64 described_size = 0u;
            for (u64 chunk_index = 0u; chunk_index < chunk_count; ++chunk_index) {
                if (cursor > bundle.size || bundle.size - cursor < 41u) {
                    entry.manifest_complete = false;
                    incomplete = true;
                    break;
                }
                CheckpointChunkRef ref;
                memcpy(ref.id, bundle.data + cursor, 32u);
                cursor += 32u;
                u64 ref_length = 0u;
                if (!checkpoint_read_u64(bundle.data, bundle.size, cursor, ref_length) ||
                    ref_length == 0u || ref_length > (u64)USIZE_MAX_VALUE || cursor >= bundle.size) {
                    entry.manifest_complete = false;
                    incomplete = true;
                    break;
                }
                ref.length = (usize)ref_length;
                ref.kind = bundle.data[cursor++];
                if (ref.kind > 2u || ref.length > U64_MAX_VALUE - described_size) {
                    entry.manifest_complete = false;
                    incomplete = true;
                    break;
                }
                described_size += ref.length;
                if (ref.kind == 1u) {
                    ref.bundle_offset = cursor;
                    if (ref.length > bundle.size - cursor) {
                        entry.manifest_complete = false;
                        incomplete = true;
                        if (!checkpoint_entry_add_ref(entry, ref)) return false;
                        cursor = bundle.size;
                        break;
                    }
                    if (!bundled_chunks.insert(ref.id, 2u, 0, 0u, ref.length, cursor)) return false;
                    cursor += ref.length;
                }
                if (!checkpoint_entry_add_ref(entry, ref)) return false;
            }
            if (described_size != entry.file_size) {
                entry.manifest_complete = false;
                incomplete = true;
            }
            if (!entries.push(entry)) return false;
            if (!entry.manifest_complete) break;
        } else {
            if (!entries.push(entry)) return false;
        }
        ++parsed_entries;
    }
    if (parsed_entries != entry_count) incomplete = true;
    return entries.count > 0u;
}

int command_checkpoint(int arg_count, char** args) {
    if (arg_count == 0 || arguments_request_help(arg_count, args)) {
        console_line(1, "makocode checkpoint");
        console_line(1, "Usage:");
        console_line(1, "  makocode checkpoint create --input PATH ... [--base DIR ...] --output FILE [--chunk-size SIZE]");
        console_line(1, "  makocode checkpoint restore --input FILE [--base DIR ...] --output DIR");
        console_line(1, "Creates an incremental snapshot bundle or restores available file chunks.");
        console_line(1, "Chunk sizes accept bytes or K/M/G suffixes (default 4M; range 256 bytes to 1G).");
        return 0;
    }
    bool create = ascii_compare(args[0], "create") == 0;
    bool restore = ascii_compare(args[0], "restore") == 0;
    if (!create && !restore) {
        console_line(2, "checkpoint: expected create or restore");
        return 1;
    }
    static const usize MAX_CHECKPOINT_PATHS = 512u;
    const char* input_paths[MAX_CHECKPOINT_PATHS];
    const char* base_paths[MAX_CHECKPOINT_PATHS];
    usize input_count = 0u;
    usize base_count = 0u;
    const char* output_path = 0;
    u64 chunk_size = CHECKPOINT_DEFAULT_CHUNK_SIZE;
    for (int i = 1; i < arg_count; ++i) {
        const char* arg = args[i];
        usize length = ascii_length(arg);
        const char* value = 0;
        enum Option { NONE, INPUT, BASE, OUTPUT, CHUNK_SIZE } option = NONE;
        if (ascii_equals_token(arg, length, "--input")) option = INPUT;
        else if (ascii_equals_token(arg, length, "--base")) option = BASE;
        else if (ascii_equals_token(arg, length, "--output")) option = OUTPUT;
        else if (ascii_equals_token(arg, length, "--chunk-size")) option = CHUNK_SIZE;
        else {
            const char* equal = 0;
            for (usize j = 0u; j < length; ++j) if (arg[j] == '=') { equal = arg + j; break; }
            if (equal) {
                usize name_length = (usize)(equal - arg);
                value = equal + 1;
                if (ascii_equals_token(arg, name_length, "--input")) option = INPUT;
                else if (ascii_equals_token(arg, name_length, "--base")) option = BASE;
                else if (ascii_equals_token(arg, name_length, "--output")) option = OUTPUT;
                else if (ascii_equals_token(arg, name_length, "--chunk-size")) option = CHUNK_SIZE;
            }
        }
        if (option == NONE) {
            console_write(2, "checkpoint: unknown option ");
            console_line(2, arg);
            return 1;
        }
        if (!value) {
            if (i + 1 >= arg_count || !args[i + 1][0]) {
                console_line(2, "checkpoint: option requires a value");
                return 1;
            }
            value = args[++i];
        }
        if (option == INPUT) {
            if (input_count >= MAX_CHECKPOINT_PATHS) return 1;
            input_paths[input_count++] = value;
        } else if (option == BASE) {
            if (base_count >= MAX_CHECKPOINT_PATHS) return 1;
            base_paths[base_count++] = value;
        } else if (option == OUTPUT) {
            if (output_path) {
                console_line(2, "checkpoint: --output may be supplied once");
                return 1;
            }
            output_path = value;
        } else if (!checkpoint_parse_size(value, chunk_size)) {
            console_line(2, "checkpoint: invalid --chunk-size");
            return 1;
        }
    }
    if (!output_path || (restore && input_count != 1u) || (create && input_count == 0u)) {
        console_line(2, "checkpoint: required input or output is missing");
        return 1;
    }
    if (create) {
        makocode::ByteBuffer bundle;
        if (!bundle.append_bytes(CHECKPOINT_MAGIC, sizeof(CHECKPOINT_MAGIC)) ||
            !checkpoint_append_u64(bundle, chunk_size) || !checkpoint_append_u64(bundle, 0u)) {
            console_line(2, "checkpoint: failed to initialize bundle");
            return 1;
        }
        CheckpointChunkIndex base_chunks;
        CheckpointChunkIndex bundle_chunks;
        CheckpointChunkIndex entry_paths;
        CheckpointChunkIndex unused_bundle_chunks;
        u64 ignored_count = 0u;
        for (usize i = 0u; i < base_count; ++i) {
            makocode::ByteBuffer normalized;
            if (!normalize_store_path(base_paths[i], normalized) ||
                !checkpoint_scan_tree(base_paths[i], (const char*)normalized.data, false, chunk_size,
                                      base_chunks, unused_bundle_chunks, entry_paths, bundle, ignored_count)) {
                console_line(2, "checkpoint: failed to scan base data");
                return 1;
            }
        }
        u64 entry_count = 0u;
        for (usize i = 0u; i < input_count; ++i) {
            makocode::ByteBuffer normalized;
            if (!normalize_store_path(input_paths[i], normalized) ||
                !checkpoint_scan_tree(input_paths[i], (const char*)normalized.data, true, chunk_size,
                                      base_chunks, bundle_chunks, entry_paths, bundle, entry_count)) {
                console_line(2, "checkpoint: failed to build snapshot");
                return 1;
            }
        }
        write_le_u64(bundle.data + sizeof(CHECKPOINT_MAGIC) + 8u, entry_count);
        if (!ensure_parent_directories(output_path) ||
            !write_bytes_to_file(output_path, bundle.data, bundle.size)) {
            console_line(2, "checkpoint: failed to write bundle");
            return 1;
        }
        console_write(1, "checkpoint: wrote ");
        console_line(1, output_path);
        return 0;
    }

    makocode::ByteBuffer bundle;
    if (!read_entire_file(input_paths[0], bundle)) {
        console_line(2, "checkpoint: failed to read bundle");
        return 1;
    }
    CheckpointEntryList entries;
    CheckpointChunkIndex bundled_chunks;
    bool bundle_incomplete = false;
    if (!checkpoint_parse_bundle(bundle, chunk_size, entries, bundled_chunks, bundle_incomplete)) {
        if (bundle.size < sizeof(CHECKPOINT_MAGIC) ||
            memcmp(bundle.data, CHECKPOINT_MAGIC, sizeof(CHECKPOINT_MAGIC)) != 0 ||
            !ensure_directory_tree(output_path)) return 1;
        const char unmapped_name[] = "checkpoint_unmapped.partial";
        const char report_name[] = "checkpoint_recovery.txt";
        const char message[] = "No complete snapshot records were recoverable; raw bundle bytes are preserved in checkpoint_unmapped.partial.\n";
        makocode::ByteBuffer unmapped_path;
        makocode::ByteBuffer report_path;
        if (!join_output_path(output_path, unmapped_name, unmapped_path) ||
            !write_bytes_to_file((const char*)unmapped_path.data, bundle.data, bundle.size) ||
            !join_output_path(output_path, report_name, report_path) ||
            !write_bytes_to_file((const char*)report_path.data, (const u8*)message, sizeof(message) - 1u)) return 1;
        console_line(2, "checkpoint: preserved raw bundle bytes; no file records were recoverable");
        return 0;
    }
    CheckpointChunkIndex base_chunks;
    for (usize i = 0u; i < base_count; ++i) {
        if (!checkpoint_scan_base_for_restore(base_paths[i], chunk_size, base_chunks)) {
            console_write(2, "checkpoint: failed to scan base ");
            console_line(2, base_paths[i]);
            return 1;
        }
    }
    if (!ensure_directory_tree(output_path)) {
        console_line(2, "checkpoint: failed to create output directory");
        return 1;
    }
    makocode::ByteBuffer report;
    bool any_partial = bundle_incomplete;
    if (bundle_incomplete) report.append_ascii("bundle manifest truncated; later entries may be unavailable\n");
    for (usize entry_index = 0u; entry_index < entries.count; ++entry_index) {
        CheckpointEntry& entry = entries.entries[entry_index];
        if (entry.type == 1u) {
            makocode::ByteBuffer directory_path;
            if (!join_output_path(output_path, entry.path, directory_path) ||
                !ensure_directory_tree((const char*)directory_path.data)) return 1;
            continue;
        }
        if (entry.file_size > (u64)USIZE_MAX_VALUE) return 1;
        makocode::ByteBuffer output_file;
        if (!output_file.ensure((usize)entry.file_size)) return 1;
        output_file.size = (usize)entry.file_size;
        if (output_file.size) memset(output_file.data, 0, output_file.size);
        usize file_offset = 0u;
        bool file_partial = !entry.manifest_complete;
        makocode::ByteBuffer source_file;
        makocode::ByteBuffer cached_source_path;
        for (usize chunk_index = 0u; chunk_index < entry.chunk_count; ++chunk_index) {
            const CheckpointChunkRef& ref = entry.chunks[chunk_index];
            const u8* chunk_data = 0;
            usize source_length = 0u;
            CheckpointIndexEntry* bundled = bundled_chunks.find(ref.id);
            if (bundled && bundled->source.source_kind == 2u &&
                bundled->source.bundle_offset <= bundle.size &&
                bundled->source.length <= bundle.size - bundled->source.bundle_offset) {
                chunk_data = bundle.data + bundled->source.bundle_offset;
                source_length = bundled->source.length;
            } else {
                CheckpointIndexEntry* base = base_chunks.find(ref.id);
                const char* source_path = base ? base_chunks.source_path(*base) : 0;
                usize source_path_length = source_path ? ascii_length(source_path) : 0u;
                bool same_cached_path = source_path && cached_source_path.size == source_path_length + 1u &&
                    memcmp(cached_source_path.data, source_path, source_path_length) == 0;
                if (base && source_path && base->source.source_kind == 1u) {
                    bool source_ready = same_cached_path;
                    if (!source_ready && read_entire_file(source_path, source_file) &&
                        cached_source_path.ensure(source_path_length + 1u)) {
                        memcpy(cached_source_path.data, source_path, source_path_length);
                        cached_source_path.data[source_path_length] = 0u;
                        cached_source_path.size = source_path_length + 1u;
                        source_ready = true;
                    }
                    if (source_ready && base->source.offset <= source_file.size &&
                        base->source.length <= source_file.size - base->source.offset) {
                        chunk_data = source_file.data + base->source.offset;
                        source_length = base->source.length;
                    }
                }
            }
            u8 actual_id[32];
            if (chunk_data && source_length == ref.length) checkpoint_chunk_id(chunk_data, ref.length, actual_id);
            bool chunk_ok = chunk_data && source_length == ref.length && memcmp(actual_id, ref.id, 32u) == 0;
            if (chunk_ok && file_offset <= output_file.size &&
                ref.length <= output_file.size - file_offset) {
                memcpy(output_file.data + file_offset, chunk_data, ref.length);
            } else {
                file_partial = true;
                any_partial = true;
                makocode::ByteBuffer line;
                line.append_ascii("missing ");
                checkpoint_append_hex(line, (const u8*)entry.path, entry.path_length);
                line.append_ascii(" offset=");
                char number[32];
                u64_to_ascii((u64)file_offset, number, sizeof(number));
                line.append_ascii(number);
                line.append_ascii(" length=");
                u64_to_ascii((u64)ref.length, number, sizeof(number));
                line.append_ascii(number);
                line.append_ascii(" chunk=");
                checkpoint_append_hex(line, ref.id, sizeof(ref.id));
                line.append_char('\n');
                report.append_bytes(line.data, line.size);
            }
            if (ref.length > USIZE_MAX_VALUE - file_offset) {
                file_partial = true;
                any_partial = true;
                break;
            }
            file_offset += ref.length;
        }
        if (file_offset != output_file.size) {
            file_partial = true;
            any_partial = true;
            if (file_offset < output_file.size) {
                makocode::ByteBuffer line;
                line.append_ascii("missing ");
                checkpoint_append_hex(line, (const u8*)entry.path, entry.path_length);
                line.append_ascii(" offset=");
                char number[32];
                u64_to_ascii((u64)file_offset, number, sizeof(number));
                line.append_ascii(number);
                line.append_ascii(" length=");
                u64_to_ascii((u64)(output_file.size - file_offset), number, sizeof(number));
                line.append_ascii(number);
                line.append_ascii(" manifest-tail\n");
                report.append_bytes(line.data, line.size);
            }
        }
        if (file_partial) any_partial = true;
        makocode::ByteBuffer destination;
        if (!destination.append_bytes((const u8*)entry.path, entry.path_length) ||
            (file_partial && !destination.append_ascii(".partial")) ||
            !destination.append_char('\0')) return 1;
        makocode::ByteBuffer destination_path;
        if (!join_output_path(output_path, (const char*)destination.data, destination_path) ||
            !ensure_parent_directories((const char*)destination_path.data) ||
            !write_bytes_to_file((const char*)destination_path.data, output_file.data, output_file.size)) {
            console_line(2, "checkpoint: failed to write restored file");
            return 1;
        }
    }
    if (any_partial) {
        const char report_name[] = "checkpoint_recovery.txt";
        const char heading[] = "Partial checkpoint recovery; missing ranges are zero-filled. File paths are hex encoded.\n";
        makocode::ByteBuffer report_path;
        makocode::ByteBuffer complete_report;
        complete_report.append_bytes((const u8*)heading, sizeof(heading) - 1u);
        complete_report.append_bytes(report.data, report.size);
        if (!join_output_path(output_path, report_name, report_path) ||
            !write_bytes_to_file((const char*)report_path.data, complete_report.data, complete_report.size)) return 1;
        console_line(2, "checkpoint: restored available chunks; see checkpoint_recovery.txt");
    } else {
        console_line(1, "checkpoint: restored snapshot");
    }
    return 0;
}

