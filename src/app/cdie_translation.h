/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_TRANSLATION_H
#define CDIE_TRANSLATION_H
#include <wchar.h>
#include <xxwidgets/xxwidgets_translation.h>
/* The interactive application has one UI app. Bind its catalog before creating
 * controls; close the binding before destroying the app. This helper owns no
 * catalog: xxwidgets owns it, so CDiE and built-in controls use the same PO.
 * A NULL path reads CDIE_TRANSLATION. An unset/empty environment means English.
 * A failed load preserves the previous catalog/binding.
 */
xxwidgets_status cdie_translation_open(xxwidgets_app *app, const char *path, size_t *error_line);
void cdie_translation_close(void);
const char *cdie_translation_gettext(const char *text);
/* Preserve the exact ordered printf conversion specifications. Invalid formats,
 * added/missing arguments, positional reordering and %n fall back to English. */
const char *cdie_translation_format(const char *format);
/* The catalog's plural form for n, checked like cdie_translation_format
 * against the English form the count selects; falls back to that form. */
const char *cdie_translation_ngettext(const char *singular, const char *plural, unsigned long long n);
/* Bounded UTF-8 formatting; a truncated final character is removed. */
#if defined(__MINGW32__)
__attribute__((format(gnu_printf, 3, 4)))
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
int cdie_translation_snprintf(char *buffer, size_t capacity, const char *format, ...);
/* Remove --translation FILE from argv, respecting --. 1 success, 0 missing or
 * empty path. On failure argv/argc are unchanged. Last explicit option wins.
 * Strings are not freed; owners should pass a pointer copy of their argv. */
int cdie_translation_arguments(int *argc, char **argv, const char **path);
#ifdef _WIN32
int cdie_translation_swprintf(wchar_t *buffer, size_t capacity, const wchar_t *format, ...);
/* Same, for a UTF-8 format already selected (cdie_translation_ngettext);
 * no further catalog lookup. */
int cdie_translation_swprintf_utf8(wchar_t *buffer, size_t capacity, const char *format, ...);
#endif
#define CDIE_TR(text) cdie_translation_gettext(text)
#define CDIE_FMT(text) cdie_translation_format(text)
#define CDIE_N_(text) text
#endif
