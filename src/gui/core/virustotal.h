#ifndef VIRUSTOTAL_H
#define VIRUSTOTAL_H

#include "theme/theme.h"   /* for PlWidget; adjust if your PlWidget typedef lives elsewhere */

#ifdef __cplusplus
extern "C" {
#endif

	typedef struct {
		int found;
		int malicious, suspicious, harmless, undetected;
		long http_code;   /* 0 = network failure */
	} VtResult;

	int vt_sha256_file(const char* utf8_path, char out_hex[65]);
	int vt_open_report(const char* sha256_hex);
	void vt_set_api_key(const char* key);
	int vt_api_available(void);
	int vt_lookup(const char* sha256_hex, VtResult* out);

	/* ---- GUI page ---- */
	typedef struct VtView VtView;

	VtView* vtview_create(void* parent_native, int x, int y, int w, int h);
	PlWidget* vtview_widget(const VtView* v);
	void      vtview_set_rect(VtView* v, int x, int y, int w, int h);
	void      vtview_set_file(VtView* v, const char* utf8_path); /* NULL = no file */
	void      vtview_refresh(VtView* v);  /* start a check if one is ready and idle */
	void      vtview_tick(VtView* v);     /* call from the main loop */
	void      vtview_cancel(VtView* v);   /* wait for the worker; call before exit */
	void vtview_set_callbacks(VtView* v, int (*copy)(void*, const char*), void* user);

#ifdef __cplusplus
}
#endif
#endif