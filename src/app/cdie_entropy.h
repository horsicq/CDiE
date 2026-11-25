/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_ENTROPY_H
#define CDIE_ENTROPY_H
#include "cdie_file_info.h"
#define CDIE_ENTROPY_MAX_REGIONS 1024
#define CDIE_ENTROPY_MAX_SAMPLES 100
/* Engine-independent result. All offsets are physical file offsets. The model
 * and errors use xx_rt allocation; callers release with xx_rt_free. */
typedef struct cdie_entropy_sample {
    uint64_t offset, size;
    double entropy;
} cdie_entropy_sample;
typedef struct cdie_entropy_region {
    uint64_t offset, size;
    double entropy;
    char name[192];
} cdie_entropy_region;
typedef struct cdie_entropy_data {
    uint64_t size, counts[256];
    double total;
    char file_type[32];
    size_t region_count, sample_count;
    cdie_entropy_region regions[CDIE_ENTROPY_MAX_REGIONS];
    cdie_entropy_sample samples[CDIE_ENTROPY_MAX_SAMPLES];
} cdie_entropy_data;
/* Bounded-memory streaming; no signatures or DataStructs. Failures and
 * cancellation return no partial model. Empty files have entropy zero. */
int cdie_entropy_read(const char *path, xx_pd_struct *progress, cdie_entropy_data **data, char **error);
int cdie_entropy_report(const char *path, cdie_output_format format, char **text, char **error);
#endif
