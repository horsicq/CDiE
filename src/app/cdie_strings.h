/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_STRINGS_H
#define CDIE_STRINGS_H
#include "cdie_file_info.h"
#define CDIE_STRINGS_MAX_RECORDS 10000U
#define CDIE_STRINGS_MAX_TEXT (8U * 1024U * 1024U)
#define CDIE_STRINGS_MAX_STRING (1024U * 1024U)
typedef enum cdie_strings_encoding {
    CDIE_STRINGS_ASCII = 1,
    CDIE_STRINGS_UTF8 = 2,
    CDIE_STRINGS_UTF16LE = 4,
    CDIE_STRINGS_UTF16BE = 8,
    CDIE_STRINGS_ALL = 14
} cdie_strings_encoding;
typedef struct cdie_strings_options {
    unsigned minimum_length, encodings;
} cdie_strings_options;
typedef struct cdie_string_record {
    uint64_t offset, byte_size, character_length;
    cdie_strings_encoding encoding;
    char *text;
} cdie_string_record;
typedef struct cdie_strings_data {
    uint64_t size;
    cdie_string_record *records;
    size_t count, capacity, text_bytes;
} cdie_strings_data;
cdie_strings_options cdie_strings_defaults(void);
const char *cdie_strings_encoding_name(cdie_strings_encoding encoding);
/* CLI names: ascii, utf8, utf16le, utf16be, all. Zero means invalid. */
unsigned cdie_strings_parse_encoding(const char *name);
int cdie_strings_options_valid(const cdie_strings_options *options);
/* Whole-file checked streaming; buffers preserve partial codepoints across
 * reads. Errors/cancellation/limits return no model. All allocations use
 * xx_rt, including individual decoded texts; release via free below. */
int cdie_strings_read_device(xx_io_device *device, const cdie_strings_options *options, xx_pd_struct *progress, cdie_strings_data **data, char **error);
int cdie_strings_read(const char *path, const cdie_strings_options *options, xx_pd_struct *progress, cdie_strings_data **data, char **error);
void cdie_strings_free(cdie_strings_data *data);
int cdie_strings_report(const char *path, const cdie_strings_options *options, cdie_output_format format, char **text, char **error);
#endif
