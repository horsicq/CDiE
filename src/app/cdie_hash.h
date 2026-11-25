/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_HASH_H
#define CDIE_HASH_H
#include "cdie_file_info.h"
#define CDIE_HASH_COUNT 4
/* Whole-file digests, lowercase hexadecimal. No parser/signature dependency.
 * Results and errors use xx_rt allocations. Failures return no partial result. */
typedef struct cdie_hash_entry {
    char algorithm[8], value[65];
} cdie_hash_entry;
typedef struct cdie_hash_data {
    uint64_t size;
    cdie_hash_entry entries[CDIE_HASH_COUNT];
} cdie_hash_data;
/* Strict checked streaming over a known-size seekable device. The device is
 * borrowed; its cursor may move. Empty inputs produce the standard digests. */
int cdie_hash_read_device(xx_io_device *device, xx_pd_struct *progress, cdie_hash_data **data, char **error);
int cdie_hash_read(const char *path, xx_pd_struct *progress, cdie_hash_data **data, char **error);
/* NULL/empty selects all (-1); unknown names return -2. Otherwise returns
 * the row index for exact public names CRC32, MD5, SHA1 and SHA256. */
int cdie_hash_algorithm_index(const char *algorithm);
int cdie_hash_report(const char *path, const char *algorithm, cdie_output_format format, char **text, char **error);
#endif
