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
#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "platform/platform.h"
#include "theme/theme.h"
#include "core/i18n.h"
#include "core/config.h"
#include "core/appupdate.h"
#include "global.h"

#define UPD_API_URL   "https://api.github.com/repos/horsicq/CDiE/releases?per_page=10"
#define UPD_API_URL_W L"https://api.github.com/repos/horsicq/CDiE/releases?per_page=10"

typedef volatile long upd_atom;

static unsigned long s_http_status, s_win_err;   
static upd_atom s_done_kb, s_total_kb, s_cancel;
static AppUpdProgressFn s_hook;
static void* s_hook_ud;

#ifdef _WIN32
static void atom_set(upd_atom* p, long v) { InterlockedExchange((volatile LONG*)p, (LONG)v); }
static unsigned long upd_tick(void) { return (unsigned long)GetTickCount(); }
static void upd_sleep_ms(unsigned ms) { Sleep(ms); }
#else
static void atom_set(upd_atom* p, long v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

static unsigned long upd_tick(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

static void upd_sleep_ms(unsigned ms) { usleep(ms * 1000u); }
#endif

static void set_err(char* err, size_t cap, const char* msg)
{
	if (err && cap) snprintf(err, cap, "%s", msg);
}

static int json_string_after(const char* json, const char* key, char* out, size_t cap)
{
	const char* p = strstr(json, key);
	size_t n = 0;
	if (!p) return 0;
	p += strlen(key);
	while (*p == ' ' || *p == ':' || *p == '\t') ++p;
	if (*p != '"') return 0;
	++p;
	while (*p&&* p != '"' && n + 1 < cap) out[n++] = *p++;
	out[n] = 0;
	return n > 0;
}

static void lower_copy(const char* s, char* d, size_t cap)
{
	size_t i = 0;
	for (; s[i] && i + 1 < cap; ++i) d[i] = (char)tolower((unsigned char)s[i]);
	d[i] = 0;
}

static int ends_with(const char* s, const char* suffix)
{
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static int is_setup_install(void)
{
#ifdef _WIN32
	WCHAR exe[MAX_PATH], flag[MAX_PATH + 32], pf[MAX_PATH];
	const WCHAR* vars[] = { L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432" };
	WCHAR* slash;
	int i;

	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 0;
	lstrcpyW(flag, exe);
	slash = wcsrchr(flag, L'\\');
	if (slash) {
		*slash = 0;
		lstrcatW(flag, L"\\uninstaller.exe");
		if (GetFileAttributesW(flag) != INVALID_FILE_ATTRIBUTES) return 1;
	}
	for (i = 0; i < 3; ++i) {
		DWORD n = GetEnvironmentVariableW(vars[i], pf, MAX_PATH);
		if (n && n < MAX_PATH && !_wcsnicmp(exe, pf, n) && exe[n] == L'\\') return 1;
	}
#endif
	return 0;
}

enum { OS_UNK, OS_WIN, OS_LINUX, OS_MAC };
enum { AR_UNK, AR_X64, AR_X86, AR_ARM64 };

#if defined(_WIN32)
#define OUR_OS OS_WIN
#elif defined(__APPLE__)
#define OUR_OS OS_MAC
#else
#define OUR_OS OS_LINUX
#endif

#if defined(_M_ARM64) || defined(__aarch64__) || defined(__arm64__)
#define OUR_ARCH AR_ARM64
#elif defined(_WIN64) || defined(__x86_64__) || defined(_M_X64)
#define OUR_ARCH AR_X64
#else
#define OUR_ARCH AR_X86
#endif

static int os_of(const char* low)
{
	if (strstr(low, "darwin") || strstr(low, "macos") || strstr(low, "osx") || strstr(low, "-mac")) return OS_MAC;
	if (strstr(low, "linux") || strstr(low, "appimage")) return OS_LINUX;
	if (strstr(low, "win")) return OS_WIN;
	return OS_UNK;
}

static int arch_of(const char* low)
{
	if (strstr(low, "arm64") || strstr(low, "aarch64")) return AR_ARM64;
	if (strstr(low, "x86_64") || strstr(low, "x64") || strstr(low, "amd64")) return AR_X64;
	if (strstr(low, "x86") || strstr(low, "i386") || strstr(low, "i686") || strstr(low, "win32")) return AR_X86;
	return AR_UNK;
}

static int kind_of(const char* low)
{
	if (ends_with(low, ".zip")) return 1;
#ifdef _WIN32
	if (ends_with(low, ".exe") && strstr(low, "setup")) return 3;
#endif
#ifndef _WIN32
	if (ends_with(low, ".tar.gz") || ends_with(low, ".tgz")) return 2;
#endif
	return 0;
}

static int pick_asset(const char* json, char* out, size_t cap, unsigned long* size_kb)
{
	const char* key = "\"browser_download_url\"";
	const char* p = json, * seg = json;
	char url[512], low[512], best[512] = "";
	int best_score = 0;
	unsigned long best_kb = 0;

	while ((p = strstr(p, key)) != NULL) {
		const char* here = p, * sz;
		unsigned long kb = 0;
		int score, os, ar;

		if (!json_string_after(p, key, url, sizeof(url))) { p += strlen(key); seg = p; continue; }
		sz = strstr(seg, "\"size\"");               
		if (sz && sz < here) {
			const char* q = sz + 6;
			while (*q == ' ' || *q == ':') ++q;
			kb = (unsigned long)(strtoull(q, NULL, 10) / 1024ULL);
		}
		p += strlen(key); seg = p;

		lower_copy(url, low, sizeof(low));
		{
			int k = kind_of(low);
			if (!k) continue;
			if (is_setup_install() ? k != 3 : k == 3) continue;
		}
		score = 1;
		if (strstr(low, "portable")) score += 4;
		os = os_of(low);
		if (os == OUR_OS) score += 3; else if (os != OS_UNK) score -= 10;
		ar = arch_of(low);
		if (ar == OUR_ARCH || strstr(low, "universal")) score += 2;
		else if (ar != AR_UNK) score -= 10;

		if (score > best_score) { best_score = score; best_kb = kb; snprintf(best, sizeof(best), "%s", url); }
	}
	if (best_score <= 0 || !best[0]) return 0;
	snprintf(out, cap, "%s", best);
	if (size_kb) *size_kb = best_kb;
	return 1;
}

static int parse_version(const char* s, int v[4])
{
	int i = 0, any = 0;
	v[0] = v[1] = v[2] = v[3] = 0;
	while (*s && !isdigit((unsigned char)*s)) ++s;
	while (*s && i < 4) {
		if (isdigit((unsigned char)*s)) { v[i] = v[i] * 10 + (*s - '0'); any = 1; }
		else if (*s == '.') ++i;
		else break;
		++s;
	}
	return any;
}

static int is_newer(const char* remote, const char* local)
{
	int a[4], b[4], i;
	if (!parse_version(remote, a) || !parse_version(local, b)) return 0;
	for (i = 0; i < 4; ++i) {
		if (a[i] > b[i]) return 1;
		if (a[i] < b[i]) return 0;
	}
	return 0;
}

#ifdef _WIN32

static int http_get(const WCHAR* url, HANDLE file, char** out, size_t* outlen)
{
	URL_COMPONENTS uc;
	WCHAR host[256], path[2048], extra[1024];
	HINTERNET ses = NULL, con = NULL, req = NULL;
	DWORD status = 0, sz = sizeof(status), got;
	char buf[16384];
	char* mem = NULL;
	size_t len = 0, cap = 0;
	unsigned long long tot = 0;
	int ok = 0;

	s_http_status = 0; s_win_err = 0;
	ZeroMemory(&uc, sizeof(uc));
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = host;  uc.dwHostNameLength = 256;
	uc.lpszUrlPath = path;   uc.dwUrlPathLength = 2048;
	uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
	if (!WinHttpCrackUrl(url, 0, 0, &uc)) return 0;
	if (uc.nScheme != INTERNET_SCHEME_HTTPS) return 0;    /* HTTPS only */
	if (uc.dwExtraInfoLength && lstrlenW(path) + lstrlenW(extra) < 2047)
		lstrcatW(path, extra);                          

	ses = WinHttpOpen(L"CDiE-Updater/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!ses) goto done;
	{  
		DWORD protos = 0x00000800;   /* WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 */
		WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
	}
	WinHttpSetTimeouts(ses, 10000, 10000, 30000, 60000);
	con = WinHttpConnect(ses, host, uc.nPort, 0);
	if (!con) goto done;
	req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!req) goto done;
	WinHttpAddRequestHeaders(req, L"Accept: application/vnd.github+json",
		(DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

	if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto done;
	if (!WinHttpReceiveResponse(req, NULL)) goto done;
	if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX)) goto done;
	s_http_status = status;
	if (status != 200) goto done;
	if (file) {
		DWORD cl = 0, csz = sizeof(cl);
		if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &cl, &csz, WINHTTP_NO_HEADER_INDEX))
			atom_set(&s_total_kb, (long)(cl / 1024));
	}

	for (;;) {
		got = 0;
		if (!WinHttpReadData(req, buf, sizeof(buf), &got)) goto done;
		if (!got) break;
		if (file) {
			DWORD w = 0;
			if (!WriteFile(file, buf, got, &w, NULL) || w != got) goto done;
			tot += got;
			atom_set(&s_done_kb, (long)(tot / 1024));
			if (s_hook) s_hook(s_hook_ud);
			if (s_cancel) goto done;
		}
		else {
			if (len + got + 1 > cap) {
				size_t nc = cap ? cap * 2 : 65536;
				char* n;
				while (nc < len + got + 1) nc *= 2;
				if (nc > (size_t)8 * 1024 * 1024) goto done;   /* JSON sanity cap */
				n = (char*)realloc(mem, nc);
				if (!n) goto done;
				mem = n; cap = nc;
			}
			memcpy(mem + len, buf, got);
			len += got;
		}
	}
	if (!file) {
		if (!mem) goto done;
		mem[len] = 0;
		*out = mem; *outlen = len; mem = NULL;
	}
	ok = 1;

done:
	if (!ok) s_win_err = GetLastError();
	if (mem) free(mem);
	if (req) WinHttpCloseHandle(req);
	if (con) WinHttpCloseHandle(con);
	if (ses) WinHttpCloseHandle(ses);
	return ok;
}

static int fetch_releases(char** json)
{
	size_t len = 0;
	return http_get(UPD_API_URL_W, NULL, json, &len);
}

static int marker_path(WCHAR* out, size_t cap)
{
	WCHAR exe[MAX_PATH], * slash;
	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 0;
	slash = wcsrchr(exe, L'\\');
	if (!slash) return 0;
	*slash = 0;
	return (size_t)wsprintfW(out, L"%s\\cdie_release.txt", exe) < cap;
}

static int read_marker(char* out, size_t cap)
{
	WCHAR p[MAX_PATH + 32];
	HANDLE f;
	DWORD n = 0;
	out[0] = 0;
	if (!marker_path(p, MAX_PATH + 32)) return 0;
	f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f == INVALID_HANDLE_VALUE) return 0;
	ReadFile(f, out, (DWORD)(cap - 1), &n, NULL);
	CloseHandle(f);
	out[n] = 0;
	while (n && (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
	return n > 0;
}

static void write_marker(const char* stamp)
{
	WCHAR p[MAX_PATH + 32];
	HANDLE f;
	DWORD n = 0;
	if (!stamp || !stamp[0] || !marker_path(p, MAX_PATH + 32)) return;
	f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	WriteFile(f, stamp, (DWORD)strlen(stamp), &n, NULL);
	CloseHandle(f);
}

static const char s_script[] =
"param($ProcId,$Zip,$Dest,$Exe,$Keep)\r\n"
"$ErrorActionPreference='Stop'\r\n"
"try { Wait-Process -Id $ProcId -Timeout 30 } catch {}\r\n"
"Start-Sleep -Milliseconds 500\r\n"
"$tmp = Join-Path $env:TEMP 'cdie_update_x'\r\n"
"Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue\r\n"
"Expand-Archive -LiteralPath $Zip -DestinationPath $tmp -Force\r\n"
"$root = $tmp\r\n"
"$top = @(Get-ChildItem -LiteralPath $tmp)\r\n"
"if ($top.Count -eq 1 -and $top[0].PSIsContainer) { $root = $top[0].FullName }\r\n"
"Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {\r\n"
"  $rel = $_.FullName.Substring($root.Length).TrimStart('\\')\r\n"
"  if ($Keep -and ($rel -ieq $Keep)) { return }\r\n"
"  $t = Join-Path $Dest $rel\r\n"
"  New-Item -ItemType Directory -Force -Path (Split-Path $t) | Out-Null\r\n"
"  for ($i=0; $i -lt 20; $i++) {\r\n"
"    try { Copy-Item -LiteralPath $_.FullName -Destination $t -Force; break }\r\n"
"    catch { Start-Sleep -Milliseconds 500 }\r\n"
"  }\r\n"
"}\r\n"
"Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue\r\n"
"Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue\r\n"
"Start-Process -FilePath $Exe\r\n"
"Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\r\n";

static int dir_writable(const WCHAR* dir)
{
	WCHAR probe[MAX_PATH + 32];
	HANDLE h;
	wsprintfW(probe, L"%s\\.cdie_write_test", dir);
	h = CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;
	CloseHandle(h);
	return 1;
}

static int apply_update(const AppUpdateInfo* info, char* err, size_t err_cap)
{
	WCHAR exe[MAX_PATH], dir[MAX_PATH], tmp[MAX_PATH];
	WCHAR zip[MAX_PATH + 32], script[MAX_PATH + 32], wurl[1024], keep[MAX_PATH] = L"";
	WCHAR cmd[4 * MAX_PATH + 256];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	HANDLE f;
	DWORD w = 0;
	WCHAR* slash;
	const char* cfg;

	if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) { set_err(err, err_cap, "Cannot find the exe path."); return 0; }
	lstrcpyW(dir, exe);
	slash = wcsrchr(dir, L'\\');
	if (slash) *slash = 0;
	GetTempPathW(MAX_PATH, tmp);

	if (is_setup_install()) {
		WCHAR setup[MAX_PATH + 32];
		char sig[2] = { 0, 0 };
		DWORD r = 0;

		wsprintfW(setup, L"%scdie_setup_update.exe", tmp);
		if (!MultiByteToWideChar(CP_UTF8, 0, info->url, -1, wurl, 1024)) {
			set_err(err, err_cap, "Bad download URL."); return 0;
		}
		f = CreateFileW(setup, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
		if (f == INVALID_HANDLE_VALUE) { set_err(err, err_cap, "Cannot create the temporary file."); return 0; }
		if (!http_get(wurl, f, NULL, NULL)) {
			CloseHandle(f); DeleteFileW(setup);
			set_err(err, err_cap, s_cancel ? "Cancelled." : "Download failed. Check your connection and try again.");
			return 0;
		}
		CloseHandle(f);
		f = CreateFileW(setup, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
		if (f != INVALID_HANDLE_VALUE) { ReadFile(f, sig, 2, &r, NULL); CloseHandle(f); }
		if (r != 2 || sig[0] != 'M' || sig[1] != 'Z') {
			DeleteFileW(setup);
			set_err(err, err_cap, "The downloaded file is not a valid installer.");
			return 0;
		}
		if ((INT_PTR)ShellExecuteW(NULL, L"open", setup, NULL, NULL, SW_SHOWNORMAL) <= 32) {
			set_err(err, err_cap, "Could not start the installer.");
			return 0;
		}
		return 1;
	}

	if (!dir_writable(dir)) {
		set_err(err, err_cap, "The program folder is not writable. Move the app to a folder you own.");
		return 0;
	}
	wsprintfW(zip, L"%scdie_update.zip", tmp);
	wsprintfW(script, L"%scdie_update.ps1", tmp);

	if (!MultiByteToWideChar(CP_UTF8, 0, info->url, -1, wurl, 1024)) {
		set_err(err, err_cap, "Bad download URL."); return 0;
	}

	f = CreateFileW(zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) { set_err(err, err_cap, "Cannot create the temporary file."); return 0; }
	if (!http_get(wurl, f, NULL, NULL)) {
		CloseHandle(f);
		DeleteFileW(zip);
		set_err(err, err_cap, s_cancel ? "Cancelled." : "Download failed. Check your connection and try again.");
		return 0;
	}
	CloseHandle(f);

	f = CreateFileW(zip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (f != INVALID_HANDLE_VALUE) {
		char sig[2] = { 0, 0 };
		DWORD r = 0;
		ReadFile(f, sig, 2, &r, NULL);
		CloseHandle(f);
		if (r != 2 || sig[0] != 'P' || sig[1] != 'K') {
			DeleteFileW(zip);
			set_err(err, err_cap, "The downloaded file is not a valid zip.");
			return 0;
		}
	}

	f = CreateFileW(script, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) { set_err(err, err_cap, "Cannot write the update script."); return 0; }
	if (!WriteFile(f, s_script, (DWORD)(sizeof(s_script) - 1), &w, NULL)) {
		CloseHandle(f);
		set_err(err, err_cap, "Cannot write the update script.");
		return 0;
	}
	CloseHandle(f);

	cfg = config_file_path();
	if (cfg && cfg[0]) {
		WCHAR wcfg[MAX_PATH];
		if (MultiByteToWideChar(CP_UTF8, 0, cfg, -1, wcfg, MAX_PATH)) {
			size_t dl = lstrlenW(dir);
			if (!_wcsnicmp(wcfg, dir, dl) && wcfg[dl] == L'\\')
				lstrcpynW(keep, wcfg + dl + 1, MAX_PATH);
		}
	}

	wsprintfW(cmd,
		L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden "
		L"-File \"%s\" -ProcId %lu -Zip \"%s\" -Dest \"%s\" -Exe \"%s\" -Keep \"%s\"",
		script, GetCurrentProcessId(), zip, dir, exe, keep);

	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	write_marker(info->stamp);
	if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &si, &pi)) {
		set_err(err, err_cap, "Could not start the update helper.");
		return 0;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 1;
}

#else

typedef struct {
	char exe[PATH_MAX];   /* running executable                              */
	char dir[PATH_MAX];   /* install folder (parent of the .app on macOS)    */
	char run[PATH_MAX];   /* what to relaunch: the exe, or the .app bundle   */
	int  app;             /* 1 = macOS .app bundle                           */
} Layout;

static int self_exe(char* out, size_t cap)
{
#ifdef __APPLE__
	char tmp[PATH_MAX], real[PATH_MAX];
	uint32_t n = (uint32_t)sizeof(tmp);
	if (_NSGetExecutablePath(tmp, &n) != 0) return 0;
	if (!realpath(tmp, real)) return 0;
	snprintf(out, cap, "%s", real);
	return 1;
#else
	ssize_t n = readlink("/proc/self/exe", out, cap - 1);
	if (n <= 0) return 0;
	out[n] = 0;
	return 1;
#endif
}

static void parent_dir(const char* path, char* out, size_t cap)
{
	const char* slash = strrchr(path, '/');
	size_t n = slash ? (size_t)(slash - path) : 0;
	if (n == 0) { snprintf(out, cap, "%s", slash ? "/" : "."); return; }
	if (n >= cap) n = cap - 1;
	memcpy(out, path, n);
	out[n] = 0;
}

static int get_layout(Layout* L)
{
	memset(L, 0, sizeof(*L));
	if (!self_exe(L->exe, sizeof(L->exe))) return 0;
	snprintf(L->run, sizeof(L->run), "%s", L->exe);
	parent_dir(L->exe, L->dir, sizeof(L->dir));
#ifdef __APPLE__
	{
		const char* b = strstr(L->exe, ".app/Contents/MacOS/");
		if (b) {
			size_t n = (size_t)(b - L->exe) + 4;          /* up to and including ".app" */
			if (n >= sizeof(L->run)) n = sizeof(L->run) - 1;
			memcpy(L->run, L->exe, n);
			L->run[n] = 0;
			L->app = 1;
			parent_dir(L->run, L->dir, sizeof(L->dir));
		}
	}
#endif
	return 1;
}

static int fetch_releases(char** out)
{
	FILE* fp;
	char* mem = NULL;
	char* nl;
	size_t len = 0, cap = 0, got;
	char buf[8192];
	int st;

	s_http_status = 0; s_win_err = 0;
	fp = popen("curl -sS -L --max-time 20 -H 'Accept: application/vnd.github+json' "
		"-w '\\n%{http_code}' '" UPD_API_URL "' 2>/dev/null", "r");
	if (!fp) { s_win_err = (unsigned long)errno; return 0; }
	while ((got = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (len + got + 1 > cap) {
			size_t nc = cap ? cap * 2 : 65536;
			char* n;
			while (nc < len + got + 1) nc *= 2;
			if (nc > (size_t)8 * 1024 * 1024) { free(mem); pclose(fp); return 0; }
			n = (char*)realloc(mem, nc);
			if (!n) { free(mem); pclose(fp); return 0; }
			mem = n; cap = nc;
		}
		memcpy(mem + len, buf, got);
		len += got;
	}
	st = pclose(fp);
	if (!mem) {
		if (WIFEXITED(st) && WEXITSTATUS(st) == 127) s_win_err = 127;
		return 0;
	}
	mem[len] = 0;
	nl = strrchr(mem, '\n');
	if (!nl) { free(mem); return 0; }
	s_http_status = strtoul(nl + 1, NULL, 10);
	*nl = 0;
	if (s_http_status != 200) { free(mem); return 0; }
	*out = mem;
	return 1;
}

static int download_file(const char* url, const char* path, unsigned long hint_kb)
{
	pid_t pid;
	int st = 0;
	struct stat sb;

	if (hint_kb && !s_total_kb) atom_set(&s_total_kb, (long)hint_kb);
	pid = fork();
	if (pid < 0) return 0;
	if (pid == 0) {
		int fd = open("/dev/null", O_WRONLY);
		if (fd >= 0) dup2(fd, 2);
		execlp("curl", "curl", "-fsSL", "--connect-timeout", "15", "-o", path, url, (char*)NULL);
		_exit(127);
	}
	for (;;) {
		pid_t r = waitpid(pid, &st, WNOHANG);
		if (r == pid) break;
		if (r < 0 && errno != EINTR) return 0;
		if (stat(path, &sb) == 0) {
			atom_set(&s_done_kb, (long)(sb.st_size / 1024));
			if (s_hook) s_hook(s_hook_ud);
		}
		if (s_cancel) {
			kill(pid, SIGTERM);
			waitpid(pid, &st, 0);
			unlink(path);
			return 0;
		}
		usleep(100000);
	}
	if (WIFEXITED(st) && WEXITSTATUS(st) == 127) s_win_err = 127;
	return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

static int marker_path(char* out, size_t cap)
{
	Layout L;
	if (!get_layout(&L)) return 0;
	return snprintf(out, cap, "%s/cdie_release.txt", L.dir) < (int)cap;
}

static int read_marker(char* out, size_t cap)
{
	char p[PATH_MAX + 32];
	FILE* f;
	size_t n;
	out[0] = 0;
	if (!marker_path(p, sizeof(p))) return 0;
	f = fopen(p, "rb");
	if (!f) return 0;
	n = fread(out, 1, cap - 1, f);
	fclose(f);
	out[n] = 0;
	while (n && (out[n - 1] == '\r' || out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
	return n > 0;
}

static void write_marker(const char* stamp)
{
	char p[PATH_MAX + 32];
	FILE* f;
	if (!stamp || !stamp[0] || !marker_path(p, sizeof(p))) return;
	f = fopen(p, "wb");
	if (!f) return;
	fwrite(stamp, 1, strlen(stamp), f);
	fclose(f);
}

static const char s_script[] =
"#!/bin/sh\n"
"PID=\"$1\"; ARCH=\"$2\"; DEST=\"$3\"; RUN=\"$4\"; KEEP=\"$5\"; KIND=\"$6\"; MODE=\"$7\"\n"
"i=0\n"
"while kill -0 \"$PID\" 2>/dev/null && [ $i -lt 100 ]; do sleep 0.3; i=$((i+1)); done\n"
"sleep 0.5\n"
"TMP=\"$(mktemp -d \"${TMPDIR:-/tmp}/cdie_upd.XXXXXX\")\" || exit 1\n"
"case \"$KIND\" in\n"
"  zip) unzip -o -q \"$ARCH\" -d \"$TMP\" || { rm -rf \"$TMP\"; exit 1; } ;;\n"
"  *)   tar -xzf \"$ARCH\" -C \"$TMP\" || { rm -rf \"$TMP\"; exit 1; } ;;\n"
"esac\n"
"ROOT=\"$TMP\"\n"
"set -- \"$TMP\"/*\n"
"if [ $# -eq 1 ] && [ -d \"$1\" ] && [ \"$MODE\" != \"app\" ]; then ROOT=\"$1\"; fi\n"
"if [ -n \"$KEEP\" ] && [ -f \"$DEST/$KEEP\" ]; then cp -p \"$DEST/$KEEP\" \"$TMP.keep\"; fi\n"
"cp -R \"$ROOT\"/. \"$DEST\"/\n"
"if [ -f \"$TMP.keep\" ]; then cp -p \"$TMP.keep\" \"$DEST/$KEEP\"; rm -f \"$TMP.keep\"; fi\n"
"rm -rf \"$TMP\" \"$ARCH\"\n"
"if [ \"$MODE\" = \"app\" ]; then\n"
"  open -n \"$RUN\"\n"
"else\n"
"  chmod +x \"$RUN\" 2>/dev/null\n"
"  nohup \"$RUN\" >/dev/null 2>&1 &\n"
"fi\n"
"rm -f \"$0\"\n";

static int dir_writable(const char* dir) { return access(dir, W_OK) == 0; }

static int apply_update(const AppUpdateInfo* info, char* err, size_t err_cap)
{
	Layout L;
	char tmpdir[PATH_MAX], arch[PATH_MAX + 32], script[PATH_MAX + 32];
	char keep[PATH_MAX] = "", pidstr[32], low[512];
	const char* tmp, * cfg, * kind, * mode;
	struct stat sb;
	FILE* f;
	pid_t p;
	int k;
	unsigned char sig[2] = { 0, 0 };

	if (!get_layout(&L)) { set_err(err, err_cap, "Cannot find the program path."); return 0; }
	if (!dir_writable(L.dir)) {
		set_err(err, err_cap, "The program folder is not writable. Move the app to a folder you own.");
		return 0;
	}

	lower_copy(info->url, low, sizeof(low));
	k = kind_of(low);
	if (!k) { set_err(err, err_cap, "Unsupported update archive."); return 0; }
	kind = k == 1 ? "zip" : "targz";
	mode = L.app ? "app" : "exe";

	tmp = getenv("TMPDIR");
	if (!tmp || !tmp[0]) tmp = "/tmp";
	snprintf(tmpdir, sizeof(tmpdir), "%s/cdie_update_%ld", tmp, (long)getuid());
	if (mkdir(tmpdir, 0700) != 0 && errno != EEXIST) {
		set_err(err, err_cap, "Cannot create the temporary folder."); return 0;
	}
	if (lstat(tmpdir, &sb) != 0 || !S_ISDIR(sb.st_mode) || sb.st_uid != getuid()) {
		set_err(err, err_cap, "The temporary folder is not safe to use."); return 0;
	}
	chmod(tmpdir, 0700);
	snprintf(arch, sizeof(arch), "%s/update.%s", tmpdir, k == 1 ? "zip" : "tar.gz");
	snprintf(script, sizeof(script), "%s/update.sh", tmpdir);

	if (info->size_kb && !s_total_kb) atom_set(&s_total_kb, (long)info->size_kb);
	if (!download_file(info->url, arch, info->size_kb)) {
		unlink(arch);
		if (s_cancel) set_err(err, err_cap, "Cancelled.");
		else if (s_win_err == 127) set_err(err, err_cap, "curl was not found. Install curl to use the updater.");
		else set_err(err, err_cap, "Download failed. Check your connection and try again.");
		return 0;
	}

	f = fopen(arch, "rb");
	if (f) { if (fread(sig, 1, 2, f) != 2) sig[0] = sig[1] = 0; fclose(f); }
	if (k == 1 ? !(sig[0] == 'P' && sig[1] == 'K') : !(sig[0] == 0x1F && sig[1] == 0x8B)) {
		unlink(arch);
		set_err(err, err_cap, "The downloaded file is not a valid archive.");
		return 0;
	}

	f = fopen(script, "wb");
	if (!f) { set_err(err, err_cap, "Cannot write the update script."); return 0; }
	fwrite(s_script, 1, sizeof(s_script) - 1, f);
	fclose(f);
	chmod(script, 0700);

	cfg = config_file_path();
	if (cfg && cfg[0]) {
		size_t dl = strlen(L.dir);
		if (!strncmp(cfg, L.dir, dl) && cfg[dl] == '/')
			snprintf(keep, sizeof(keep), "%s", cfg + dl + 1);
	}
	snprintf(pidstr, sizeof(pidstr), "%ld", (long)getpid());

	write_marker(info->stamp);

	p = fork();
	if (p < 0) { set_err(err, err_cap, "Could not start the update helper."); return 0; }
	if (p == 0) {                         /* double fork: helper outlives us, no zombie */
		pid_t q = fork();
		if (q < 0) _exit(1);
		if (q > 0) _exit(0);
		setsid();
		{
			int fd = open("/dev/null", O_RDWR);
			if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); dup2(fd, 2); if (fd > 2) close(fd); }
		}
		execl("/bin/sh", "sh", script, pidstr, arch, L.dir, L.run, keep, kind, mode, (char*)NULL);
		_exit(127);
	}
	waitpid(p, NULL, 0);
	return 1;
}

#endif

int appupd_check(const char* current_version, AppUpdateInfo* info,
	char* err, size_t err_cap)
{
	char* json = NULL;

	if (!current_version || !current_version[0])
		current_version = X_APPLICATIONVERSION;

	memset(info, 0, sizeof(*info));
	if (!fetch_releases(&json)) {
		char m[200];
		if (s_http_status == 404)
			snprintf(m, sizeof(m), "GitHub returned 404: no published release found (or wrong repo name).");
		else if (s_http_status == 403 || s_http_status == 429)
			snprintf(m, sizeof(m), "GitHub returned %lu: rate limit reached, try again later.", s_http_status);
		else if (s_http_status)
			snprintf(m, sizeof(m), "GitHub returned HTTP %lu.", s_http_status);
#ifndef _WIN32
		else if (s_win_err == 127)
			snprintf(m, sizeof(m), "curl was not found. Install curl to check for updates.");
#endif
		else
			snprintf(m, sizeof(m), "Could not connect to GitHub (error %lu).", s_win_err);
		set_err(err, err_cap, m);
		return 0;
	}
	if (!json_string_after(json, "\"tag_name\"", info->tag, sizeof(info->tag))) {
		free(json);
		set_err(err, err_cap, "Unexpected reply from GitHub.");
		return 0;
	}
	{   
		char* first = strstr(json, "\"tag_name\"");
		char* second = first ? strstr(first + 10, "\"tag_name\"") : NULL;
		if (second) *second = 0;
	}
	json_string_after(json, "\"published_at\"", info->stamp, sizeof(info->stamp));
	{
		int a[4], b[4];
		if (parse_version(info->tag, a) && parse_version(current_version ? current_version : "", b)) {
			info->newer = is_newer(info->tag, current_version);
		}
		else {  
			char have[64];
			if (!info->stamp[0]) info->newer = 0;
			else if (!read_marker(have, sizeof(have))) info->newer = 1;  
			else info->newer = strcmp(have, info->stamp) != 0;
		}
	}
	if (info->newer && !pick_asset(json, info->url, sizeof(info->url), &info->size_kb)) {
		free(json);
		set_err(err, err_cap, "A newer version exists but has no download for this system.");
		return 0;
	}
	free(json);
	return 1;
}

int appupd_download_and_apply(const AppUpdateInfo* info, char* err, size_t err_cap)
{
	atom_set(&s_cancel, 0);
	atom_set(&s_done_kb, 0);
	atom_set(&s_total_kb, 0);
	if (!info || !info->url[0]) { set_err(err, err_cap, "No download URL."); return 0; }
	if (!config_is_portable() && !is_setup_install()) {
		set_err(err, err_cap, "This is not a portable install; update it the way it was installed.");
		return 0;
	}
	return apply_update(info, err, err_cap);
}

void appupd_set_progress_hook(AppUpdProgressFn fn, void* user)
{
	s_hook_ud = user;
	s_hook = fn;
}

void appupd_get_progress(unsigned long* done_kb, unsigned long* total_kb)
{
	if (done_kb)  *done_kb = (unsigned long)s_done_kb;
	if (total_kb) *total_kb = (unsigned long)s_total_kb;
}

void appupd_cancel(void)
{
	atom_set(&s_cancel, 1);
}

#define DLG_W 480
#define DLG_H 330
#define BTN_W 110
#define BTN_H 34

#ifdef _WIN32
#define UPD_DONE_BUTTON 0     
#else
#define UPD_DONE_BUTTON 1    
#endif

enum { S_OFFER, S_DL, S_DONE, S_ERR, S_CANCELLED };

typedef struct {
	PlWidget* w;
	PlWindow* win;
	PlFont* font, * fontb, * fontt;
	upd_atom state, cancel_req;
	int W, H, hov;
#ifdef _WIN32
	HWND hwnd;                    /* captured on first paint (UI thread) */
	HANDLE thread;
#else
	pthread_t thread;
	int thread_started;
#endif
	unsigned long last_poke;
	int result;
	AppUpdateInfo info;
	char cur[64];
	char err[200];
} Dlg;

static Dlg s_d;

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static int inr(const PlRect* r, int x, int y) { return x >= r->l && x < r->r && y >= r->t && y < r->b; }

static void redraw(Dlg* d)
{
	PlRect r = mkr(0, 0, d->W, d->H);
	pl_invalidate(d->w, &r);
}

static void ui_poke(Dlg* d)
{
#ifdef _WIN32
	if (d->hwnd) RedrawWindow(d->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
#else
	redraw(d);
#endif
}

static void progress_hook(void* u)
{
	Dlg* d = (Dlg*)u;
	unsigned long now = upd_tick();
	if (now - d->last_poke < 60) return;
	d->last_poke = now;
	ui_poke(d);
}

static void worker_run(Dlg* d)
{
	char err[200] = "";
	int ok = appupd_download_and_apply(&d->info, err, sizeof(err));

	if (ok) {
		d->result = 1;
		atom_set(&d->state, S_DONE);
	}
	else if (d->cancel_req) {
		atom_set(&d->state, S_CANCELLED);
	}
	else {
		snprintf(d->err, sizeof(d->err), "%s", err[0] ? err : "The update failed.");
		atom_set(&d->state, S_ERR);
	}
	ui_poke(d);
#ifdef _WIN32
	if (ok) upd_sleep_ms(1200);                      
	if ((ok || d->cancel_req) && d->hwnd) PostMessageW(d->hwnd, WM_CLOSE, 0, 0);
#endif
}

#ifdef _WIN32
static DWORD WINAPI worker_entry(LPVOID p) { worker_run((Dlg*)p); return 0; }
#else
static void* worker_entry(void* p) { worker_run((Dlg*)p); return NULL; }
#endif

static void start_download(Dlg* d)
{
	int ok;
	atom_set(&d->state, S_DL);
#ifdef _WIN32
	{
		DWORD id;
		d->thread = CreateThread(NULL, 0, worker_entry, d, 0, &id);
		ok = d->thread != NULL;
	}
#else
	ok = pthread_create(&d->thread, NULL, worker_entry, d) == 0;
	d->thread_started = ok;
#endif
	if (!ok) {
		snprintf(d->err, sizeof(d->err), "Could not start the download.");
		atom_set(&d->state, S_ERR);
	}
	redraw(d);
}

static void request_close(Dlg* d)
{
	if (d->state == S_DL && !d->cancel_req) {   
		d->cancel_req = 1;
		appupd_cancel();
		redraw(d);
		return;
	}
	pl_window_close(d->win);
}

static void buttons(const Dlg* d, PlRect* b1, PlRect* b2)
{
	int by = d->H - 20 - BTN_H;
	*b2 = mkr(d->W - 20 - BTN_W, by, d->W - 20, by + BTN_H);
	*b1 = mkr(b2->l - 10 - BTN_W, by, b2->l - 10, by + BTN_H);
}

static void kb_label(unsigned long kb, char* out, size_t cap)
{
	if (kb >= 1024) {
		unsigned long t = (unsigned long)(((unsigned long long)kb * 10) / 1024);
		snprintf(out, cap, "%lu.%lu MiB", t / 10, t % 10);
	}
	else snprintf(out, cap, "%lu KiB", kb);
}

static void text_line(PlCanvas* c, PlFont* f, PlColor col, int x, int y, int r, const char* t)
{
	pl_text(c, f, mkr(x, y, r, y + 22), t, col, PL_TEXT_ELLIPSIS);
}

static void draw_button(PlCanvas* c, Dlg* d, PlRect r, const char* label, int accent, int idx)
{
	int hov = d->hov == idx;
	PlColor fill = accent ? (hov ? CLR_ACCENT_HOVER : CLR_ACCENT)
		: (hov ? CLR_BTN_GHOST_HOVER : CLR_BTN_GHOST);
	pl_round_rect(c, r, fill, accent ? fill : CLR_BTN_GHOST_BD, 8);
	pl_text(c, d->font, r, label, CLR_TEXT_PRIMARY, PL_TEXT_CENTER);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	Dlg* d = (Dlg*)u;
	PlRect b1, b2, card;
	char buf[160], a[48], b[48];
	int x = 24, state = (int)d->state;
	unsigned long done = 0, total = 0;

	d->W = w; d->H = h;
#ifdef _WIN32
	if (!d->hwnd) d->hwnd = GetActiveWindow();
#endif

	pl_fill(c, 0, 0, w, h, CLR_BG);

	pl_round_rect(c, mkr(x, 22, x + 44, 66), CLR_ACCENT, CLR_ACCENT, 12);
	pl_line(c, x + 22, 33, x + 22, 52, CLR_TEXT_PRIMARY);
	pl_line(c, x + 14, 44, x + 22, 53, CLR_TEXT_PRIMARY);
	pl_line(c, x + 30, 44, x + 22, 53, CLR_TEXT_PRIMARY);
	pl_line(c, x + 13, 58, x + 31, 58, CLR_TEXT_PRIMARY);

	pl_text(c, d->fontt, mkr(x + 60, 20, w - x, 46),
		state == S_DONE ? translate("Update installed")
		: state == S_DL ? translate("Downloading update")
		: state == S_ERR ? translate("Update failed")
		: translate("Update available"),
		CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
	snprintf(buf, sizeof(buf), "cdie %s", d->cur[0] ? d->cur : "");
	pl_text(c, d->font, mkr(x + 60, 46, w - x, 68), buf, CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);

	card = mkr(x, 84, w - x, h - 20 - BTN_H - 16);
	pl_round_rect(c, card, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);

	if (state == S_OFFER || state == S_CANCELLED) {
		char date[16] = "";
		strncpy(date, d->info.stamp, 10); date[10] = 0;
		pl_text(c, d->fontb, mkr(card.l + 18, card.t + 14, card.r - 18, card.t + 36),
			translate("NEW VERSION"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
		pl_text(c, d->fontt, mkr(card.l + 18, card.t + 38, card.r - 18, card.t + 66),
			d->info.tag, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
		if (date[0]) {
			snprintf(buf, sizeof(buf), "%s %s", translate("Released"), date);
			text_line(c, d->font, CLR_TEXT_SECONDARY, card.l + 18, card.t + 68, card.r - 18, buf);
		}
		if (is_setup_install()) {
			text_line(c, d->font, CLR_TEXT_CODE, card.l + 18, card.t + 98, card.r - 18,
				translate("cdie will download the installer, close itself"));
			text_line(c, d->font, CLR_TEXT_CODE, card.l + 18, card.t + 118, card.r - 18,
				translate("and start the setup. Your settings are kept."));
		}
		else {
			text_line(c, d->font, CLR_TEXT_CODE, card.l + 18, card.t + 98, card.r - 18,
				translate("cdie will download it, replace the program files"));
			text_line(c, d->font, CLR_TEXT_CODE, card.l + 18, card.t + 118, card.r - 18,
				translate("and restart automatically. Your settings are kept."));
		}
	}
	else if (state == S_DL || state == S_DONE) {
		PlRect track = mkr(card.l + 18, card.t + 52, card.r - 18, card.t + 66);
		int pct = 0, fw;
		appupd_get_progress(&done, &total);
		if (state == S_DONE) pct = 100;
		else if (total) pct = (int)((unsigned long long)done * 100 / total);
		if (pct > 100) pct = 100;

		text_line(c, d->font, CLR_TEXT_PRIMARY, card.l + 18, card.t + 18, card.r - 18,
			state == S_DONE ? translate(is_setup_install() ? "Starting the installer..." : (UPD_DONE_BUTTON ? "Ready. Restart cdie to finish." : "Restarting cdie..."))
			: d->cancel_req ? translate("Cancelling...")
			: translate("Downloading the latest version..."));

		pl_round_rect(c, track, CLR_PANEL_HEADER, CLR_PANEL_BORDER, 7);
		fw = (track.r - track.l - 2) * pct / 100;
		if (fw > 6)
			pl_round_rect(c, mkr(track.l + 1, track.t + 1, track.l + 1 + fw, track.b - 1),
				CLR_ACCENT, CLR_ACCENT, 6);

		if (state == S_DONE) snprintf(buf, sizeof(buf), "100%%");
		else if (total) {
			kb_label(done, a, sizeof(a)); kb_label(total, b, sizeof(b));
			snprintf(buf, sizeof(buf), "%s / %s    %d%%", a, b, pct);
		}
		else {
			kb_label(done, a, sizeof(a));
			snprintf(buf, sizeof(buf), "%s", a);
		}
		text_line(c, d->font, CLR_TEXT_SECONDARY, card.l + 18, card.t + 76, card.r - 18, buf);
		text_line(c, d->font, CLR_TEXT_CODE, card.l + 18, card.t + 104, card.r - 18,
			translate("Please keep this window open."));
	}
	else { /* S_ERR: two-line message */
		char l1[100], l2[100];
		size_t n = strlen(d->err);
		size_t cut = n > 56 ? 56 : n;
		if (n > 56) { while (cut > 30 && d->err[cut] != ' ') --cut; }
		snprintf(l1, sizeof(l1), "%.*s", (int)cut, d->err);
		snprintf(l2, sizeof(l2), "%s", d->err + cut + (d->err[cut] == ' ' ? 1 : 0));
		text_line(c, d->fontb, CLR_TEXT_PRIMARY, card.l + 18, card.t + 18, card.r - 18,
			translate("Something went wrong"));
		text_line(c, d->font, CLR_TEXT_SECONDARY, card.l + 18, card.t + 52, card.r - 18, l1);
		text_line(c, d->font, CLR_TEXT_SECONDARY, card.l + 18, card.t + 74, card.r - 18, l2);
	}

	buttons(d, &b1, &b2);
	if (state == S_OFFER || state == S_CANCELLED) {
		draw_button(c, d, b1, translate("Update now"), 1, 1);
		draw_button(c, d, b2, translate("Later"), 0, 2);
	}
	else if (state == S_DL) {
		draw_button(c, d, b2, translate("Cancel"), 0, 2);
	}
	else if (state == S_DONE) {
		if (UPD_DONE_BUTTON) draw_button(c, d, b2, translate("Restart now"), 1, 2);
	}
	else if (state == S_ERR) {
		draw_button(c, d, b2, translate("Close"), 0, 2);
	}
}

static int hit(Dlg* d, int x, int y)
{
	PlRect b1, b2;
	int s = (int)d->state;
	buttons(d, &b1, &b2);
	if ((s == S_OFFER || s == S_CANCELLED) && inr(&b1, x, y)) return 1;
	if (s == S_DONE) return (UPD_DONE_BUTTON && inr(&b2, x, y)) ? 2 : 0;
	if (inr(&b2, x, y)) return 2;
	return 0;
}

static void on_mouse_move(void* u, int x, int y)
{
	Dlg* d = (Dlg*)u;
	int h = hit(d, x, y);
	if (h != d->hov) { d->hov = h; redraw(d); }
	else if (d->state == S_DL || d->state == S_DONE) redraw(d);
}

static void on_mouse_leave(void* u)
{
	Dlg* d = (Dlg*)u;
	if (d->hov) { d->hov = 0; redraw(d); }
}

static void on_mouse_down(void* u, int x, int y)
{
	Dlg* d = (Dlg*)u;
	int h = hit(d, x, y), s = (int)d->state;
	if (h == 1 && (s == S_OFFER || s == S_CANCELLED)) start_download(d);
	else if (h == 2) request_close(d);
}

static int on_hand(void* u, int x, int y) { return hit((Dlg*)u, x, y) != 0; }

static void on_key(void* u, int key, uint32_t ch, int mods)
{
	Dlg* d = (Dlg*)u;
	(void)ch; (void)mods;
	if (key == PL_KEY_ESC) request_close(d);
	else if (key == PL_KEY_ENTER && d->state == S_OFFER) start_download(d);
	else if (key == PL_KEY_ENTER && d->state == S_DONE && UPD_DONE_BUTTON) request_close(d);
}

static void on_resize(void* u, int w, int h)
{
	Dlg* d = (Dlg*)u;
	d->W = w; d->H = h;
	redraw(d);
}

static void on_destroy(void* u) { (void)u; }

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
	(void)u; (void)x; (void)y; (void)delta; (void)mods;
}

static const PlWidgetOps s_ops = {
	on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
	on_resize, on_hand, on_destroy,
	on_key, on_wheel
};

static void on_win_size(void* u, int w, int h)
{
	Dlg* d = (Dlg*)u;
	d->W = w; d->H = h;
	pl_widget_set_rect(d->w, 0, 0, w, h);
}

static void on_win_close(void* u)
{
	request_close((Dlg*)u);
}

int appupd_dialog_show(void* owner_hwnd, const AppUpdateInfo* info,
	const char* current_version)
{
	Dlg* d = &s_d;
	int result;

	memset(d, 0, sizeof(*d));
	d->info = *info;
	snprintf(d->cur, sizeof(d->cur), "%s",
		(current_version && current_version[0]) ? current_version : X_APPLICATIONVERSION);
	d->W = DLG_W; d->H = DLG_H;

	d->font = pl_font_create(9, 0);
	d->fontb = pl_font_create(10, 1);
	d->fontt = pl_font_create(13, 1);
	if (!d->font || !d->fontb || !d->fontt) goto fail;

	d->win = pl_window_create(owner_hwnd, translate("Update"), DLG_W, DLG_H, DLG_W, DLG_H,
		on_win_size, on_win_close, d);
	if (!d->win) goto fail;
	d->w = pl_widget_create(pl_window_client(d->win), &s_ops, d, 0, 0, DLG_W, DLG_H - 20);
	if (!d->w) { pl_window_destroy(d->win); goto fail; }

	appupd_set_progress_hook(progress_hook, d);
	pl_window_run_modal(d->win);
	appupd_set_progress_hook(NULL, NULL);

#ifdef _WIN32
	if (d->thread) {
		appupd_cancel();                          /* no-op if already finished */
		WaitForSingleObject(d->thread, 4000);
		CloseHandle(d->thread);
		d->thread = NULL;
	}
	d->hwnd = NULL;
#else
	if (d->thread_started) {
		appupd_cancel();                          /* no-op if already finished */
		pthread_join(d->thread, NULL);
		d->thread_started = 0;
	}
#endif
	result = d->result;
	pl_widget_destroy(d->w);
	pl_window_destroy(d->win);
	pl_font_destroy(d->font); pl_font_destroy(d->fontb); pl_font_destroy(d->fontt);
	return result;

fail:
	if (d->font) pl_font_destroy(d->font);
	if (d->fontb) pl_font_destroy(d->fontb);
	if (d->fontt) pl_font_destroy(d->fontt);
	return 0;
}