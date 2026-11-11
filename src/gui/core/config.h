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
  int theme_mode;
  int  has_prefs;
  int  deep, heuristic, verbose, aggressive, hide_unknown, format, monitor_downloads;
  int  db_extra, db_custom;
  int  use_sse2, use_avx2;
  unsigned long long buffer_size, file_buffer_size;
  char         font_family[CDIE_FONT_ROLES][128];
  unsigned int font_size[CDIE_FONT_ROLES];
  int          font_bold[CDIE_FONT_ROLES];
  int          font_italic[CDIE_FONT_ROLES];

  int db_commits;
} CdieConfig;

int config_set_db_version(const char* version);
int config_user_dir(char* out, size_t cap);
int config_is_portable(void);
int config_db_required(void);
int config_load(CdieConfig* cfg);

int config_save(const CdieConfig* cfg);

const char* config_file_path(void);

#endif