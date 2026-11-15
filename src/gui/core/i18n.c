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
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "i18n.h"
#include "core/config.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#define PATH_SEP '\\'
#define PATH_CAP MAX_PATH
#define STRDUP _strdup
#else
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define PATH_SEP '/'
#define PATH_CAP PATH_MAX
#define STRDUP strdup
#endif

#define I18N_HOST "raw.githubusercontent.com"
#define I18N_PATH "/horsicq/CDiE/NewLook/lang/"
#define I18N_MAX  (1024 * 1024)

typedef struct { char* key; char* val; } Pair;
static Pair* s_pairs; static int s_n;

static int exe_dir(char* out, size_t cap) {
  char* slash;
#ifdef _WIN32
  WCHAR w[MAX_PATH];
  DWORD n = GetModuleFileNameW(NULL, w, MAX_PATH);
  if (!n || n >= MAX_PATH) return 0;
  if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL)) return 0;
#elif defined(__APPLE__)
  char tmp[PATH_MAX]; uint32_t sz = sizeof tmp;
  if (_NSGetExecutablePath(tmp, &sz) != 0) return 0;
  if (!realpath(tmp, out)) return 0;
#else
  ssize_t n = readlink("/proc/self/exe", out, cap - 1);
  if (n <= 0) return 0;
  out[n] = 0;
#endif
  slash = strrchr(out, PATH_SEP);
  if (!slash) return 0;
  *slash = 0;
  return 1;
}

static FILE* open_utf8(const char* path) {
#ifdef _WIN32
  WCHAR w[MAX_PATH];
  if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH)) return NULL;
  return _wfopen(w, L"rb");
#else
  return fopen(path, "rb");
#endif
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void unescape(char* s) {           // \n  \\  \xHH
  char* d = s;
  for (; *s; ++s) {
    if (s[0] == '\\' && s[1] == 'n') { *d++ = '\n'; ++s; }
    else if (s[0] == '\\' && s[1] == '\\') { *d++ = '\\'; ++s; }
    else if (s[0] == '\\' && s[1] == 'x' && hexv(s[2]) >= 0 && hexv(s[3]) >= 0) {
      *d++ = (char)(hexv(s[2]) * 16 + hexv(s[3])); s += 3;
    }
    else *d++ = *s;
  }
  *d = 0;
}

static int cmp(const void* a, const void* b) {
  return strcmp(((const Pair*)a)->key, ((const Pair*)b)->key);
}

static void clear(void) {
  for (int i = 0; i < s_n; ++i) { free(s_pairs[i].key); free(s_pairs[i].val); }
  free(s_pairs); s_pairs = NULL; s_n = 0;
}

static char* skip_bom(char* p) {
  if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
    return p + 3;
  return p;
}

static int lang_base(char* out, size_t cap)
{
  const char* p = config_file_path();
  char* slash;
  if (!p) return 0;
  snprintf(out, cap, "%s", p);
  slash = strrchr(out, PATH_SEP);
  if (!slash) return 0;
  *slash = 0;
  return 1;
}

static FILE* open_lang(const char* code)
{
  char base[PATH_CAP], path[PATH_CAP + 64];
  FILE* f;
  if (lang_base(base, sizeof base)) {
    snprintf(path, sizeof path, "%s%clang%c%s.txt", base, PATH_SEP, PATH_SEP, code);
    if ((f = open_utf8(path)) != NULL) return f;
  }
  if (exe_dir(base, sizeof base)) {
    snprintf(path, sizeof path, "%s%clang%c%s.txt", base, PATH_SEP, PATH_SEP, code);
    return open_utf8(path);
  }
  return NULL;
}

int i18n_load(const char* code) {
  char line[2048];
  FILE* f; int cap = 0, first = 1;
  clear();
  if (!code || !code[0]) return 1;                 // English
  f = open_lang(code);
  if (!f) return 0;
  while (fgets(line, sizeof line, f)) {
    char* p = line, * eq; size_t n;
    if (first) { p = skip_bom(p); first = 0; }
    n = strlen(p);
    while (n && (p[n - 1] == '\n' || p[n - 1] == '\r')) p[--n] = 0;
    if (!n || p[0] == '#' || p[0] == '@') continue;
    eq = strchr(p, '='); if (!eq) continue;
    *eq = 0;
    if (s_n == cap) {
      int ncap = cap ? cap * 2 : 256;
      Pair* np = realloc(s_pairs, ncap * sizeof(Pair));
      if (!np) break;
      s_pairs = np; cap = ncap;
    }
    s_pairs[s_n].key = STRDUP(p);       unescape(s_pairs[s_n].key);
    s_pairs[s_n].val = STRDUP(eq + 1);  unescape(s_pairs[s_n].val);
    ++s_n;
  }
  fclose(f);
  qsort(s_pairs, s_n, sizeof(Pair), cmp);
  return 1;
}

const char* translate(const char* english) {
  Pair k, * p;
  if (!s_n || !english) return english;
  k.key = (char*)english;
  p = bsearch(&k, s_pairs, s_n, sizeof(Pair), cmp);
  return p && p->val[0] ? p->val : english;
}

static void report_lang(const char* dir, const char* code,
  void (*add)(const char*, const char*, void*), void* ud) {
  char path[PATH_CAP + 64], name[128], line[512];
  FILE* f;
  snprintf(name, sizeof name, "%s", code);
  snprintf(path, sizeof path, "%s%clang%c%s.txt", dir, PATH_SEP, PATH_SEP, code);
  f = open_utf8(path);
  if (f) {
    int first = 1;
    while (fgets(line, sizeof line, f)) {
      char* p = line; size_t len;
      if (first) { p = skip_bom(p); first = 0; }
      if (!strncmp(p, "@name=", 6)) {
        len = strlen(p);
        while (len && (p[len - 1] == '\n' || p[len - 1] == '\r')) p[--len] = 0;
        snprintf(name, sizeof name, "%s", p + 6);
        break;
      }
    }
    fclose(f);
  }
  add(code, name, ud);
}

static int list_in(const char* dir, void (*add)(const char* code, const char* name, void* ud), void* ud)
{
  char code[128];
  int n = 0;

#ifdef _WIN32
  {
    WCHAR wdir[MAX_PATH], pattern[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    if (!MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, MAX_PATH)) return 0;
    wsprintfW(pattern, L"%s\\lang\\*.txt", wdir);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
      WCHAR* dot = wcsrchr(fd.cFileName, L'.');
      if (dot) *dot = 0;
      if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, code, sizeof code, NULL, NULL))
        continue;
      report_lang(dir, code, add, ud);
      ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
#else
  {
    char langdir[PATH_CAP + 16];
    DIR* d; struct dirent* e;
    snprintf(langdir, sizeof langdir, "%s/lang", dir);
    d = opendir(langdir);
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
      size_t len = strlen(e->d_name);
      if (len < 5 || strcmp(e->d_name + len - 4, ".txt") != 0) continue;
      if (len - 4 >= sizeof code) continue;
      memcpy(code, e->d_name, len - 4);
      code[len - 4] = 0;
      report_lang(dir, code, add, ud);
      ++n;
    }
    closedir(d);
  }
#endif
  return n;
}

int i18n_list(void (*add)(const char*, const char*, void*), void* ud)
{
  char base[PATH_CAP];
  int n = 0;
  if (exe_dir(base, sizeof base)) n += list_in(base, add, ud);
  if (lang_base(base, sizeof base)) n += list_in(base, add, ud);
  return n;
}

static int safe_code(const char* c)
{
  size_t n = strlen(c);
  if (!n || n > 31) return 0;
  for (; *c; ++c)
    if (!isalnum((unsigned char)*c) && *c != '-' && *c != '_') return 0;
  return 1;  
}

static void make_dir(const char* p)
{
#ifdef _WIN32
  WCHAR w[MAX_PATH];
  if (MultiByteToWideChar(CP_UTF8, 0, p, -1, w, MAX_PATH)) CreateDirectoryW(w, NULL);
#else
  mkdir(p, 0755);
#endif
}

static FILE* open_write(const char* path)
{
#ifdef _WIN32
  WCHAR w[MAX_PATH];
  if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, MAX_PATH)) return NULL;
  return _wfopen(w, L"wb");
#else
  return fopen(path, "wb");
#endif
}

static char* http_get(const char* path, size_t* out_len)
{
#ifdef _WIN32
  HINTERNET s = NULL, c = NULL, r = NULL;
  char* buf = NULL; size_t len = 0, cap = 0; int ok = 0;
  WCHAR wh[128], wp[512];
  DWORD code = 0, sz = sizeof code;
  if (!MultiByteToWideChar(CP_UTF8, 0, I18N_HOST, -1, wh, 128)) return NULL;
  if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, 512)) return NULL;
  s = WinHttpOpen(L"cdie", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
  if (!s) goto done;
  WinHttpSetTimeouts(s, 3000, 3000, 5000, 5000);
  c = WinHttpConnect(s, wh, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!c) goto done;
  r = WinHttpOpenRequest(c, L"GET", wp, NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
  if (!r || !WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) || !WinHttpReceiveResponse(r, NULL)) goto done;
  if (!WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
    NULL, &code, &sz, NULL) || code != 200) goto done;
  for (;;) {
    DWORD avail = 0, got = 0;
    if (!WinHttpQueryDataAvailable(r, &avail)) goto done;
    if (!avail) break;
    if (len + avail + 1 > I18N_MAX) goto done;
    if (len + avail + 1 > cap) {
      size_t nc = (len + avail + 1) * 2;
      char* nb = (char*)realloc(buf, nc);
      if (!nb) goto done;
      buf = nb; cap = nc;
    }
    if (!WinHttpReadData(r, buf + len, avail, &got)) goto done;
    len += got;
  }
  if (buf) { buf[len] = 0; ok = 1; }
done:
  if (r) WinHttpCloseHandle(r);
  if (c) WinHttpCloseHandle(c);
  if (s) WinHttpCloseHandle(s);
  if (!ok) { free(buf); return NULL; }
  *out_len = len;
  return buf;
#else
  {
    char cmd[512], tmp[4096];
    char* buf = NULL; size_t len = 0, cap = 0;
    FILE* p; int rc, bad = 0;
    snprintf(cmd, sizeof cmd,
      "curl -fsSL --max-time 10 \"https://%s%s\" 2>/dev/null", I18N_HOST, path);
    p = popen(cmd, "r");
    if (!p) return NULL;
    for (;;) {
      size_t n = fread(tmp, 1, sizeof tmp, p);
      if (!n) break;
      if (len + n + 1 > I18N_MAX) { bad = 1; break; }
      if (len + n + 1 > cap) {
        size_t nc = (len + n + 1) * 2;
        char* nb = (char*)realloc(buf, nc);
        if (!nb) { bad = 1; break; }
        buf = nb; cap = nc;
      }
      memcpy(buf + len, tmp, n);
      len += n;
    }
    rc = pclose(p);
    if (bad || rc != 0 || !buf) { free(buf); return NULL; }
    buf[len] = 0;
    *out_len = len;
    return buf;
  }
#endif
}

int i18n_installed(const char* code)
{
  FILE* f;
  if (!code || !safe_code(code)) return 0;
  f = open_lang(code);
  if (!f) return 0;
  fclose(f);
  return 1;
}

int i18n_remote_list(void (*add)(const char*, const char*, void*), void* ud)
{
  size_t len; int n = 0;
  char* data = http_get(I18N_PATH "index.txt", &len);
  char* p = data;
  if (!data) return 0;
  while (*p) {
    char* nl = strpbrk(p, "\r\n");
    char* eq;
    if (nl) *nl = 0;
    eq = strchr(p, '=');
    if (p[0] != '#' && eq) {
      *eq = 0;
      if (safe_code(p) && eq[1]) { add(p, eq + 1, ud); ++n; }
    }
    if (!nl) break;
    p = nl + 1;
    while (*p == '\r' || *p == '\n') ++p;
  }
  free(data);
  return n;
}

int i18n_download(const char* code)
{
  char base[PATH_CAP], dir[PATH_CAP + 16], path[PATH_CAP + 64], url[256];
  size_t len; char* data; FILE* f; int ok;

  if (!code || !safe_code(code)) return 0;
  if (!lang_base(base, sizeof base)) return 0;
  make_dir(base);
  snprintf(dir, sizeof dir, "%s%clang", base, PATH_SEP);
  make_dir(dir);
  snprintf(path, sizeof path, "%s%c%s.txt", dir, PATH_SEP, code);
  snprintf(url, sizeof url, I18N_PATH "%s.txt", code);

  data = http_get(url, &len);
  if (!data || !len) { free(data); return 0; }
  f = open_write(path);
  if (!f) { free(data); return 0; }
  ok = fwrite(data, 1, len, f) == len;
  ok = (fclose(f) == 0) && ok;
  free(data);
  return ok;
}