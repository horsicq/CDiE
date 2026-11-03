// i18n.h
const char* translate(const char* english);
int  i18n_load(const char* code);          // "de", or "" for English
int  i18n_list(void (*add)(const char* code, const char* name, void* ud), void* ud);