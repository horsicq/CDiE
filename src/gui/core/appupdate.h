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
#ifndef CDIE_APPUPDATE_H
#define CDIE_APPUPDATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct {
		char tag[64];          
		char url[512];         
		char stamp[40];         
		unsigned long size_kb; 
		int  newer;            
	} AppUpdateInfo;

	int appupd_check(const char* current_version, AppUpdateInfo* info,
		char* err, size_t err_cap);

	int appupd_download_and_apply(const AppUpdateInfo* info, char* err, size_t err_cap);

	typedef void (*AppUpdProgressFn)(void* user);
	void appupd_set_progress_hook(AppUpdProgressFn fn, void* user);
	void appupd_get_progress(unsigned long* done_kb, unsigned long* total_kb);
	void appupd_cancel(void);

	int appupd_dialog_show(void* owner_hwnd, const AppUpdateInfo* info,
		const char* current_version);

#ifdef __cplusplus
}
#endif
#endif