// i18n.c
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

typedef struct { char* key; char* val; } Pair;
static Pair* s_pairs; static int s_n;

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

int i18n_load(const char* code) {
  WCHAR wpath[MAX_PATH], dir[MAX_PATH], wcode[32];
  FILE* f; char line[2048]; int cap = 0;
  clear();
  if (!code || !code[0]) return 1;                 // English
  GetModuleFileNameW(NULL, dir, MAX_PATH);
  *wcsrchr(dir, L'\\') = 0;
  MultiByteToWideChar(CP_UTF8, 0, code, -1, wcode, 32);
  wsprintfW(wpath, L"%s\\lang\\%s.txt", dir, wcode);
  f = _wfopen(wpath, L"rb");
  if (!f) return 0;
  while (fgets(line, sizeof line, f)) {
    char* eq; size_t n = strlen(line);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (!n || line[0] == '#' || line[0] == '@') continue;
    eq = strchr(line, '='); if (!eq) continue;
    *eq = 0;
    if (s_n == cap) { cap = cap ? cap * 2 : 256; s_pairs = realloc(s_pairs, cap * sizeof(Pair)); }
    s_pairs[s_n].key = _strdup(line);  unescape(s_pairs[s_n].key);
    s_pairs[s_n].val = _strdup(eq + 1); unescape(s_pairs[s_n].val);
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

int i18n_list(void (*add)(const char* code, const char* name, void* ud), void* ud)
{
  WCHAR dir[MAX_PATH], pattern[MAX_PATH];
  WIN32_FIND_DATAW fd;
  HANDLE h;
  int n = 0;
  GetModuleFileNameW(NULL, dir, MAX_PATH);
  *wcsrchr(dir, L'\\') = 0;
  wsprintfW(pattern, L"%s\\lang\\*.txt", dir);
  h = FindFirstFileW(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    WCHAR wfile[MAX_PATH];
    char code[32], name[128], line[512];
    FILE* f;
    WCHAR* dot = wcsrchr(fd.cFileName, L'.');
    if (dot) *dot = 0;
    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, code, sizeof code, NULL, NULL);
    snprintf(name, sizeof name, "%s", code);
    wsprintfW(wfile, L"%s\\lang\\%s.txt", dir, fd.cFileName);
    f = _wfopen(wfile, L"rb");
    if (f) {
      while (fgets(line, sizeof line, f)) {
        size_t len;
        char* p = line;
        if ((unsigned char)p[0] == 0xEF) p += 3;         
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
    ++n;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return n;
}