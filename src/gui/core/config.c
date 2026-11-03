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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/config.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#define SEP '\\'
#else
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#define SEP '/'
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#define CONFIG_NAME     "cdie.ini"

static char s_path[1200];
static int  s_resolved;
static int  s_portable;

#ifdef _WIN32
static int to_wide(const char* u8, WCHAR* out, int cap)
{
  return MultiByteToWideChar(CP_UTF8, 0, u8, -1, out, cap) > 0;
}

static int to_utf8(const WCHAR* w, char* out, int cap)
{
  return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL) > 0;
}

static FILE* open_utf8(const char* path, const char* mode)
{
  WCHAR wp[2048], wm[8];
  if (!to_wide(path, wp, 2048) || !to_wide(mode, wm, 8)) return NULL;
  return _wfopen(wp, wm);
}

static int file_exists(const char* path)
{
  WCHAR wp[2048];
  return to_wide(path, wp, 2048) && GetFileAttributesW(wp) != INVALID_FILE_ATTRIBUTES;
}

static int exe_dir(char* out, size_t cap)
{
  WCHAR w[2048];
  char* slash;
  DWORD n = GetModuleFileNameW(NULL, w, 2048);
  if (!n || n >= 2048 || !to_utf8(w, out, (int)cap)) return 0;
  slash = strrchr(out, '\\');
  if (!slash) return 0;
  *slash = 0;
  return 1;
}

static int user_config_dir(char* out, size_t cap)
{
  WCHAR w[MAX_PATH];
  if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
    NULL, SHGFP_TYPE_CURRENT, w))) return 0;
  return to_utf8(w, out, (int)cap);
}

static void make_parent_dirs(const char* file)
{
  char tmp[1200]; WCHAR w[2048]; char* slash;
  snprintf(tmp, sizeof(tmp), "%s", file);
  slash = strrchr(tmp, '\\');
  if (!slash) return;
  *slash = 0;
  if (to_wide(tmp, w, 2048)) SHCreateDirectoryExW(NULL, w, NULL);
}

#else
static FILE* open_utf8(const char* path, const char* mode) { return fopen(path, mode); }
static int file_exists(const char* path) { return access(path, F_OK) == 0; }

static int exe_dir(char* out, size_t cap)
{
  char buf[PATH_MAX];
  char* slash;
#ifdef __linux__
  const char* appimage = getenv("APPIMAGE");   /* real location of the .AppImage */
  if (appimage && *appimage) snprintf(buf, sizeof(buf), "%s", appimage);
  else {
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
  }
#elif defined(__APPLE__)
  uint32_t sz = (uint32_t)sizeof(buf);
  if (_NSGetExecutablePath(buf, &sz) != 0) return 0;
#else
  return 0;
#endif
  slash = strrchr(buf, '/');
  if (!slash) return 0;
  *slash = 0;
  snprintf(out, cap, "%s", buf);
  return 1;
}

static int user_config_dir(char* out, size_t cap)
{
  const char* home = getenv("HOME");
#ifdef __APPLE__
  if (!home || !*home) return 0;
  snprintf(out, cap, "%s/Library/Application Support", home);
  return 1;
#else
  const char* xdg = getenv("XDG_CONFIG_HOME");
  if (xdg && *xdg) { snprintf(out, cap, "%s", xdg); return 1; }
  if (!home || !*home) return 0;
  snprintf(out, cap, "%s/.config", home);
  return 1;
#endif
}

static void make_parent_dirs(const char* file)
{
  char tmp[1200]; size_t i;
  snprintf(tmp, sizeof(tmp), "%s", file);
  for (i = 1; tmp[i]; ++i)
    if (tmp[i] == '/') { tmp[i] = 0; mkdir(tmp, 0700); tmp[i] = '/'; }
}
#endif

static int starts_with_ci(const char* s, const char* prefix)
{
  size_t n = strlen(prefix);

  while (n && (prefix[n - 1] == '\\' || prefix[n - 1] == '/')) --n;
  if (!n) return 0;

#ifdef _WIN32
  if (_strnicmp(s, prefix, n) != 0) return 0;
#else
  if (strncmp(s, prefix, n) != 0) return 0;
#endif

  return s[n] == 0 || s[n] == '\\' || s[n] == '/';
}

static int is_system_dir(const char* dir)
{
#ifdef _WIN32
  static const WCHAR* vars[] = { L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432" };
  size_t i;
  for (i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) {
    WCHAR w[1024]; char u8[1024];
    DWORD n = GetEnvironmentVariableW(vars[i], w, 1024);
    if (n && n < 1024 && to_utf8(w, u8, sizeof(u8)) && starts_with_ci(dir, u8))
      return 1;
  }
  return 0;
#else
  static const char* prefixes[] = {
    "/usr/", "/opt/", "/Applications/", "/System/", "/snap/", "/app/"
  };
  size_t i;
  for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
    if (starts_with_ci(dir, prefixes[i])) return 1;
  if (strstr(dir, ".app/Contents")) return 1;   /* any macOS app bundle */
  return 0;
#endif
}

static void remove_file(const char* path)
{
#ifdef _WIN32
  WCHAR w[2048];
  if (to_wide(path, w, 2048)) DeleteFileW(w);
#else
  remove(path);
#endif
}

static int dir_writable(const char* dir)
{
  char test[1200];
  FILE* f;
  snprintf(test, sizeof(test), "%s%c.cdie_write_test", dir, SEP);
  f = open_utf8(test, "w");
  if (!f) return 0;
  fclose(f);
  remove_file(test);
  return 1;
}

const char* config_file_path(void)
{
  char dir[1024];

  if (s_resolved) return s_path[0] ? s_path : NULL;
  s_resolved = 1;

#ifndef CDIE_STORE_BUILD
  if (exe_dir(dir, sizeof(dir)) && !is_system_dir(dir) && dir_writable(dir)) {
    snprintf(s_path, sizeof(s_path), "%s%c%s", dir, SEP, CONFIG_NAME);
    s_portable = 1;
    return s_path;
  }
#endif

  if (user_config_dir(dir, sizeof(dir)))
    snprintf(s_path, sizeof(s_path), "%s%ccdie%c%s", dir, SEP, SEP, CONFIG_NAME);

  return s_path[0] ? s_path : NULL;
}

static void copy_field(char* dst, size_t cap, const char* src)
{
  size_t n = strlen(src);
  while (n && (src[n - 1] == '\r' || src[n - 1] == '\n')) --n;
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = 0;
}

int config_user_dir(char* out, size_t cap)
{
  char dir[1024];
  if (!user_config_dir(dir, sizeof(dir))) return 0;
  snprintf(out, cap, "%s%ccdie", dir, SEP);
  return 1;
}

int config_is_portable(void)
{
  config_file_path();
  return s_portable;
}

int config_db_required(void)
{
  CdieConfig cfg;
  if (config_is_portable()) return 0;
  return !config_load(&cfg) || !cfg.main_db[0];
}

static int int_key(const char* line, const char* key, int* out)
{
  size_t n = strlen(key);
  if (strncmp(line, key, n) != 0 || line[n] != '=') return 0;
  *out = atoi(line + n + 1);
  return 1;
}

/* Order matches OPTIONS_FONT_ROLES: Controls, Table views, Tree views, Text edits */
static const char* s_font_role[CDIE_FONT_ROLES] = { "controls", "tables", "trees", "edits" };

static void apply_font_field(CdieConfig* cfg, int k, const char* p)
{
  if (!strncmp(p, "family=", 7))
    copy_field(cfg->font_family[k], sizeof(cfg->font_family[k]), p + 7);
  else if (!strncmp(p, "size=", 5))   cfg->font_size[k] = (unsigned)atoi(p + 5);
  else if (!strncmp(p, "bold=", 5))   cfg->font_bold[k] = atoi(p + 5);
  else if (!strncmp(p, "italic=", 7)) cfg->font_italic[k] = atoi(p + 7);
}

/* Returns 1 if the line was a font key */
static int parse_font(CdieConfig* cfg, const char* line)
{
  int k;
  for (k = 0; k < CDIE_FONT_ROLES; ++k) {
    size_t n = strlen(s_font_role[k]);
    if (!strncmp(line, s_font_role[k], n) && line[n] == '_') {
      apply_font_field(cfg, k, line + n + 1);
      return 1;
    }
  }
  /* legacy format: fontN_family= etc. */
  if (!strncmp(line, "font", 4) && line[4] >= '0' &&
    line[4] < '0' + CDIE_FONT_ROLES && line[5] == '_') {
    apply_font_field(cfg, line[4] - '0', line + 6);
    return 1;
  }
  return 0;
}

int config_load(CdieConfig* cfg)
{
  const char* path = config_file_path();
  FILE* f;
  char line[1400];

  memset(cfg, 0, sizeof(*cfg));
  if (!path || !(f = open_utf8(path, "r"))) return 0;

  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
    if (!strncmp(line, "main=", 5))        copy_field(cfg->main_db, sizeof(cfg->main_db), line + 5);
    else if (!strncmp(line, "extra=", 6))  copy_field(cfg->extra_db, sizeof(cfg->extra_db), line + 6);
    else if (!strncmp(line, "custom=", 7)) copy_field(cfg->custom_db, sizeof(cfg->custom_db), line + 7);
    else if (!strncmp(line, "db_version=", 11)) copy_field(cfg->db_version, sizeof(cfg->db_version), line + 11);
    else if (!strncmp(line, "language=", 9))   copy_field(cfg->language, sizeof(cfg->language), line + 9);
    else if (int_key(line, "prefs", &cfg->has_prefs)) {}
    else if (int_key(line, "deep", &cfg->deep)) {}
    else if (int_key(line, "heuristic", &cfg->heuristic)) {}
    else if (int_key(line, "verbose", &cfg->verbose)) {}
    else if (int_key(line, "aggressive", &cfg->aggressive)) {}
    else if (int_key(line, "hide_unknown", &cfg->hide_unknown)) {}
    else if (int_key(line, "format", &cfg->format)) {}
    else if (int_key(line, "monitor_downloads", &cfg->monitor_downloads)) {}
    else if (int_key(line, "db_extra", &cfg->db_extra)) {}
    else if (int_key(line, "db_custom", &cfg->db_custom)) {}
    else if (int_key(line, "sse2", &cfg->use_sse2)) {}
    else if (int_key(line, "avx2", &cfg->use_avx2)) {}
    else if (!strncmp(line, "buffer=", 7))      cfg->buffer_size = strtoull(line + 7, NULL, 10);
    else if (!strncmp(line, "file_buffer=", 12)) cfg->file_buffer_size = strtoull(line + 12, NULL, 10);
    else if (parse_font(cfg, line)) {}
  }
  fclose(f);
  return 1;
}

int config_save(const CdieConfig* cfg)
{
  const char* path = config_file_path();
  FILE* f;
  char ver[32];

  if (!path) return 0;

  snprintf(ver, sizeof(ver), "%s", cfg->db_version);
  if (!ver[0]) {
    CdieConfig old;
    if (config_load(&old)) snprintf(ver, sizeof(ver), "%s", old.db_version);
  }

  make_parent_dirs(path);
  f = open_utf8(path, "w");
  if (!f) return 0;
  fprintf(f, "# cdie settings (1 = on, 0 = off)\n\n");
  fprintf(f, "[databases]\n");
  fprintf(f, "main=%s\n", cfg->main_db);
  fprintf(f, "extra=%s\n", cfg->extra_db);
  fprintf(f, "custom=%s\n", cfg->custom_db);
  fprintf(f, "db_version=%s\n", ver);
	fprintf(f, "language=%s\n", cfg->language);
  if (cfg->has_prefs) {
    int k;
    fprintf(f, "\n[scan]\n");
    fprintf(f, "prefs=1\n");
    fprintf(f, "deep=%d\nheuristic=%d\naggressive=%d\n",
      cfg->deep, cfg->heuristic, cfg->aggressive);
    fprintf(f, "verbose=%d\nhide_unknown=%d\nformat=%d\n",
      cfg->verbose, cfg->hide_unknown, cfg->format);
    fprintf(f, "db_extra=%d\ndb_custom=%d\n", cfg->db_extra, cfg->db_custom);

    fprintf(f, "\n[window]\n");
    fprintf(f, "monitor_downloads=%d\n", cfg->monitor_downloads);

    fprintf(f, "\n[optimization]\n");
    fprintf(f, "sse2=%d\navx2=%d\n", cfg->use_sse2, cfg->use_avx2);
    fprintf(f, "# buffer sizes in bytes (0 = default)\n");
    fprintf(f, "buffer=%llu\nfile_buffer=%llu\n", cfg->buffer_size, cfg->file_buffer_size);

    fprintf(f, "\n[fonts]\n");
    fprintf(f, "# empty family and size 0 = system default\n");
    for (k = 0; k < CDIE_FONT_ROLES; ++k) {
      fprintf(f, "%s_family=%s\n", s_font_role[k], cfg->font_family[k]);
      fprintf(f, "%s_size=%u\n", s_font_role[k], cfg->font_size[k]);
      fprintf(f, "%s_bold=%d\n", s_font_role[k], cfg->font_bold[k]);
      fprintf(f, "%s_italic=%d\n", s_font_role[k], cfg->font_italic[k]);
    }
  }
  return fclose(f) == 0;
}

int config_set_db_version(const char* version)
{
  CdieConfig cfg;
  config_load(&cfg);
  snprintf(cfg.db_version, sizeof(cfg.db_version), "%s", version ? version : "");
  return config_save(&cfg);
}