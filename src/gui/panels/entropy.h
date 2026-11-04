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
#ifndef CDIE_ENTROPYVIEW_H
#define CDIE_ENTROPYVIEW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct EntropyView EntropyView;

	/* Same shape as hex_read in main_gui: returns 1 on success */
	typedef int (*EntropyReadFn)(void* user, uint64_t off, void* buf, size_t len);
	/* Same shape as opt_copy_text in main_gui: returns 1 on success */
	typedef int (*EntropyCopyFn)(void* user, const char* utf8);

	EntropyView* entropyview_create(void* parent_native, int x, int y, int w, int h);
	void entropyview_destroy(EntropyView* e);
	void entropyview_set_rect(EntropyView* e, int x, int y, int w, int h);
	void entropyview_set_callbacks(EntropyView* e, EntropyCopyFn copy, void* user);

	/* Sets the data source. The analysis runs lazily the first time the view is painted. */
	void entropyview_open(EntropyView* e, const char* utf8_path,
		EntropyReadFn rd, void* rd_user, uint64_t size);
	void entropyview_close(EntropyView* e);

	/* Optional: call from your wheel handler if the platform layer has one */
	void entropyview_scroll(EntropyView* e, int rows);

#ifdef __cplusplus
}
#endif

#endif