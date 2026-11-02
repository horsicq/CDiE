/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_hash.h"
#include "cdie_app.h"
#include <xxfclib/algo/crc/xx_crc.h>
#include <xxfclib/algo/md5/xx_md5.h>
#include <xxfclib/algo/sha/xx_sha.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/global/xx_global.h>

static const char *const algorithms[CDIE_HASH_COUNT] = {"CRC32", "MD5", "SHA1", "SHA256"};
int cdie_hash_algorithm_index(const char *algorithm)
{
    int i;
    if (!algorithm || !*algorithm) return -1;
    for (i = 0; i < CDIE_HASH_COUNT; ++i) if (!xx_rt_strcmp(algorithm, algorithms[i])) return i;
    return -2;
}
static void hex_digest(char *text, const unsigned char *digest, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < size; ++i) { text[2*i] = digits[digest[i] >> 4]; text[2*i+1] = digits[digest[i] & 15]; }
    text[2*size] = 0;
}
static int cancelled(xx_pd_struct *progress)
{
    if (!xx_pd_is_stopped(progress)) return 0;
    xx_pd_stop(progress);
    return 1;
}
/* Each read is the general buffer size (diec --buffersize, the GUI's
 * Feature/ReadBufferSize), no larger than the input. The default size is used
 * when that much memory is not available, so the setting never changes a hash. */
static unsigned char *read_buffer(uint64_t size, size_t *capacity)
{
    size_t wanted = xx_get_buffer_size();
    unsigned char *buffer;
    if ((uint64_t)wanted > size) wanted = size ? (size_t)size : 1;
    buffer = xx_rt_malloc(wanted);
    if (!buffer && wanted > XX_DEFAULT_BUFFER_SIZE) buffer = xx_rt_malloc(wanted = XX_DEFAULT_BUFFER_SIZE);
    *capacity = buffer ? wanted : 0;
    return buffer;
}
int cdie_hash_read_device(xx_io_device *device, xx_pd_struct *progress, cdie_hash_data **out, char **error)
{
    xx_md5_context md5;
    xx_sha1_context sha1;
    xx_sha256_context sha256;
    xx_crc_context crc;
    unsigned char digest[32], *buffer = NULL;
    cdie_hash_data *data = NULL;
    const char *message = "Cannot read hash data";
    uint64_t done = 0;
    int64_t size;
    size_t capacity = 0;
    int ok = 0, level = -1, i;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    if (cancelled(progress)) goto finish;
    if (!device || (size = xx_io_total_size(device)) < 0) { message = "Cannot determine file size"; goto finish; }
    data = xx_rt_calloc(1, sizeof(*data)); buffer = read_buffer((uint64_t)size, &capacity);
    if (!data || !buffer) { message = "Cannot allocate hash data"; goto finish; }
    if (!xx_md5_init(&md5) || !xx_sha1_init(&sha1) || !xx_sha256_init(&sha256) ||
        !xx_crc_context_init_type(&crc, XX_CRC_TYPE_CRC32)) { message = "Cannot initialize hashes"; goto finish; }
    data->size = (uint64_t)size;
    level = xx_pd_enter_level(progress, data->size, "Calculating hashes");
    while (done < data->size) {
        size_t chunk = data->size - done < capacity ? (size_t)(data->size - done) : capacity;
        if (cancelled(progress)) goto finish;
        if (!xx_io_read_at(device, (int64_t)done, buffer, chunk)) goto finish;
        xx_crc_context_update(&crc, buffer, chunk);
        xx_md5_update(&md5, buffer, chunk); xx_sha1_update(&sha1, buffer, chunk); xx_sha256_update(&sha256, buffer, chunk);
        done += chunk; xx_pd_set_current(progress, level, done);
    }
    if (cancelled(progress)) goto finish;
    for (i = 0; i < CDIE_HASH_COUNT; ++i) xx_rt_snprintf(data->entries[i].algorithm, sizeof(data->entries[i].algorithm), "%s", algorithms[i]);
    xx_rt_snprintf(data->entries[0].value, sizeof(data->entries[0].value), "%08x", (unsigned)xx_crc_context_final(&crc));
    if (!xx_md5_final(&md5, digest, sizeof(digest))) goto finish;
    hex_digest(data->entries[1].value, digest, XX_MD5_DIGEST_SIZE);
    if (!xx_sha1_final(&sha1, digest, sizeof(digest))) goto finish;
    hex_digest(data->entries[2].value, digest, XX_SHA1_DIGEST_SIZE);
    if (!xx_sha256_final(&sha256, digest, sizeof(digest))) goto finish;
    hex_digest(data->entries[3].value, digest, XX_SHA256_DIGEST_SIZE);
    ok = 1;
finish:
    xx_pd_leave_level(progress, level);
    if (cancelled(progress)) { ok = 0; message = "Hash calculation cancelled"; }
    xx_rt_free(buffer);
    if (ok) *out = data;
    else { xx_rt_free(data); *error = cdie_strdup(message); }
    return ok;
}
int cdie_hash_read(const char *path, xx_pd_struct *progress, cdie_hash_data **out, char **error)
{
    xx_io_device *device;
    int ok;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    if (cancelled(progress)) { *error = cdie_strdup("Hash calculation cancelled"); return 0; }
    device = path && *path ? xx_io_file_open(path, "rb") : NULL;
    if (!device) { *error = cdie_strdup("Cannot open file"); return 0; }
    ok = cdie_hash_read_device(device, progress, out, error);
    xx_io_close(device);
    return ok;
}
int cdie_hash_report(const char *path, const char *algorithm, cdie_output_format format, char **text, char **error)
{
    cdie_hash_data *data;
    xx_buf_t buffer;
    int selected = cdie_hash_algorithm_index(algorithm), i, written = 0;
    if (!text || !error) return 0;
    *text = NULL; *error = NULL;
    if (selected == -2) { *error = cdie_strdup("Unsupported hash algorithm (use CRC32, MD5, SHA1 or SHA256)"); return 0; }
    if (!cdie_hash_read(path, NULL, &data, error)) return 0;
    xx_buf_init(&buffer);
    if (format == CDIE_OUTPUT_JSON) xx_buf_append_str(&buffer, "{\"data\":{\"Hash\":{");
    else if (format == CDIE_OUTPUT_XML) xx_buf_append_str(&buffer, "<data>\n  <record name=\"Hash\">\n");
    else if (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV)
        xx_buf_appendf(&buffer, "Hash%c\n", format == CDIE_OUTPUT_CSV ? ';' : '\t');
    else xx_buf_append_str(&buffer, "Hash:\n");
    for (i = 0; i < CDIE_HASH_COUNT; ++i) {
        const cdie_hash_entry *entry = data->entries + i;
        if (selected >= 0 && selected != i) continue;
        if (format == CDIE_OUTPUT_JSON) xx_buf_appendf(&buffer, "%s\"%s\":\"%s\"", written ? "," : "", entry->algorithm, entry->value);
        else if (format == CDIE_OUTPUT_XML) xx_buf_appendf(&buffer, "    <record name=\"%s\" value=\"%s\"/>\n", entry->algorithm, entry->value);
        else if (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV)
            xx_buf_appendf(&buffer, "%s%c%s\n", entry->algorithm, format == CDIE_OUTPUT_CSV ? ';' : '\t', entry->value);
        else xx_buf_appendf(&buffer, "    %s: %s\n", entry->algorithm, entry->value);
        ++written;
    }
    if (format == CDIE_OUTPUT_JSON) xx_buf_append_str(&buffer, "}}}\n");
    else if (format == CDIE_OUTPUT_XML) xx_buf_append_str(&buffer, "  </record>\n</data>\n");
    *text = xx_buf_detach(&buffer, NULL); xx_buf_free(&buffer); xx_rt_free(data);
    if (!*text) *error = cdie_strdup("Cannot allocate hash report");
    return *text != NULL;
}
