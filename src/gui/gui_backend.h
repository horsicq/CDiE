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

/* gui_backend.h - the engine side of the WinAPI GUI.
 *
 * The scan engine headers (<windows.h>-hostile: xpe.h and friends define their
 * own PE structures) never meet <windows.h>. gui_backend.c includes the engine;
 * main_gui.c includes <windows.h>. This header is the only thing they share, so
 * it exposes plain C types alone - no engine structs, no Win32 types leak. */

#ifndef CDIE_GUI_BACKEND_H
#define CDIE_GUI_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves db/db_extra/db_custom next to the executable (exactly as the console
 * does), loads all three once and sorts them. All three are loaded regardless
 * of the toggles, because the "Databases" toggles gate which databases actually
 * contribute at scan time - so changing them never needs a reload. Returns 1
 * when the main database yielded at least one signature, else 0. */
int gui_backend_init(void);

/* The resolved main / extra / custom database paths (UTF-8), for the Options
 * dialog and the "not found" message. Owned by the backend; valid until
 * gui_backend_shutdown or the next gui_backend_set_db_paths; do not free. */
const char *gui_backend_main_db_path(void);
const char *gui_backend_extra_db_path(void);
const char *gui_backend_custom_db_path(void);

/* Replaces the three database paths (UTF-8; a NULL or empty entry re-resolves
 * that slot's default next to the executable) and reloads all three. Returns 1
 * when the main database loaded at least one signature, else 0. */
int gui_backend_set_db_paths(const char *pMain, const char *pExtra, const char *pCustom);

/* Total number of signatures loaded across the three databases. */
int gui_backend_signature_count(void);

/* "cdie <version>" (UTF-8) for the window title. Static storage; do not free. */
const char *gui_backend_app_title(void);

/* Sets the scan-flag toggles (non-zero = on). */
void gui_backend_set_flags(int bDeep, int bHeuristic, int bVerbose, int bAggressive, int bHideUnknown, int bFormat);

/* Selects which databases contribute (the main database is always used). */
void gui_backend_set_databases(int bUseExtra, int bUseCustom);

/* Scans one file given a UTF-8 path. On success returns 1 and hands back two
 * heap strings (UTF-8): the formatted result text and the detected file-type
 * name; free each with gui_backend_free. On failure returns 0 and sets both
 * out-pointers to NULL. */
int gui_backend_scan(const char *pUtf8Path, char **ppResultText, char **ppTypeName);

/* Frees a string handed back by gui_backend_scan (uses the engine allocator,
 * which on the CRT-free build is not the Win32 heap the frontend uses). */
void gui_backend_free(void *pPtr);

/* Releases the databases and options. */
void gui_backend_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_GUI_BACKEND_H */
