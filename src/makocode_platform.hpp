/*
 * Host platform interface for MakoCode.
 * Copyright (C) 2026 Justin Alan Bass.
 * SPDX-License-Identifier: AGPL-3.0-or-later; see LICENSE.
 * No warranty; see LICENSE.
 */
#ifndef MAKOCODE_PLATFORM_HPP
#define MAKOCODE_PLATFORM_HPP

#ifndef INCLUDES
#define INCLUDES 1
#endif

enum {
    MAKOCODE_OPEN_READ = 1,
    MAKOCODE_OPEN_WRITE = 2,
    MAKOCODE_OPEN_CREATE = 4,
    MAKOCODE_OPEN_TRUNCATE = 8,
    MAKOCODE_OPEN_EXCLUSIVE = 16
};

struct MakoPlatformFileInfo {
    bool is_directory;
    bool is_regular;
};

struct MakoPlatformDirectory;

struct MakoPlatformDirectoryEntry {
    char name[4096];
};

extern "C" {
void* makocode_platform_malloc(unsigned long size);
void* makocode_platform_realloc(void* ptr, unsigned long size);
void makocode_platform_free(void* ptr);
long makocode_platform_read(int fd, void* buffer, unsigned long count);
long makocode_platform_write(int fd, const void* buffer, unsigned long count);
int makocode_platform_open(const char* path, int flags, ...);
int makocode_platform_create_file(const char* path, int mode);
int makocode_platform_close(int fd);
int makocode_platform_unlink(const char* path);
int makocode_platform_stat(const char* path, MakoPlatformFileInfo* info);
int makocode_platform_create_directory(const char* path);
MakoPlatformDirectory* makocode_platform_open_directory(const char* path);
int makocode_platform_read_directory(MakoPlatformDirectory* directory,
                                     MakoPlatformDirectoryEntry* entry);
void makocode_platform_close_directory(MakoPlatformDirectory* directory);
char* makocode_platform_realpath(const char* path);
const char* makocode_platform_getenv(const char* name);
int makocode_platform_snprintf(char* buffer, unsigned long size, const char* format, ...);
bool makocode_platform_utc_timestamp(char* buffer, unsigned long capacity);
bool makocode_platform_local_timestamp(char* buffer, unsigned long capacity);
}

#endif
