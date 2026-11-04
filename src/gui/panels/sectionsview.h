#ifndef CORE_SECTIONSVIEW_H
#define CORE_SECTIONSVIEW_H

/* Needs the same pl_* / PlWidget declarations that virustotal.h gets.
   If those come from another header in your project, include that one instead. */
#include "core/virustotal.h"

typedef struct SecView SecView;

SecView*  secview_create(void* parent_native, int x, int y, int w, int h);
PlWidget* secview_widget(const SecView* v);
void      secview_set_rect(SecView* v, int x, int y, int w, int h);

/* Load a PE file's section table. Pass NULL or "" to clear. */
void      secview_set_file(SecView* v, const char* utf8_path);

/* Optional: called when the user clicks "View in Entropy tab". */
void      secview_set_callbacks(SecView* v, void (*open_entropy)(void*), void* user);

void      secview_scroll(SecView* v, int delta_pixels);

#endif
