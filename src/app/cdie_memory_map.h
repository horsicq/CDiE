/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_MEMORY_MAP_H
#define CDIE_MEMORY_MAP_H
#include <xxfclib/data/xx_pd.h>
#include <xxfclib/formats/xx_memory_map.h>
#define CDIE_MEMORY_MAP_MAX_RECORDS 1024U
#define CDIE_MEMORY_MAP_MODE_DATA 1U
#define CDIE_MEMORY_MAP_MODE_SECTIONS 2U
#define CDIE_MEMORY_MAP_MODE_SEGMENTS 4U
typedef struct cdie_memory_map_data {
    uint64_t file_size, module_address, entry_point_address;
    char file_type[32], mode[24];
    size_t count;
    xx_memory_record records[CDIE_MEMORY_MAP_MAX_RECORDS];
    char bitness[16], endianness[8], architecture[32];
    unsigned available_modes;
} cdie_memory_map_data;
/* Owned metadata snapshot, no device/format pointers or file bytes. No database
 * or DataStructs. Release both result and errors with xx_rt_free. On failure or
 * cancellation no partial model is returned. Native default map for PE/ELF/
 * thin Mach-O; other inputs get an unmapped physical Data region. */
int cdie_memory_map_read(const char *path, xx_pd_struct *progress, cdie_memory_map_data **out, char **error);
/* NULL/empty/Default format selects the detected native format; Binary forces
 * a flat physical Data map. Native format names must match the detected family
 * or precise file type. A NULL/empty mode uses its default. Unsupported format
 * or map-mode requests fail without returning a partial snapshot. */
int cdie_memory_map_read_mode(const char *path, const char *format_name, const char *mode_name, xx_pd_struct *progress, cdie_memory_map_data **out, char **error);
#endif
