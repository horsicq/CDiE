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
#if defined(_WIN32)
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#elif defined(__linux__)
#define _GNU_SOURCE
#include <glib.h>
#include <poll.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <mach/mach_time.h>
#include <sys/stat.h>
#endif
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/dlwatch.h"
#include "core/gui_backend.h"
#include "core/config.h"
#include "core/tray.h"
#include "core/i18n.h"


#define PLAT_PATH_MAX 4096

#define DL_MAX          8
#define DL_MAX_AGE      14400   /* ticks of 250 ms = 1 hour */
#define DL_DEDUP_MS     15000

typedef struct { char path[PLAT_PATH_MAX]; uint64_t size; int stable, age; } DlPending;
typedef struct { char text[200]; int count; } quick_scan;
typedef struct { int id, got; } first_type;

static DlWatchHost g_host;
static int         g_on;
static char* g_lastDl;
static DlPending   g_dl[DL_MAX];
static int         g_dlCount;
static char        g_dlDone[PLAT_PATH_MAX];
static uint64_t    g_dlDoneTick;

const DlWatchHost* dlwatch_host(void) { return &g_host; }
int  dlwatch_running(void) { return dlp_running(); }
void dlwatch_init(const DlWatchHost* host) { g_host = *host; }
int  dlwatch_enabled(void) { return g_on; }

static int is_sep(char c)
{
#ifdef _WIN32
  return c == '\\' || c == '/';
#else
  return c == '/';
#endif
}

static int ci_eq(const char* a, const char* b)
{
  for (; *a && *b; ++a, ++b)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
  return *a == *b;
}

static int ci_contains(const char* s, const char* needle)
{
  size_t n = strlen(needle), i;
  if (!s) return 0;
  for (; *s; ++s) {
    for (i = 0; i < n && s[i] && tolower((unsigned char)s[i]) == tolower((unsigned char)needle[i]); ++i) {}
    if (i == n) return 1;
  }
  return 0;
}

static int path_eq(const char* a, const char* b)
{
#if defined(_WIN32) || defined(__APPLE__)
  return ci_eq(a, b);        /* case-insensitive file systems by default */
#else
  return !strcmp(a, b);
#endif
}

int dlwatch_is_temp(const char* path)
{
  static const char* ext[] = { ".crdownload", ".part", ".partial", ".tmp",
    ".download", ".opdownload", ".!ut", ".filepart" };
  const char* dot = NULL, * p;
  size_t i;
  for (p = path; *p; ++p) {
    if (is_sep(*p)) dot = NULL;
    else if (*p == '.') dot = p;
  }
  if (!dot) return 0;
  for (i = 0; i < sizeof(ext) / sizeof(ext[0]); ++i)
    if (ci_eq(dot, ext[i])) return 1;
  return 0;
}

static const char* base_name(const char* path)
{
  const char* p, * name = path;
  for (p = path; *p; ++p) if (is_sep(*p)) name = p + 1;
  return name;
}

static void host_status(const char* text)
{
  if (g_host.set_status) g_host.set_status(text, g_host.user);
}

static void first_type_cb(int type_id, const char* name, void* u)
{
  first_type* f = (first_type*)u;
  (void)name;
  if (!f->got) { f->id = type_id; f->got = 1; }
}

static void quick_cb(const char* type, const char* name, const char* version,
  const char* info, void* u)
{
  quick_scan* q = (quick_scan*)u;
  size_t used;

  if (!type || (!ci_contains(type, translate("Packer")) && !ci_contains(type, translate("Protector"))) && !ci_contains(type, translate("Malware")))
    return;

  q->count++;
  if (q->count > 2) return;
  used = strlen(q->text);
  snprintf(q->text + used, sizeof(q->text) - used, "%s%s: %s%s%s%s%s%s",
    q->count > 1 ? "\n" : "",
    type,
    name ? name : "",
    (version && version[0]) ? " " : "",
    version ? version : "",
    (info && info[0]) ? " [" : "",
    info ? info : "",
    (info && info[0]) ? "]" : "");
}

static void on_download_found(const char* path)
{
  first_type ft = { 0, 0 };
  quick_scan q;
  char* report = NULL;
  char* type = NULL;
  char text[1024];
  uint64_t t0 = dlp_tick_ms();
  unsigned scan_ms;
  char* keep;

  if (config_db_required()) return;
  memset(&q, 0, sizeof(q));

  gui_backend_set_flags(0, 1, 1, 0, 0, 0);
  gui_backend_file_types(path, first_type_cb, &ft);
  gui_backend_scan_results_type(path, ft.id, quick_cb, &q, &report, &type);
  if (report) gui_backend_free(report);
  if (type)   gui_backend_free(type);
  if (g_host.restore_flags) g_host.restore_flags(g_host.user);

  scan_ms = (unsigned)(dlp_tick_ms() - t0);
  snprintf(text, sizeof(text), translate("%s\n%s (%u ms)"), base_name(path),
    q.count ? q.text : translate("No packer or protector found"), scan_ms);
  tray_notify(translate("New download scanned"), text);

  keep = (char*)malloc(strlen(path) + 1);
  if (keep) {
    strcpy(keep, path);
    free(g_lastDl);
    g_lastDl = keep;
  }
}

static int recently_done(const char* path)
{
  return path_eq(path, g_dlDone) && dlp_tick_ms() - g_dlDoneTick < DL_DEDUP_MS;
}

static void mark_done(const char* path)
{
  snprintf(g_dlDone, sizeof(g_dlDone), "%s", path);
  g_dlDoneTick = dlp_tick_ms();
}

static void dl_queue(const char* path)
{
  int i;
  uint64_t s;
  if (dlwatch_is_temp(path) || recently_done(path)) return;
  for (i = 0; i < g_dlCount; ++i)
    if (path_eq(g_dl[i].path, path)) { g_dl[i].stable = 0; return; }
  if (g_dlCount == DL_MAX) return;
  snprintf(g_dl[g_dlCount].path, sizeof(g_dl[0].path), "%s", path);
  g_dl[g_dlCount].size = dlp_file_size(path, &s) ? s : (uint64_t)-1;
  g_dl[g_dlCount].stable = 0;
  g_dl[g_dlCount].age = 0;
  g_dlCount++;
  dlp_timer(1);
}

static void dl_clear(void)
{
  g_dlCount = 0;
  dlp_timer(0);
}

void dlwatch_on_timer(void)
{
  int i;
  for (i = g_dlCount - 1; i >= 0; --i) {
    DlPending* e = &g_dl[i];
    uint64_t size;
    char path[PLAT_PATH_MAX];
    int remove = 0, ready = 0;

    if (!dlp_file_size(e->path, &size) || ++e->age > DL_MAX_AGE) remove = 1;
    else if (size != e->size) { e->size = size; e->stable = 0; }
    else if (size > 0 && ++e->stable >= 2) {
      if (dlp_can_open(e->path, 1)) { ready = 1; remove = 1; }
    }
    if (remove) {
      snprintf(path, sizeof(path), "%s", e->path);
      g_dl[i] = g_dl[--g_dlCount];
      if (ready) {
        mark_done(path);
        on_download_found(path);
      }
    }
  }
  if (!g_dlCount) dlp_timer(0);
}

static void dl_complete(const char* path)
{
  int i;
  if (dlwatch_is_temp(path) || recently_done(path)) return;

  for (i = 0; i < g_dlCount; ++i)
    if (path_eq(g_dl[i].path, path)) { g_dl[i] = g_dl[--g_dlCount]; break; }

  if (!dlp_can_open(path, 0)) { dl_queue(path); return; }
  mark_done(path);
  on_download_found(path);
}

void dlwatch_on_event(int complete, const char* path)
{
  uint64_t s;
  if (!g_on || !path) return;
  if (!dlp_file_size(path, &s)) return;        /* gone, or a directory */
  if (complete) dl_complete(path);
  else          dl_queue(path);
}

void dlwatch_set_enabled(int on)
{
  if (on) {
    if (!dlp_start()) {
      g_on = 0;
      tray_set_monitor(0);
      host_status(translate("Could not watch the Downloads folder."));
      return;
    }
    g_on = 1;
    tray_set_monitor(1);
    tray_set_persistent(1);
    host_status(translate("Download monitoring on. Closing the window keeps cdie in the tray."));
  }
  else {
    dlp_stop();
    dl_clear();
    g_on = 0;
    tray_set_monitor(0);
    tray_set_persistent(0);
    host_status(translate("Download monitoring off."));
  }
}

void dlwatch_shutdown(void)
{
  dlp_stop();
  free(g_lastDl);
  g_lastDl = NULL;
}

void dlwatch_balloon_click(void* user)
{
  (void)user;
  if (g_lastDl && g_host.request_scan)
    g_host.request_scan(g_lastDl, g_host.user);
}

void dlwatch_menu_toggle(void* user, int enabled)
{
  (void)user;
  dlwatch_set_enabled(enabled);
}

#if defined(_WIN32)

#ifndef WM_DLWATCH
#define WM_DLWATCH (WM_APP + 2)
#endif
#define IDT_DLSTABLE 0xD101

static HANDLE g_thread, g_stop;
static HWND   g_wnd;
static WCHAR  g_wdir[MAX_PATH * 2];
static char   g_dir[PLAT_PATH_MAX];

static int w2u(const WCHAR* w, char* out, int cap)
{
  return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL) > 0;
}
static int u2w(const char* u, WCHAR* out, int cap)
{
  return MultiByteToWideChar(CP_UTF8, 0, u, -1, out, cap) > 0;
}

uint64_t dlp_tick_ms(void) { return GetTickCount64(); }
int dlp_running(void) { return g_thread != NULL; }

int dlp_file_size(const char* path, uint64_t* size)
{
  WCHAR w[PLAT_PATH_MAX];
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!u2w(path, w, PLAT_PATH_MAX)) return 0;
  if (!GetFileAttributesExW(w, GetFileExInfoStandard, &fa)) return 0;
  if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 0;
  *size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
  return 1;
}

int dlp_can_open(const char* path, int exclusive)
{
  WCHAR w[PLAT_PATH_MAX];
  HANDLE h;
  DWORD share = exclusive ? 0 : (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
  if (!u2w(path, w, PLAT_PATH_MAX)) return 0;
  h = CreateFileW(w, GENERIC_READ, share, NULL, OPEN_EXISTING, 0, NULL);
  if (h == INVALID_HANDLE_VALUE) return 0;
  CloseHandle(h);
  return 1;
}

void dlp_timer(int on)
{
  if (on) SetTimer(g_wnd, IDT_DLSTABLE, 250, NULL);
  else    KillTimer(g_wnd, IDT_DLSTABLE);
}

static void post_file(const WCHAR* wname, int complete)
{
  char name[MAX_PATH * 4], * copy;
  size_t dl, nl;
  if (!w2u(wname, name, sizeof(name))) return;
  if (dlwatch_is_temp(name)) return;
  dl = strlen(g_dir); nl = strlen(name);
  copy = (char*)malloc(dl + nl + 2);
  if (!copy) return;
  memcpy(copy, g_dir, dl);
  copy[dl] = '\\';
  memcpy(copy + dl + 1, name, nl + 1);
  if (!PostMessageW(g_wnd, WM_DLWATCH, (WPARAM)complete, (LPARAM)copy)) free(copy);
}

static DWORD WINAPI watch_thread(LPVOID param)
{
  HANDLE hDir;
  OVERLAPPED ov;
  DWORD buf[4096];
  int prev_temp = 0;
  (void)param;

  hDir = CreateFileW(g_wdir, FILE_LIST_DIRECTORY,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
  if (hDir == INVALID_HANDLE_VALUE) return 1;

  ZeroMemory(&ov, sizeof(ov));
  ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!ov.hEvent) { CloseHandle(hDir); return 1; }

  for (;;) {
    HANDLE waits[2];
    DWORD w, n = 0;
    ResetEvent(ov.hEvent);
    if (!ReadDirectoryChangesW(hDir, buf, sizeof(buf), FALSE,
      FILE_NOTIFY_CHANGE_FILE_NAME, NULL, &ov, NULL))
      break;
    waits[0] = g_stop; waits[1] = ov.hEvent;
    w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (w == WAIT_OBJECT_0) {
      CancelIoEx(hDir, &ov);
      GetOverlappedResult(hDir, &ov, &n, TRUE);
      break;
    }
    if (!GetOverlappedResult(hDir, &ov, &n, FALSE) || n == 0) continue;
    {
      BYTE* p = (BYTE*)buf;
      for (;;) {
        FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)p;
        WCHAR name[MAX_PATH];
        DWORD chars = fni->FileNameLength / sizeof(WCHAR);
        if (chars > 0 && chars < MAX_PATH) {
          CopyMemory(name, fni->FileName, chars * sizeof(WCHAR));
          name[chars] = 0;
          if (fni->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            char u[MAX_PATH * 4];
            prev_temp = w2u(name, u, sizeof(u)) && dlwatch_is_temp(u);
          }
          else if (fni->Action == FILE_ACTION_RENAMED_NEW_NAME) {
            post_file(name, prev_temp);   /* temp -> final rename == download finished */
            prev_temp = 0;
          }
          else if (fni->Action == FILE_ACTION_ADDED) {
            post_file(name, 0);
          }
        }
        if (!fni->NextEntryOffset) break;
        p += fni->NextEntryOffset;
      }
    }
  }
  CloseHandle(ov.hEvent);
  CloseHandle(hDir);
  return 0;
}

int dlp_start(void)
{
  PWSTR known = NULL;
  if (g_thread) return 1;
  g_wnd = (HWND)dlwatch_host()->native;

  if (FAILED(SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &known)) || !known) return 0;
  lstrcpynW(g_wdir, known, (int)(sizeof(g_wdir) / sizeof(WCHAR)));
  CoTaskMemFree(known);
  if (!w2u(g_wdir, g_dir, sizeof(g_dir))) return 0;

  g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!g_stop) return 0;
  g_thread = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
  if (!g_thread) { CloseHandle(g_stop); g_stop = NULL; return 0; }
  return 1;
}

void dlp_stop(void)
{
  if (!g_thread) return;
  SetEvent(g_stop);
  WaitForSingleObject(g_thread, 3000);
  CloseHandle(g_thread); g_thread = NULL;
  CloseHandle(g_stop);   g_stop = NULL;
}

int dlwatch_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
  (void)hwnd;
  if (msg == WM_DLWATCH) {
    char* p = (char*)lp;
    if (p) { dlwatch_on_event((int)wp, p); free(p); }
    *result = 0;
    return 1;
  }
  if (msg == WM_TIMER && wp == IDT_DLSTABLE) {
    dlwatch_on_timer();
    *result = 0;
    return 1;
  }
  return 0;
}

#elif defined(__APPLE__)

static FSEventStreamRef g_stream;
static dispatch_queue_t g_queue;
static dispatch_source_t g_timer;
static char g_dir[PLAT_PATH_MAX];

uint64_t dlp_tick_ms(void)
{
  static mach_timebase_info_data_t tb;
  if (!tb.denom) mach_timebase_info(&tb);
  return mach_absolute_time() * tb.numer / tb.denom / 1000000u;
}

int dlp_running(void) { return g_stream != NULL; }

int dlp_file_size(const char* path, uint64_t* size)
{
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
  *size = (uint64_t)st.st_size;
  return 1;
}

int dlp_can_open(const char* path, int exclusive)
{
  FILE* f = fopen(path, "rb");
  (void)exclusive;
  if (!f) return 0;
  fclose(f);
  return 1;
}

void dlp_timer(int on)
{
  if (on && !g_timer) {
    g_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_timer(g_timer, dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC),
      250 * NSEC_PER_MSEC, 20 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(g_timer, ^ { dlwatch_on_timer(); });
    dispatch_resume(g_timer);
  }
  else if (!on && g_timer) {
    dispatch_source_t t = g_timer;
    g_timer = NULL;
    dispatch_source_set_cancel_handler(t, ^ { dispatch_release(t); });
    dispatch_source_cancel(t);
  }
}

static void fs_cb(ConstFSEventStreamRef s, void* info, size_t n, void* paths,
  const FSEventStreamEventFlags flags[], const FSEventStreamEventId ids[])
{
  size_t i, dl = strlen(g_dir);
  char** p = (char**)paths;
  (void)s; (void)info; (void)ids;
  for (i = 0; i < n; ++i) {
    const char* path = p[i];
    const char* slash;
    char* copy;
    if (!(flags[i] & kFSEventStreamEventFlagItemIsFile)) continue;
    if (!(flags[i] & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRenamed))) continue;
    if (strncmp(path, g_dir, dl) != 0 || path[dl] != '/') continue;
    slash = strchr(path + dl + 1, '/');
    if (slash) continue;                          /* not recursive */
    if (dlwatch_is_temp(path)) continue;
    copy = strdup(path);
    if (!copy) continue;
    dispatch_async(dispatch_get_main_queue(), ^ { dlwatch_on_event(0, copy); free(copy); });
  }
}

int dlp_start(void)
{
  const char* home = getenv("HOME");
  CFStringRef dir;
  CFArrayRef arr;
  FSEventStreamContext ctx = { 0, NULL, NULL, NULL, NULL };
  if (g_stream) return 1;
  if (!home) return 0;
  snprintf(g_dir, sizeof(g_dir), "%s/Downloads", home);

  dir = CFStringCreateWithCString(NULL, g_dir, kCFStringEncodingUTF8);
  arr = CFArrayCreate(NULL, (const void**)&dir, 1, &kCFTypeArrayCallBacks);
  g_stream = FSEventStreamCreate(NULL, fs_cb, &ctx, arr, kFSEventStreamEventIdSinceNow, 0.1,
    kFSEventStreamCreateFlagFileEvents);   /* no UseCFTypes: paths arrive as char** */
  CFRelease(arr); CFRelease(dir);
  if (!g_stream) return 0;

  g_queue = dispatch_queue_create("cdie.dlwatch", DISPATCH_QUEUE_SERIAL);
  FSEventStreamSetDispatchQueue(g_stream, g_queue);
  if (!FSEventStreamStart(g_stream)) {
    FSEventStreamInvalidate(g_stream); FSEventStreamRelease(g_stream);
    g_stream = NULL;
    return 0;
  }
  return 1;
}

void dlp_stop(void)
{
  if (!g_stream) return;
  FSEventStreamStop(g_stream);
  FSEventStreamInvalidate(g_stream);
  FSEventStreamRelease(g_stream);
  g_stream = NULL;
  dispatch_release(g_queue);
  g_queue = NULL;
}

#else

static pthread_t g_thread;
static int  g_running;
static int  g_pipe[2] = { -1, -1 };
static char g_dir[PLAT_PATH_MAX];
static guint g_timer;

uint64_t dlp_tick_ms(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int dlp_running(void) { return g_running; }

int dlp_file_size(const char* path, uint64_t* size)
{
  struct stat st;
  if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
  *size = (uint64_t)st.st_size;
  return 1;
}

int dlp_can_open(const char* path, int exclusive)
{
  FILE* f = fopen(path, "rb");
  (void)exclusive;
  if (!f) return 0;
  fclose(f);
  return 1;
}

static gboolean tick_cb(gpointer d)
{
  (void)d;
  dlwatch_on_timer();
  if (!g_timer) return G_SOURCE_REMOVE;
  return G_SOURCE_CONTINUE;
}

void dlp_timer(int on)
{
  if (on && !g_timer) g_timer = g_timeout_add(250, tick_cb, NULL);
  else if (!on && g_timer) { g_source_remove(g_timer); g_timer = 0; }
}

typedef struct { int complete; char* path; } Ev;

static gboolean ev_idle(gpointer d)
{
  Ev* e = (Ev*)d;
  dlwatch_on_event(e->complete, e->path);
  g_free(e->path);
  g_free(e);
  return G_SOURCE_REMOVE;
}

static void post_file(const char* name, int complete)
{
  Ev* e;
  if (dlwatch_is_temp(name)) return;
  e = g_new0(Ev, 1);
  e->complete = complete;
  e->path = g_build_filename(g_dir, name, NULL);
  g_idle_add(ev_idle, e);
}

static int downloads_dir(char* out, size_t cap)
{
  const char* home = g_get_home_dir();
  char* cfg = g_build_filename(g_get_user_config_dir(), "user-dirs.dirs", NULL);
  char* text = NULL;
  int ok = 0;
  if (g_file_get_contents(cfg, &text, NULL, NULL)) {
    char* line = strstr(text, "XDG_DOWNLOAD_DIR=\"");
    if (line && (line == text || line[-1] == '\n')) {
      char* v = line + strlen("XDG_DOWNLOAD_DIR=\""), * end = strchr(v, '"');
      if (end) {
        *end = 0;
        if (!strncmp(v, "$HOME", 5)) snprintf(out, cap, "%s%s", home, v + 5);
        else snprintf(out, cap, "%s", v);
        ok = 1;
      }
    }
  }
  g_free(text); g_free(cfg);
  if (!ok) snprintf(out, cap, "%s/Downloads", home);
  return 1;
}

static void* watch_thread(void* arg)
{
  char buf[8192] __attribute__((aligned(__alignof__(struct inotify_event))));
  uint32_t prev_cookie = 0;
  int prev_temp = 0;
  int fd = inotify_init1(IN_CLOEXEC);
  (void)arg;
  if (fd < 0) return NULL;
  if (inotify_add_watch(fd, g_dir, IN_CREATE | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE) < 0) {
    close(fd);
    return NULL;
  }
  for (;;) {
    struct pollfd p[2] = { { fd, POLLIN, 0 },{ g_pipe[0], POLLIN, 0 } };
    ssize_t n, off = 0;
    if (poll(p, 2, -1) < 0) continue;
    if (p[1].revents) break;
    n = read(fd, buf, sizeof(buf));
    if (n <= 0) continue;
    while (off < n) {
      struct inotify_event* ev = (struct inotify_event*)(buf + off);
      off += (ssize_t)(sizeof(*ev) + ev->len);
      if (!ev->len || (ev->mask & IN_ISDIR)) continue;
      if (ev->mask & IN_MOVED_FROM) {
        prev_cookie = ev->cookie;
        prev_temp = dlwatch_is_temp(ev->name);
      }
      else if (ev->mask & IN_MOVED_TO) {
        post_file(ev->name, ev->cookie == prev_cookie && prev_temp);
        prev_cookie = 0; prev_temp = 0;
      }
      else if (ev->mask & IN_CREATE)      post_file(ev->name, 0);
      else if (ev->mask & IN_CLOSE_WRITE) post_file(ev->name, 1);
    }
  }
  close(fd);
  return NULL;
}

int dlp_start(void)
{
  if (g_running) return 1;
  downloads_dir(g_dir, sizeof(g_dir));
  if (pipe(g_pipe) != 0) return 0;
  if (pthread_create(&g_thread, NULL, watch_thread, NULL) != 0) {
    close(g_pipe[0]); close(g_pipe[1]);
    g_pipe[0] = g_pipe[1] = -1;
    return 0;
  }
  g_running = 1;
  return 1;
}

void dlp_stop(void)
{
  if (!g_running) return;
  if (write(g_pipe[1], "x", 1) < 0) { /* thread exits on close anyway */ }
  pthread_join(g_thread, NULL);
  close(g_pipe[0]); close(g_pipe[1]);
  g_pipe[0] = g_pipe[1] = -1;
  g_running = 0;
}

#endif