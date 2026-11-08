/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Presentation only: retain the engine's scan identities and record ordering.
 */
#include "gui_result_tree.h"
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/formats/xx_format.h>
#undef fileType
#include <xxfclib/rt/xx_rt.h>
#include <stdint.h>
#include <string.h>

typedef struct result_group {
    const xx_scan_record *record;
    size_t node;
    int emitted;
} result_group;

static int same_group(const xx_scan_record *a, const xx_scan_record *b)
{
    return a->scan_id == b->scan_id && a->parent_id == b->parent_id && a->scan_part == b->scan_part &&
        a->file_type == b->file_type && a->offset == b->offset && a->size == b->size &&
        (a->scan_part != XX_SCAN_PART_ARCHIVE ||
         (a->archive_offset == b->archive_offset && a->archive_size == b->archive_size && a->archive_compressed == b->archive_compressed &&
          !strcmp(a->archive_member ? a->archive_member : "", b->archive_member ? b->archive_member : "")));
}
static int emit_record(const xx_scan_record *record, size_t parent, gui_backend_tree_fn callback, void *user)
{
    xx_buf_t text;
    char *type = xx_scan_translate_type(record->type ? record->type : "");
    char *label;
    gui_backend_detection detection;
    int ok;
    if (!type) return 0;
    xx_buf_init(&text);
    if (record->is_heuristic) xx_buf_append_str(&text, "(Heur) ");
    else if (record->is_aggressive_heuristic) xx_buf_append_str(&text, "(A-Heur) ");
    xx_buf_append_str(&text, type);
    xx_buf_append_str(&text, ": ");
    xx_buf_append_str(&text, record->name ? record->name : "Unknown");
    if (record->version && record->version[0]) xx_buf_appendf(&text, " (%s)", record->version);
    if (record->info && record->info[0]) xx_buf_appendf(&text, " [%s]", record->info);
    label = xx_buf_detach(&text, NULL);
    detection.name = record->name ? record->name : "Unknown";
    detection.signature = record->source_ref;
    ok = label && callback(parent, label, type, &detection, user);
    xx_rt_free(label); xx_rt_free(type);
    return ok;
}

int gui_result_tree_emit(const xx_scan_result *result, int hide_unknown, gui_backend_tree_fn callback, void *user)
{
    result_group *groups;
    size_t group_count = 0, emitted = 0, next_node = 1, i, j;
    int has_overlay = 0, multiple_formats = 0, ok = 0;
    const xx_scan_record *first_input = NULL;
    if (!result || result->nCount < 0 || (!result->pRecords && result->nCount) || !callback) return 0;
    groups = xx_rt_calloc((size_t)result->nCount + 1, sizeof(*groups));
    if (!groups) return 0;
    if (!callback(SIZE_MAX, xx_scan_file_type_name(result->file_type), "", NULL, user)) goto done;
    for (i = 0; i < (size_t)result->nCount; ++i) {
        const xx_scan_record *record = &result->pRecords[i];
        if (!record->parent_id && record->scan_part == XX_SCAN_PART_INPUT) {
            if (!first_input) first_input = record;
            else if (record->file_type != first_input->file_type) multiple_formats = 1;
        }
        for (j = 0; j < group_count; ++j) if (same_group(record, groups[j].record)) break;
        if (j == group_count) { groups[group_count++].record = record; }
        if (record->parent_id && (record->scan_part == XX_SCAN_PART_OVERLAY || record->scan_part == XX_SCAN_PART_INPUT)) has_overlay = 1;
    }
    while (emitted < group_count) {
        size_t before = emitted;
        for (i = 0; i < group_count; ++i) {
            const xx_scan_record *record = groups[i].record;
            size_t parent = 0;
            int waiting = 0;
            if (groups[i].emitted) continue;
            if (record->parent_id) {
                size_t matches = 0;
                for (j = 0; j < group_count; ++j) {
                    if (groups[j].record->scan_id == record->parent_id) {
                        ++matches;
                        if (!groups[j].emitted) waiting = 1;
                        else parent = groups[j].node;
                    }
                }
                /* Parent-format passes share a scan identity. Their children
                 * belong to the containing input, not an arbitrary pass. */
                if (matches > 1) parent = 0;
            }
            if (waiting) continue;
            groups[i].node = 0;
            if (record->parent_id || record->offset || multiple_formats) {
                xx_buf_t text;
                char *label;
                const char *format = record->file_type == XX_FILE_TYPE_BINARY ? "Binary" : xx_format_file_type_to_string(record->file_type);
                xx_buf_init(&text);
                if (record->scan_part == XX_SCAN_PART_ARCHIVE) {
                    xx_buf_appendf(&text, "Archive member: %s: %s [Packed offset = 0x%llx, Packed size = 0x%llx, %s offset = 0x%llx, Size = 0x%llx]",
                        record->archive_member ? record->archive_member : "", format,
                        (unsigned long long)record->archive_offset, (unsigned long long)record->archive_size,
                        record->archive_compressed ? "Member" : "Input", (unsigned long long)record->offset,
                        (unsigned long long)record->size);
                } else if (!record->parent_id && record->scan_part == XX_SCAN_PART_INPUT) {
                    xx_buf_appendf(&text, "Format: %s", format);
                } else {
                    xx_buf_appendf(&text, "%s: %s [Offset = 0x%08llx,Size = 0x%llx]",
                        record->scan_part == XX_SCAN_PART_RESOURCE ? "Resource" : "Overlay", format,
                        (unsigned long long)record->offset, (unsigned long long)record->size);
                }
                label = xx_buf_detach(&text, NULL);
                if (!label) { ok = 0; goto done; }
                ok = callback(parent, label, "", NULL, user);
                xx_rt_free(label);
                if (!ok) goto done;
                groups[i].node = next_node++;
            }
            for (j = 0; j < (size_t)result->nCount; ++j) {
                const xx_scan_record *child = &result->pRecords[j];
                if (!same_group(child, record) || (hide_unknown && child->is_unknown)) continue;
                /* A legacy flat overlay summary is replaced by the actual
                 * child scan when the engine supplies one. */
                if (has_overlay && !child->parent_id && child->type && !strcmp(child->type, "Overlay")) continue;
                if (!emit_record(child, groups[i].node, callback, user)) { ok = 0; goto done; }
                ++next_node;
            }
            groups[i].emitted = 1; ++emitted;
        }
        if (emitted == before) { ok = 0; goto done; }
    }
    ok = 1;
done:
    xx_rt_free(groups);
    return ok;
}
