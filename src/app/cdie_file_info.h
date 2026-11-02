/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_FILE_INFO_H
#define CDIE_FILE_INFO_H

#include <xxfclib/formats/xx_format.h>

typedef enum cdie_output_format {
    CDIE_OUTPUT_TEXT = 0,
    CDIE_OUTPUT_JSON,
    CDIE_OUTPUT_XML,
    CDIE_OUTPUT_CSV,
    CDIE_OUTPUT_TSV,
    CDIE_OUTPUT_PLAIN
} cdie_output_format;

/* Native DataStructs only: PE, ELF and Mach-O. A fat (universal) Mach-O gives
 * its fat_header and fat_arch table, then one slice's own DataStructs in place
 * of that slice's RAW_DATA (see cdie_file_info_visit_slice; here the automatic
 * choice). A NULL path with inventory lists the supported catalog. selector is
 * NULL/Info or NAME[#FIELD], optionally qualified as PE::NAME, ELF::NAME or
 * MACHO::NAME (fat table names are MACHO:: too). Output and error are runtime
 * allocations released with xx_rt_free. Failure never returns partial output. */
int cdie_file_info(const char *path, const char *selector, int inventory,
    cdie_output_format output, char **text, char **error);

/* Synchronous native walker. Callbacks borrow records until they return and
 * may stop traversal by returning zero. No widget access is performed here;
 * callers may run this on a worker with an observed progress object. */
typedef struct cdie_file_info_visitor {
    int (*begin_file)(const char *path, const char *type, int64_t size, void *user);
    int (*begin_structure)(const char *name, const xx_data_struct *item, void *user);
    int (*field)(const xx_data_struct_record *record, void *user);
    int (*end_structure)(void *user);
} cdie_file_info_visitor;
int cdie_file_info_visit(const char *path, const char *selector, int inventory,
    const cdie_file_info_visitor *visitor, void *user, xx_pd_struct *progress, char **error);

/* As cdie_file_info_visit, which passes CDIE_FILE_INFO_SLICE_AUTO. For a fat
 * Mach-O, slice is the fat_arch index of the slice to open; AUTO takes the
 * first slice built for the architecture cdie runs as, else the first slice.
 * An index past the table fails; thin files and other formats ignore it. */
#define CDIE_FILE_INFO_SLICE_AUTO (-1)
int cdie_file_info_visit_slice(const char *path, int slice, const char *selector, int inventory,
    const cdie_file_info_visitor *visitor, void *user, xx_pd_struct *progress, char **error);

#endif
