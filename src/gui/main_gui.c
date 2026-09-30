/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* The main controls and common modal forms belong to xxwidgets. Win32 supplies
 * file/folder pickers, drag-and-drop and pixel layout. The engine stays behind
 * gui_backend.h because its PE declarations must not meet <windows.h>.
 * Scratch storage uses the process heap; xxwidgets supplies the hosted GUI
 * runtime while the console keeps its own runtime configuration. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>

#include "gui_backend.h"
#include "resource.h"

#define IDC_EDIT_PATH   102
#define IDC_BTN_BROWSE  103
#define IDC_TYPE        104
#define IDC_BTN_FLAGS   105
#define IDC_BTN_DB      106
#define IDC_STATIC_MSEC 107
#define IDC_BTN_SCAN    108
#define IDC_SCANRESULTS 109
#define IDC_BTN_EXIT    110
#define IDC_BTN_OPTIONS 111
#define IDC_BTN_ABOUT   112
#define IDC_BTN_REPORT  113
#define IDC_STATUS      114
#define IDC_BTN_DB_PATHS 115

enum gui_action { ACTION_NONE, ACTION_SCAN, ACTION_BROWSE,
                  ACTION_OPTIONS, ACTION_ABOUT, ACTION_REPORT,
                  ACTION_DATABASE_PATHS };

static HINSTANCE g_hInst;
static xxwidgets_app *g_app;
static xxwidgets_widget *g_window, *g_fileLabel, *g_path, *g_browse, *g_type;
static xxwidgets_widget *g_flags, *g_databases, *g_msec, *g_scan, *g_results;
static xxwidgets_widget *g_flagLabel, *g_databaseLabel, *g_databasePaths;
static xxwidgets_widget *g_options, *g_aboutButton, *g_reportButton, *g_exit, *g_status;
static xxwidgets_scan_panel *g_panel;
static xxwidgets_about_dialog *g_about;
static int g_running, g_pending;

static int g_bFlagDeep = 1, g_bFlagHeur = 1, g_bFlagVerbose = 1;
static int g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat;
static int g_bDbExtra = 1, g_bDbCustom = 1;

static HWND native(xxwidgets_widget *widget)
{
    return (HWND)xxwidgets_widget_native_handle(widget);
}

static void apply_flags_to_backend(void)
{
    gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose,
                          g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
    gui_backend_set_databases(g_bDbExtra, g_bDbCustom);
}

static WCHAR *utf8_to_wide(const char *text)
{
    int length;
    WCHAR *wide;
    if (!text) return NULL;
    length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length * sizeof(WCHAR));
    if (wide) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length);
    return wide;
}

static char *wide_to_utf8(const WCHAR *wide)
{
    int length;
    char *text;
    if (!wide) return NULL;
    length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (length <= 0) return NULL;
    text = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length);
    if (text) WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, length, NULL, NULL);
    return text;
}

static char *copy_utf8(const char *text)
{
    SIZE_T length = 0;
    char *copy;
    if (!text) text = "";
    while (text[length]) ++length;
    copy = (char *)HeapAlloc(GetProcessHeap(), 0, length + 1);
    if (copy) CopyMemory(copy, text, length + 1);
    return copy;
}

static void set_status_wide(const WCHAR *wide)
{
    char *text = wide_to_utf8(wide);
    if (text) {
        xxwidgets_widget_set_text(g_status, text);
        HeapFree(GetProcessHeap(), 0, text);
    }
}

static void report_ui_error(const char *operation, xxwidgets_status status)
{
    WCHAR text[256];
    WCHAR *name = utf8_to_wide(operation);
    WCHAR *detail = utf8_to_wide(xxwidgets_status_string(status));
    wsprintfW(text, L"%s: %s", name ? name : L"Interface error", detail ? detail : L"unknown error");
    set_status_wide(text);
    if (name) HeapFree(GetProcessHeap(), 0, name);
    if (detail) HeapFree(GetProcessHeap(), 0, detail);
}

static unsigned int flags_mask(void)
{
    return (g_bFlagDeep ? 1u : 0u) | (g_bFlagHeur ? 2u : 0u) |
           (g_bFlagVerbose ? 4u : 0u) | (g_bFlagAggr ? 8u : 0u) |
           (g_bFlagHideUnknown ? 16u : 0u) | (g_bFlagFormat ? 32u : 0u);
}

static unsigned int databases_mask(void)
{
    return (g_bDbExtra ? 1u : 0u) | (g_bDbCustom ? 2u : 0u);
}

static xxwidgets_status sync_panel_options(void)
{
    xxwidgets_status status = xxwidgets_scan_panel_set_flags(g_panel, flags_mask());
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_set_databases(g_panel, databases_mask());
    return status;
}

static xxwidgets_status read_panel_options(void)
{
    unsigned int flags = 0, databases = 0;
    xxwidgets_status status = xxwidgets_scan_panel_get_flags(g_panel, &flags);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_databases(g_panel, &databases);
    if (status != XXWIDGETS_OK) return status;
    g_bFlagDeep = (flags & 1u) != 0; g_bFlagHeur = (flags & 2u) != 0;
    g_bFlagVerbose = (flags & 4u) != 0; g_bFlagAggr = (flags & 8u) != 0;
    g_bFlagHideUnknown = (flags & 16u) != 0; g_bFlagFormat = (flags & 32u) != 0;
    g_bDbExtra = (databases & 1u) != 0; g_bDbCustom = (databases & 2u) != 0;
    apply_flags_to_backend();
    return XXWIDGETS_OK;
}

static void clear_scan(void)
{
    xxwidgets_scan_panel_clear(g_panel);
    xxwidgets_widget_set_text(g_type, "Type: -");
    xxwidgets_widget_set_text(g_msec, "");
}

typedef struct result_buffer {
    xxwidgets_scan_result *records;
    SIZE_T count, capacity;
    int failed;
} result_buffer;

static void free_result_record(xxwidgets_scan_result *record)
{
    if (record->type) HeapFree(GetProcessHeap(), 0, (void *)record->type);
    if (record->name) HeapFree(GetProcessHeap(), 0, (void *)record->name);
    if (record->version) HeapFree(GetProcessHeap(), 0, (void *)record->version);
    if (record->info) HeapFree(GetProcessHeap(), 0, (void *)record->info);
}

static void free_result_buffer(result_buffer *buffer)
{
    SIZE_T i;
    for (i = 0; i < buffer->count; ++i) free_result_record(&buffer->records[i]);
    if (buffer->records) HeapFree(GetProcessHeap(), 0, buffer->records);
}

static void collect_result(const char *type, const char *name, const char *version,
                           const char *info, void *user_data)
{
    result_buffer *buffer = (result_buffer *)user_data;
    xxwidgets_scan_result record;
    if (buffer->failed) return;
    if (buffer->count == buffer->capacity) {
        SIZE_T capacity = buffer->capacity ? buffer->capacity * 2 : 16;
        xxwidgets_scan_result *records;
        if (capacity < buffer->capacity || capacity > ((SIZE_T)-1) / sizeof(*records)) {
            buffer->failed = 1;
            return;
        }
        records = buffer->records
            ? (xxwidgets_scan_result *)HeapReAlloc(GetProcessHeap(), 0, buffer->records, capacity * sizeof(*records))
            : (xxwidgets_scan_result *)HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*records));
        if (!records) { buffer->failed = 1; return; }
        buffer->records = records;
        buffer->capacity = capacity;
    }
    record.type = copy_utf8(type);
    record.name = copy_utf8(name);
    record.version = copy_utf8(version);
    record.info = copy_utf8(info);
    if (!record.type || !record.name || !record.version || !record.info) {
        free_result_record(&record);
        buffer->failed = 1;
        return;
    }
    buffer->records[buffer->count++] = record;
}

static void do_scan(void)
{
    SIZE_T required = 0;
    char *path, *report = NULL, *type = NULL;
    result_buffer buffer = {0};
    LARGE_INTEGER frequency, started, ended;
    xxwidgets_status status;
    int ok;

    status = xxwidgets_widget_get_text(g_path, NULL, 0, &required);
    if (status != XXWIDGETS_OK) { report_ui_error("Read file name", status); return; }
    if (required <= 1) { MessageBeep(MB_ICONASTERISK); xxwidgets_widget_focus(g_path); return; }
    path = (char *)HeapAlloc(GetProcessHeap(), 0, required);
    if (!path) { report_ui_error("Read file name", XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, required, NULL);
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Read file name", status);
        return;
    }
    status = read_panel_options();
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error("Read scan options", status);
        return;
    }
    clear_scan();
    xxwidgets_widget_set_enabled(g_scan, 0);
    xxwidgets_widget_set_text(g_msec, "Scanning...");
    xxwidgets_widget_set_text(g_status, "Scanning the selected file...");
    UpdateWindow(native(g_window));
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    ok = gui_backend_scan_results(path, collect_result, &buffer, &report, &type);
    QueryPerformanceCounter(&ended);
    if (ok) {
        WCHAR text[160];
        unsigned int msec = frequency.QuadPart > 0
            ? (unsigned int)(((ended.QuadPart - started.QuadPart) * 1000) / frequency.QuadPart) : 0;
        status = buffer.failed ? XXWIDGETS_OUT_OF_MEMORY
            : xxwidgets_scan_panel_set_results(g_panel, path, buffer.records, buffer.count, report);
        if (status != XXWIDGETS_OK) report_ui_error("Display scan results", status);
        else {
            wsprintfW(text, L"Scan complete. %u result%s. Use Report for the full engine output.",
                      (unsigned int)buffer.count, buffer.count == 1 ? L"" : L"s");
            set_status_wide(text);
        }
        wsprintfW(text, L"%u msec", msec);
        {
            char *utf8 = wide_to_utf8(text);
            if (utf8) { xxwidgets_widget_set_text(g_msec, utf8); HeapFree(GetProcessHeap(), 0, utf8); }
        }
        xxwidgets_widget_set_text(g_type, type && type[0] ? type : "Unknown type");
    } else {
        status = xxwidgets_scan_panel_set_results(g_panel, path, NULL, 0, report);
        xxwidgets_widget_set_text(g_status, report && report[0] ? report : "Cannot open the file.");
        xxwidgets_widget_set_text(g_msec, "");
        if (status != XXWIDGETS_OK) report_ui_error("Display scan report", status);
    }
    HeapFree(GetProcessHeap(), 0, path);
    if (report) gui_backend_free(report);
    if (type) gui_backend_free(type);
    free_result_buffer(&buffer);
    xxwidgets_widget_set_enabled(g_scan, 1);
}

static void do_browse(void)
{
    OPENFILENAMEW picker;
    WCHAR file[32768];
    file[0] = 0;
    GetWindowTextW(native(g_path), file, (int)(sizeof(file) / sizeof(file[0])));
    ZeroMemory(&picker, sizeof(picker));
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = native(g_window);
    picker.lpstrFile = file;
    picker.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    picker.lpstrFilter = L"All files\0*.*\0";
    picker.lpstrTitle = L"Select a file to scan";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
    if (GetOpenFileNameW(&picker)) {
        char *path = wide_to_utf8(file);
        if (path) {
            xxwidgets_widget_set_text(g_path, path);
            HeapFree(GetProcessHeap(), 0, path);
            do_scan();
        }
    }
}

static int do_options(void)
{
    xxwidgets_option options[8] = {
        {"Deep scan", g_bFlagDeep}, {"Heuristic scan", g_bFlagHeur},
        {"Verbose results", g_bFlagVerbose}, {"Aggressive scan", g_bFlagAggr},
        {"Hide unknown results", g_bFlagHideUnknown}, {"Format text report", g_bFlagFormat},
        {"Use extra signature database", g_bDbExtra}, {"Use custom signature database", g_bDbCustom}
    };
    int accepted = 0;
    xxwidgets_status status = xxwidgets_options_dialog(g_window, "cdie Options", options, 8, &accepted);
    if (status != XXWIDGETS_OK) { report_ui_error("Open Options", status); return -1; }
    if (accepted) {
        g_bFlagDeep = options[0].value; g_bFlagHeur = options[1].value;
        g_bFlagVerbose = options[2].value; g_bFlagAggr = options[3].value;
        g_bFlagHideUnknown = options[4].value; g_bFlagFormat = options[5].value;
        g_bDbExtra = options[6].value; g_bDbCustom = options[7].value;
        apply_flags_to_backend();
        status = sync_panel_options();
        if (status != XXWIDGETS_OK) report_ui_error("Update scan options", status);
        else xxwidgets_widget_set_text(g_status, "Options applied to the next scan. Use Database paths to choose signature folders.");
    }
    return accepted;
}

static xxwidgets_status configure_about(void)
{
    static const char license[] =
        "MIT License\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy "
        "of this software and associated documentation files (the Software), to deal "
        "in the Software without restriction, including without limitation the rights "
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
        "copies of the Software, and to permit persons to whom the Software is "
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in all "
        "copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";
    const char *title = gui_backend_app_title();
    const char *version = title;
    xxwidgets_status status;
    while (*version && *version != ' ') ++version;
    if (*version) ++version;
    status = xxwidgets_about_dialog_create(&g_about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_TITLE, "About cdie");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_PROGRAM_NAME, "Detect It Easy - cdie");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_VERSION, version);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_DESCRIPTION,
        "Detect It Easy scan engine, ported to C.\nNative graphical frontend with the reusable xxwidgets scan panel and dialogs.");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_COPYRIGHT,
        "Copyright (C) 2026 hors <horsicq@gmail.com>");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_WEBSITE, "https://ntinfo.biz");
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_LICENSE, license);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_set_text(g_about, XXWIDGETS_ABOUT_CREDITS,
        "Detect It Easy by hors and contributors\nxxfclib - C format and runtime support\nxxwidgets - native widgets and common dialogs");
    return status;
}

static void do_about(void)
{
    xxwidgets_status status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK) report_ui_error("Open About", status);
}

static void set_dlg_edit_utf8(HWND dialog, int id, const char *text)
{
    WCHAR *wide = utf8_to_wide(text);
    SetDlgItemTextW(dialog, id, wide ? wide : L"");
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
}

static char *get_dlg_edit_utf8(HWND dialog, int id)
{
    HWND edit = GetDlgItem(dialog, id);
    int length = GetWindowTextLengthW(edit);
    WCHAR *wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(length + 1) * sizeof(WCHAR));
    char *text;
    if (!wide) return NULL;
    GetWindowTextW(edit, wide, length + 1);
    text = wide_to_utf8(wide);
    HeapFree(GetProcessHeap(), 0, wide);
    return text;
}

static void browse_folder_into(HWND dialog, int id)
{
    BROWSEINFOW picker;
    LPITEMIDLIST item;
    WCHAR path[MAX_PATH];
    ZeroMemory(&picker, sizeof(picker));
    picker.hwndOwner = dialog;
    picker.lpszTitle = L"Select the database folder";
    picker.ulFlags = BIF_RETURNONLYFSDIRS;
    item = SHBrowseForFolderW(&picker);
    if (item) {
        if (SHGetPathFromIDListW(item, path)) SetDlgItemTextW(dialog, id, path);
        CoTaskMemFree(item);
    }
}

static INT_PTR CALLBACK database_dlg_proc(HWND dialog, UINT message, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (message) {
    case WM_INITDIALOG:
        set_dlg_edit_utf8(dialog, IDC_OPT_MAIN, gui_backend_main_db_path());
        set_dlg_edit_utf8(dialog, IDC_OPT_EXTRA, gui_backend_extra_db_path());
        set_dlg_edit_utf8(dialog, IDC_OPT_CUSTOM, gui_backend_custom_db_path());
        SendDlgItemMessageW(dialog, IDC_OPT_MAIN, EM_SETLIMITTEXT, 32767, 0);
        SendDlgItemMessageW(dialog, IDC_OPT_EXTRA, EM_SETLIMITTEXT, 32767, 0);
        SendDlgItemMessageW(dialog, IDC_OPT_CUSTOM, EM_SETLIMITTEXT, 32767, 0);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_OPT_MAIN_BROWSE: browse_folder_into(dialog, IDC_OPT_MAIN); return TRUE;
        case IDC_OPT_EXTRA_BROWSE: browse_folder_into(dialog, IDC_OPT_EXTRA); return TRUE;
        case IDC_OPT_CUSTOM_BROWSE: browse_folder_into(dialog, IDC_OPT_CUSTOM); return TRUE;
        case IDOK: {
            char *main_path = get_dlg_edit_utf8(dialog, IDC_OPT_MAIN);
            char *extra_path = get_dlg_edit_utf8(dialog, IDC_OPT_EXTRA);
            char *custom_path = get_dlg_edit_utf8(dialog, IDC_OPT_CUSTOM);
            int ok = main_path && extra_path && custom_path;
            if (ok) ok = gui_backend_set_db_paths(main_path, extra_path, custom_path);
            if (main_path) HeapFree(GetProcessHeap(), 0, main_path);
            if (extra_path) HeapFree(GetProcessHeap(), 0, extra_path);
            if (custom_path) HeapFree(GetProcessHeap(), 0, custom_path);
            if (ok) EndDialog(dialog, 1);
            else {
                SetDlgItemTextW(dialog, IDC_OPT_ERROR,
                    L"Cannot load the main signature database. Check its folder and try again.");
                SetFocus(GetDlgItem(dialog, IDC_OPT_MAIN));
            }
            return TRUE;
        }
        case IDCANCEL: EndDialog(dialog, 0); return TRUE;
        default: break;
        }
        break;
    case WM_CLOSE: EndDialog(dialog, 0); return TRUE;
    default: break;
    }
    return FALSE;
}

static void do_database_paths(void)
{
    INT_PTR accepted = DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_OPTIONS), native(g_window), database_dlg_proc, 0);
    if (accepted == 1) {
        WCHAR text[160];
        clear_scan();
        wsprintfW(text, L"Database reloaded: %d signatures. Choose a file and press Scan.", gui_backend_signature_count());
        set_status_wide(text);
    } else if (accepted == -1) report_ui_error("Open database paths", XXWIDGETS_PLATFORM_ERROR);
}

/* The native backend intentionally uses text-cell rectangles. Pixel placement
 * here allows the scan panel's result tree to fill
 * the available client area, including sizes between whole text cells. */
static void layout(void)
{
    RECT bounds;
    int width, height, results_bottom;
    const int margin = 10, row = 26, gap = 8, button = 92;
    if (!g_results) return;
    GetClientRect(native(g_window), &bounds);
    width = bounds.right; height = bounds.bottom;
    MoveWindow(native(g_fileLabel), margin, margin, 300, 16, TRUE);
    MoveWindow(native(g_path), margin, 28, width - 70, row, TRUE);
    MoveWindow(native(g_browse), width - 54, 28, 44, row, TRUE);
    MoveWindow(native(g_type), margin, 80, 136, row, TRUE);
    MoveWindow(native(g_flagLabel), 152, 62, 220, 16, TRUE);
    MoveWindow(native(g_flags), 152, 80, 220, row, TRUE);
    MoveWindow(native(g_databaseLabel), 380, 62, 170, 16, TRUE);
    MoveWindow(native(g_databases), 380, 80, 170, row, TRUE);
    MoveWindow(native(g_msec), width - margin - button, 62, button, 16, TRUE);
    MoveWindow(native(g_scan), width - margin - button, 80, button, row, TRUE);
    results_bottom = height - margin - row - gap - 36 - gap;
    if (results_bottom < 156) results_bottom = 156;
    MoveWindow(native(g_results), margin, 116, width - 2 * margin, results_bottom - 116, TRUE);
    MoveWindow(native(g_status), margin, results_bottom + gap, width - 2 * margin, 36, TRUE);
    MoveWindow(native(g_options), margin, height - margin - row, button, row, TRUE);
    MoveWindow(native(g_aboutButton), margin + button + 6, height - margin - row, button, row, TRUE);
    MoveWindow(native(g_reportButton), margin + 2 * (button + 6), height - margin - row, button, row, TRUE);
    MoveWindow(native(g_databasePaths), margin + 3 * (button + 6), height - margin - row, 116, row, TRUE);
    MoveWindow(native(g_exit), width - margin - button, height - margin - row, button, row, TRUE);
}

static LRESULT CALLBACK path_subclass(HWND handle, UINT message, WPARAM wp, LPARAM lp,
                                      UINT_PTR subclass_id, DWORD_PTR reference)
{
    (void)subclass_id; (void)reference;
    if (message == WM_GETDLGCODE) {
        MSG *key = (MSG *)lp;
        LRESULT code = DefSubclassProc(handle, message, wp, lp);
        if (key && key->wParam == VK_RETURN) code |= DLGC_WANTMESSAGE;
        return code;
    }
    if (message == WM_KEYDOWN && wp == VK_RETURN) { g_pending = ACTION_SCAN; return 0; }
    if (message == WM_CHAR && wp == VK_RETURN) return 0;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(handle, path_subclass, 1);
    return DefSubclassProc(handle, message, wp, lp);
}

static LRESULT CALLBACK window_subclass(HWND handle, UINT message, WPARAM wp, LPARAM lp,
                                        UINT_PTR subclass_id, DWORD_PTR reference)
{
    (void)subclass_id; (void)reference;
    switch (message) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lp;
        info->ptMinTrackSize.x = 720;
        info->ptMinTrackSize.y = 380;
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT length = DragQueryFileW(drop, 0, NULL, 0);
        WCHAR *wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)length + 1) * sizeof(WCHAR));
        if (wide && DragQueryFileW(drop, 0, wide, length + 1)) {
            char *path = wide_to_utf8(wide);
            if (path) {
                xxwidgets_widget_set_text(g_path, path);
                HeapFree(GetProcessHeap(), 0, path);
                g_pending = ACTION_SCAN;
            }
        }
        if (wide) HeapFree(GetProcessHeap(), 0, wide);
        DragFinish(drop);
        return 0;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(handle, window_subclass, 1); break;
    default: break;
    }
    return DefSubclassProc(handle, message, wp, lp);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    (void)user_data;
    if ((event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) ||
        (event->type == XXWIDGETS_EVENT_CLICK && event->widget == g_exit)) {
        g_running = 0;
        xxwidgets_app_quit(app, 0);
    } else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) layout();
    else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == g_scan) g_pending = ACTION_SCAN;
        else if (event->widget == g_browse) g_pending = ACTION_BROWSE;
        else if (event->widget == g_options) g_pending = ACTION_OPTIONS;
        else if (event->widget == g_aboutButton) g_pending = ACTION_ABOUT;
        else if (event->widget == g_reportButton) g_pending = ACTION_REPORT;
        else if (event->widget == g_databasePaths) g_pending = ACTION_DATABASE_PATHS;
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               (event->widget == g_flags || event->widget == g_databases)) {
        xxwidgets_status status = read_panel_options();
        if (status != XXWIDGETS_OK) report_ui_error("Read scan options", status);
    } else if (event->type == XXWIDGETS_EVENT_SHORTCUT) g_pending = event->value;
}

static int create_control(xxwidgets_widget **control, xxwidgets_kind kind, const char *text, int id)
{
    xxwidgets_rect bounds = {1, 1, 12, 1};
    xxwidgets_status status = xxwidgets_widget_create(g_app, g_window, kind, text, bounds, control);
    if (status != XXWIDGETS_OK) return 0;
    if (id) SetWindowLongPtrW(native(*control), GWLP_ID, id);
    return 1;
}

static int create_controls(void)
{
    xxwidgets_rect panel_bounds = {1, 4, 96, 18};
    xxwidgets_shortcut shortcuts[] = {
        {"F5", ACTION_SCAN}, {"Ctrl+O", ACTION_BROWSE}, {"F1", ACTION_ABOUT},
        {"Ctrl+Shift+D", ACTION_DATABASE_PATHS}
    };
    if (!create_control(&g_fileLabel, XXWIDGETS_LABEL, "File name", 0) ||
        !create_control(&g_path, XXWIDGETS_EDIT, "", IDC_EDIT_PATH) ||
        !create_control(&g_browse, XXWIDGETS_BUTTON, "...", IDC_BTN_BROWSE) ||
        !create_control(&g_type, XXWIDGETS_LABEL, "Type: -", IDC_TYPE) ||
        !create_control(&g_msec, XXWIDGETS_LABEL, "", IDC_STATIC_MSEC) ||
        !create_control(&g_status, XXWIDGETS_LABEL, "", IDC_STATUS) ||
        !create_control(&g_options, XXWIDGETS_BUTTON, "Options", IDC_BTN_OPTIONS) ||
        !create_control(&g_aboutButton, XXWIDGETS_BUTTON, "About", IDC_BTN_ABOUT) ||
        !create_control(&g_databasePaths, XXWIDGETS_BUTTON, "Database paths", IDC_BTN_DB_PATHS) ||
        !create_control(&g_exit, XXWIDGETS_BUTTON, "Exit", IDC_BTN_EXIT)) return 0;
    if (xxwidgets_scan_panel_create(g_window, panel_bounds, &g_panel) != XXWIDGETS_OK) return 0;
    g_flags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS);
    g_databases = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES);
    g_scan = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SCAN);
    g_reportButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_REPORT);
    g_results = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_RESULTS);
    g_flagLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS_LABEL);
    g_databaseLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL);
    if (!g_flags || !g_databases || !g_scan || !g_reportButton || !g_results ||
        !g_flagLabel || !g_databaseLabel || sync_panel_options() != XXWIDGETS_OK) return 0;
    SetWindowLongPtrW(native(g_flags), GWLP_ID, IDC_BTN_FLAGS);
    SetWindowLongPtrW(native(g_databases), GWLP_ID, IDC_BTN_DB);
    SetWindowLongPtrW(native(g_scan), GWLP_ID, IDC_BTN_SCAN);
    SetWindowLongPtrW(native(g_reportButton), GWLP_ID, IDC_BTN_REPORT);
    SetWindowLongPtrW(native(g_results), GWLP_ID, IDC_SCANRESULTS);
    xxwidgets_widget_set_text(g_databaseLabel, "Databases (Main always)");
    if (!SetWindowSubclass(native(g_path), path_subclass, 1, 0) ||
        !SetWindowSubclass(native(g_window), window_subclass, 1, 0)) return 0;
    SetWindowLongPtrW(native(g_type), GWL_STYLE, GetWindowLongPtrW(native(g_type), GWL_STYLE) | SS_SUNKEN);
    SetWindowLongPtrW(native(g_msec), GWL_STYLE,
        (GetWindowLongPtrW(native(g_msec), GWL_STYLE) & ~(LONG_PTR)SS_TYPEMASK) | SS_RIGHT);
    DragAcceptFiles(native(g_window), TRUE);
    return xxwidgets_window_set_shortcuts(g_window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0])) == XXWIDGETS_OK;
}

/* Explicit --smoke-test mode validates the real GUI and modal ownership with
 * bounded timers on the UI thread. It never scans a user file or alters paths. */
static const WCHAR *g_smokeTitle;
static UINT_PTR g_smokeTimer;
static int g_smokeAction, g_smokeDone, g_smokeTicks;

static BOOL CALLBACK smoke_children(HWND child, LPARAM data)
{
    HWND *controls = (HWND *)data;
    WCHAR text[64], class_name[32];
    GetClassNameW(child, class_name, 32);
    if (lstrcmpW(class_name, L"Button")) return TRUE;
    GetWindowTextW(child, text, 64);
    if (!lstrcmpW(text, L"OK")) controls[1] = child;
    else if (!lstrcmpW(text, L"Cancel")) controls[2] = child;
    if ((GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX && !controls[0]) controls[0] = child;
    return TRUE;
}

static BOOL CALLBACK smoke_windows(HWND window, LPARAM data)
{
    WCHAR title[128];
    HWND controls[3] = {0};
    (void)data;
    if (GetWindow(window, GW_OWNER) != native(g_window)) return TRUE;
    GetWindowTextW(window, title, 128);
    if (lstrcmpW(title, g_smokeTitle)) return TRUE;
    if (IsWindowEnabled(native(g_window))) return TRUE;
    if (g_smokeAction == 3) {
        HWND cancel = GetDlgItem(window, IDCANCEL);
        if (!cancel) return TRUE;
        SetDlgItemTextW(window, IDC_OPT_MAIN, L"Smoke test cancelled database path");
        SendMessageW(cancel, BM_CLICK, 0, 0);
    } else if (g_smokeAction) {
        EnumChildWindows(window, smoke_children, (LPARAM)controls);
        if (!controls[0] || !controls[1] || !controls[2]) return TRUE;
        SendMessageW(controls[0], BM_CLICK, 0, 0);
        SendMessageW(controls[g_smokeAction == 1 ? 1 : 2], BM_CLICK, 0, 0);
    } else PostMessageW(window, WM_CLOSE, 0, 0);
    g_smokeDone = 1;
    KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    return FALSE;
}

static VOID CALLBACK smoke_tick(HWND window, UINT message, UINT_PTR timer, DWORD time)
{
    (void)window; (void)message; (void)timer; (void)time;
    EnumThreadWindows(GetCurrentThreadId(), smoke_windows, 0);
    if (!g_smokeDone && ++g_smokeTicks >= 250) {
        KillTimer(NULL, g_smokeTimer);
        g_smokeTimer = 0;
        xxwidgets_app_quit(g_app, 1);
    }
}

static int smoke_begin(const WCHAR *title, int action)
{
    g_smokeTitle = title; g_smokeAction = action;
    g_smokeDone = g_smokeTicks = 0;
    g_smokeTimer = SetTimer(NULL, 0, 20, smoke_tick);
    return g_smokeTimer != 0;
}

static int smoke_finish(void)
{
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    return g_smokeDone && IsWindowEnabled(native(g_window));
}

static int run_smoke(void)
{
    xxwidgets_scan_result row = {"Compiler", "Microsoft Visual C/C++", "19.29", "Smoke test"};
    unsigned int flags, databases;
    SIZE_T report_length = 0;
    xxwidgets_status status;
    if (xxwidgets_widget_kind(g_results) != XXWIDGETS_TREEVIEW ||
        xxwidgets_widget_kind(g_flags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_databases) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_scan) != XXWIDGETS_BUTTON ||
        xxwidgets_widget_kind(g_reportButton) != XXWIDGETS_BUTTON ||
        xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != 3u ||
        xxwidgets_scan_panel_set_results(g_panel, "Smoke file", &row, 1,
            "Smoke report\nCompiler: Microsoft Visual C/C++ 19.29") != XXWIDGETS_OK ||
        xxwidgets_scan_panel_count(g_panel) != 1 || TreeView_GetCount(native(g_results)) < 2 ||
        xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
        report_length <= 1) return 1;
    if (gui_backend_signature_count() > 0) {
        WCHAR executable[32768];
        char *path;
        char type[256];
        DWORD length = GetModuleFileNameW(NULL, executable, (DWORD)(sizeof(executable) / sizeof(executable[0])));
        if (!length || length >= sizeof(executable) / sizeof(executable[0])) return 1;
        path = wide_to_utf8(executable);
        if (!path) return 1;
        status = xxwidgets_widget_set_text(g_path, path);
        HeapFree(GetProcessHeap(), 0, path);
        if (status != XXWIDGETS_OK) return 1;
        do_scan();
        if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
            report_length <= 1 || !xxwidgets_scan_panel_count(g_panel) || TreeView_GetCount(native(g_results)) < 2 ||
            xxwidgets_widget_get_text(g_type, type, sizeof(type), NULL) != XXWIDGETS_OK || !type[0]) return 1;
    }
    if (!smoke_begin(L"cdie Options", 2) || do_options() != 0 || !smoke_finish() || !g_bFlagDeep) return 1;
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u) return 1;
    if (!smoke_begin(L"cdie Options", 1) || do_options() != 1 || !smoke_finish() || g_bFlagDeep) return 1;
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 6u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != 3u) return 1;
    if (!smoke_begin(L"About cdie", 0)) return 1;
    status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK || !smoke_finish()) return 1;
    if (!smoke_begin(L"Scan report", 0)) return 1;
    status = xxwidgets_scan_panel_show_report(g_panel);
    if (status != XXWIDGETS_OK || !smoke_finish()) return 1;
    {
        char *original_path = copy_utf8(gui_backend_main_db_path());
        int changed;
        if (!original_path || !smoke_begin(L"Database paths", 3)) {
            if (original_path) HeapFree(GetProcessHeap(), 0, original_path);
            return 1;
        }
        do_database_paths();
        changed = lstrcmpA(original_path, gui_backend_main_db_path());
        HeapFree(GetProcessHeap(), 0, original_path);
        if (!smoke_finish() || changed) return 1;
    }
    return xxwidgets_scan_panel_clear(g_panel) != XXWIDGETS_OK ||
           xxwidgets_scan_panel_count(g_panel) != 0 || TreeView_GetCount(native(g_results)) != 0;
}

static int is_smoke_requested(void)
{
    const WCHAR *command = GetCommandLineW();
    const WCHAR token[] = L"--smoke-test";
    SIZE_T i, length = (sizeof(token) / sizeof(token[0])) - 1;
    for (; *command; ++command) {
        for (i = 0; i < length && command[i] == token[i]; ++i) { }
        if (i == length && (!command[i] || command[i] == L' ' || command[i] == L'"')) return 1;
    }
    return 0;
}

int gui_run(void)
{
    xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
    xxwidgets_rect bounds = {8, 4, 100, 27};
    xxwidgets_status status;
    int result = 0, smoke = is_smoke_requested();
    WCHAR title[160], ready[512];
    WCHAR *base;
    char *utf8_title;

    g_hInst = GetModuleHandleW(NULL);
    status = xxwidgets_app_create(&config, &g_app);
    if (status != XXWIDGETS_OK) return 1;
    base = utf8_to_wide(gui_backend_app_title());
#if defined(_WIN64)
    wsprintfW(title, L"%s (x64)", base ? base : L"cdie");
#else
    wsprintfW(title, L"%s (x86)", base ? base : L"cdie");
#endif
    if (base) HeapFree(GetProcessHeap(), 0, base);
    utf8_title = wide_to_utf8(title);
    if (!utf8_title) { result = 1; goto finish; }
    status = xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, utf8_title, bounds, &g_window);
    HeapFree(GetProcessHeap(), 0, utf8_title);
    if (status != XXWIDGETS_OK) { result = 1; goto finish; }
    xxwidgets_widget_set_visible(g_window, 0);
    if (!create_controls() || configure_about() != XXWIDGETS_OK) { result = 1; goto finish; }
    SetWindowPos(native(g_window), NULL, 0, 0, 850, 580, SWP_NOMOVE | SWP_NOZORDER);
    if (gui_backend_init()) {
        wsprintfW(ready, L"Ready. %d signatures loaded. Choose a file or drag one here, then press Scan.",
                  gui_backend_signature_count());
        set_status_wide(ready);
    } else {
        xxwidgets_widget_set_text(g_status,
            "Main signature database not found. Use Database paths to select the database folder.");
    }
    apply_flags_to_backend();
    layout();
    if (smoke) { result = run_smoke(); goto finish; }
    xxwidgets_widget_set_visible(g_window, 1);
    xxwidgets_widget_focus(g_path);
    g_running = 1;
    while (g_running) {
        int action;
        status = xxwidgets_app_poll(g_app, 30);
        if (status != XXWIDGETS_OK) { result = 1; break; }
        if (!g_running) break;
        action = g_pending; g_pending = ACTION_NONE;
        /* All modal loops and scans start after returning from app_poll. */
        switch (action) {
        case ACTION_SCAN: do_scan(); break;
        case ACTION_BROWSE: do_browse(); break;
        case ACTION_OPTIONS: do_options(); break;
        case ACTION_ABOUT: do_about(); break;
        case ACTION_DATABASE_PATHS: do_database_paths(); break;
        case ACTION_REPORT:
            status = xxwidgets_scan_panel_show_report(g_panel);
            if (status != XXWIDGETS_OK) report_ui_error("Open scan report", status);
            break;
        default: break;
        }
    }
finish:
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    gui_backend_shutdown();
    if (g_about && xxwidgets_about_dialog_destroy(g_about) != XXWIDGETS_OK) result = 1;
    g_about = NULL;
    if (g_panel && xxwidgets_scan_panel_destroy(g_panel) != XXWIDGETS_OK) result = 1;
    g_panel = NULL;
    if (xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
    g_app = NULL;
    return result;
}
