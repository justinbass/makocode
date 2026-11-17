/*
 * POSIX host services behind the platform interface.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#include "makocode_platform.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct MakoPlatformDirectory {
    DIR* handle;
};

#ifdef MAKOCODE_TEST_BUILD
extern "C" void* makocode_test_malloc(unsigned long size);
#endif

extern "C" void* makocode_platform_malloc(unsigned long size) {
#ifdef MAKOCODE_TEST_BUILD
    return makocode_test_malloc(size);
#else
    return ::malloc((size_t)size);
#endif
}

extern "C" void* makocode_platform_realloc(void* ptr, unsigned long size) {
    return ::realloc(ptr, (size_t)size);
}

extern "C" void makocode_platform_free(void* ptr) {
    ::free(ptr);
}

extern "C" long makocode_platform_read(int fd, void* buffer, unsigned long count) {
    return (long)::read(fd, buffer, (size_t)count);
}

extern "C" long makocode_platform_write(int fd, const void* buffer, unsigned long count) {
    return (long)::write(fd, buffer, (size_t)count);
}

extern "C" int makocode_platform_open(const char* path, int flags, ...) {
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

extern "C" int makocode_platform_create_directory(const char* path) {
    if (::mkdir(path, 0755) == 0) return 0;
    return errno == EEXIST ? 1 : -1;
}

extern "C" MakoPlatformDirectory* makocode_platform_open_directory(const char* path) {
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
    struct dirent* native_entry = ::readdir(directory->handle);
    if (!native_entry) return 0;
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
                                             unsigned long size,
                                             const char* format,
                                             ...) {
    va_list args;
    va_start(args, format);
    int result = ::vsnprintf(buffer, (size_t)size, format, args);
    va_end(args);
    return result;
}

extern "C" bool makocode_platform_utc_timestamp(char* buffer, unsigned long capacity) {
    if (!buffer || !capacity) return false;
    time_t now = ::time(0);
    struct tm utc;
    if (!::gmtime_r(&now, &utc)) return false;
    size_t length = ::strftime(buffer, (size_t)capacity, "%Y%m%dT%H%M%SZ", &utc);
    return length > 0u && length < capacity;
}

extern "C" bool makocode_platform_local_timestamp(char* buffer, unsigned long capacity) {
    if (!buffer || !capacity) return false;
    time_t now = ::time(0);
    struct tm local;
    if (!::localtime_r(&now, &local)) return false;
    size_t length = ::strftime(buffer,
                               (size_t)capacity,
                               "makocode_decoded_%Y%m%d%H%M%S.txt",
                               &local);
    return length > 0u && length < capacity;
}
