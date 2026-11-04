/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * (MIT license header, same as the other files)
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