#ifndef CDIE_CONFIG_H
#define CDIE_CONFIG_H

#include <stddef.h>         

#define CDIE_FONT_ROLES 4

typedef struct {
  char main_db[1024];
  char extra_db[1024];
  char custom_db[1024];
  char db_version[32];
	char language[32];
  char vt_key[128];

  int  has_prefs;
  int  deep, heuristic, verbose, aggressive, hide_unknown, format, monitor_downloads;
  int  db_extra, db_custom;
  int  use_sse2, use_avx2;
  unsigned long long buffer_size, file_buffer_size;
  char         font_family[CDIE_FONT_ROLES][128];
  unsigned int font_size[CDIE_FONT_ROLES];
  int          font_bold[CDIE_FONT_ROLES];
  int          font_italic[CDIE_FONT_ROLES];
} CdieConfig;

int config_set_db_version(const char* version);
int config_user_dir(char* out, size_t cap);
int config_is_portable(void);
int config_db_required(void);
int config_load(CdieConfig* cfg);

int config_save(const CdieConfig* cfg);

const char* config_file_path(void);

#endif