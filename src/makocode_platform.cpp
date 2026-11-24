/*
 * POSIX host services behind the platform interface.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_platform.hpp"

#if INCLUDES
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif

#if INCLUDES
struct MakoPlatformDirectory {
    DIR* handle;
};

namespace {

volatile sig_atomic_t password_prompt_signal = 0;

void password_prompt_signal_handler(int signal_number) {
    password_prompt_signal = signal_number;
}

bool write_terminal_text(int fd, const char* text, size_t length) {
    size_t written = 0u;
    while (written < length) {
        ssize_t result = ::write(fd, text + written, length - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        written += (size_t)result;
    }
    return true;
}

} // namespace

#ifdef MAKOCODE_TEST_BUILD
extern "C" void* makocode_test_malloc(size_t size);
#endif

extern "C" void* makocode_platform_malloc(size_t size) {
#ifdef MAKOCODE_TEST_BUILD
    return makocode_test_malloc(size);
#else
    return ::malloc(size);
#endif
}

extern "C" void* makocode_platform_realloc(void* ptr, size_t size) {
    return ::realloc(ptr, size);
}

extern "C" void makocode_platform_free(void* ptr) {
    ::free(ptr);
}

extern "C" ptrdiff_t makocode_platform_read(int fd, void* buffer, size_t count) {
    ssize_t result;
    do {
        result = ::read(fd, buffer, count);
    } while (result < 0 && errno == EINTR);
    return (ptrdiff_t)result;
}

extern "C" ptrdiff_t makocode_platform_write(int fd, const void* buffer, size_t count) {
    ssize_t result;
    do {
        result = ::write(fd, buffer, count);
    } while (result < 0 && errno == EINTR);
    return (ptrdiff_t)result;
}

extern "C" int makocode_platform_open(const char* path, int flags, ...) {
#ifdef MAKOCODE_TEST_BUILD
    const char* failed_path = ::getenv("MAKOCODE_TEST_FAIL_OPEN_FILE");
    if ((flags & MAKOCODE_OPEN_READ) && failed_path && path &&
        ::strcmp(failed_path, path) == 0) {
        errno = EACCES;
        return -1;
    }
#endif
    int native_flags = 0;
    if (flags & MAKOCODE_OPEN_READ) native_flags |= O_RDONLY;
    if (flags & MAKOCODE_OPEN_WRITE) native_flags |= O_WRONLY;
    if (flags & MAKOCODE_OPEN_CREATE) native_flags |= O_CREAT;
    if (flags & MAKOCODE_OPEN_TRUNCATE) native_flags |= O_TRUNC;
    if (flags & MAKOCODE_OPEN_EXCLUSIVE) native_flags |= O_EXCL;
    if (flags & MAKOCODE_OPEN_CREATE) {
        va_list args;
        va_start(args, flags);
        int mode = va_arg(args, int);
        va_end(args);
        return ::open(path, native_flags, (mode_t)mode);
    }
    return ::open(path, native_flags);
}

extern "C" int makocode_platform_create_file(const char* path, int mode) {
    return ::creat(path, (mode_t)mode);
}

extern "C" int makocode_platform_close(int fd) {
    return ::close(fd);
}

extern "C" int makocode_platform_unlink(const char* path) {
    return ::unlink(path);
}

extern "C" int makocode_platform_stat(const char* path, MakoPlatformFileInfo* info) {
    if (!info) return -1;
    struct stat native_info;
    if (::stat(path, &native_info) != 0) return -1;
    info->is_directory = S_ISDIR(native_info.st_mode);
    info->is_regular = S_ISREG(native_info.st_mode);
    return 0;
}

extern "C" int makocode_platform_is_symlink(const char* path) {
    if (!path) return -2;
    struct stat native_info;
    if (::lstat(path, &native_info) != 0) {
        return errno == ENOENT || errno == ENOTDIR ? -1 : -2;
    }
    return S_ISLNK(native_info.st_mode) ? 1 : 0;
}

extern "C" int makocode_platform_create_directory(const char* path) {
#ifdef MAKOCODE_TEST_BUILD
    const char* race_path = ::getenv("MAKOCODE_TEST_CREATE_DIR_RACE_PATH");
    if (race_path && path && ::strcmp(path, race_path) == 0 &&
        ::mkdir(path, 0700) == 0) {
        return 1;
    }
#endif
    int result = ::mkdir(path, 0700);
#ifdef MAKOCODE_TEST_BUILD
    if (result == 0 && path) {
        const char* swap_path = ::getenv("MAKOCODE_TEST_SWAP_DIR_PATH");
        const char* backup_path = ::getenv("MAKOCODE_TEST_SWAP_DIR_BACKUP");
        const char* target_path = ::getenv("MAKOCODE_TEST_SWAP_DIR_TARGET");
        if (swap_path && backup_path && target_path && ::strcmp(path, swap_path) == 0) {
            if (::rename(swap_path, backup_path) == 0) {
                ::symlink(target_path, swap_path);
            }
        }
    }
#endif
    if (result == 0) return 0;
    return errno == EEXIST ? 1 : -1;
}

extern "C" MakoPlatformDirectory* makocode_platform_open_directory(const char* path) {
#ifdef MAKOCODE_TEST_BUILD
    const char* failed_path = ::getenv("MAKOCODE_TEST_FAIL_OPEN_DIRECTORY");
    if (failed_path && path && ::strcmp(failed_path, path) == 0) return 0;
#endif
    DIR* handle = ::opendir(path);
    if (!handle) return 0;
    MakoPlatformDirectory* directory =
        (MakoPlatformDirectory*)::malloc(sizeof(MakoPlatformDirectory));
    if (!directory) {
        ::closedir(handle);
        return 0;
    }
    directory->handle = handle;
    return directory;
}

extern "C" int makocode_platform_read_directory(MakoPlatformDirectory* directory,
                                                 MakoPlatformDirectoryEntry* entry) {
    if (!directory || !entry) return -1;
    errno = 0;
    struct dirent* native_entry = ::readdir(directory->handle);
    if (!native_entry) return errno ? -1 : 0;
    size_t length = strlen(native_entry->d_name);
    if (length >= sizeof(entry->name)) return -1;
    memcpy(entry->name, native_entry->d_name, length + 1u);
    return 1;
}

extern "C" void makocode_platform_close_directory(MakoPlatformDirectory* directory) {
    if (!directory) return;
    ::closedir(directory->handle);
    ::free(directory);
}

extern "C" char* makocode_platform_realpath(const char* path) {
    return ::realpath(path, 0);
}

extern "C" const char* makocode_platform_getenv(const char* name) {
    return ::getenv(name);
}

extern "C" int makocode_platform_snprintf(char* buffer,
                                             size_t size,
                                             const char* format,
                                             ...) {
    va_list args;
    va_start(args, format);
    int result = ::vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

extern "C" bool makocode_platform_utc_timestamp(char* buffer, size_t capacity) {
    if (!buffer || !capacity) return false;
    time_t now = ::time(0);
    struct tm utc;
    if (!::gmtime_r(&now, &utc)) return false;
    size_t length = ::strftime(buffer, capacity, "%Y%m%dT%H%M%SZ", &utc);
    return length > 0u && length < capacity;
}

extern "C" bool makocode_platform_local_timestamp(char* buffer, size_t capacity) {
    if (!buffer || !capacity) return false;
    time_t now = ::time(0);
    struct tm local;
    if (!::localtime_r(&now, &local)) return false;
    size_t length = ::strftime(buffer,
                               capacity,
                               "makocode_decoded_%Y%m%d%H%M%S.txt",
                               &local);
    return length > 0u && length < capacity;
}

extern "C" bool makocode_platform_secure_random(void* buffer, size_t size) {
    if (!buffer && size) return false;
    int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    size_t offset = 0u;
    while (offset < size) {
        ssize_t result = ::read(fd, (char*)buffer + offset, size - offset);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            ::close(fd);
            return false;
        }
        offset += (size_t)result;
    }
    return ::close(fd) == 0;
}

extern "C" bool makocode_platform_read_password(char* buffer,
                                                 size_t capacity,
                                                 size_t* length,
                                                 const char* prompt) {
    if (!buffer || capacity < 2u || !length || !prompt) return false;
    *length = 0u;
    buffer[0] = '\0';
    int fd = ::open("/dev/tty", O_RDWR);
    if (fd < 0) return false;

    struct termios original;
    if (::tcgetattr(fd, &original) != 0) {
        ::close(fd);
        return false;
    }
    int original_flags = ::fcntl(fd, F_GETFL);
    bool nonblocking = original_flags >= 0 &&
                       ::fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) == 0;
    struct sigaction old_actions[5];
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = password_prompt_signal_handler;
    sigemptyset(&action.sa_mask);
    const int signals[5] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
    unsigned long actions_installed = 0u;
    password_prompt_signal = 0;
    for (; actions_installed < 5u; ++actions_installed) {
        if (::sigaction(signals[actions_installed], &action,
                        &old_actions[actions_installed]) != 0) break;
    }
    if (actions_installed != 5u) {
        while (actions_installed) {
            --actions_installed;
            ::sigaction(signals[actions_installed], &old_actions[actions_installed], 0);
        }
        ::close(fd);
        return false;
    }

    struct termios hidden = original;
    hidden.c_lflag &= (tcflag_t)~(ECHO | ECHONL | ICANON);
    hidden.c_iflag &= (tcflag_t)~(IXON | IXOFF);
    hidden.c_cc[VMIN] = 1;
    hidden.c_cc[VTIME] = 0;
    bool changed = ::tcsetattr(fd, TCSANOW, &hidden) == 0;
    bool ok = nonblocking && changed && ::tcflush(fd, TCIFLUSH) == 0 &&
              write_terminal_text(fd, prompt, strlen(prompt));
    bool overflow = false;
    size_t used = 0u;
    while (ok && !password_prompt_signal) {
        if (fd >= FD_SETSIZE) {
            ok = false;
            break;
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        int ready = ::select(fd + 1, &readable, 0, 0, &timeout);
        if (ready < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (ready == 0) continue;
        if (!FD_ISSET(fd, &readable)) {
            ok = false;
            break;
        }
        if (password_prompt_signal) break;
        char ch = '\0';
        ssize_t result = ::read(fd, &ch, 1u);
        if (result < 0 && errno == EINTR) {
            if (password_prompt_signal) break;
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (password_prompt_signal) break;
            continue;
        }
        if (result != 1) {
            ok = false;
            break;
        }
        if (ch == '\n' || ch == '\r') break;
        if (ch == original.c_cc[VERASE] || ch == '\b' || ch == '\x7f') {
            if (used) {
                --used;
                while (used && ((unsigned char)buffer[used] & 0xC0u) == 0x80u) --used;
                buffer[used] = '\0';
            }
            continue;
        }
        if (ch == original.c_cc[VKILL] || ch == '\x15') {
            memset(buffer, 0, (size_t)capacity);
            used = 0u;
            continue;
        }
        if (ch == original.c_cc[VEOF] || ch == '\x04') {
            ok = false;
            break;
        }
        if (used + 1u < capacity) buffer[used++] = ch;
        else overflow = true;
    }

    if (changed) {
        int restored;
        do {
            restored = ::tcsetattr(fd, TCSAFLUSH, &original);
        } while (restored != 0 && errno == EINTR);
        if (restored != 0) ok = false;
    }
    if (nonblocking && ::fcntl(fd, F_SETFL, original_flags) != 0) ok = false;
    if (!write_terminal_text(fd, "\n", 1u)) ok = false;
    if (::close(fd) != 0) ok = false;
    for (unsigned long i = 0u; i < 5u; ++i) {
        ::sigaction(signals[i], &old_actions[i], 0);
    }
    if (password_prompt_signal) {
        int signal_number = (int)password_prompt_signal;
        password_prompt_signal = 0;
        if (signal_number == SIGTSTP) ::raise(signal_number);
        ok = false;
    }
    if (overflow || !used) ok = false;
    if (!ok) {
        volatile char* clear = buffer;
        for (unsigned long i = 0u; i < capacity; ++i) clear[i] = 0;
        *length = 0u;
        return false;
    }
    buffer[used] = '\0';
    *length = used;
    return true;
}
#endif
