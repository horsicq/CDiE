/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_translation.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <xxfclib/strings/xx_string.h>
#endif
static xxwidgets_app *translation_app;
xxwidgets_status cdie_translation_open(xxwidgets_app *app, const char *path, size_t *error_line)
{
#ifdef _WIN32
    wchar_t *environment = path ? NULL : _wgetenv(L"CDIE_TRANSLATION");
    char *owned = environment ? xx_str_unicode_to_utf8(environment) : NULL;
    const char *selected = path ? path : owned;
#else
    const char *selected = path ? path : getenv("CDIE_TRANSLATION");
#endif
    xxwidgets_status status = XXWIDGETS_OK;
    if (error_line)
        *error_line = 0;
    if (!app || (path && !path[0])) {
#ifdef _WIN32
        xx_str_free(owned);
#endif
        return XXWIDGETS_INVALID_ARGUMENT;
    }
#ifdef _WIN32
    if (environment && !owned)
        return XXWIDGETS_OUT_OF_MEMORY;
#endif
    if (selected && selected[0])
        status = xxwidgets_app_load_po_file(app, selected, error_line);
    else
        status = xxwidgets_app_clear_translation(app);
#ifdef _WIN32
    xx_str_free(owned);
#endif
    if (status == XXWIDGETS_OK)
        translation_app = app;
    return status;
}
void cdie_translation_close(void) { translation_app = NULL; }
const char *cdie_translation_gettext(const char *text) { return xxwidgets_app_gettext(translation_app, text); }
/* Tokens must match byte-for-byte. Escaped %% does not consume an argument. */
static int next_conversion(const char **cursor, const char **start, size_t *length)
{
    const char *p = *cursor;
    while (*p) {
        if (*p++ != '%')
            continue;
        if (*p == '%') {
            ++p;
            continue;
        }
        *start = p - 1;
        while (*p && strchr("-+ #0'", *p))
            ++p;
        if (*p == '*')
            ++p;
        else
            while (*p >= '0' && *p <= '9')
                ++p;
        if (*p == '.') {
            ++p;
            if (*p == '*')
                ++p;
            else
                while (*p >= '0' && *p <= '9')
                    ++p;
        }
        if (*p == 'h' || *p == 'l') {
            char c = *p++;
            if (*p == c)
                ++p;
        } else if (*p && strchr("jztLw", *p))
            ++p;
        else if (*p == 'I') {
            ++p;
            if ((p[0] == '3' && p[1] == '2') || (p[0] == '6' && p[1] == '4'))
                p += 2;
        }
        if (!*p || !strchr("diouxXfFeEgGaAcCsSp@", *p))
            return -1;
        *length = (size_t)(++p - *start);
        *cursor = p;
        return 1;
    }
    *cursor = p;
    return 0;
}
/* The translation when its conversions match the English format exactly. */
static const char *checked_format(const char *format, const char *translated)
{
    const char *a = format, *b = translated, *sa = NULL, *sb = NULL;
    if (!translated)
        return format;
    for (;;) {
        size_t na = 0, nb = 0;
        int ra = next_conversion(&a, &sa, &na), rb = next_conversion(&b, &sb, &nb);
        if (ra < 0 || rb < 0 || ra != rb)
            return format;
        if (!ra)
            return translated;
        if (na != nb || memcmp(sa, sb, na))
            return format;
    }
}
const char *cdie_translation_format(const char *format)
{
    if (!format)
        return "";
    return checked_format(format, CDIE_TR(format));
}
const char *cdie_translation_ngettext(const char *singular, const char *plural, unsigned long long n)
{
    const char *english;
    if (!singular || !plural)
        return singular ? singular : plural ? plural : "";
    english = n == 1 ? singular : plural;
    return checked_format(english, xxwidgets_app_ngettext(translation_app, singular, plural, n));
}
int cdie_translation_snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    va_list args;
    int length;
    if ((!buffer && capacity) || !format)
        return -1;
    va_start(args, format);
    length = vsnprintf(buffer, capacity, CDIE_FMT(format), args);
    va_end(args);
    if (capacity) {
        size_t end, start, lead;
        unsigned int byte, needed;
        buffer[capacity - 1] = 0;
        end = strlen(buffer);
        start = end;
        while (start && ((unsigned char)buffer[start - 1] & 0xc0) == 0x80)
            --start;
        if (end && start) {
            lead = start - 1;
            byte = (unsigned char)buffer[lead];
            needed = byte < 0x80 ? 1u : byte < 0xe0 ? 2u : byte < 0xf0 ? 3u : 4u;
            if (end - lead < needed)
                buffer[lead] = 0;
        }
    }
    return length;
}
int cdie_translation_arguments(int *argc, char **argv, const char **path)
{
    int i, count, positional = 0;
    const char *selected = NULL;
    if (!argc || !argv || !path || *argc < 1)
        return 0;
    for (i = 1; i < *argc; ++i) {
        if (!strcmp(argv[i], "--"))
            positional = 1;
        if (!positional && !strcmp(argv[i], "--translation")) {
            if (++i >= *argc || !argv[i][0])
                return 0;
            selected = argv[i];
        }
    }
    count = 1;
    positional = 0;
    for (i = 1; i < *argc; ++i) {
        if (!strcmp(argv[i], "--"))
            positional = 1;
        if (!positional && !strcmp(argv[i], "--translation")) {
            ++i;
            continue;
        }
        argv[count++] = argv[i];
    }
    argv[count] = NULL;
    *argc = count;
    *path = selected;
    return 1;
}
#ifdef _WIN32
#include <stdarg.h>
#include <strsafe.h>
#include <windows.h>
#include <xxfclib/strings/xx_string.h>
/* Bounded wide formatting; a truncated final surrogate pair is removed. */
static int format_wide(wchar_t *buffer, size_t capacity, const wchar_t *format, va_list args)
{
    HRESULT result = StringCchVPrintfW(buffer, capacity, format, args);
    size_t end = wcslen(buffer);
    if (end && buffer[end - 1] >= 0xd800 && buffer[end - 1] <= 0xdbff)
        buffer[end - 1] = 0;
    return SUCCEEDED(result) ? lstrlenW(buffer) : -1;
}
int cdie_translation_swprintf(wchar_t *buffer, size_t capacity, const wchar_t *format, ...)
{
    char *source;
    wchar_t *wide;
    va_list args;
    int length;
    if (!buffer || !capacity || !format)
        return -1;
    source = xx_str_unicode_to_utf8(format);
    wide = source ? xx_str_utf8_to_unicode(CDIE_FMT(source)) : NULL;
    va_start(args, format);
    length = format_wide(buffer, capacity, wide ? wide : format, args);
    va_end(args);
    xx_str_free(source);
    xx_str_wfree(wide);
    return length;
}
int cdie_translation_swprintf_utf8(wchar_t *buffer, size_t capacity, const char *format, ...)
{
    wchar_t *wide;
    va_list args;
    int length;
    if (!buffer || !capacity || !format)
        return -1;
    wide = xx_str_utf8_to_unicode(format);
    if (!wide) {
        buffer[0] = 0;
        return -1;
    }
    va_start(args, format);
    length = format_wide(buffer, capacity, wide, args);
    va_end(args);
    xx_str_wfree(wide);
    return length;
}
#endif
