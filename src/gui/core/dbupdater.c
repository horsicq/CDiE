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

#include "core/dbupdater.h"
#include "core/config.h"
#include "core/gui_backend.h"
#include "core/i18n.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#define SEP '\\'
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#define SEP '/'
#endif

#define RELEASE_API           "https://api.github.com/repos/horsicq/Detect-It-Easy/releases/tags/current-database"
#define RELEASE_ZIP           "https://github.com/horsicq/Detect-It-Easy/releases/download/current-database/db.zip"
#define RELEASE_ZIP_FALLBACK  "https://github.com/horsicq/Detect-It-Easy/archive/refs/heads/master.zip"
#define COMMITS_API           "https://api.github.com/repos/horsicq/Detect-It-Easy/commits?sha=master&per_page=1"
#define COMMITS_ZIP_FMT       "https://github.com/horsicq/Detect-It-Easy/archive/%s.zip"

enum { JOB_CHECK, JOB_INSTALL };

typedef struct { char name[512]; int isdir; } Ent;
typedef struct { Updater* u; int total, done; } CopyCtx;

static void notify(Updater* u) { if (u->notify) u->notify(u->user); }
static void set_pct(Updater* u, int p) { if (p != u->pct) { u->pct = p; notify(u); } }

static void set_label(Updater* u, const char* l)
{
  snprintf(u->label, sizeof(u->label), "%s", l);
  notify(u);
}

static void fail(Updater* u, const char* m)
{
  snprintf(u->error, sizeof(u->error), "%s", m);
  u->phase = UPD_ERROR;
}

static void join_path(char* out, size_t cap, const char* dir, const char* name)
{
  snprintf(out, cap, "%s%c%s", dir, SEP, name);
}

static int ci_eq(const char* a, const char* b)
{
  for (;; ++a, ++b) {
    int x = (unsigned char)*a, y = (unsigned char)*b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return 0;
    if (!x) return 1;
  }
}

static int is_sep(char c)
{
#ifdef _WIN32
  return c == '\\' || c == '/';
#else
  return c == '/';
#endif
}

#ifdef _WIN32

static int to_w(const char* s, WCHAR* w, int cap) { return MultiByteToWideChar(CP_UTF8, 0, s, -1, w, cap) > 0; }

static FILE* u8fopen(const char* p, const char* m)
{
  WCHAR wp[2048], wm[8];
  if (!to_w(p, wp, 2048) || !to_w(m, wm, 8)) return NULL;
  return _wfopen(wp, wm);
}

static void del_file(const char* p) { WCHAR w[2048]; if (to_w(p, w, 2048)) DeleteFileW(w); }
static void del_dir(const char* p) { WCHAR w[2048]; if (to_w(p, w, 2048)) RemoveDirectoryW(w); }
static void mk1(const char* p) { WCHAR w[2048]; if (to_w(p, w, 2048)) CreateDirectoryW(w, NULL); }

static int temp_dir(char* out, size_t cap)
{
  WCHAR w[MAX_PATH + 2];
  DWORD n = GetTempPathW(MAX_PATH + 1, w);      
  if (!n || n > MAX_PATH) return 0;
  return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL) > 0;
}

static int list_dir(const char* dir, Ent** out)
{
  char pat[1200]; WCHAR wp[1200]; WIN32_FIND_DATAW fd; HANDLE h;
  int n = 0, cap = 0; Ent* e = NULL;
  *out = NULL;
  snprintf(pat, sizeof(pat), "%s\\*", dir);
  if (!to_w(pat, wp, 1200)) return 0;
  h = FindFirstFileW(wp, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (fd.cFileName[0] == L'.' &&
      (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
    if (n == cap) {
      Ent* ne = (Ent*)realloc(e, (size_t)(cap = cap ? cap * 2 : 64) * sizeof(Ent));
      if (!ne) break;
      e = ne;
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, e[n].name, sizeof(e[n].name), NULL, NULL)) continue;
    e[n].isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    ++n;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  *out = e;
  return n;
}

static int run_hidden(const char* cmd)
{
  WCHAR w[4096]; STARTUPINFOW si; PROCESS_INFORMATION pi; DWORD code = 1;
  if (!to_w(cmd, w, 4096)) return 0;
  ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  if (!CreateProcessW(NULL, w, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return 0;
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  return code == 0;
}

static int extract_zip(const char* zip, const char* dir)
{
  char cmd[3000], sys8[MAX_PATH * 3]; WCHAR sys[MAX_PATH];
  if (GetSystemDirectoryW(sys, MAX_PATH) &&
    WideCharToMultiByte(CP_UTF8, 0, sys, -1, sys8, sizeof(sys8), NULL, NULL)) {
    snprintf(cmd, sizeof(cmd), "\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys8, zip, dir);
    if (run_hidden(cmd)) return 1;
  }
  snprintf(cmd, sizeof(cmd),
    "powershell.exe -NoProfile -NonInteractive -Command "
    "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"", zip, dir);
  return run_hidden(cmd);
}

static int http_get(Updater* u, const char* url, const char* dest, int track)
{
  WCHAR wurl[2048], host[256], path[1024], extra[1024], full[2100];
  URL_COMPONENTSW uc;
  HINTERNET ses = NULL, con = NULL, req = NULL;
  FILE* f = NULL;
  char* buf = (char*)malloc(65536);
  DWORD code = 0, sz, total = 0, got;
  ULONGLONG done = 0;
  int ok = 0;

  if (!buf || !to_w(url, wurl, 2048)) goto end;
  ZeroMemory(&uc, sizeof(uc)); uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;      uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;       uc.dwUrlPathLength = 1024;
  uc.lpszExtraInfo = extra;    uc.dwExtraInfoLength = 1024;
  if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) goto end;
  lstrcpyW(full, path); lstrcatW(full, extra);

  ses = WinHttpOpen(L"cdie-updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
  if (!ses) goto end;
  WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
  {
    DWORD protos = 0x00000800 /* TLS 1.2 */ | 0x00002000 /* TLS 1.3 if present */;
    WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
  }
  con = WinHttpConnect(ses, host, uc.nPort, 0);
  if (!con) goto end;
  req = WinHttpOpenRequest(con, L"GET", full, NULL, NULL, NULL,
    uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!req) goto end;
  if (!lstrcmpiW(host, L"api.github.com"))
    WinHttpAddRequestHeaders(req, L"Accept: application/vnd.github+json", (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
    !WinHttpReceiveResponse(req, NULL)) goto end;

  sz = sizeof(code);
  if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX) || code != 200) goto end;
  sz = sizeof(total);
  if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX, &total, &sz, WINHTTP_NO_HEADER_INDEX)) total = 0;

  f = u8fopen(dest, "wb");
  if (!f) goto end;
  for (;;) {
    if (u->cancel) goto end;
    if (!WinHttpReadData(req, buf, 65536, &got)) goto end;
    if (!got) { ok = 1; break; }
    if (fwrite(buf, 1, got, f) != got) goto end;
    done += got;
    if (track && total) set_pct(u, (int)(done * 70 / total));
  }
end:
  if (f) { fclose(f); if (!ok) del_file(dest); }
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
  if (ses) WinHttpCloseHandle(ses);
  free(buf);
  return ok;
}

#else

static FILE* u8fopen(const char* p, const char* m) { return fopen(p, m); }
static void del_file(const char* p) { remove(p); }
static void del_dir(const char* p) { rmdir(p); }
static void mk1(const char* p) { mkdir(p, 0755); }

static int temp_dir(char* out, size_t cap)
{
  const char* t = getenv("TMPDIR");
  if (!t || !*t) t = "/tmp";
  snprintf(out, cap, "%s/", t);
  return 1;
}

static int list_dir(const char* dir, Ent** out)
{
  DIR* d = opendir(dir);
  struct dirent* de;
  int n = 0, cap = 0; Ent* e = NULL;
  *out = NULL;
  if (!d) return 0;
  while ((de = readdir(d)) != NULL) {
    struct stat st; char full[1200];
    if (de->d_name[0] == '.' &&
      (!de->d_name[1] || (de->d_name[1] == '.' && !de->d_name[2]))) continue;
    if (n == cap) {
      Ent* ne = (Ent*)realloc(e, (size_t)(cap = cap ? cap * 2 : 64) * sizeof(Ent));
      if (!ne) break;
      e = ne;
    }
    snprintf(e[n].name, sizeof(e[n].name), "%s", de->d_name);
    join_path(full, sizeof(full), dir, de->d_name);
    e[n].isdir = (stat(full, &st) == 0) ? S_ISDIR(st.st_mode) : 0;
    ++n;
  }
  closedir(d);
  *out = e;
  return n;
}

static int extract_zip(const char* zip, const char* dir)
{
  char cmd[3000];
  snprintf(cmd, sizeof(cmd), "unzip -q -o \"%s\" -d \"%s\" >/dev/null 2>&1", zip, dir);
  if (system(cmd) == 0) return 1;
  snprintf(cmd, sizeof(cmd), "tar -xf \"%s\" -C \"%s\" >/dev/null 2>&1", zip, dir);
  return system(cmd) == 0;
}

static int http_get(Updater* u, const char* url, const char* dest, int track)
{
  char cmd[3000];
  int ok;
  snprintf(cmd, sizeof(cmd), "curl -fsSL -A cdie-updater/1.0 %s -o \"%s\" \"%s\"",
    strstr(url, "api.github.com") ? "-H \"Accept: application/vnd.github+json\"" : "", dest, url);
  if (track) set_pct(u, 10);
  ok = system(cmd) == 0;
  if (!ok) del_file(dest);
  if (ok && track) set_pct(u, 70);
  return ok && !u->cancel;
}

#endif

static void mkdirs(const char* path)
{
  char t[1200]; size_t i;
  snprintf(t, sizeof(t), "%s", path);
  for (i = 1; t[i]; ++i)
    if (is_sep(t[i])) { char c = t[i]; t[i] = 0; mk1(t); t[i] = c; }
  mk1(t);
}

static void rm_tree(const char* dir)
{
  Ent* e; int n = list_dir(dir, &e), i;
  for (i = 0; i < n; ++i) {
    char sub[1200];
    join_path(sub, sizeof(sub), dir, e[i].name);
    if (e[i].isdir) rm_tree(sub); else del_file(sub);
  }
  free(e);
  del_dir(dir);
}

static int count_files(const char* dir, int top)
{
  Ent* e; int n = list_dir(dir, &e), i, c = 0;
  for (i = 0; i < n; ++i) {
    char sub[1200];
    if (e[i].isdir) {
      if (top && ci_eq(e[i].name, "db_custom")) continue;
      join_path(sub, sizeof(sub), dir, e[i].name);
      c += count_files(sub, 0);
    }
    else ++c;
  }
  free(e);
  return c;
}

static int find_dir(const char* root, const char* name, int depth, char* out, size_t cap)
{
  Ent* e; int n = list_dir(root, &e), i, ok = 0;
  for (i = 0; i < n && !ok; ++i)
    if (e[i].isdir && ci_eq(e[i].name, name)) { join_path(out, cap, root, e[i].name); ok = 1; }
  for (i = 0; i < n && !ok && depth > 0; ++i)
    if (e[i].isdir) {
      char sub[1200];
      join_path(sub, sizeof(sub), root, e[i].name);
      ok = find_dir(sub, name, depth - 1, out, cap);
    }
  free(e);
  return ok;
}

static int file_exists(const char* p)
{
  FILE* f = u8fopen(p, "rb");
  if (!f) return 0;
  fclose(f);
  return 1;
}

static int files_equal(const char* a, const char* b)
{
  FILE* fa = u8fopen(a, "rb"); FILE* fb = u8fopen(b, "rb");
  char ba[16384], bb[16384]; int eq = 1;
  if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
  for (;;) {
    size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
    if (na != nb || memcmp(ba, bb, na) != 0) { eq = 0; break; }
    if (na == 0) break;
  }
  fclose(fa); fclose(fb);
  return eq;
}

static int copy_file(const char* s, const char* d)
{
  FILE* fs = u8fopen(s, "rb"); FILE* fd;
  char buf[16384]; size_t n; int ok = 1;
  if (!fs) return 0;
  fd = u8fopen(d, "wb");
  if (!fd) { fclose(fs); return 0; }
  while ((n = fread(buf, 1, sizeof(buf), fs)) > 0)
    if (fwrite(buf, 1, n, fd) != n) { ok = 0; break; }
  fclose(fs);
  if (fclose(fd) != 0) ok = 0;
  return ok;
}

static int copy_tree(CopyCtx* cx, const char* src, const char* dst, int top)
{
  Updater* u = cx->u;
  Ent* e; int n = list_dir(src, &e), i, ok = 1;
  mkdirs(dst);
  for (i = 0; i < n && ok; ++i) {
    char s[1200], d[1200];
    if (u->cancel) { ok = 0; break; }
    join_path(s, sizeof(s), src, e[i].name);
    join_path(d, sizeof(d), dst, e[i].name);
    if (e[i].isdir) {
      if (top && ci_eq(e[i].name, "db_custom")) continue;
      ok = copy_tree(cx, s, d, 0);
    }
    else {
      int exists = file_exists(d);
      ++cx->done;
      if (cx->total) set_pct(u, 80 + cx->done * 19 / cx->total);
      if (exists && files_equal(s, d)) continue;
      if (!copy_file(s, d)) { ok = 0; break; }
      if (exists) ++u->changed; else ++u->added;
    }
  }
  free(e);
  return ok;
}

static void read_version(Updater* u)
{
  CdieConfig cfg;
  snprintf(u->installed, sizeof(u->installed), "unknown");
  if (config_load(&cfg) && cfg.db_version[0])
    snprintf(u->installed, sizeof(u->installed), "%s", cfg.db_version);
}

static void save_install_state(Updater* u)
{
  CdieConfig cfg;
  config_load(&cfg);
  snprintf(cfg.main_db, sizeof(cfg.main_db), "%s", u->main_db);
  snprintf(cfg.extra_db, sizeof(cfg.extra_db), "%s", u->extra_db);
  snprintf(cfg.custom_db, sizeof(cfg.custom_db), "%s", u->custom_db);
  snprintf(cfg.db_version, sizeof(cfg.db_version), "%s", u->latest);
  config_save(&cfg);
}

static char* read_all(const char* path)
{
  FILE* f = u8fopen(path, "rb"); long n; char* b;
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
  if (n <= 0 || n > 4 * 1024 * 1024) { fclose(f); return NULL; }
  b = (char*)malloc((size_t)n + 1);
  if (b) { n = (long)fread(b, 1, (size_t)n, f); b[n] = 0; }
  fclose(f);
  return b;
}

static int is_date(const char* p)
{
  return p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' &&
    p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9' && p[4] == '-' &&
    p[5] >= '0' && p[5] <= '9' && p[6] >= '0' && p[6] <= '9' && p[7] == '-' &&
    p[8] >= '0' && p[8] <= '9' && p[9] >= '0' && p[9] <= '9';
}

static void parse_version(const char* json, char* out, size_t cap)
{
  const char* p = strstr(json, "Last updated: ");
  out[0] = 0;
  if (p && is_date(p + 14)) { snprintf(out, cap, "%.10s", p + 14); return; }
  p = strstr(json, "\"updated_at\"");
  if (p && (p = strchr(p + 12, '"')) && (p = strchr(p + 1, '"')) && is_date(p + 1))
    snprintf(out, cap, "%.10s", p + 1);
}

static int parse_zip_url(const char* json, char* out, size_t cap)
{
  const char* p = json;
  while ((p = strstr(p, "\"browser_download_url\"")) != NULL) {
    const char* q;
    p += 22;
    while (*p && *p != ':') ++p;
    if (*p) ++p;
    while (*p == ' ') ++p;
    if (*p != '"') continue;
    ++p;
    q = strchr(p, '"');
    if (!q) break;
    if (q - p >= 7 && (size_t)(q - p) < cap && !strncmp(q - 7, "/db.zip", 7)) {
      memcpy(out, p, (size_t)(q - p)); out[q - p] = 0;
      return 1;
    }
    p = q;
  }
  return 0;
}

static int parse_commit_sha(const char* json, char* out, size_t cap)
{
  const char* p = strstr(json, "\"sha\"");
  size_t n = 0;
  out[0] = 0;
  if (!p) return 0;
  p += 5;
  while (*p == ' ' || *p == ':') ++p;
  if (*p != '"') return 0;
  ++p;
  while (p[n] && p[n] != '"' && n + 1 < cap) { out[n] = p[n]; ++n; }
  out[n] = 0;
  return n >= 7;
}

static int writable_dir(const char* dir)
{
  char t[1300]; FILE* f;
  if (!dir[0]) return 0;
  mkdirs(dir);
  join_path(t, sizeof(t), dir, ".cdie_write_test");
  f = u8fopen(t, "w");
  if (!f) return 0;
  fclose(f);
  del_file(t);
  return 1;
}

static int base_is(const char* path, const char* name)
{
  const char* p = path + strlen(path);
  while (p > path && !is_sep(p[-1])) --p;                                                                               
  return ci_eq(p, name);
}

static void nest_if_empty(char* path, size_t cap, const char* leaf)
{
  size_t n; Ent* e; int cnt;
  if (!path[0]) return;
  n = strlen(path);
  while (n > 1 && is_sep(path[n - 1])) path[--n] = 0;
  if (base_is(path, leaf)) return;
  cnt = list_dir(path, &e);
  free(e);
  if (cnt == 0) {
    char t[1200];
    join_path(t, sizeof(t), path, leaf);
    snprintf(path, cap, "%s", t);
  }
}

static int pick_targets(Updater* u)
{
  char base[1100];
  if (!u->main_db[0]) return 0;
  nest_if_empty(u->main_db, sizeof(u->main_db), "db");
  if (u->extra_db[0]) nest_if_empty(u->extra_db, sizeof(u->extra_db), "db_extra");
  if (u->custom_db[0]) {
    nest_if_empty(u->custom_db, sizeof(u->custom_db), "db_custom");
    mkdirs(u->custom_db);
  }
  if (!config_user_dir(base, sizeof(base))) return writable_dir(u->main_db);
  if (!writable_dir(u->main_db)) join_path(u->main_db, sizeof(u->main_db), base, "db");
  if (u->extra_db[0] && !writable_dir(u->extra_db))
    join_path(u->extra_db, sizeof(u->extra_db), base, "db_extra");
  return writable_dir(u->main_db);
}

static int db_present(Updater* u)
{
  return u->main_db[0] && count_files(u->main_db, 1) > 0;
}

static void job_check(Updater* u)
{
  char tmp[1100], file[1200]; char* json;
  read_version(u);
  if (!temp_dir(tmp, sizeof(tmp))) { fail(u, "No temporary folder available."); return; }
  snprintf(file, sizeof(file), "%scdie_release.json", tmp);
  {
    int attempt, got_it = 0;
    for (attempt = 0; attempt < 3 && !got_it && !u->cancel; ++attempt) {
      if (attempt) {
#ifdef _WIN32
        Sleep(1500 * attempt);
#else
        usleep(1500000u * (unsigned)attempt);
#endif
      }
      got_it = http_get(u, u->use_commits ? COMMITS_API : RELEASE_API, file, 0);
    }
    if (!got_it) { fail(u, "Could not reach GitHub (server busy or offline). Try again in a moment."); return; }
  }
  json = read_all(file);
  del_file(file);
  if (!json) { fail(u, "Could not read the release information."); return; }
  if (u->use_commits) {
    char sha[64];
    if (!parse_commit_sha(json, sha, sizeof(sha))) {
      free(json);
      fail(u, "Could not read the latest commit from GitHub.");
      return;
    }
    snprintf(u->latest, sizeof(u->latest), "git:%.7s", sha);
    snprintf(u->url, sizeof(u->url), COMMITS_ZIP_FMT, sha);
  }
  else {
    parse_version(json, u->latest, sizeof(u->latest));
    if (!parse_zip_url(json, u->url, sizeof(u->url)))
      snprintf(u->url, sizeof(u->url), "%s", RELEASE_ZIP);
  }
  free(json);
  if (!u->latest[0]) { fail(u, "Could not find the latest database version."); return; }
  if (!db_present(u)) snprintf(u->installed, sizeof(u->installed), "none");
  u->phase = (!strcmp(u->installed, u->latest) && db_present(u)) ? UPD_UPTODATE : UPD_READY;
}

static void job_install(Updater* u)
{
  char tmp[1100], zip[1200], work[1200], src_main[1200], src_extra[1200];
  CopyCtx cx; int have_extra;

  if (!u->main_db[0]) { fail(u, "No database folder is set. Use Options > Databases."); return; }
  if (!pick_targets(u)) { fail(u, "No writable folder is available for the database."); return; }
  u->added = u->changed = 0;
  if (!temp_dir(tmp, sizeof(tmp))) { fail(u, "No temporary folder available."); return; }
  snprintf(zip, sizeof(zip), "%scdie_db.zip", tmp);
  snprintf(work, sizeof(work), "%scdie_db_extract", tmp);

  set_label(u, "Downloading database");
  {
    int attempt, got_it = 0;
    for (attempt = 0; attempt < 3 && !got_it && !u->cancel; ++attempt) {
      if (attempt) {
#ifdef _WIN32
        Sleep(2000 * attempt);
#else
        usleep(2000000u * (unsigned)attempt);
#endif
      }
      got_it = http_get(u, (attempt == 1 && !u->use_commits) ? RELEASE_ZIP_FALLBACK
        : (u->url[0] ? u->url : RELEASE_ZIP), zip, 1);
    }
    if (!got_it) {
      if (u->cancel) u->phase = UPD_READY;
      else fail(u, "Download failed. Check your connection.");
      return;
    }
  }

  set_label(u, "Extracting");
  set_pct(u, 72);
  rm_tree(work);
  mkdirs(work);
  if (!extract_zip(zip, work)) {
    fail(u, "Could not unpack the archive (tar/unzip missing?).");
    goto cleanup;
  }
  if (u->cancel) { u->phase = UPD_READY; goto cleanup; }

  set_label(u, "Verifying");
  set_pct(u, 78);
  if (!find_dir(work, "db", 3, src_main, sizeof(src_main)))
    snprintf(src_main, sizeof(src_main), "%s", work);          
  have_extra = u->extra_db[0] && find_dir(work, "db_extra", 3, src_extra, sizeof(src_extra));
  cx.u = u; cx.done = 0;
  cx.total = count_files(src_main, 1) + (have_extra ? count_files(src_extra, 0) : 0);
  if (count_files(src_main, 1) == 0) { fail(u, "The downloaded archive is empty or has an unexpected layout."); goto cleanup; }

  set_label(u, "Installing");
  if (!copy_tree(&cx, src_main, u->main_db, 1) ||
    (have_extra && !copy_tree(&cx, src_extra, u->extra_db, 0))) {
    if (u->cancel) u->phase = UPD_READY;
    else fail(u, "Could not write to the database folder. Check permissions.");
    goto cleanup;
  }

  set_label(u, "Finishing up");
  set_pct(u, 99);
  if (!gui_backend_set_db_paths(u->main_db, u->extra_db, u->custom_db)) {
    fail(u, "Updated, but the engine could not reload the database.");
    goto cleanup;
  }
  save_install_state(u);
  snprintf(u->installed, sizeof(u->installed), "%s", u->latest);
  u->db_changed = 1;
  u->pct = 100;
  u->phase = UPD_DONE;

cleanup:
  del_file(zip);
  rm_tree(work);
}

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID p)
#else
static void* thread_main(void* p)
#endif
{
  Updater* u = (Updater*)p;
  if (u->job == JOB_CHECK) job_check(u); else job_install(u);
  u->busy = 0;
  notify(u);
  return 0;
}

static void join_thread(Updater* u)
{
  if (!u->thread) return;
#ifdef _WIN32
  WaitForSingleObject((HANDLE)u->thread, INFINITE);
  CloseHandle((HANDLE)u->thread);
#else
  pthread_join(*(pthread_t*)u->thread, NULL);
  free(u->thread);
#endif
  u->thread = NULL;
}

static void load_paths(Updater* u)
{
  CdieConfig cfg;
  const char* m = gui_backend_main_db_path();
  const char* e = gui_backend_extra_db_path();
  const char* c = gui_backend_custom_db_path();
  config_load(&cfg);
  snprintf(u->main_db, sizeof(u->main_db), "%s", cfg.main_db[0] ? cfg.main_db : (m ? m : ""));
  snprintf(u->extra_db, sizeof(u->extra_db), "%s", cfg.extra_db[0] ? cfg.extra_db : (e ? e : ""));
  snprintf(u->custom_db, sizeof(u->custom_db), "%s", cfg.custom_db[0] ? cfg.custom_db : (c ? c : ""));
  u->use_commits = cfg.db_commits;
  if (config_db_required()) u->main_db[0] = 0;     /* installed/Store: user must choose a folder first */
}

static void start(Updater* u, int job)
{
  if (u->busy) return;
  join_thread(u);
  load_paths(u);
  u->job = job; u->cancel = 0; u->pct = 0; u->error[0] = 0;
  u->phase = job == JOB_CHECK ? UPD_CHECKING : UPD_RUNNING;
  u->busy = 1;
#ifdef _WIN32
  u->thread = CreateThread(NULL, 0, thread_main, u, 0, NULL);
  if (!u->thread) { u->busy = 0; fail(u, "Could not start the update thread."); }
#else
  u->thread = malloc(sizeof(pthread_t));
  if (!u->thread || pthread_create((pthread_t*)u->thread, NULL, thread_main, u) != 0) {
    free(u->thread); u->thread = NULL; u->busy = 0;
    fail(u, "Could not start the update thread.");
  }
#endif
}

void updater_init(Updater* u, void (*cb)(void*), void* user)
{
  memset(u, 0, sizeof(*u));
  u->notify = cb; u->user = user;
  snprintf(u->installed, sizeof(u->installed), "unknown");
}

void updater_check(Updater* u) { start(u, JOB_CHECK); }
void updater_install(Updater* u) { start(u, JOB_INSTALL); }
void updater_cancel(Updater* u) { u->cancel = 1; }
void updater_shutdown(Updater* u) { u->cancel = 1; join_thread(u); }