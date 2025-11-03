/*
    test.cpp
    --------
    Minimal regression test for makocode. Builds without including any headers
    by declaring the small set of libc entry points we need directly.
*/

typedef unsigned char      u8;
typedef unsigned long      usize;
typedef unsigned long long u64;

extern "C" void* malloc(unsigned long size);
extern "C" void  free(void* ptr);
extern "C" int   write(int fd, const void* buf, unsigned long count);
extern "C" int   read(int fd, void* buf, unsigned long count);
extern "C" int   close(int fd);
extern "C" int   open(const char* path, int flags, ...);
extern "C" int   unlink(const char* path);
extern "C" int   system(const char* command);
extern "C" int   creat(const char* path, unsigned int mode);

static const int O_RDONLY = 0;

static usize ascii_length(const char* text) {
    if (!text) {
        return 0u;
    }
    const char* cursor = text;
    while (*cursor) {
        ++cursor;
    }
    return (usize)(cursor - text);
}

static void console_line(const char* text) {
    if (text) {
        write(1, text, ascii_length(text));
    }
    write(1, "\n", 1u);
}

struct ByteBuffer {
    u8*   data;
    usize size;
    usize capacity;

    ByteBuffer() : data(0), size(0u), capacity(0u) {}

    ~ByteBuffer() {
        release();
    }

    void release() {
        if (data) {
            free(data);
            data = 0;
        }
        size = 0u;
        capacity = 0u;
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
        u8* next = (u8*)malloc(grow);
        if (!next) {
            return false;
        }
        for (usize i = 0; i < size; ++i) {
            next[i] = data[i];
        }
        free(data);
        data = next;
        capacity = grow;
        return true;
    }

    bool push(u8 value) {
        if (!ensure(size + 1u)) {
            return false;
        }
        data[size++] = value;
        return true;
    }
};

static void fill_random(ByteBuffer& buffer, usize bytes) {
    buffer.size = 0u;
    buffer.ensure(bytes);
    u64 state = 0x1234abcdefULL;
    for (usize i = 0; i < bytes; ++i) {
        state = state * 6364136223846793005ULL + 1ULL;
        buffer.data[i] = (u8)(state >> 24);
    }
    buffer.size = bytes;
}

static bool write_all(int fd, const u8* data, usize length) {
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

static bool write_file(const char* path, const ByteBuffer& buffer) {
    int fd = creat(path, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = write_all(fd, buffer.data, buffer.size);
    close(fd);
    return ok;
}

static bool read_file(const char* path, ByteBuffer& buffer) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return false;
    }
    buffer.size = 0u;
    const usize chunk = 4096u;
    for (;;) {
        if (!buffer.ensure(buffer.size + chunk)) {
            close(fd);
            return false;
        }
        int result = read(fd, buffer.data + buffer.size, chunk);
        if (result < 0) {
            close(fd);
            return false;
        }
        if (result == 0) {
            break;
        }
        buffer.size += (usize)result;
    }
    close(fd);
    return true;
}

static bool buffers_equal(const ByteBuffer& a, const ByteBuffer& b) {
    if (a.size != b.size) {
        return false;
    }
    for (usize i = 0u; i < a.size; ++i) {
        if (a.data[i] != b.data[i]) {
            return false;
        }
    }
    return true;
}

static bool run_command(const char* command) {
    int rc = system(command);
    return (rc == 0);
}

static void cleanup_files() {
    unlink("payload.bin");
    unlink("encoded.ppm");
    unlink("decoded.bin");
}

int main() {
    ByteBuffer payload;
    ByteBuffer decoded;

    fill_random(payload, 1024u);

    if (!write_file("payload.bin", payload)) {
        console_line("test: failed to write payload.bin");
        cleanup_files();
        return 1;
    }

    if (!run_command("./makocode encode < payload.bin > encoded.ppm")) {
        console_line("test: encode command failed");
        cleanup_files();
        return 1;
    }

    if (!run_command("./makocode decode < encoded.ppm > decoded.bin")) {
        console_line("test: decode command failed");
        cleanup_files();
        return 1;
    }

    if (!read_file("decoded.bin", decoded)) {
        console_line("test: failed to read decoded.bin");
        cleanup_files();
        return 1;
    }

    if (!buffers_equal(payload, decoded)) {
        console_line("test: roundtrip mismatch");
        cleanup_files();
        return 1;
    }

    console_line("test: roundtrip ok");
    cleanup_files();
    return 0;
}
