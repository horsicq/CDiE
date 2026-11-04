// i18n.c
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

#ifdef _WIN32
#include <windows.h>
#define PATH_SEP '\\'
#define PATH_CAP MAX_PATH
#define STRDUP _strdup
#else
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define PATH_SEP '/'
#define PATH_CAP PATH_MAX
#define STRDUP strdup
#endif

typedef struct { char* key; char* val; } Pair;
static Pair* s_pairs; static int s_n;

/* ---------- platform helpers ---------- */

// Writes the directory containing the executable (UTF-8, no trailing separator).
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

/* ---------- parsing ---------- */

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

int i18n_load(const char* code) {
  char dir[PATH_CAP], path[PATH_CAP + 64], line[2048];
  FILE* f; int cap = 0, first = 1;
  clear();
  if (!code || !code[0]) return 1;                 // English
  if (!exe_dir(dir, sizeof dir)) return 0;
  snprintf(path, sizeof path, "%s%clang%c%s.txt", dir, PATH_SEP, PATH_SEP, code);
  f = open_utf8(path);
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

/* ---------- language listing ---------- */

// Reads "@name=" from the file (if present) and reports the language.
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

int i18n_list(void (*add)(const char* code, const char* name, void* ud), void* ud)
{
  char dir[PATH_CAP], code[128];
  int n = 0;
  if (!exe_dir(dir, sizeof dir)) return 0;

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