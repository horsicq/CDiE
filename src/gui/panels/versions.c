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
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/platform.h"
#include "theme/theme.h"
#include "core/i18n.h"
#include "panels/versions.h"

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#endif

#define VERS_PRODUCT_ID "9NQ58D7GHB2X"

#if defined(_M_ARM64) || defined(__aarch64__)
#define VERS_ARCH "arm64"
#elif defined(_WIN64)
#define VERS_ARCH "x64"
#else
#define VERS_ARCH "x86"
#endif

#define MAX_ITEMS 256
#define MAX_FILES 8
#define MAX_PKGS  128

#define FE3_URL   "https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx"
#define FE3_SEC   FE3_URL "/secured"
#define SVC_NS    "http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService"
#define NS_SOAP   "http://www.w3.org/2003/05/soap-envelope"
#define NS_ADDR   "http://www.w3.org/2005/08/addressing"
#define NS_SEC    "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-secext-1.0.xsd"
#define NS_UTIL   "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-wssecurity-utility-1.0.xsd"
#define NS_WUAUTH "http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization"

#define MARGIN   12
#define HEAD_H   84
#define BAR_H    56
#define TBL_HEAD 34
#define ROW_H    36
#define BTN_H    34

#define DEVICE_ATTRIBUTES \
  "BranchReadinessLevel=CB;CurrentBranch=rs_prerelease;OEMModel=Virtual Machine;" \
  "FlightRing=WIS;AttrDataVer=21;SystemManufacturer=Microsoft Corporation;" \
  "InstallLanguage=en-US;OSUILocale=en-US;InstallationType=Client;" \
  "FlightingBranchName=external;FirmwareVersion=Hyper-V UEFI Release v2.5;" \
  "SystemProductName=Virtual Machine;OSSkuId=48;FlightContent=Branch;App=WU;" \
  "OEMName_Uncleaned=Microsoft Corporation;AppVer=10.0.16184.1001;OSArchitecture=AMD64;" \
  "SystemSKU=None;UpdateManagementGroup=2;IsFlightingEnabled=1;IsDeviceRetailDemo=0;" \
  "TelemetryLevel=3;OSVersion=10.0.16184.1001;DeviceFamily=Windows.Desktop;"

enum { ST_INIT, ST_LOADING, ST_READY, ST_ERROR };
enum { B_REFRESH, B_DOWNLOAD, B_INSTALL, B_CLOSE, B_COUNT };
enum { JOB_LIST, JOB_DOWNLOAD };

typedef struct {
  char name[260];
  char digest[96];
  char algo[12];
  unsigned long long size;
  char ident[260];
} FileRec;

typedef struct {
  char id[64];
  char upd[64];
  char rev[16];
  int secured;
  char moniker[260];
  FileRec files[MAX_FILES];
  int nfiles;
} Item;

typedef struct {
  char moniker[260];
  char name[128];
  char version[40];
  char arch[12];
  char ext[16];
  unsigned long long size;
  char digest[96];
  char algo[12];
  char upd[64];
  char rev[16];
  int  main;
} Pkg;

typedef int (*DataFn)(void* u, const BYTE* d, DWORD n);
typedef struct { PlRect r; int id; int accent; } Btn;
typedef struct { char* p; size_t n, cap; } Buf;

typedef struct Vers {
  PlWidget* w;
  PlWindow* win;
  PlFont* font, * fontb;
  int W, H;
  PlRect r_head, r_list, r_bar;
  Btn btn[B_COUNT];
  int hov_btn, hov_row;

  int state, busy, closing, job;
  int sel, scroll, vis;
  Pkg* pkgs;
  int npkgs;
  char title[160], publisher[160], pfn[160];
  char current[64];
  char status[320];

  Pkg job_pkg;
  int job_install;
  Pkg* out_pkgs;
  int out_n;
  char out_title[160], out_pub[160], out_pfn[160];
  WCHAR out_path[MAX_PATH * 2];
  char out_err[256];
} Vers;

typedef struct {
  HANDLE file;
  BCRYPT_HASH_HANDLE hash;
  unsigned long long done;
} DlSink;

static const int s_installed_ids[] = {
  1, 2, 3, 11, 19, 544, 549, 2359974, 2359977, 5169044, 8788830, 23110993, 23110994,
  54341900, 54343656, 59830006, 59830007, 59830008, 60484010, 62450018, 62450019,
  62450020, 66027979, 66053150, 97657898, 98822896, 98959022, 98959023, 98959024,
  98959025, 98959026, 104433538, 104900364, 105489019, 117765322, 129905029,
  130040031, 132387090, 132393049, 133399034, 138537048, 140377312, 143747671,
  158941041, 158941042, 158941043, 158941044, 159123858, 159130928, 164836897,
  164847386, 164848327, 164852241, 164852246, 164852252, 164852253
};

static const char s_msa_device_b64[] =
"dAA9AEUAdwBBAHcAQQBzAE4AMwBCAEEAQQBVADEAYgB5AHMAZQBtAGIAZQBEAFYAQwArADMAZgBtADcAbwBXAHkASAA3AGIAbgBn"
"AEcAWQBtAEEAQQBMAGoAbQBqAFYAVQB2AFEAYwA0AEsAVwBFAC8AYwBDAEwANQBYAGUANABnAHYAWABkAGkAegBHAGwAZABjADEA"
"ZAAvAFcAeQAvAHgASgBQAG4AVwBRAGUAYwBtAHYAbwBjAGkAZwA5AGoAZABwAE4AawBIAG0AYQBzAHAAVABKAEwARAArAFAAYwBB"
"AFgAbQAvAFQAcAA3AEgAagBzAEYANAA0AEgAdABsAC8AMQBtAHUAcgAwAFMAdQBtAG8AMABZAGEAdgBqAFIANwArADQAcABoAC8A"
"cwA4ADEANgBFAFkANQBNAFIAbQBnAFIAQwA2ADMAQwBSAEoAQQBVAHYAZgBzADQAaQB2AHgAYwB5AEwAbAA2AHoAOABlAHgAMABr"
"AFgAOQBPAHcAYQB0ADEAdQBwAFMAOAAxAEgANgA4AEEASABzAEoAegBnAFQAQQBMAG8AbgBBADIAWQBBAEEAQQBpAGcANQBJADMA"
"UQAvAFYASABLAHcANABBAEIAcQA5AFMAcQBhADEAQgA4AGsAVQAxAGEAbwBLAEEAdQA0AHYAbABWAG4AdwBWADMAUQB6AHMATgBt"
"AEQAaQBqAGgANQBkAEcAcgBpADgAQQBlAEUARQBWAEcAbQBXAGgASQBCAE0AUAAyAEQAVwA0ADMAZABWAGkARABUAHoAVQB0AHQA"
"RQBMAEgAaABSAGYAcgBhAGIAWgBsAHQAQQBUAEUATABmAHMARQBGAFUAYQBRAFMASgB4ADUAeQBRADgAagBaAEUAZQAyAHgANABC"
"ADMAMQB2AEIAMgBqAC8AUgBLAGEAWQAvAHEAeQB0AHoANwBUAHYAdAB3AHQAagBzADYAUQBYAEIAZQA4AHMAZwBJAG8AOQBiADUA"
"QQBCADcAOAAxAHMANgAvAGQAUwBFAHgATgBEAEQAYQBRAHoAQQBYAFAAWABCAFkAdQBYAFEARQBzAE8AegA4AHQAcgBpAGUATQBi"
"AEIAZQBUAFkAOQBiAG8AQgBOAE8AaQBVADcATgBSAEYAOQAzAG8AVgArAFYAQQBiAGgAcAAwAHAAUgBQAFMAZQBmAEcARwBPAHEA"
"dwBTAGcANwA3AHMAaAA5AEoASABNAHAARABNAFMAbgBrAHEAcgAyAGYARgBpAEMAUABrAHcAVgBvAHgANgBuAG4AeABGAEQAbwBX"
"AC8AYQAxAHQAYQBaAHcAegB5AGwATAAxADIAdwB1AGIAbQA1AHUAbQBwAHEAeQBXAGMASwBSAGoAeQBoADIASgBUAEYASgBXADUA"
"ZwBYAEUASQA1AHAAOAAwAEcAdQAyAG4AeABMAFIATgB3AGkAdwByADcAVwBNAFIAQQBWAEsARgBXAE0AZQBSAHoAbAA5AFUAcQBn"
"AC8AcABYAC8AdgBlAEwAdwBTAGsAMgBTAFMASABmAGEASwA2AGoAYQBvAFkAdQBuAFIARwByADgAbQBiAEUAbwBIAGwARgA2AEoA"
"QwBhAGEAVABCAFgAQgBjAHYAdQBlAEMASgBvADkAOABoAFIAQQByAEcAdwA0ACsAUABIAGUAVABiAE4AUwBFAFgAWAB6AHYAWgA2"
"AHUAVwA1AEUAQQBmAGQAWgBtAFMAOAA4AFYASgBjAFoAYQBGAEsANwB4AHgAZwAwAHcAbwBuADcAaAAwAHgAQwA2AFoAQgAwAGMA"
"WQBqAEwAcgAvAEcAZQBPAHoAOQBHADQAUQBVAEgAOQBFAGsAeQAwAGQAeQBGAC8AcgBlAFUAMQBJAHkAaQBhAHAAcABoAE8AUAA4"
"AFMAMgB0ADQAQgByAFAAWgBYAFQAdgBDADAAUAA3AHoATwArAGYARwBrAHgAVgBtACsAVQBmAFoAYgBRADUANQBzAHcARQA9ACYA"
"cAA9AA==";


static volatile LONG s_cancel;
static volatile LONG s_done_kb, s_total_kb;
static char s_neterr[256];
static const char* volatile s_step;      
static volatile LONG s_timedout;
static HINTERNET volatile s_hsession;    

static Vers* s_v;
static int s_launched;

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static int inr(const PlRect* r, int x, int y) { return x >= r->l && x < r->r && y >= r->t && y < r->b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void vlog(const char* fmt, ...)
{
  WCHAR path[MAX_PATH + 32];
  char msg[1200], line[1300];
  va_list ap;
  SYSTEMTIME st;
  FILE* f;

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  GetLocalTime(&st);
  snprintf(line, sizeof(line), "[cdie-versions %02u:%02u:%02u.%03u] %s\n",
    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
  OutputDebugStringA(line);

  if (!GetTempPathW(MAX_PATH, path)) return;
  lstrcatW(path, L"cdie-versions.log");
  f = _wfopen(path, L"ab");
  if (!f) return;
  fputs(line, f);
  fclose(f);
}


static int buf_reserve(Buf* b, size_t add)
{
  size_t need = b->n + add + 1;
  if (need > b->cap) {
    size_t nc = b->cap ? b->cap : 4096;
    char* np;
    while (nc < need) nc *= 2;
    np = (char*)realloc(b->p, nc);
    if (!np) return 0;
    b->p = np; b->cap = nc;
  }
  return 1;
}

static int buf_add(Buf* b, const void* d, size_t n)
{
  if (!buf_reserve(b, n)) return 0;
  memcpy(b->p + b->n, d, n);
  b->n += n;
  b->p[b->n] = 0;
  return 1;
}

static int buf_str(Buf* b, const char* s) { return buf_add(b, s, strlen(s)); }

static int buf_fmt(Buf* b, const char* fmt, ...)
{
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0 || !buf_reserve(b, (size_t)n)) return 0;
  va_start(ap, fmt);
  vsnprintf(b->p + b->n, (size_t)n + 1, fmt, ap);
  va_end(ap);
  b->n += (size_t)n;
  return 1;
}

static void buf_free(Buf* b) { free(b->p); b->p = NULL; b->n = b->cap = 0; }

static WCHAR* towide(const char* s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  WCHAR* w;
  if (n <= 0) return NULL;
  w = (WCHAR*)malloc((size_t)n * sizeof(WCHAR));
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}

static void set_neterr(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_neterr, sizeof(s_neterr), fmt, ap);
  va_end(ap);
  vlog("error: %s", s_neterr);
}


static int buf_sink(void* u, const BYTE* d, DWORD n)
{
  return buf_add((Buf*)u, d, n);
}

static HINTERNET open_session(void)
{
  WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie;
  HINTERNET h = NULL;
  memset(&ie, 0, sizeof(ie));
  if (WinHttpGetIEProxyConfigForCurrentUser(&ie) && ie.lpszProxy && !ie.fAutoDetect && !ie.lpszAutoConfigUrl)
    h = WinHttpOpen(L"CDiE-Versions/1.0", WINHTTP_ACCESS_TYPE_NAMED_PROXY, ie.lpszProxy,
      ie.lpszProxyBypass ? ie.lpszProxyBypass : WINHTTP_NO_PROXY_BYPASS, 0);
  if (ie.lpszProxy) GlobalFree(ie.lpszProxy);
  if (ie.lpszProxyBypass) GlobalFree(ie.lpszProxyBypass);
  if (ie.lpszAutoConfigUrl) GlobalFree(ie.lpszAutoConfigUrl);
  if (!h) h = WinHttpOpen(L"CDiE-Versions/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
  return h;
}

static void abort_net(void)
{
  HINTERNET h = (HINTERNET)InterlockedExchangePointer((PVOID volatile*)&s_hsession, NULL);
  if (h) WinHttpCloseHandle(h);
}

static int http_request(const char* url8, const WCHAR* method, const WCHAR* headers,
  const char* body, DWORD bodylen, DataFn fn, void* u, DWORD* status_out)
{
  WCHAR* url = towide(url8);
  URL_COMPONENTS uc;
  WCHAR host[256];
  HINTERNET hs = NULL, hc = NULL, hr = NULL;
  DWORD status = 0, sz = sizeof(status);
  BYTE* chunk = NULL;
  int ok = 0;
  unsigned long long rx = 0, nextlog = 262144;

  if (!url) { set_neterr("Out of memory."); return 0; }
  memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
  if (!WinHttpCrackUrl(url, 0, 0, &uc) || uc.dwHostNameLength == 0 || uc.dwHostNameLength >= 255) {
    set_neterr("Bad URL.");
    goto done;
  }
  memcpy(host, uc.lpszHostName, uc.dwHostNameLength * sizeof(WCHAR));
  host[uc.dwHostNameLength] = 0;

  vlog("%ls %.100s", method, url8);
  hs = open_session();
  if (!hs) { set_neterr("WinHttpOpen failed (%lu).", GetLastError()); goto done; }
  InterlockedExchangePointer((PVOID volatile*)&s_hsession, hs);
  WinHttpSetTimeouts(hs, 15000, 15000, 30000, 120000);

  hc = WinHttpConnect(hs, host, uc.nPort, 0);
  if (!hc) { set_neterr("Cannot connect (%lu).", GetLastError()); goto done; }

  hr = WinHttpOpenRequest(hc, method, uc.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
  if (!hr) { set_neterr("Cannot open request (%lu).", GetLastError()); goto done; }

  vlog("  sending request, %lu body bytes", (unsigned long)bodylen);
  if (!WinHttpSendRequest(hr, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS,
    headers ? (DWORD)-1 : 0, (LPVOID)body, bodylen, bodylen, 0) ||
    !WinHttpReceiveResponse(hr, NULL)) {
    set_neterr("Network error (%lu). Check your internet connection.", GetLastError());
    goto done;
  }
  if (!WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
    WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX)) {
    set_neterr("No HTTP status (%lu).", GetLastError());
    goto done;
  }
  if (status_out) *status_out = status;
  vlog("  -> HTTP %lu", status);

  chunk = (BYTE*)malloc(65536);
  if (!chunk) { set_neterr("Out of memory."); goto done; }
  for (;;) {
    DWORD got = 0;
    if (s_cancel) { set_neterr("Cancelled."); goto done; }
    if (!WinHttpReadData(hr, chunk, 65536, &got)) {
      set_neterr("Read error (%lu).", GetLastError());
      goto done;
    }
    if (!got) break;
    rx += got;
    if (rx >= nextlog) { vlog("  received %llu bytes so far", rx); nextlog += 262144; }
    if (fn && !fn(u, chunk, got)) {
      if (!s_neterr[0]) set_neterr("Write failed or cancelled.");
      goto done;
    }
  }
  vlog("  body complete: %llu bytes", rx);
  ok = 1;

done:
  free(chunk);
  if (hr) WinHttpCloseHandle(hr);
  if (hc) WinHttpCloseHandle(hc);
  if (hs && InterlockedExchangePointer((PVOID volatile*)&s_hsession, NULL) == hs)
    WinHttpCloseHandle(hs);
  free(url);
  return ok;
}

static void new_uuid(char* out, size_t cap)
{
  unsigned char r[16];
  if (BCryptGenRandom(NULL, r, sizeof(r), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    DWORD t = GetTickCount();
    memset(r, 0, sizeof(r));
    memcpy(r, &t, sizeof(t));
  }
  r[6] = (unsigned char)((r[6] & 0x0F) | 0x40);
  r[8] = (unsigned char)((r[8] & 0x3F) | 0x80);
  snprintf(out, cap, "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
}

static int soap_exchange(const char* url, const Buf* xml, Buf* out, DWORD* status)
{
  *status = 0;
  s_neterr[0] = 0;
  return http_request(url, L"POST", L"Content-Type: application/soap+xml; charset=utf-8",
    xml->p, (DWORD)xml->n, buf_sink, out, status);
}

static int build_device_ticket(Buf* t)
{
  const char* env = getenv("FE3_MSA_TOKEN");
  const char* tok = (env && env[0]) ? env : s_msa_device_b64;
  if (!strncmp(tok, "<Device>", 8)) return buf_str(t, tok);
  return buf_fmt(t, "<Device>%s</Device>", tok);
}

static int soap_with_tickets(const char* url, const char* action, const char* created,
  const char* expires, const char* body, Buf* out)
{
  int attempt;
  for (attempt = 0; attempt < 2; ++attempt) {
    Buf ticket = { 0 }, env = { 0 };
    char mid[64];
    DWORD st = 0;
    int ok;

    if (attempt == 0) build_device_ticket(&ticket); else buf_str(&ticket, "<User />");
    new_uuid(mid, sizeof(mid));
    buf_fmt(&env,
      "<s:Envelope xmlns:a=\"" NS_ADDR "\" xmlns:s=\"" NS_SOAP "\">"
      "<s:Header>"
      "<a:Action s:mustUnderstand=\"1\">" SVC_NS "/%s</a:Action>"
      "<a:MessageID>%s</a:MessageID>"
      "<a:To s:mustUnderstand=\"1\">%s</a:To>"
      "<o:Security s:mustUnderstand=\"1\" xmlns:o=\"" NS_SEC "\">"
      "<Timestamp xmlns=\"" NS_UTIL "\"><Created>%s</Created><Expires>%s</Expires></Timestamp>"
      "<wuws:WindowsUpdateTicketsToken wsu:id=\"ClientMSA\" xmlns:wsu=\"" NS_UTIL "\" xmlns:wuws=\"" NS_WUAUTH "\">"
      "<TicketType Name=\"MSA\" Version=\"1.0\" Policy=\"MBI_SSL\">%s</TicketType>"
      "</wuws:WindowsUpdateTicketsToken>"
      "</o:Security></s:Header>"
      "<s:Body>%s</s:Body></s:Envelope>",
      action, mid, url, created, expires, ticket.p ? ticket.p : "", body);
    buf_free(&ticket);
    if (!env.p) return 0;

    out->n = 0;
    if (out->p) out->p[0] = 0;
    ok = soap_exchange(url, &env, out, &st);
    buf_free(&env);
    if (!ok) return 0;
    if (st == 200) { vlog("soap %s ok (attempt %d)", action, attempt + 1); return 1; }
    vlog("soap %s attempt %d failed: HTTP %lu", action, attempt + 1, st);
    if (out->p && strstr(out->p, "InvalidSecurity") && attempt == 0) continue;
    set_neterr("Microsoft delivery service returned HTTP %lu.", st);
    return 0;
  }
  return 0;
}

static char* get_cookie(void)
{
  Buf env = { 0 }, out = { 0 };
  char mid[64], now[40];
  SYSTEMTIME st;
  DWORD hs = 0;
  const char* a, * b;
  char* cookie = NULL;

  GetSystemTime(&st);
  snprintf(now, sizeof(now), "%04u-%02u-%02uT%02u:%02u:%02u.000Z",
    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  new_uuid(mid, sizeof(mid));
  buf_fmt(&env,
    "<Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" NS_SOAP "\">"
    "<Header>"
    "<Action d3p1:mustUnderstand=\"1\" xmlns:d3p1=\"" NS_SOAP "\" xmlns=\"" NS_ADDR "\">" SVC_NS "/GetCookie</Action>"
    "<MessageID xmlns=\"" NS_ADDR "\">%s</MessageID>"
    "<To d3p1:mustUnderstand=\"1\" xmlns:d3p1=\"" NS_SOAP "\" xmlns=\"" NS_ADDR "\">" FE3_URL "</To>"
    "<Security d3p1:mustUnderstand=\"1\" xmlns:d3p1=\"" NS_SOAP "\" xmlns=\"" NS_SEC "\">"
    "<Timestamp xmlns=\"" NS_UTIL "\"><Created>2017-12-02T00:16:15.210Z</Created>"
    "<Expires>2017-12-29T06:25:43.943Z</Expires></Timestamp>"
    "<WindowsUpdateTicketsToken d4p1:id=\"ClientMSA\" xmlns:d4p1=\"" NS_UTIL "\" xmlns=\"" NS_WUAUTH "\">"
    "<TicketType Name=\"MSA\" Version=\"1.0\" Policy=\"MBI_SSL\"><User /></TicketType>"
    "</WindowsUpdateTicketsToken></Security></Header>"
    "<Body><GetCookie xmlns=\"" SVC_NS "\"><oldCookie></oldCookie>"
    "<lastChange>2015-10-21T17:01:07.1472913Z</lastChange><currentTime>%s</currentTime>"
    "<protocolVersion>1.40</protocolVersion></GetCookie></Body></Envelope>", mid, now);
  if (!env.p) return NULL;

  if (!soap_exchange(FE3_URL, &env, &out, &hs)) { buf_free(&env); buf_free(&out); return NULL; }
  buf_free(&env);
  if (hs != 200) {
    set_neterr("Microsoft delivery service returned HTTP %lu (GetCookie).", hs);
    buf_free(&out);
    return NULL;
  }
  a = out.p ? strstr(out.p, "<EncryptedData>") : NULL;
  if (a) {
    a += 15;
    b = strstr(a, "</EncryptedData>");
    if (b && b > a) {
      cookie = (char*)malloc((size_t)(b - a) + 1);
      if (cookie) { memcpy(cookie, a, (size_t)(b - a)); cookie[b - a] = 0; }
    }
  }
  if (!cookie) set_neterr("Delivery service returned no cookie.");
  buf_free(&out);
  return cookie;
}

static int sync_updates(const char* cookie, const char* category, Buf* out)
{
  Buf body = { 0 };
  size_t i;
  int ok;

  buf_fmt(&body,
    "<SyncUpdates xmlns=\"" SVC_NS "\">"
    "<cookie><Expiration>2045-03-11T02:02:48Z</Expiration><EncryptedData>%s</EncryptedData></cookie>"
    "<parameters><ExpressQuery>false</ExpressQuery><InstalledNonLeafUpdateIDs>", cookie);
  for (i = 0; i < sizeof(s_installed_ids) / sizeof(s_installed_ids[0]); ++i)
    buf_fmt(&body, "<int>%d</int>", s_installed_ids[i]);
  buf_fmt(&body,
    "</InstalledNonLeafUpdateIDs><SkipSoftwareSync>false</SkipSoftwareSync>"
    "<NeedTwoGroupOutOfScopeUpdates>true</NeedTwoGroupOutOfScopeUpdates>"
    "<FilterAppCategoryIds><CategoryIdentifier><Id>%s</Id></CategoryIdentifier></FilterAppCategoryIds>"
    "<TreatAppCategoryIdsAsInstalled>true</TreatAppCategoryIdsAsInstalled>"
    "<AlsoPerformRegularSync>false</AlsoPerformRegularSync><ComputerSpec />"
    "<ExtendedUpdateInfoParameters><XmlUpdateFragmentTypes><XmlUpdateFragmentType>Extended</XmlUpdateFragmentType>"
    "</XmlUpdateFragmentTypes><Locales><string>en-US</string><string>en</string></Locales>"
    "</ExtendedUpdateInfoParameters>"
    "<ClientPreferredLanguages><string>en-US</string></ClientPreferredLanguages>"
    "<ProductsParameters><SyncCurrentVersionOnly>false</SyncCurrentVersionOnly>"
    "<DeviceAttributes>" DEVICE_ATTRIBUTES "</DeviceAttributes>"
    "<CallerAttributes>Interactive=1;IsSeeker=0;</CallerAttributes><Products /></ProductsParameters>"
    "</parameters></SyncUpdates>", category);
  if (!body.p) return 0;
  ok = soap_with_tickets(FE3_URL, "SyncUpdates", "2017-08-05T02:03:05.038Z",
    "2017-08-05T02:08:05.038Z", body.p, out);
  buf_free(&body);
  return ok;
}

static char* xml_unescape(const char* s, size_t n)
{
  char* o = (char*)malloc(n + 1);
  size_t i = 0, k = 0;
  if (!o) return NULL;
  while (i < n) {
    if (s[i] == '&') {
      if (n - i >= 4 && !strncmp(s + i, "&lt;", 4)) { o[k++] = '<'; i += 4; continue; }
      if (n - i >= 4 && !strncmp(s + i, "&gt;", 4)) { o[k++] = '>'; i += 4; continue; }
      if (n - i >= 5 && !strncmp(s + i, "&amp;", 5)) { o[k++] = '&'; i += 5; continue; }
      if (n - i >= 6 && !strncmp(s + i, "&quot;", 6)) { o[k++] = '"'; i += 6; continue; }
      if (n - i >= 6 && !strncmp(s + i, "&apos;", 6)) { o[k++] = '\''; i += 6; continue; }
    }
    o[k++] = s[i++];
  }
  o[k] = 0;
  return o;
}

static const char* tag_find(const char* s, const char* name, const char** gt)
{
  size_t nl = strlen(name);
  const char* p = s;
  while ((p = strstr(p, "<")) != NULL) {
    if (!strncmp(p + 1, name, nl) &&
      (p[1 + nl] == ' ' || p[1 + nl] == '>' || p[1 + nl] == '/' || p[1 + nl] == '\t' || p[1 + nl] == '\n')) {
      const char* g = strchr(p, '>');
      if (!g) return NULL;
      *gt = g;
      return p;
    }
    ++p;
  }
  return NULL;
}

static int attr(const char* tag, const char* gt, const char* name, char* out, size_t cap)
{
  size_t nl = strlen(name);
  const char* p = tag;
  while (p < gt && (p = strstr(p, name)) != NULL && p < gt) {
    if (p > tag && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') && p[nl] == '=') {
      char q = p[nl + 1];
      if (q == '"' || q == '\'') {
        const char* v = p + nl + 2;
        const char* e = (const char*)memchr(v, q, (size_t)(gt - v));
        size_t len;
        if (!e) return 0;
        len = (size_t)(e - v);
        if (len >= cap) len = cap - 1;
        memcpy(out, v, len);
        out[len] = 0;
        return 1;
      }
    }
    p += nl;
  }
  out[0] = 0;
  return 0;
}

static Item* item_get(Item* items, int* n, const char* id)
{
  int i;
  for (i = 0; i < *n; ++i) if (!strcmp(items[i].id, id)) return &items[i];
  if (*n >= MAX_ITEMS) return NULL;
  snprintf(items[*n].id, sizeof(items[*n].id), "%s", id);
  return &items[(*n)++];
}

static void parse_fragment(Item* it, const char* x)
{
  const char* tag, * gt, * p;
  char tmp[32];

  if (!it->upd[0] && (tag = tag_find(x, "UpdateIdentity", &gt)) != NULL) {
    attr(tag, gt, "UpdateID", it->upd, sizeof(it->upd));
    if (!attr(tag, gt, "RevisionNumber", it->rev, sizeof(it->rev))) snprintf(it->rev, sizeof(it->rev), "1");
  }
  if (strstr(x, "<SecuredFragment")) it->secured = 1;
  if (!it->moniker[0] && (tag = tag_find(x, "AppxMetadata", &gt)) != NULL)
    attr(tag, gt, "PackageMoniker", it->moniker, sizeof(it->moniker));

  p = x;
  while ((tag = tag_find(p, "File", &gt)) != NULL) {
    if (it->nfiles < MAX_FILES) {
      FileRec* f = &it->files[it->nfiles];
      memset(f, 0, sizeof(*f));
      attr(tag, gt, "FileName", f->name, sizeof(f->name));
      attr(tag, gt, "Digest", f->digest, sizeof(f->digest));
      if (!attr(tag, gt, "DigestAlgorithm", f->algo, sizeof(f->algo))) snprintf(f->algo, sizeof(f->algo), "SHA1");
      if (attr(tag, gt, "Size", tmp, sizeof(tmp))) f->size = _strtoui64(tmp, NULL, 10);
      attr(tag, gt, "InstallerSpecificIdentifier", f->ident, sizeof(f->ident));
      { size_t i; for (i = 0; f->algo[i]; ++i) f->algo[i] = (char)toupper((unsigned char)f->algo[i]); }
      if (f->name[0]) it->nfiles++;
    }
    p = gt;
  }
}

static void parse_block(Item* items, int* n, const char* blk, const char* end)
{
  const char* a = strstr(blk, "<ID>"), * b, * x0, * x1;
  char id[64];
  size_t len;
  Item* it;
  char* frag;

  if (!a || a >= end) return;
  a += 4;
  b = strstr(a, "</ID>");
  if (!b || b >= end) return;
  len = (size_t)(b - a);
  if (len >= sizeof(id)) return;
  memcpy(id, a, len); id[len] = 0;

  x0 = strstr(blk, "<Xml>");
  if (!x0 || x0 >= end) return;
  x0 += 5;
  x1 = strstr(x0, "</Xml>");
  if (!x1 || x1 > end) return;

  it = item_get(items, n, id);
  if (!it) return;
  frag = xml_unescape(x0, (size_t)(x1 - x0));
  if (!frag) return;
  parse_fragment(it, frag);
  free(frag);
}

static void parse_sync(const char* xml, Item* items, int* n)
{
  static const char* open_t[2] = { "<UpdateInfo>", "<Update>" };
  static const char* close_t[2] = { "</UpdateInfo>", "</Update>" };
  int k;
  for (k = 0; k < 2; ++k) {
    const char* p = xml;
    while ((p = strstr(p, open_t[k])) != NULL) {
      const char* e = strstr(p, close_t[k]);
      if (!e) break;
      parse_block(items, n, p, e);
      p = e + strlen(close_t[k]);
    }
  }
}

static int is_pkg_ext(const char* name, char* ext, size_t cap)
{
  const char* dot = strrchr(name, '.');
  size_t i;
  if (!dot) return 0;
  snprintf(ext, cap, "%s", dot);
  for (i = 0; ext[i]; ++i) ext[i] = (char)tolower((unsigned char)ext[i]);
  return !strcmp(ext, ".appx") || !strcmp(ext, ".msix") ||
    !strcmp(ext, ".appxbundle") || !strcmp(ext, ".msixbundle");
}

static void split_moniker(const char* moniker, Pkg* p)
{
  char tmp[260], * part[5] = { 0 }, * s;
  int n = 0;
  snprintf(tmp, sizeof(tmp), "%s", moniker);
  s = tmp;
  while (n < 5) {
    part[n++] = s;
    s = strchr(s, '_');
    if (!s) break;
    *s++ = 0;
  }
  snprintf(p->name, sizeof(p->name), "%s", part[0] ? part[0] : "");
  snprintf(p->version, sizeof(p->version), "%s", n >= 2 ? part[1] : "?");
  snprintf(p->arch, sizeof(p->arch), "neutral");
  if (n >= 3) {
    static const char* known[] = { "x64", "x86", "arm", "arm64", "neutral" };
    size_t i;
    for (i = 0; i < 5; ++i)
      if (!_stricmp(part[2], known[i])) { snprintf(p->arch, sizeof(p->arch), "%s", known[i]); break; }
  }
}

static int add_pkg(Pkg* out, int cnt, int cap, const Item* it, const FileRec* f, const char* mainname)
{
  Pkg p;
  char ext[16];
  int i;
  if (cnt >= cap || !is_pkg_ext(f->name, ext, sizeof(ext))) return cnt;
  memset(&p, 0, sizeof(p));
  if (it->moniker[0]) snprintf(p.moniker, sizeof(p.moniker), "%s", it->moniker);
  else if (f->ident[0]) snprintf(p.moniker, sizeof(p.moniker), "%s", f->ident);
  else {
    const char* dot = strrchr(f->name, '.');
    size_t l = dot ? (size_t)(dot - f->name) : strlen(f->name);
    if (l >= sizeof(p.moniker)) l = sizeof(p.moniker) - 1;
    memcpy(p.moniker, f->name, l); p.moniker[l] = 0;
  }
  split_moniker(p.moniker, &p);
  snprintf(p.ext, sizeof(p.ext), "%s", ext);
  p.size = f->size;
  snprintf(p.digest, sizeof(p.digest), "%s", f->digest);
  snprintf(p.algo, sizeof(p.algo), "%s", f->algo);
  snprintf(p.upd, sizeof(p.upd), "%s", it->upd);
  snprintf(p.rev, sizeof(p.rev), "%s", it->rev);
  p.main = mainname[0] && !_stricmp(p.name, mainname);
  for (i = 0; i < cnt; ++i)
    if (!strcmp(out[i].moniker, p.moniker) && !strcmp(out[i].ext, p.ext)) return cnt;
  out[cnt] = p;
  return cnt + 1;
}

static int resolve_packages(Item* items, int n, const char* mainname, Pkg* out, int cap)
{
  int i, k, m, cnt = 0;
  for (i = 0; i < n; ++i) {
    Item* it = &items[i];
    const FileRec* hit = NULL;
    if (!it->secured || !it->upd[0]) continue;
    if (it->moniker[0]) {
      for (k = 0; k < n && !hit; ++k)
        for (m = 0; m < items[k].nfiles; ++m)
          if (!strcmp(items[k].files[m].ident, it->moniker)) { hit = &items[k].files[m]; break; }
    }
    if (hit) cnt = add_pkg(out, cnt, cap, it, hit, mainname);
    else for (m = 0; m < it->nfiles; ++m) cnt = add_pkg(out, cnt, cap, it, &it->files[m], mainname);
  }
  return cnt;
}

static void ver_parse(const char* s, unsigned v[4])
{
  v[0] = v[1] = v[2] = v[3] = 0;
  sscanf(s, "%u.%u.%u.%u", &v[0], &v[1], &v[2], &v[3]);
}

static int cmp_pkg(const void* a, const void* b)
{
  const Pkg* x = (const Pkg*)a;
  const Pkg* y = (const Pkg*)b;
  unsigned vx[4], vy[4];
  int i;
  ver_parse(x->version, vx);
  ver_parse(y->version, vy);
  for (i = 0; i < 4; ++i) if (vx[i] != vy[i]) return vx[i] < vy[i] ? 1 : -1;   /* newest first */
  return _stricmp(x->arch, y->arch);
}

static int json_str(const char* j, const char* key, char* out, size_t cap)
{
  char pat[64];
  const char* p = j;
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  while ((p = strstr(p, pat)) != NULL) {
    const char* q = p + strlen(pat);
    size_t n = 0;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    if (*q != ':') { p = q; continue; }
    ++q;
    while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') ++q;
    if (*q != '"') { p = q; continue; }
    ++q;
    while (*q && *q != '"' && n + 1 < cap) {
      if (*q == '\\' && q[1]) ++q;
      out[n++] = *q++;
    }
    out[n] = 0;
    if (n) return 1;
    p = q;
  }
  out[0] = 0;
  return 0;
}

static void get_market(char* out)
{
  GEOID g = GetUserGeoID(GEOCLASS_NATION);
  WCHAR w[8] = { 0 };
  out[0] = 'U'; out[1] = 'S'; out[2] = 0;
  if (g != GEOID_NOT_AVAILABLE && GetGeoInfoW(g, GEO_ISO2, w, 8, 0) >= 2 && w[0] < 128 && w[1] < 128) {
    out[0] = (char)w[0]; out[1] = (char)w[1]; out[2] = 0;
  }
}


static void redraw(Vers* o)
{
  PlRect r = mkr(0, 0, o->W, o->H);
  pl_invalidate(o->w, &r);
}

static void txt_l(PlCanvas* c, PlFont* f, PlColor col, PlRect r, const char* t)
{
  pl_text(c, f, r, t, col, PL_TEXT_ELLIPSIS);
}

static void txt_c(PlCanvas* c, PlFont* f, PlColor col, PlRect r, const char* t)
{
  pl_text(c, f, r, t, col, PL_TEXT_CENTER);
}

static void txt_r(PlCanvas* c, PlFont* f, PlColor col, PlRect r, const char* t)
{
  int w = pl_text_width(f, t);
  if (w < r.r - r.l) r.l = r.r - w;
  pl_text(c, f, r, t, col, PL_TEXT_LEFT);
}

static void fmt_size(unsigned long long n, char* out, size_t cap)
{
  if (!n) snprintf(out, cap, "-");
  else if (n >= 1024ULL * 1024) snprintf(out, cap, "%.1f MB", (double)n / (1024.0 * 1024.0));
  else snprintf(out, cap, "%.0f KB", (double)n / 1024.0);
}

static PlColor mix_color(PlColor a, PlColor b, int pct)
{
  uint32_t x = (uint32_t)a, y = (uint32_t)b, r = 0;
  int i;
  for (i = 0; i < 32; i += 8) {
    uint32_t ca = (x >> i) & 255, cb = (y >> i) & 255;
    r |= ((ca * (uint32_t)pct + cb * (uint32_t)(100 - pct)) / 100) << i;
  }
  return (PlColor)r;
}

static const char* type_label(const char* ext) {
  if (!strcmp(ext, ".msix")) return "MSIX";
  if (!strcmp(ext, ".msixbundle")) return "MSIX bundle";
  if (!strcmp(ext, ".appx")) return "APPX";
  if (!strcmp(ext, ".appxbundle")) return "APPX bundle";
  return ext;
}

static DWORD WINAPI list_job(LPVOID param)
{
  Vers* o = (Vers*)param;
  Buf cat = { 0 }, sync = { 0 };
  char market[8], url[512], category[96] = { 0 }, mainname[128] = { 0 };
  char* cookie = NULL;
  Item* items = NULL;
  Pkg* pk = NULL;
  int nitems = 0, n, i, kept = 0;
  DWORD st = 0;

  o->out_err[0] = 0;
  o->out_n = 0;
  o->out_pkgs = NULL;
  vlog("---- list job start (product %s, arch %s)", VERS_PRODUCT_ID, VERS_ARCH);
  s_step = "Step 1/3: reading the Store catalog...";

  get_market(market);
  snprintf(url, sizeof(url),
    "https://displaycatalog.mp.microsoft.com/v7.0/products/%s?market=%s&languages=en-US,neutral",
    VERS_PRODUCT_ID, market);
  s_neterr[0] = 0;
  if (!http_request(url, L"GET", L"MS-CV: DGU1mcuYo0WMMp+F.1", NULL, 0, buf_sink, &cat, &st)) {
    snprintf(o->out_err, sizeof(o->out_err), "%s", s_neterr);
    goto done;
  }
  if (st != 200 || !cat.p) {
    snprintf(o->out_err, sizeof(o->out_err),
      "Store catalog returned HTTP %lu. Check the product ID and your region.", st);
    goto done;
  }
  vlog("catalog: HTTP %lu, %lu bytes, market %s", st, (unsigned long)cat.n, market);
  json_str(cat.p, "ProductTitle", o->out_title, sizeof(o->out_title));
  json_str(cat.p, "PublisherName", o->out_pub, sizeof(o->out_pub));
  json_str(cat.p, "PackageFamilyName", o->out_pfn, sizeof(o->out_pfn));
  if (!json_str(cat.p, "WuCategoryId", category, sizeof(category))) {
    snprintf(o->out_err, sizeof(o->out_err), "This product has no downloadable package in your region.");
    goto done;
  }
  {
    char* us = strchr(o->out_pfn, '_'); size_t l = us ? (size_t)(us - o->out_pfn) : strlen(o->out_pfn);
    if (l >= sizeof(mainname)) l = sizeof(mainname) - 1;
    memcpy(mainname, o->out_pfn, l); mainname[l] = 0;
  }
  if (s_cancel) { snprintf(o->out_err, sizeof(o->out_err), "Cancelled."); goto done; }

  s_step = "Step 2/3: getting a delivery cookie...";
  cookie = get_cookie();
  vlog("cookie: %s", cookie ? "ok" : "FAILED");
  if (!cookie) { snprintf(o->out_err, sizeof(o->out_err), "%s", s_neterr); goto done; }
  s_step = "Step 3/3: requesting the version list...";
  vlog("category %s, main package name '%s'", category, mainname);
  if (!sync_updates(cookie, category, &sync)) {
    snprintf(o->out_err, sizeof(o->out_err), "%s", s_neterr[0] ? s_neterr : "SyncUpdates failed.");
    goto done;
  }

  items = (Item*)calloc(MAX_ITEMS, sizeof(Item));
  pk = (Pkg*)calloc(MAX_PKGS, sizeof(Pkg));
  if (!items || !pk) { snprintf(o->out_err, sizeof(o->out_err), "Out of memory."); goto done; }
  s_step = "Reading the version list...";
  vlog("parsing %lu bytes", (unsigned long)sync.n);
  parse_sync(sync.p ? sync.p : "", items, &nitems);
  vlog("parsed %d updates", nitems);
  s_step = "Resolving packages...";
  n = resolve_packages(items, nitems, mainname, pk, MAX_PKGS);
  vlog("sync: %lu bytes, %d updates parsed, %d packages resolved", (unsigned long)sync.n, nitems, n);
  for (i = 0; i < n; ++i)
    vlog("  pkg %s  ext=%s main=%d size=%llu", pk[i].moniker, pk[i].ext, pk[i].main, pk[i].size);
  if (n == 0 && sync.p) vlog("sync response head: %.800s", sync.p);

  for (i = 0; i < n; ++i)
    if (pk[i].main && (!_stricmp(pk[i].arch, VERS_ARCH) || !_stricmp(pk[i].arch, "neutral")))
      pk[kept++] = pk[i];
  if (!kept) for (i = 0; i < n; ++i) if (pk[i].main) pk[kept++] = pk[i];
  if (!kept) kept = n;
  qsort(pk, (size_t)kept, sizeof(Pkg), cmp_pkg);

  o->out_pkgs = pk;
  o->out_n = kept;
  pk = NULL;
  if (!kept) snprintf(o->out_err, sizeof(o->out_err), "Microsoft returned no downloadable packages.");

done:
  vlog("list job end: err='%s' packages=%d", o->out_err, o->out_n);
  free(pk);
  free(items);
  free(cookie);
  buf_free(&cat);
  buf_free(&sync);
  return 0;
}

static int dl_sink(void* u, const BYTE* d, DWORD n)
{
  DlSink* s = (DlSink*)u;
  DWORD wr = 0;
  if (!WriteFile(s->file, d, n, &wr, NULL) || wr != n) { set_neterr("Disk write failed."); return 0; }
  BCryptHashData(s->hash, (PUCHAR)d, n, 0);
  s->done += n;
  InterlockedExchange(&s_done_kb, (LONG)(s->done >> 10));
  return 1;
}

static int host_is_microsoft(const char* url)
{
  const char* h = strstr(url, "://");
  const char* e;
  size_t l;
  char host[256];
  if (!h) return 0;
  h += 3;
  e = h;
  while (*e && *e != '/' && *e != ':' && *e != '?') ++e;
  l = (size_t)(e - h);
  if (l == 0 || l >= sizeof(host)) return 0;
  memcpy(host, h, l); host[l] = 0;
  if (!_stricmp(host, "microsoft.com")) return 1;
  return l > 14 && !_stricmp(host + l - 14, ".microsoft.com");
}

/* Time limited URL: asked for right before the download. */
static int fetch_file_url(const Pkg* pk, char* url, size_t cap)
{
  Buf body = { 0 }, resp = { 0 };
  const char* p;
  int found = 0;

  buf_fmt(&body,
    "<GetExtendedUpdateInfo2 xmlns=\"" SVC_NS "\"><updateIDs><UpdateIdentity>"
    "<UpdateID>%s</UpdateID><RevisionNumber>%s</RevisionNumber></UpdateIdentity></updateIDs>"
    "<infoTypes><XmlUpdateFragmentType>FileUrl</XmlUpdateFragmentType>"
    "<XmlUpdateFragmentType>FileDecryption</XmlUpdateFragmentType></infoTypes>"
    "<deviceAttributes>" DEVICE_ATTRIBUTES "</deviceAttributes></GetExtendedUpdateInfo2>",
    pk->upd, pk->rev);
  if (!body.p) return 0;
  s_neterr[0] = 0;
  if (!soap_with_tickets(FE3_SEC, "GetExtendedUpdateInfo2", "2017-08-01T00:29:01.868Z",
    "2017-08-01T00:34:01.868Z", body.p, &resp)) {
    buf_free(&body); buf_free(&resp);
    return 0;
  }
  buf_free(&body);

  p = resp.p;
  while (p && (p = strstr(p, "<FileLocation>")) != NULL) {
    const char* e = strstr(p, "</FileLocation>");
    const char* u0, * u1, * d0, * d1;
    if (!e) break;
    u0 = strstr(p, "<Url>");
    d0 = strstr(p, "<FileDigest>");
    if (u0 && d0 && u0 < e && d0 < e) {
      u0 += 5; d0 += 12;
      u1 = strstr(u0, "</Url>");
      d1 = strstr(d0, "</FileDigest>");
      if (u1 && d1 && (size_t)(d1 - d0) == strlen(pk->digest) && !strncmp(d0, pk->digest, (size_t)(d1 - d0))) {
        char* un = xml_unescape(u0, (size_t)(u1 - u0));
        if (un) { snprintf(url, cap, "%s", un); free(un); found = 1; break; }
      }
    }
    p = e + 15;
  }
  buf_free(&resp);
  if (!found) set_neterr("Microsoft did not return a download link for this version.");
  return found;
}

static int build_dest_path(const Pkg* pk, WCHAR* out, size_t cap)
{
  PWSTR dl = NULL;
  WCHAR dir[MAX_PATH * 2];
  char name[300];
  WCHAR wname[300];
  size_t i;

  if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &dl)) && dl) {
    lstrcpynW(dir, dl, MAX_PATH);
    CoTaskMemFree(dl);
  }
  else {
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, dir))) return 0;
    lstrcatW(dir, L"\\Downloads");
  }
  lstrcatW(dir, L"\\CDiE-versions");
  CreateDirectoryW(dir, NULL);

  snprintf(name, sizeof(name), "%s%s", pk->moniker, pk->ext);
  for (i = 0; name[i]; ++i)
    if (strchr("\\/:*?\"<>|", name[i])) name[i] = '_';
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, 300);
  _snwprintf(out, cap, L"%s\\%s", dir, wname);
  out[cap - 1] = 0;
  return 1;
}

static DWORD WINAPI dl_job(LPVOID param)
{
  Vers* o = (Vers*)param;
  const Pkg* pk = &o->job_pkg;
  char url[4096];
  DlSink sink;
  BCRYPT_ALG_HANDLE alg = NULL;
  UCHAR digest[64];
  DWORD dlen = 0, st = 0, got = 0;
  char b64[128];
  DWORD b64len = sizeof(b64);
  int sha256 = !strcmp(pk->algo, "SHA256");
  int ok;

  memset(&sink, 0, sizeof(sink));
  sink.file = INVALID_HANDLE_VALUE;
  o->out_err[0] = 0;
  o->out_path[0] = 0;
  InterlockedExchange(&s_done_kb, 0);
  InterlockedExchange(&s_total_kb, (LONG)(pk->size >> 10));

  if (!fetch_file_url(pk, url, sizeof(url))) { snprintf(o->out_err, sizeof(o->out_err), "%s", s_neterr); return 0; }
  if (!host_is_microsoft(url)) {
    snprintf(o->out_err, sizeof(o->out_err), "Refused: the download link is not a microsoft.com host.");
    return 0;
  }
  if (!build_dest_path(pk, o->out_path, sizeof(o->out_path) / sizeof(WCHAR))) {
    snprintf(o->out_err, sizeof(o->out_err), "Cannot find the Downloads folder.");
    return 0;
  }

  if (BCryptOpenAlgorithmProvider(&alg, sha256 ? BCRYPT_SHA256_ALGORITHM : BCRYPT_SHA1_ALGORITHM, NULL, 0) != 0 ||
    BCryptCreateHash(alg, &sink.hash, NULL, 0, NULL, 0, 0) != 0) {
    snprintf(o->out_err, sizeof(o->out_err), "Hash provider unavailable.");
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return 0;
  }
  sink.file = CreateFileW(o->out_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (sink.file == INVALID_HANDLE_VALUE) {
    snprintf(o->out_err, sizeof(o->out_err), "Cannot create the file (error %lu).", GetLastError());
    goto cleanup;
  }

  s_neterr[0] = 0;
  ok = http_request(url, L"GET", NULL, NULL, 0, dl_sink, &sink, &st);
  CloseHandle(sink.file);
  sink.file = INVALID_HANDLE_VALUE;
  if (!ok || st != 200) {
    snprintf(o->out_err, sizeof(o->out_err), "%s", ok ? "Download failed (HTTP error)." : s_neterr);
    DeleteFileW(o->out_path);
    o->out_path[0] = 0;
    goto cleanup;
  }

  if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, (PUCHAR)&dlen, sizeof(dlen), &got, 0) == 0 &&
    dlen <= sizeof(digest) &&
    BCryptFinishHash(sink.hash, digest, dlen, 0) == 0 &&
    CryptBinaryToStringA(digest, dlen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64, &b64len)) {
    if (pk->digest[0] && strcmp(b64, pk->digest) != 0) {
      snprintf(o->out_err, sizeof(o->out_err), "Hash mismatch: the file is corrupt and was deleted.");
      DeleteFileW(o->out_path);
      o->out_path[0] = 0;
    }
  }
  else {
    snprintf(o->out_err, sizeof(o->out_err), "Could not verify the file hash.");
  }

cleanup:
  if (sink.file != INVALID_HANDLE_VALUE) { CloseHandle(sink.file); DeleteFileW(o->out_path); o->out_path[0] = 0; }
  if (sink.hash) BCryptDestroyHash(sink.hash);
  if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  return 0;
}

static void pump(void)
{
  MSG m;
  while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
    if (m.message == WM_QUIT) {
      PostQuitMessage((int)m.wParam);
      InterlockedExchange(&s_cancel, 1);
      return;
    }
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
}

static void run_job(Vers* o, LPTHREAD_START_ROUTINE fn)
{
  HANDLE h;
  DWORD t0 = GetTickCount();
  InterlockedExchange(&s_cancel, 0);
  o->busy = 1;
  redraw(o);
  h = CreateThread(NULL, 0, fn, o, 0, NULL);
  if (!h) {
    fn(o);
  }
  else {
    DWORD last_paint = 0;
    for (;;) {
      DWORD r = MsgWaitForMultipleObjects(1, &h, FALSE, 60, QS_ALLINPUT);
      if (r == WAIT_OBJECT_0) break;
      pump();
      if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) break;
      if (GetTickCount() - last_paint >= 50) {  
        last_paint = GetTickCount();
        redraw(o);
      }
      if (!s_timedout && o->job == JOB_LIST && GetTickCount() - t0 > 60000) {
        s_timedout = 1;
        InterlockedExchange(&s_cancel, 1);
        abort_net();
      }
    }
    CloseHandle(h);
  }
  o->busy = 0;
}

static void ensure_visible(Vers* o)
{
  if (o->sel < o->scroll) o->scroll = o->sel;
  if (o->sel >= o->scroll + o->vis) o->scroll = o->sel - o->vis + 1;
  if (o->scroll > o->npkgs - o->vis) o->scroll = o->npkgs - o->vis;
  if (o->scroll < 0) o->scroll = 0;
}

static VOID CALLBACK repaint_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
  (void)hwnd; (void)msg; (void)time;
  KillTimer(NULL, id);
  if (s_v) redraw(s_v);
}

static void do_refresh(Vers* o)
{
  if (o->busy) return;
  o->job = JOB_LIST;
  o->state = ST_LOADING;
  snprintf(o->status, sizeof(o->status), "%s", translate("Contacting Microsoft Store..."));
  InterlockedExchange(&s_done_kb, 0);
  InterlockedExchange(&s_total_kb, 0);
  s_step = NULL;
  s_timedout = 0;
  run_job(o, list_job);
  if (s_timedout)
    snprintf(o->out_err, sizeof(o->out_err),
      "Timed out (%s). Details: cdie-versions.log in your Temp folder.",
      s_step ? s_step : "network");
  s_step = NULL;

  free(o->pkgs);
  o->pkgs = NULL;
  o->npkgs = 0;
  o->sel = -1;
  o->scroll = 0;
  if (o->out_err[0]) {
    free(o->out_pkgs);
    o->out_pkgs = NULL;
    o->state = ST_ERROR;
    snprintf(o->status, sizeof(o->status), "%s", o->out_err);
  }
  else {
    o->pkgs = o->out_pkgs;
    o->npkgs = o->out_n;
    o->out_pkgs = NULL;
    o->state = ST_READY;
    if (o->npkgs) o->sel = 0;
    snprintf(o->title, sizeof(o->title), "%s", o->out_title);
    snprintf(o->publisher, sizeof(o->publisher), "%s", o->out_pub);
    snprintf(o->pfn, sizeof(o->pfn), "%s", o->out_pfn);
    snprintf(o->status, sizeof(o->status), translate("%d version(s) available."), o->npkgs);
  }
  vlog("ui: state=%d npkgs=%d sel=%d busy=%d", o->state, o->npkgs, o->sel, o->busy);
  redraw(o);
  SetTimer(NULL, 0, 50, repaint_timer);  
  if (o->closing) pl_window_close(o->win);
}

static void do_download(Vers* o, int install)
{
  char path8[MAX_PATH * 3];
  if (o->busy || o->state != ST_READY || o->sel < 0 || o->sel >= o->npkgs) return;

  o->job_pkg = o->pkgs[o->sel];
  o->job_install = install;
  o->job = JOB_DOWNLOAD;
  snprintf(o->status, sizeof(o->status), translate("Downloading %s ..."), o->job_pkg.version);
  run_job(o, dl_job);

  if (o->out_err[0]) {
    snprintf(o->status, sizeof(o->status), "%s", o->out_err);
  }
  else {
    WideCharToMultiByte(CP_UTF8, 0, o->out_path, -1, path8, sizeof(path8), NULL, NULL);
    if (install) {
      WCHAR q[160];
      MultiByteToWideChar(CP_UTF8, 0, translate("CDiE will close so that this version can be installed. Continue?"), -1, q, 160);
      if (MessageBoxW(GetActiveWindow(), q, L"CDiE", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        ShellExecuteW(NULL, L"open", o->out_path, NULL, NULL, SW_SHOWNORMAL);
        s_launched = 1;
        pl_window_close(o->win);
        return;
      }
      snprintf(o->status, sizeof(o->status), translate("Verified and saved: %s"), path8);
    }
    else {
      WCHAR arg[MAX_PATH * 2 + 16];
      _snwprintf(arg, sizeof(arg) / sizeof(WCHAR), L"/select,\"%s\"", o->out_path);
      arg[sizeof(arg) / sizeof(WCHAR) - 1] = 0;
      ShellExecuteW(NULL, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);
      snprintf(o->status, sizeof(o->status), translate("Saved and verified: %s"), path8);
    }
  }
  redraw(o);
  if (o->closing) pl_window_close(o->win);
}

static void request_close(Vers* o)
{
  if (o->busy) {
    InterlockedExchange(&s_cancel, 1);
    abort_net();
    o->closing = 1;
    snprintf(o->status, sizeof(o->status), "%s", translate("Cancelling..."));
    redraw(o);
    return;
  }
  pl_window_close(o->win);
}

static void layout(Vers* o)
{
  int by = o->H - BAR_H + (BAR_H - BTN_H) / 2;
  int x = o->W - MARGIN;
  int i;
  static const int wd[B_COUNT] = { 90, 100, 150, 80 };
  static const int ids[B_COUNT] = { B_REFRESH, B_DOWNLOAD, B_INSTALL, B_CLOSE };

  o->r_head = mkr(MARGIN, MARGIN, o->W - MARGIN, MARGIN + HEAD_H);
  o->r_list = mkr(MARGIN, o->r_head.b + 10, o->W - MARGIN, o->H - BAR_H - MARGIN);
  o->r_bar = mkr(0, o->H - BAR_H, o->W, o->H);

  for (i = B_COUNT - 1; i >= 0; --i) {
    o->btn[i].id = ids[i];
    o->btn[i].accent = (i == B_INSTALL);
    o->btn[i].r = mkr(x - wd[i], by, x, by + BTN_H);
    x -= wd[i] + 8;
  }
  o->vis = imax(1, (o->r_list.b - o->r_list.t - TBL_HEAD - 8) / ROW_H);
}

static int btn_enabled(Vers* o, int id)
{
  if (id == B_CLOSE) return 1;
  if (o->busy) return 0;
  if (id == B_REFRESH) return 1;
  return o->state == ST_READY && o->sel >= 0 && o->sel < o->npkgs;
}

static const char* btn_text(Vers* o, int id)
{
  switch (id) {
  case B_REFRESH:  return translate("Refresh");
  case B_DOWNLOAD: return translate("Download");
  case B_INSTALL:  return translate("Install selected");
  default:         return o->busy ? translate("Cancel") : translate("Close");
  }
}

static int row_at(Vers* o, int x, int y)
{
  int idx;
  if (!inr(&o->r_list, x, y) || y < o->r_list.t + TBL_HEAD + 4) return -1;
  idx = o->scroll + (y - o->r_list.t - TBL_HEAD - 4) / ROW_H;
  if (idx < 0 || idx >= o->npkgs || idx >= o->scroll + o->vis) return -1;
  return idx;
}

static int btn_at(Vers* o, int x, int y)
{
  int i;
  for (i = 0; i < B_COUNT; ++i) if (inr(&o->btn[i].r, x, y)) return i;
  return -1;
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  Vers* o = (Vers*)u;
  PlRect r, hd;
  char buf[200];
  int i, cx0, left;

  o->W = w; o->H = h;
  layout(o);
  pl_fill(c, 0, 0, w, h, CLR_BG);

  r = o->r_head;
  pl_round_rect(c, r, CLR_PANEL_HEADER, CLR_PANEL_BORDER, 12);
  txt_l(c, o->fontb, CLR_TEXT_PRIMARY, mkr(r.l + 20, r.t + 10, r.r - 20, r.t + 32), translate("Version history"));
  if (o->title[0])
    snprintf(buf, sizeof(buf), "%s  -  %s", o->title, o->publisher);
  else
    snprintf(buf, sizeof(buf), "%s", translate("Microsoft Store"));
  txt_l(c, o->font, CLR_TEXT_SECONDARY, mkr(r.l + 20, r.t + 32, r.r - 20, r.t + 52), buf);
  if (o->current[0])
    snprintf(buf, sizeof(buf), translate("You are running %s. Pick a version, then choose Install selected and click Reinstall in App Installer."), o->current);
  else
    snprintf(buf, sizeof(buf), "%s", translate("Pick a version, then choose Install selected and click Reinstall in App Installer."));
  txt_l(c, o->font, CLR_TEXT_CODE, mkr(r.l + 20, r.t + 54, r.r - 20, r.t + 74), buf);

  r = o->r_list;
  pl_round_rect(c, r, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
  pl_round_rect(c, mkr(r.l + 1, r.t + 1, r.r - 1, r.t + TBL_HEAD + 12), CLR_PANEL_HEADER, CLR_PANEL_HEADER, 11);
  pl_fill(c, r.l + 1, r.t + TBL_HEAD, r.r - 1, r.t + TBL_HEAD + 12, CLR_PANEL_BODY);
  pl_line(c, r.l + 1, r.t + TBL_HEAD, r.r - 1, r.t + TBL_HEAD, CLR_SEPARATOR);

  hd = mkr(r.l, r.t, r.r, r.t + TBL_HEAD);
  cx0 = r.l + 24;
  txt_l(c, o->fontb, CLR_TEXT_SECONDARY, mkr(cx0, hd.t, cx0 + 220, hd.b), translate("VERSION"));
  txt_l(c, o->fontb, CLR_TEXT_SECONDARY, mkr(cx0 + 240, hd.t, cx0 + 340, hd.b), translate("ARCHITECTURE"));
  txt_l(c, o->fontb, CLR_TEXT_SECONDARY, mkr(cx0 + 380, hd.t, cx0 + 500, hd.b), translate("TYPE"));
  txt_r(c, o->fontb, CLR_TEXT_SECONDARY, mkr(r.r - 160, hd.t, r.r - 30, hd.b), translate("SIZE"));

  if (o->state != ST_READY || o->npkgs == 0) {
    const char* msg = o->state == ST_LOADING ? (s_step ? s_step : translate("Contacting Microsoft Store..."))
      : o->state == ST_ERROR ? o->status
      : o->state == ST_INIT ? translate("Loading...")
      : translate("No versions found.");
    txt_c(c, o->font, o->state == ST_ERROR ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY,
      mkr(r.l + 20, r.t + TBL_HEAD, r.r - 20, r.b), msg);
  }
  else {
    for (i = 0; i < o->vis && o->scroll + i < o->npkgs; ++i) {
      int idx = o->scroll + i;
      const Pkg* p = &o->pkgs[idx];
      int y = r.t + TBL_HEAD + 4 + i * ROW_H;
      PlRect rr = mkr(r.l + 8, y, r.r - 16, y + ROW_H - 2);
      char sz[40];
      if (idx == o->sel) {
        PlColor tint = mix_color(CLR_ACCENT, CLR_PANEL_BODY, 16);   
        pl_round_rect(c, rr, tint, tint, 6);
        pl_round_rect(c, mkr(rr.l, rr.t + 9, rr.l + 3, rr.b - 9),   
          CLR_ACCENT, CLR_ACCENT, 1);
      }
      else if (idx == o->hov_row) {
        PlColor hov = mix_color(CLR_ACCENT, CLR_PANEL_BODY, 7);     
        pl_round_rect(c, rr, hov, hov, 6);
      }

      txt_l(c, o->font, CLR_TEXT_PRIMARY, mkr(cx0, y, cx0 + 200, y + ROW_H - 2), p->version);
      if (idx == 0) {
        int tw = pl_text_width(o->font, p->version);
        PlRect br = mkr(cx0 + tw + 12, y + 8, cx0 + tw + 12 + 56, y + ROW_H - 10);
        pl_round_rect(c, br, CLR_PANEL_HEADER, CLR_ACCENT, 8);
        txt_c(c, o->font, CLR_ACCENT, br, translate("Latest"));
      }
      txt_l(c, o->font, CLR_TEXT_SECONDARY, mkr(cx0 + 240, y, cx0 + 370, y + ROW_H - 2), p->arch);
      txt_l(c, o->font, CLR_TEXT_SECONDARY, mkr(cx0 + 380, y, cx0 + 520, y + ROW_H - 2), type_label(p->ext));
      fmt_size(p->size, sz, sizeof(sz));
      txt_r(c, o->font, CLR_TEXT_SECONDARY, mkr(r.r - 160, y, r.r - 30, y + ROW_H - 2), sz);
    }
    if (o->npkgs > o->vis) {
      int track = o->vis * ROW_H;
      int th = imax(18, track * o->vis / o->npkgs);
      int ty = r.t + TBL_HEAD + 4 + (track - th) * o->scroll / (o->npkgs - o->vis);
      pl_round_rect(c, mkr(r.r - 11, ty, r.r - 7, ty + th), CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 2);
    }
  }

  pl_fill(c, o->r_bar.l, o->r_bar.t, o->r_bar.r, o->r_bar.b, CLR_BG);
  pl_line(c, o->r_bar.l, o->r_bar.t, o->r_bar.r, o->r_bar.t, CLR_SEPARATOR);
  left = o->btn[0].r.l - 16;
  if (o->state != ST_ERROR || o->busy)
    txt_l(c, o->font, CLR_TEXT_SECONDARY, mkr(16, o->r_bar.t + 8, left, o->r_bar.t + 30),
      (o->busy && o->job == JOB_LIST && s_step) ? s_step : o->status);
  else
    txt_l(c, o->font, CLR_TEXT_SECONDARY, mkr(16, o->r_bar.t + 8, left, o->r_bar.t + 30), translate("Could not load the version list."));

  if (o->busy) {
    PlRect tr = mkr(16, o->r_bar.t + 38, left, o->r_bar.t + 44);
    long done = (long)s_done_kb, total = (long)s_total_kb;
    pl_round_rect(c, tr, CLR_PANEL_BORDER, CLR_PANEL_BORDER, 3);
    if (o->job == JOB_DOWNLOAD && total > 0) {
      int fw = (int)((double)(tr.r - tr.l) * (double)(done < total ? done : total) / (double)total);
      if (fw > 4) pl_round_rect(c, mkr(tr.l, tr.t, tr.l + fw, tr.b), CLR_ACCENT, CLR_ACCENT, 3);
    }
    else {
      int span = tr.r - tr.l, seg = span / 4;
      int pos = (int)((GetTickCount() / 8) % (DWORD)(span + seg)) - seg;
      int a = tr.l + (pos < 0 ? 0 : pos), b = tr.l + (pos + seg > span ? span : pos + seg);
      if (b - a > 4) pl_round_rect(c, mkr(a, tr.t, b, tr.b), CLR_ACCENT, CLR_ACCENT, 3);
    }
  }

  for (i = 0; i < B_COUNT; ++i) {
    Btn* b = &o->btn[i];
    int en = btn_enabled(o, b->id);
    int hov = (o->hov_btn == i) && en;
    PlColor fill = b->accent ? (hov ? CLR_ACCENT_HOVER : CLR_ACCENT) : (hov ? CLR_BTN_GHOST_HOVER : CLR_BTN_GHOST);
    pl_round_rect(c, b->r, fill, b->accent ? fill : CLR_BTN_GHOST_BD, 8);
    txt_c(c, o->font, en ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY, b->r, btn_text(o, b->id));
  }
}

static void on_mouse_move(void* u, int x, int y)
{
  Vers* o = (Vers*)u;
  int b = btn_at(o, x, y), r = row_at(o, x, y);
  if (b != o->hov_btn || r != o->hov_row) { o->hov_btn = b; o->hov_row = r; redraw(o); }
}

static void on_mouse_leave(void* u)
{
  Vers* o = (Vers*)u;
  if (o->hov_btn != -1 || o->hov_row != -1) { o->hov_btn = o->hov_row = -1; redraw(o); }
}

static void on_mouse_down(void* u, int x, int y)
{
  Vers* o = (Vers*)u;
  int b = btn_at(o, x, y), r;
  if (b >= 0) {
    if (!btn_enabled(o, o->btn[b].id)) return;
    switch (o->btn[b].id) {
    case B_REFRESH:  do_refresh(o); break;
    case B_DOWNLOAD: do_download(o, 0); break;
    case B_INSTALL:  do_download(o, 1); break;
    default:         request_close(o); break;
    }
    return;
  }
  if (o->busy) return;
  r = row_at(o, x, y);
  if (r >= 0) { o->sel = r; redraw(o); }
}

static int on_hand(void* u, int x, int y)
{
  Vers* o = (Vers*)u;
  int b = btn_at(o, x, y);
  if (b >= 0) return btn_enabled(o, o->btn[b].id);
  return !o->busy && row_at(o, x, y) >= 0;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  Vers* o = (Vers*)u;
  (void)x; (void)y; (void)mods;
  if (!delta || o->npkgs <= o->vis) return;
  o->scroll += delta > 0 ? -3 : 3;
  if (o->scroll > o->npkgs - o->vis) o->scroll = o->npkgs - o->vis;
  if (o->scroll < 0) o->scroll = 0;
  redraw(o);
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  Vers* o = (Vers*)u;
  (void)ch; (void)mods;
  if (key == PL_KEY_ESC) { request_close(o); return; }
  if (o->busy || o->state != ST_READY || !o->npkgs) return;
  switch (key) {
  case PL_KEY_UP:    if (o->sel > 0) --o->sel; break;
  case PL_KEY_DOWN:  if (o->sel < o->npkgs - 1) ++o->sel; break;
  case PL_KEY_PGUP:  o->sel = imax(0, o->sel - o->vis); break;
  case PL_KEY_PGDN:  o->sel = o->sel + o->vis < o->npkgs ? o->sel + o->vis : o->npkgs - 1; break;
  case PL_KEY_HOME:  o->sel = 0; break;
  case PL_KEY_END:   o->sel = o->npkgs - 1; break;
  case PL_KEY_ENTER: do_download(o, 0); return;
  default: return;
  }
  ensure_visible(o);
  redraw(o);
}

static void on_resize(void* u, int w, int h)
{
  Vers* o = (Vers*)u;
  o->W = w; o->H = h;
  layout(o);
  ensure_visible(o);
  redraw(o);
}

static void on_destroy(void* u)
{
  Vers* o = (Vers*)u;
  if (o->font) pl_font_destroy(o->font);
  if (o->fontb) pl_font_destroy(o->fontb);
  free(o->pkgs);
  s_v = NULL;
  free(o);
}

static const PlWidgetOps s_ops = {
  on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
  on_resize, on_hand, on_destroy,
  on_key, on_wheel
};

static void on_win_size(void* u, int w, int h)
{
  Vers* o = (Vers*)u;
  o->W = w; o->H = h;
  pl_widget_set_rect(o->w, 0, 0, w, h);
}

static void on_win_close(void* u)
{
  request_close((Vers*)u);
}

static VOID CALLBACK start_timer(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
  (void)hwnd; (void)msg; (void)time;
  KillTimer(NULL, id);
  if (s_v && s_v->state == ST_INIT) do_refresh(s_v);
}

int versions_show(void* owner_native, const char* current_version)
{
  Vers* o;
  PlWindow* win;

  if (s_v) return 0;
  s_launched = 0;
  o = (Vers*)calloc(1, sizeof(*o));
  if (!o) return 0;
  o->W = 820; o->H = 560;
  o->hov_btn = o->hov_row = -1;
  o->sel = -1;
  o->state = ST_INIT;
  snprintf(o->current, sizeof(o->current), "%s", current_version ? current_version : "");

  o->font = pl_font_create(9, 0);
  o->fontb = pl_font_create(10, 1);
  if (!o->font || !o->fontb) { on_destroy(o); return 0; }

  win = pl_window_create(owner_native, translate("Version history"), 820, 560, 700, 420,
    on_win_size, on_win_close, o);
  if (!win) { on_destroy(o); return 0; }
  o->win = win;
  o->w = pl_widget_create(pl_window_client(win), &s_ops, o, 0, 0, 820, 540);
  if (!o->w) { pl_window_destroy(win); on_destroy(o); return 0; }
  s_v = o;

  layout(o);
  SetTimer(NULL, 0, 80, start_timer);
  pl_window_run_modal(win);

  pl_widget_destroy(o->w);
  pl_window_destroy(win);
  return s_launched;  
}