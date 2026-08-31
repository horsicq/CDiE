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

/* main_gui.c - a pure-WinAPI front end for the cdie engine, laid out after the
 * Detect It Easy main window: a file-name field with a browse button, a row of
 * detected-type / Flags / Databases dropdowns, a scan time and a Scan button, a
 * read-only result pane, and Exit.
 *
 * Pure Win32: only USER32/GDI32/COMDLG32/COMCTL32/SHELL32 and, through
 * gui_backend, KERNEL32. No C runtime is required (see gui_entry.c); this file
 * therefore uses Win32 for everything - HeapAlloc for scratch buffers,
 * wsprintfW for formatting, MultiByteToWideChar at the UTF-8 boundary. It never
 * calls a CRT function directly. */

/* All Win32 calls here are the explicit ...W forms; defining UNICODE makes the
 * resource-id macros (IDC_ARROW, IDI_APPLICATION) resolve to their wide form so
 * they match the ...W functions with no LPSTR->LPCWSTR narrowing. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include "gui_backend.h"
#include "resource.h"

/* ------------------------------------------------------------- control ids -- */

#define IDC_LBL_FILE    101
#define IDC_EDIT_PATH   102
#define IDC_BTN_BROWSE  103
#define IDC_COMBO_TYPE  104
#define IDC_BTN_FLAGS   105
#define IDC_BTN_DB      106
#define IDC_STATIC_MSEC 107
#define IDC_BTN_SCAN    108
#define IDC_EDIT_OUTPUT 109
#define IDC_BTN_EXIT    110
#define IDC_BTN_OPTIONS 111
#define IDC_BTN_ABOUT   112

/* checkable popup-menu items */
#define IDM_FLAG_DEEP    2001
#define IDM_FLAG_HEUR    2002
#define IDM_FLAG_VERBOSE 2003
#define IDM_FLAG_AGGR    2004
#define IDM_FLAG_HIDEUNK 2005
#define IDM_FLAG_FORMAT  2006

#define IDM_DB_MAIN   2101
#define IDM_DB_EXTRA  2102
#define IDM_DB_CUSTOM 2103

/* ------------------------------------------------------------------ state -- */

static HINSTANCE g_hInst = NULL;
static HWND g_hLblFile = NULL;
static HWND g_hEditPath = NULL;
static HWND g_hBtnBrowse = NULL;
static HWND g_hComboType = NULL;
static HWND g_hBtnFlags = NULL;
static HWND g_hBtnDb = NULL;
static HWND g_hStaticMsec = NULL;
static HWND g_hBtnScan = NULL;
static HWND g_hEditOutput = NULL;
static HWND g_hBtnExit = NULL;
static HWND g_hBtnOptions = NULL;
static HWND g_hBtnAbout = NULL;

static HFONT g_hFontUI = NULL;
static HFONT g_hFontMono = NULL;
static WNDPROC g_pOldEditProc = NULL;

/* Scan-flag toggles. Defaults mirror the Detect It Easy GUI. */
static int g_bFlagDeep = 1;
static int g_bFlagHeur = 1;
static int g_bFlagVerbose = 1;
static int g_bFlagAggr = 0;
static int g_bFlagHideUnknown = 0;
static int g_bFlagFormat = 0;

/* Database toggles. Main is always on; extra/custom default on, as the console. */
static int g_bDbExtra = 1;
static int g_bDbCustom = 1;

/* ------------------------------------------------------------- utilities --- */

static void apply_flags_to_backend(void)
{
    gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose, g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
    gui_backend_set_databases(g_bDbExtra, g_bDbCustom);
}

/* Converts a UTF-8 string to a freshly HeapAlloc'ed wide string (process heap).
 * Caller frees with HeapFree. Returns NULL on failure. */
static WCHAR *utf8_to_wide(const char *pUtf8)
{
    int nLen = 0;
    WCHAR *pWide = NULL;

    if (pUtf8 == NULL) {
        return NULL;
    }

    nLen = MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, NULL, 0);

    if (nLen <= 0) {
        return NULL;
    }

    pWide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nLen * sizeof(WCHAR));

    if (pWide == NULL) {
        return NULL;
    }

    MultiByteToWideChar(CP_UTF8, 0, pUtf8, -1, pWide, nLen);

    return pWide;
}

/* Converts a wide string to a freshly HeapAlloc'ed UTF-8 string. */
static char *wide_to_utf8(const WCHAR *pWide)
{
    int nLen = 0;
    char *pUtf8 = NULL;

    if (pWide == NULL) {
        return NULL;
    }

    nLen = WideCharToMultiByte(CP_UTF8, 0, pWide, -1, NULL, 0, NULL, NULL);

    if (nLen <= 0) {
        return NULL;
    }

    pUtf8 = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)nLen);

    if (pUtf8 == NULL) {
        return NULL;
    }

    WideCharToMultiByte(CP_UTF8, 0, pWide, -1, pUtf8, nLen, NULL, NULL);

    return pUtf8;
}

/* Puts UTF-8 text into the read-only result edit, translating the engine's bare
 * '\n' line endings into the '\r\n' that a multiline EDIT needs. */
static void set_output_text_utf8(const char *pUtf8)
{
    const char *pRead = NULL;
    char *pExpanded = NULL;
    char *pWrite = NULL;
    SIZE_T nCount = 0;
    SIZE_T nNewlines = 0;
    WCHAR *pWide = NULL;

    if (pUtf8 == NULL) {
        SetWindowTextW(g_hEditOutput, L"");
        return;
    }

    for (pRead = pUtf8; *pRead != 0; pRead++) {
        nCount++;

        if (*pRead == '\n') {
            nNewlines++;
        }
    }

    pExpanded = (char *)HeapAlloc(GetProcessHeap(), 0, nCount + nNewlines + 1);

    if (pExpanded == NULL) {
        return;
    }

    pWrite = pExpanded;

    for (pRead = pUtf8; *pRead != 0; pRead++) {
        if (*pRead == '\n') {
            *pWrite++ = '\r';
        }

        *pWrite++ = *pRead;
    }

    *pWrite = 0;

    pWide = utf8_to_wide(pExpanded);
    HeapFree(GetProcessHeap(), 0, pExpanded);

    if (pWide != NULL) {
        SetWindowTextW(g_hEditOutput, pWide);
        HeapFree(GetProcessHeap(), 0, pWide);
    }
}

/* Shows the detected type as the single item of the type combo. */
static void set_type_combo_utf8(const char *pTypeUtf8)
{
    SendMessageW(g_hComboType, CB_RESETCONTENT, 0, 0);

    if ((pTypeUtf8 != NULL) && (pTypeUtf8[0] != 0)) {
        WCHAR *pWide = utf8_to_wide(pTypeUtf8);

        if (pWide != NULL) {
            SendMessageW(g_hComboType, CB_ADDSTRING, 0, (LPARAM)pWide);
            SendMessageW(g_hComboType, CB_SETCURSEL, 0, 0);
            HeapFree(GetProcessHeap(), 0, pWide);
        }
    }
}

/* ------------------------------------------------------------------ scan --- */

static void do_scan(HWND hwnd)
{
    int nLenW = 0;
    WCHAR *pPathW = NULL;
    char *pPathU8 = NULL;
    char *pResultU8 = NULL;
    char *pTypeU8 = NULL;
    int bOk = 0;
    LARGE_INTEGER nFreq;
    LARGE_INTEGER nStart;
    LARGE_INTEGER nEnd;

    nLenW = GetWindowTextLengthW(g_hEditPath);

    if (nLenW <= 0) {
        MessageBeep(MB_ICONASTERISK);
        SetFocus(g_hEditPath);
        return;
    }

    pPathW = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(nLenW + 1) * sizeof(WCHAR));

    if (pPathW == NULL) {
        return;
    }

    GetWindowTextW(g_hEditPath, pPathW, nLenW + 1);
    pPathU8 = wide_to_utf8(pPathW);
    HeapFree(GetProcessHeap(), 0, pPathW);

    if (pPathU8 == NULL) {
        return;
    }

    apply_flags_to_backend();

    /* Synchronous scan against the preloaded database - fast, and it keeps the
     * engine's global-free single-thread contract simple. Give a hint first. */
    EnableWindow(g_hBtnScan, FALSE);
    SetWindowTextW(g_hStaticMsec, L"Scanning...");
    UpdateWindow(g_hStaticMsec);

    QueryPerformanceFrequency(&nFreq);
    QueryPerformanceCounter(&nStart);
    bOk = gui_backend_scan(pPathU8, &pResultU8, &pTypeU8);
    QueryPerformanceCounter(&nEnd);

    HeapFree(GetProcessHeap(), 0, pPathU8);

    if (bOk) {
        WCHAR szMsec[64];
        unsigned int nMsec = 0;

        if (nFreq.QuadPart > 0) {
            nMsec = (unsigned int)(((nEnd.QuadPart - nStart.QuadPart) * 1000) / nFreq.QuadPart);
        }

        set_output_text_utf8(pResultU8);
        set_type_combo_utf8(pTypeU8);

        wsprintfW(szMsec, L"%u msec", nMsec);
        SetWindowTextW(g_hStaticMsec, szMsec);

        gui_backend_free(pResultU8);
        gui_backend_free(pTypeU8);
    } else {
        SetWindowTextW(g_hEditOutput, L"Cannot open the file.");
        SetWindowTextW(g_hStaticMsec, L"");
        SendMessageW(g_hComboType, CB_RESETCONTENT, 0, 0);
    }

    EnableWindow(g_hBtnScan, TRUE);
}

static void do_browse(HWND hwnd)
{
    OPENFILENAMEW ofn;
    WCHAR szFile[1024];

    szFile[0] = 0;
    GetWindowTextW(g_hEditPath, szFile, (int)(sizeof(szFile) / sizeof(szFile[0])));

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = (DWORD)(sizeof(szFile) / sizeof(szFile[0]));
    ofn.lpstrFilter = L"All files\0*.*\0";
    ofn.lpstrTitle = L"Select a file to scan";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;

    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(g_hEditPath, szFile);
        do_scan(hwnd);
    }
}

/* -------------------------------------------------------- about / options -- */

static void do_about(HWND hwnd)
{
    WCHAR szText[512];
    WCHAR *pTitle = utf8_to_wide(gui_backend_app_title());

    wsprintfW(szText,
              L"%s\r\n\r\n"
              L"Detect It Easy console engine - C port.\r\n"
              L"Native WinAPI front end - no external dependencies.\r\n\r\n"
              L"%d signatures loaded.\r\n\r\n"
              L"Copyright (C) 2026 hors\r\n"
              L"http://ntinfo.biz",
              (pTitle != NULL) ? pTitle : L"cdie", gui_backend_signature_count());

    MessageBoxW(hwnd, szText, L"About cdie", MB_OK | MB_ICONINFORMATION);

    if (pTitle != NULL) {
        HeapFree(GetProcessHeap(), 0, pTitle);
    }
}

static void set_dlg_edit_utf8(HWND hDlg, int nId, const char *pUtf8)
{
    WCHAR *pWide = utf8_to_wide(pUtf8);

    SetDlgItemTextW(hDlg, nId, (pWide != NULL) ? pWide : L"");

    if (pWide != NULL) {
        HeapFree(GetProcessHeap(), 0, pWide);
    }
}

/* Reads a dialog edit into a fresh UTF-8 string (HeapFree to release). */
static char *get_dlg_edit_utf8(HWND hDlg, int nId)
{
    HWND hEdit = GetDlgItem(hDlg, nId);
    int nLenW = GetWindowTextLengthW(hEdit);
    WCHAR *pWide = NULL;
    char *pUtf8 = NULL;

    pWide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)(nLenW + 1) * sizeof(WCHAR));

    if (pWide == NULL) {
        return NULL;
    }

    GetWindowTextW(hEdit, pWide, nLenW + 1);
    pUtf8 = wide_to_utf8(pWide);
    HeapFree(GetProcessHeap(), 0, pWide);

    return pUtf8;
}

/* "..." next to a path field: a folder picker whose choice fills the field. */
static void browse_folder_into(HWND hDlg, int nEditId)
{
    BROWSEINFOW bi;
    LPITEMIDLIST pidl = NULL;
    WCHAR szPath[MAX_PATH];

    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hDlg;
    bi.lpszTitle = L"Select the database folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;

    pidl = SHBrowseForFolderW(&bi);

    if (pidl != NULL) {
        if (SHGetPathFromIDListW(pidl, szPath)) {
            SetDlgItemTextW(hDlg, nEditId, szPath);
        }

        CoTaskMemFree(pidl);
    }
}

static INT_PTR CALLBACK options_dlg_proc(HWND hDlg, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (nMsg) {
        case WM_INITDIALOG:
            set_dlg_edit_utf8(hDlg, IDC_OPT_MAIN, gui_backend_main_db_path());
            set_dlg_edit_utf8(hDlg, IDC_OPT_EXTRA, gui_backend_extra_db_path());
            set_dlg_edit_utf8(hDlg, IDC_OPT_CUSTOM, gui_backend_custom_db_path());
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_OPT_MAIN_BROWSE: browse_folder_into(hDlg, IDC_OPT_MAIN); return TRUE;
                case IDC_OPT_EXTRA_BROWSE: browse_folder_into(hDlg, IDC_OPT_EXTRA); return TRUE;
                case IDC_OPT_CUSTOM_BROWSE: browse_folder_into(hDlg, IDC_OPT_CUSTOM); return TRUE;

                case IDOK: {
                    char *pMain = get_dlg_edit_utf8(hDlg, IDC_OPT_MAIN);
                    char *pExtra = get_dlg_edit_utf8(hDlg, IDC_OPT_EXTRA);
                    char *pCustom = get_dlg_edit_utf8(hDlg, IDC_OPT_CUSTOM);

                    gui_backend_set_db_paths(pMain, pExtra, pCustom);

                    if (pMain != NULL) HeapFree(GetProcessHeap(), 0, pMain);
                    if (pExtra != NULL) HeapFree(GetProcessHeap(), 0, pExtra);
                    if (pCustom != NULL) HeapFree(GetProcessHeap(), 0, pCustom);

                    EndDialog(hDlg, 1);
                    return TRUE;
                }

                case IDCANCEL:
                    EndDialog(hDlg, 0);
                    return TRUE;

                default:
                    break;
            }
            break;

        default:
            break;
    }

    return FALSE;
}

static void do_options(HWND hwnd)
{
    INT_PTR nResult = DialogBoxParamW(g_hInst, MAKEINTRESOURCEW(IDD_OPTIONS), hwnd, options_dlg_proc, 0);

    if (nResult == 1) {
        WCHAR szMsg[160];

        wsprintfW(szMsg, L"Database reloaded - %d signatures.\r\nChoose a file and press Scan.", gui_backend_signature_count());
        SetWindowTextW(g_hEditOutput, szMsg);
        SetWindowTextW(g_hStaticMsec, L"");
        SendMessageW(g_hComboType, CB_RESETCONTENT, 0, 0);
    }
}

/* ------------------------------------------------- checkable dropdowns ----- */

static void show_flags_menu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    RECT rc;
    int nId = 0;

    if (hMenu == NULL) {
        return;
    }

    AppendMenuW(hMenu, MF_STRING | (g_bFlagDeep ? MF_CHECKED : MF_UNCHECKED), IDM_FLAG_DEEP, L"Deep scan");
    AppendMenuW(hMenu, MF_STRING | (g_bFlagHeur ? MF_CHECKED : MF_UNCHECKED), IDM_FLAG_HEUR, L"Heuristic scan");
    AppendMenuW(hMenu, MF_STRING | (g_bFlagVerbose ? MF_CHECKED : MF_UNCHECKED), IDM_FLAG_VERBOSE, L"Verbose");
    AppendMenuW(hMenu, MF_STRING | (g_bFlagAggr ? MF_CHECKED : MF_UNCHECKED), IDM_FLAG_AGGR, L"Aggressive scan");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING | (g_bFlagHideUnknown ? MF_CHECKED : MF_UNCHECKED), IDM_FLAG_HIDEUNK, L"Hide unknown");
    AppendMenuW(hMenu, MF_STRING | (g_bFlagFormat ? MF_CHECKED : MF_UNCHECKED), IDM_FLAG_FORMAT, L"Format result");

    GetWindowRect(g_hBtnFlags, &rc);
    nId = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (nId) {
        case IDM_FLAG_DEEP: g_bFlagDeep = !g_bFlagDeep; break;
        case IDM_FLAG_HEUR: g_bFlagHeur = !g_bFlagHeur; break;
        case IDM_FLAG_VERBOSE: g_bFlagVerbose = !g_bFlagVerbose; break;
        case IDM_FLAG_AGGR: g_bFlagAggr = !g_bFlagAggr; break;
        case IDM_FLAG_HIDEUNK: g_bFlagHideUnknown = !g_bFlagHideUnknown; break;
        case IDM_FLAG_FORMAT: g_bFlagFormat = !g_bFlagFormat; break;
        default: return;
    }

    apply_flags_to_backend();
}

static void show_db_menu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    RECT rc;
    int nId = 0;

    if (hMenu == NULL) {
        return;
    }

    /* Main is always used; show it checked and greyed, like the DIE GUI. */
    AppendMenuW(hMenu, MF_STRING | MF_CHECKED | MF_GRAYED, IDM_DB_MAIN, L"Main");
    AppendMenuW(hMenu, MF_STRING | (g_bDbExtra ? MF_CHECKED : MF_UNCHECKED), IDM_DB_EXTRA, L"Extra");
    AppendMenuW(hMenu, MF_STRING | (g_bDbCustom ? MF_CHECKED : MF_UNCHECKED), IDM_DB_CUSTOM, L"Custom");

    GetWindowRect(g_hBtnDb, &rc);
    nId = (int)TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    switch (nId) {
        case IDM_DB_EXTRA: g_bDbExtra = !g_bDbExtra; break;
        case IDM_DB_CUSTOM: g_bDbCustom = !g_bDbCustom; break;
        default: return;
    }

    apply_flags_to_backend();
}

/* ---------------------------------------------------------------- layout --- */

static void layout(HWND hwnd)
{
    RECT rc;
    int nW = 0;
    int nH = 0;
    const int nMargin = 10;
    const int nRow = 26;
    const int nBrowseW = 44;
    const int nScanW = 92;
    const int nMsecW = 92;
    const int nGap = 8;
    int nY = 0;
    int nOutBottom = 0;

    GetClientRect(hwnd, &rc);
    nW = rc.right;
    nH = rc.bottom;

    nY = nMargin;
    MoveWindow(g_hLblFile, nMargin, nY, 200, 16, TRUE);
    nY += 18;

    MoveWindow(g_hEditPath, nMargin, nY, nW - (2 * nMargin) - nBrowseW - 6, nRow, TRUE);
    MoveWindow(g_hBtnBrowse, nW - nMargin - nBrowseW, nY, nBrowseW, nRow, TRUE);
    nY += nRow + nGap;

    /* Type combo, then the two checkable dropdowns; Scan sits at the right with
     * the elapsed time just to its left. */
    MoveWindow(g_hComboType, nMargin, nY, 130, 220, TRUE);
    MoveWindow(g_hBtnFlags, nMargin + 136, nY, 108, nRow, TRUE);
    MoveWindow(g_hBtnDb, nMargin + 136 + 114, nY, 108, nRow, TRUE);
    MoveWindow(g_hBtnScan, nW - nMargin - nScanW, nY, nScanW, nRow, TRUE);
    MoveWindow(g_hStaticMsec, nW - nMargin - nScanW - 6 - nMsecW, nY + 4, nMsecW, 18, TRUE);
    nY += nRow + nGap;

    nOutBottom = nH - nMargin - nRow - nGap;

    if (nOutBottom < nY + 40) {
        nOutBottom = nY + 40;
    }

    MoveWindow(g_hEditOutput, nMargin, nY, nW - (2 * nMargin), nOutBottom - nY, TRUE);

    /* Bottom row: Options and About at the left, Exit at the right. */
    MoveWindow(g_hBtnOptions, nMargin, nH - nMargin - nRow, nScanW, nRow, TRUE);
    MoveWindow(g_hBtnAbout, nMargin + nScanW + 6, nH - nMargin - nRow, nScanW, nRow, TRUE);
    MoveWindow(g_hBtnExit, nW - nMargin - nScanW, nH - nMargin - nRow, nScanW, nRow, TRUE);
}

/* ---------------------------------------------------------------- fonts ---- */

static void create_fonts(void)
{
    HDC hdc = GetDC(NULL);
    int nHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);

    ReleaseDC(NULL, hdc);

    g_hFontUI = CreateFontW(nHeight, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_hFontMono = CreateFontW(nHeight, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Consolas");
}

static void set_font(HWND hCtl, HFONT hFont)
{
    SendMessageW(hCtl, WM_SETFONT, (WPARAM)hFont, MAKELPARAM(TRUE, 0));
}

/* Enter in the path field starts a scan (and swallows the message beep). */
static LRESULT CALLBACK path_edit_proc(HWND hwnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
    if ((nMsg == WM_KEYDOWN) && (wParam == VK_RETURN)) {
        do_scan(GetParent(hwnd));
        return 0;
    }

    if ((nMsg == WM_CHAR) && (wParam == VK_RETURN)) {
        return 0;
    }

    return CallWindowProcW(g_pOldEditProc, hwnd, nMsg, wParam, lParam);
}

/* --------------------------------------------------------------- create ---- */

static void create_controls(HWND hwnd)
{
    g_hLblFile = CreateWindowExW(0, L"STATIC", L"File name", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_LBL_FILE,
                                 g_hInst, NULL);
    g_hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
                                  (HMENU)(INT_PTR)IDC_EDIT_PATH, g_hInst, NULL);
    g_hBtnBrowse = CreateWindowExW(0, L"BUTTON", L"...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                   (HMENU)(INT_PTR)IDC_BTN_BROWSE, g_hInst, NULL);
    g_hComboType = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0, 0, hwnd,
                                   (HMENU)(INT_PTR)IDC_COMBO_TYPE, g_hInst, NULL);
    g_hBtnFlags = CreateWindowExW(0, L"BUTTON", L"Flags", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                  (HMENU)(INT_PTR)IDC_BTN_FLAGS, g_hInst, NULL);
    g_hBtnDb = CreateWindowExW(0, L"BUTTON", L"Databases", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                               (HMENU)(INT_PTR)IDC_BTN_DB, g_hInst, NULL);
    g_hStaticMsec = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_STATIC_MSEC,
                                    g_hInst, NULL);
    g_hBtnScan = CreateWindowExW(0, L"BUTTON", L"Scan", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hwnd,
                                 (HMENU)(INT_PTR)IDC_BTN_SCAN, g_hInst, NULL);
    g_hEditOutput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                        ES_AUTOHSCROLL,
                                    0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_EDIT_OUTPUT, g_hInst, NULL);
    g_hBtnOptions = CreateWindowExW(0, L"BUTTON", L"Options", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                    (HMENU)(INT_PTR)IDC_BTN_OPTIONS, g_hInst, NULL);
    g_hBtnAbout = CreateWindowExW(0, L"BUTTON", L"About", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                  (HMENU)(INT_PTR)IDC_BTN_ABOUT, g_hInst, NULL);
    g_hBtnExit = CreateWindowExW(0, L"BUTTON", L"Exit", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                 (HMENU)(INT_PTR)IDC_BTN_EXIT, g_hInst, NULL);

    SendMessageW(g_hEditOutput, EM_SETLIMITTEXT, (WPARAM)0x7FFFFFFE, 0);

    set_font(g_hLblFile, g_hFontUI);
    set_font(g_hEditPath, g_hFontUI);
    set_font(g_hBtnBrowse, g_hFontUI);
    set_font(g_hComboType, g_hFontUI);
    set_font(g_hBtnFlags, g_hFontUI);
    set_font(g_hBtnDb, g_hFontUI);
    set_font(g_hStaticMsec, g_hFontUI);
    set_font(g_hBtnScan, g_hFontUI);
    set_font(g_hBtnOptions, g_hFontUI);
    set_font(g_hBtnAbout, g_hFontUI);
    set_font(g_hBtnExit, g_hFontUI);
    set_font(g_hEditOutput, g_hFontMono);

    g_pOldEditProc = (WNDPROC)(LONG_PTR)SetWindowLongPtrW(g_hEditPath, GWLP_WNDPROC, (LONG_PTR)path_edit_proc);
}

/* ------------------------------------------------------------- wndproc ----- */

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT nMsg, WPARAM wParam, LPARAM lParam)
{
    switch (nMsg) {
        case WM_CREATE: {
            create_fonts();
            create_controls(hwnd);
            DragAcceptFiles(hwnd, TRUE);

            if (gui_backend_init()) {
                WCHAR szReady[160];

                wsprintfW(szReady, L"Ready. %d signatures loaded.\r\nChoose a file (or drag one here) and press Scan.",
                          gui_backend_signature_count());
                SetWindowTextW(g_hEditOutput, szReady);
            } else {
                const char *pDb = gui_backend_main_db_path();
                WCHAR *pDbW = utf8_to_wide(pDb);
                WCHAR szMsg[1024];

                wsprintfW(szMsg,
                          L"Signature database not found.\r\n\r\nLooked for:\r\n    %s\r\n\r\n"
                          L"Place the 'db' folder next to cdie_gui.exe (the portable\r\npackage layout), then restart.",
                          (pDbW != NULL) ? pDbW : L"db");
                SetWindowTextW(g_hEditOutput, szMsg);

                if (pDbW != NULL) {
                    HeapFree(GetProcessHeap(), 0, pDbW);
                }
            }

            layout(hwnd);
            SetFocus(g_hEditPath);
            return 0;
        }

        case WM_SIZE:
            layout(hwnd);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO *pInfo = (MINMAXINFO *)lParam;

            pInfo->ptMinTrackSize.x = 540;
            pInfo->ptMinTrackSize.y = 380;
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HWND hCtl = (HWND)lParam;
            HDC hdc = (HDC)wParam;

            if (hCtl == g_hEditOutput) {
                SetBkColor(hdc, RGB(255, 255, 255));
                SetTextColor(hdc, RGB(0, 0, 0));
                return (LRESULT)GetStockObject(WHITE_BRUSH);
            }

            SetBkMode(hdc, TRANSPARENT);
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_BROWSE: do_browse(hwnd); return 0;
                case IDC_BTN_SCAN: do_scan(hwnd); return 0;
                case IDC_BTN_FLAGS: show_flags_menu(hwnd); return 0;
                case IDC_BTN_DB: show_db_menu(hwnd); return 0;
                case IDC_BTN_OPTIONS: do_options(hwnd); return 0;
                case IDC_BTN_ABOUT: do_about(hwnd); return 0;
                case IDC_BTN_EXIT: DestroyWindow(hwnd); return 0;
                default: break;
            }
            break;

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            WCHAR szFile[1024];

            if (DragQueryFileW(hDrop, 0, szFile, (UINT)(sizeof(szFile) / sizeof(szFile[0])))) {
                SetWindowTextW(g_hEditPath, szFile);
                do_scan(hwnd);
            }

            DragFinish(hDrop);
            return 0;
        }

        case WM_DESTROY:
            gui_backend_shutdown();

            if (g_hFontUI != NULL) {
                DeleteObject(g_hFontUI);
                g_hFontUI = NULL;
            }

            if (g_hFontMono != NULL) {
                DeleteObject(g_hFontMono);
                g_hFontMono = NULL;
            }

            PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, nMsg, wParam, lParam);
}

/* ------------------------------------------------------------------- run --- */

int gui_run(void)
{
    INITCOMMONCONTROLSEX icc;
    WNDCLASSEXW wc;
    HWND hwnd = NULL;
    MSG msg;
    WCHAR szTitle[128];
    WCHAR *pTitleBase = NULL;

    g_hInst = GetModuleHandleW(NULL);

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"cdieGuiWindow";
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(NULL, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        return 1;
    }

    /* "cdie <version> (x64)" - the version comes from the engine, no drift. */
    pTitleBase = utf8_to_wide(gui_backend_app_title());
#if defined(_WIN64)
    wsprintfW(szTitle, L"%s (x64)", (pTitleBase != NULL) ? pTitleBase : L"cdie");
#else
    wsprintfW(szTitle, L"%s (x86)", (pTitleBase != NULL) ? pTitleBase : L"cdie");
#endif

    if (pTitleBase != NULL) {
        HeapFree(GetProcessHeap(), 0, pTitleBase);
    }

    hwnd = CreateWindowExW(0, L"cdieGuiWindow", szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 520, NULL, NULL, g_hInst,
                           NULL);

    if (hwnd == NULL) {
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
