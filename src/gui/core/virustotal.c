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
#include "core/virustotal.h"
#include "core/i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")   /* MinGW: add -lwinhttp */
#else
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#ifdef VT_USE_CURL
#include <curl/curl.h>
#endif

/* ================= SHA-256 (portable) ================= */

typedef struct {
	uint32_t h[8];
	uint64_t total;
	unsigned char buf[64];
	size_t used;
} Sha256;

static const uint32_t K[64] = {
	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha_block(Sha256* s, const unsigned char* p)
{
	uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
	int i;
	for (i = 0; i < 16; ++i)
		w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
		((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
	for (i = 16; i < 64; ++i) {
		uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3];
	e = s->h[4]; f = s->h[5]; g = s->h[6]; h = s->h[7];
	for (i = 0; i < 64; ++i) {
		t1 = h + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d;
	s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void sha_init(Sha256* s)
{
	static const uint32_t init[8] = {
		0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
	};
	memcpy(s->h, init, sizeof(init));
	s->total = 0;
	s->used = 0;
}

static void sha_update(Sha256* s, const unsigned char* data, size_t len)
{
	s->total += len;
	while (len) {
		size_t n = 64 - s->used;
		if (n > len) n = len;
		memcpy(s->buf + s->used, data, n);
		s->used += n; data += n; len -= n;
		if (s->used == 64) { sha_block(s, s->buf); s->used = 0; }
	}
}

static void sha_final(Sha256* s, char out_hex[65])
{
	static const char hex[] = "0123456789abcdef";
	uint64_t bits = s->total * 8;
	unsigned char pad = 0x80, zero = 0, lenbuf[8];
	int i;
	sha_update(s, &pad, 1);
	while (s->used != 56) sha_update(s, &zero, 1);
	for (i = 0; i < 8; ++i) lenbuf[i] = (unsigned char)(bits >> (56 - 8 * i));
	sha_update(s, lenbuf, 8);
	for (i = 0; i < 8; ++i) {
		int j;
		for (j = 0; j < 4; ++j) {
			unsigned char byte = (unsigned char)(s->h[i] >> (24 - 8 * j));
			out_hex[i * 8 + j * 2] = hex[byte >> 4];
			out_hex[i * 8 + j * 2 + 1] = hex[byte & 15];
		}
	}
	out_hex[64] = 0;
}

static FILE* open_utf8(const char* path)
{
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
	wchar_t* w;
	FILE* f;
	if (n <= 0) return NULL;
	w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
	if (!w) return NULL;
	MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
	f = _wfopen(w, L"rb");
	free(w);
	return f;
#else
	return fopen(path, "rb");
#endif
}

int vt_sha256_file(const char* utf8_path, char out_hex[65])
{
	Sha256 s;
	unsigned char* buf;
	size_t got;
	FILE* f;
	if (!utf8_path || !utf8_path[0] || !out_hex) return 0;
	f = open_utf8(utf8_path);
	if (!f) return 0;
	buf = (unsigned char*)malloc(1 << 16);
	if (!buf) { fclose(f); return 0; }
	sha_init(&s);
	while ((got = fread(buf, 1, 1 << 16, f)) > 0) sha_update(&s, buf, got);
	free(buf);
	if (ferror(f)) { fclose(f); return 0; }
	fclose(f);
	sha_final(&s, out_hex);
	return 1;
}

/* ================= Browser fallback ================= */

static int is_hex64(const char* h)
{
	int i;
	if (!h) return 0;
	for (i = 0; i < 64; ++i) {
		char c = h[i];
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return 0;
	}
	return h[64] == 0;
}

int vt_open_report(const char* sha256_hex)
{
	char url[160];
	if (!is_hex64(sha256_hex)) return 0;
	snprintf(url, sizeof(url), "https://www.virustotal.com/gui/file/%s", sha256_hex);
#ifdef _WIN32
	{
		WCHAR wurl[160];
		MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 160);
		return (INT_PTR)ShellExecuteW(NULL, L"open", wurl, NULL, NULL, SW_SHOWNORMAL) > 32;
	}
#else
	{
		pid_t pid = fork();
		int st = 0;
		if (pid < 0) return 0;
		if (pid == 0) {
#ifdef __APPLE__
			execlp("open", "open", url, (char*)NULL);
#else
			execlp("xdg-open", "xdg-open", url, (char*)NULL);
#endif
			_exit(127);
		}
		if (waitpid(pid, &st, 0) < 0) return 0;
		return WIFEXITED(st) && WEXITSTATUS(st) == 0;
	}
#endif
}

/* ================= API key ================= */

static char s_api_key[128];

static int is_space_ch(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void vt_set_api_key(const char* key)
{
	size_t n;
	const char* s = key ? key : "";
	while (is_space_ch(*s)) ++s;                 /* trim pasted whitespace */
	snprintf(s_api_key, sizeof(s_api_key), "%s", s);
	n = strlen(s_api_key);
	while (n && is_space_ch(s_api_key[n - 1])) s_api_key[--n] = 0;
}

static const char* vt_key(void)
{
	if (s_api_key[0]) return s_api_key;
	return getenv("VT_API_KEY");   /* fallback */
}

/* ================= API lookup ================= */

static int json_int(const char* hay, const char* key)
{
	const char* p = strstr(hay, key);
	if (!p) return 0;
	p += strlen(key);
	while (*p == ' ' || *p == ':' || *p == '"') ++p;
	return atoi(p);
}

static void parse_stats(const char* body, VtResult* out)
{
	const char* s = strstr(body, "\"last_analysis_stats\"");
	if (!s) return;
	out->found = 1;
	out->malicious = json_int(s, "\"malicious\"");
	out->suspicious = json_int(s, "\"suspicious\"");
	out->harmless = json_int(s, "\"harmless\"");
	out->undetected = json_int(s, "\"undetected\"");
}

#if defined(_WIN32)
/* ---- Windows: built-in WinHTTP, no extra DLLs ---- */

int vt_api_available(void)
{
	const char* k = vt_key();
	return k && k[0];
}

int vt_lookup(const char* sha256_hex, VtResult* out)
{
	const char* key = vt_key();
	HINTERNET hs = NULL, hc = NULL, hr = NULL;
	WCHAR wkey[160], whash[80], wpath[160], whdr[300];
	char* body = NULL;
	size_t len = 0, cap = 0;
	DWORD code = 0, csz = sizeof(code);
	int ok = 0;

	if (!out) return 0;
	memset(out, 0, sizeof(*out));
	if (!key || !key[0] || !is_hex64(sha256_hex)) return 0;

	if (!MultiByteToWideChar(CP_UTF8, 0, key, -1, wkey, 160)) return 0;
	if (!MultiByteToWideChar(CP_UTF8, 0, sha256_hex, -1, whash, 80)) return 0;
	wsprintfW(wpath, L"/api/v3/files/%s", whash);
	wsprintfW(whdr, L"x-apikey: %s", wkey);

	hs = WinHttpOpen(L"cdie/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hs) goto done;
	WinHttpSetTimeouts(hs, 10000, 10000, 20000, 20000);
	hc = WinHttpConnect(hs, L"www.virustotal.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	if (!hc) goto done;
	hr = WinHttpOpenRequest(hc, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (!hr) goto done;
	if (!WinHttpSendRequest(hr, whdr, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) goto done;
	if (!WinHttpReceiveResponse(hr, NULL)) goto done;
	if (!WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &code, &csz, WINHTTP_NO_HEADER_INDEX)) goto done;
	out->http_code = (long)code;

	if (code == 200) {
		for (;;) {
			DWORD avail = 0, got = 0;
			if (!WinHttpQueryDataAvailable(hr, &avail) || avail == 0) break;
			if (len + avail + 1 > cap) {
				size_t nc = (len + avail + 1) * 2;
				char* p;
				if (nc > ((size_t)8 << 20)) goto done;     /* 8 MB sanity limit */
				p = (char*)realloc(body, nc);
				if (!p) goto done;
				body = p; cap = nc;
			}
			if (!WinHttpReadData(hr, body + len, avail, &got) || got == 0) break;
			len += got;
		}
		if (body) { body[len] = 0; parse_stats(body, out); }
		ok = 1;
	}
	else if (code == 404) {
		ok = 1;   /* completed, just not known */
	}

done:
	if (hr) WinHttpCloseHandle(hr);
	if (hc) WinHttpCloseHandle(hc);
	if (hs) WinHttpCloseHandle(hs);
	free(body);
	return ok;
}

#elif defined(VT_USE_CURL)
/* ---- Optional: link against libcurl (define VT_USE_CURL) ---- */

typedef struct { char* data; size_t len; } Mem;

static size_t write_cb(void* ptr, size_t size, size_t nmemb, void* ud)
{
	Mem* m = (Mem*)ud;
	size_t n = size * nmemb;
	char* p = (char*)realloc(m->data, m->len + n + 1);
	if (!p) return 0;
	m->data = p;
	memcpy(m->data + m->len, ptr, n);
	m->len += n;
	m->data[m->len] = 0;
	return n;
}

int vt_api_available(void)
{
	const char* k = vt_key();
	return k && k[0];
}

int vt_lookup(const char* sha256_hex, VtResult* out)
{
	const char* key = vt_key();
	char url[160], hdr[256];
	Mem mem = { NULL, 0 };
	struct curl_slist* headers = NULL;
	CURL* curl;
	long code = 0;
	int ok = 0;

	if (!out) return 0;
	memset(out, 0, sizeof(*out));
	if (!key || !key[0] || !is_hex64(sha256_hex)) return 0;

	curl = curl_easy_init();
	if (!curl) return 0;
	snprintf(url, sizeof(url), "https://www.virustotal.com/api/v3/files/%s", sha256_hex);
	snprintf(hdr, sizeof(hdr), "x-apikey: %s", key);
	headers = curl_slist_append(headers, hdr);

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);   /* required for use from a thread */

	if (curl_easy_perform(curl) == CURLE_OK) {
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
		out->http_code = code;
		if (code == 200 && mem.data) { parse_stats(mem.data, out); ok = 1; }
		else if (code == 404) ok = 1;
	}
	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	free(mem.data);
	return ok;
}

#else
/* ---- Linux / macOS default: use the system `curl` tool ---- */

static int key_is_safe(const char* k)
{
	for (; *k; ++k) {
		unsigned char c = (unsigned char)*k;
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') || c == '-' || c == '_'))
			return 0;
	}
	return 1;
}

int vt_api_available(void)
{
	const char* k = vt_key();
	return k && k[0];
}

int vt_lookup(const char* sha256_hex, VtResult* out)
{
	const char* key = vt_key();
	int inp[2], outp[2], st = 0, ok = 0;
	pid_t pid;
	char cfg[512], chunk[4096];
	char* body = NULL;
	size_t len = 0, cap = 0, off = 0, cfglen;
	ssize_t n;
	long code = 0;
	char* nl;

	if (!out) return 0;
	memset(out, 0, sizeof(*out));
	if (!key || !key[0] || !is_hex64(sha256_hex) || !key_is_safe(key)) return 0;

	/* curl reads its options from stdin, so the key never shows in `ps` */
	snprintf(cfg, sizeof(cfg),
		"url = \"https://www.virustotal.com/api/v3/files/%s\"\n"
		"header = \"x-apikey: %s\"\n", sha256_hex, key);
	cfglen = strlen(cfg);

	signal(SIGPIPE, SIG_IGN);   /* curl missing => don't die on write */
	if (pipe(inp) < 0) return 0;
	if (pipe(outp) < 0) { close(inp[0]); close(inp[1]); return 0; }

	pid = fork();
	if (pid < 0) {
		close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
		return 0;
	}
	if (pid == 0) {
		int dn;
		dup2(inp[0], 0);
		dup2(outp[1], 1);
		close(inp[0]); close(inp[1]); close(outp[0]); close(outp[1]);
		dn = open("/dev/null", O_WRONLY);
		if (dn >= 0) dup2(dn, 2);
		execlp("curl", "curl", "-s", "--max-time", "20", "-K", "-",
			"-w", "\n%{http_code}", (char*)NULL);
		_exit(127);
	}
	close(inp[0]);
	close(outp[1]);

	while (off < cfglen) {
		n = write(inp[1], cfg + off, cfglen - off);
		if (n <= 0) break;
		off += (size_t)n;
	}
	close(inp[1]);

	while ((n = read(outp[0], chunk, sizeof(chunk))) > 0) {
		if (len + (size_t)n + 1 > cap) {
			size_t nc = (len + (size_t)n + 1) * 2;
			char* p;
			if (nc > ((size_t)8 << 20)) break;       /* 8 MB sanity limit */
			p = (char*)realloc(body, nc);
			if (!p) break;
			body = p; cap = nc;
		}
		memcpy(body + len, chunk, (size_t)n);
		len += (size_t)n;
	}
	close(outp[0]);
	waitpid(pid, &st, 0);

	if (body) {
		body[len] = 0;
		nl = strrchr(body, '\n');
		if (nl) { code = atol(nl + 1); *nl = 0; }
		out->http_code = code;
		if (code == 200) { parse_stats(body, out); ok = 1; }
		else if (code == 404) ok = 1;
	}
	free(body);
	return ok;
}

#endif

/* ================= GUI page ================= */

enum { VTS_NOFILE, VTS_READY, VTS_BUSY, VTS_DONE, VTS_NOTFOUND, VTS_NOKEY, VTS_ERROR };

struct VtView {
	PlWidget* w;
	PlFont* fBig, * fTitle, * fText;
	int width, height;
	int state;
	char path[2048];
	char hash[65];
	char err[128];
	VtResult res;

	/* worker */
	int running;
	volatile int worker_done;
	int w_state;
	char w_hash[65];
	char w_err[128];
	VtResult w_res;
#ifdef _WIN32
	HANDLE th;
#else
	pthread_t th;
#endif

	/* buttons from last paint */
	PlRect btn_main, btn_browser, btn_copy;
	int has_main, has_browser, has_copy;
	int hover;                       /* 0 none, 1 main, 2 browser, 3 copy */

	int (*copy_cb)(void*, const char*);
	void* copy_user;
};

static void worker_run(VtView* v)
{
	v->w_hash[0] = 0; v->w_err[0] = 0;
	memset(&v->w_res, 0, sizeof(v->w_res));

	if (!vt_sha256_file(v->path, v->w_hash)) {
		v->w_hash[0] = 0;
		snprintf(v->w_err, sizeof(v->w_err), "Cannot read the file to hash it.");
		v->w_state = VTS_ERROR;
	}
	else if (!vt_api_available()) {
		v->w_state = VTS_NOKEY;
	}
	else if (!vt_lookup(v->w_hash, &v->w_res)) {
		long c = v->w_res.http_code;
		if (c == 401 || c == 403)
			snprintf(v->w_err, sizeof(v->w_err), "VirusTotal rejected the API key.");
		else if (c == 429)
			snprintf(v->w_err, sizeof(v->w_err), "Rate limit reached. Wait a minute and retry.");
		else if (c == 0)
			snprintf(v->w_err, sizeof(v->w_err), "Network error. Check your connection.");
		else
			snprintf(v->w_err, sizeof(v->w_err), "VirusTotal returned HTTP %ld.", c);
		v->w_state = VTS_ERROR;
	}
	else {
		v->w_state = v->w_res.found ? VTS_DONE : VTS_NOTFOUND;
	}
	v->worker_done = 1;
}

#ifdef _WIN32
static DWORD WINAPI thread_main(LPVOID p) { worker_run((VtView*)p); return 0; }
static int thread_start(VtView* v)
{
	v->th = CreateThread(NULL, 0, thread_main, v, 0, NULL);
	return v->th != NULL;
}
static void thread_join(VtView* v)
{
	WaitForSingleObject(v->th, INFINITE);
	CloseHandle(v->th);
	v->th = NULL;
}
#else
static void* thread_main(void* p) { worker_run((VtView*)p); return NULL; }
static int thread_start(VtView* v) { return pthread_create(&v->th, NULL, thread_main, v) == 0; }
static void thread_join(VtView* v) { pthread_join(v->th, NULL); }
#endif

static void join_worker(VtView* v)
{
	if (!v->running) return;
	thread_join(v);
	v->running = 0;
	v->worker_done = 0;
}

static void start_check(VtView* v)
{
	if (v->running || !v->path[0]) return;
	v->worker_done = 0;
	v->hash[0] = 0;
	v->err[0] = 0;
	v->state = VTS_BUSY;
	if (thread_start(v)) {
		v->running = 1;
	}
	else {
		v->state = VTS_ERROR;
		snprintf(v->err, sizeof(v->err), "Could not start the background check.");
	}
	pl_invalidate(v->w, NULL);
}

/* ---- painting ---- */

/* palette */
#define VT_RED    ((PlColor)0xD9534F)
#define VT_AMBER  ((PlColor)0xF0AD4E)
#define VT_GREEN  ((PlColor)0x5CB85C)
#define VT_GREY   ((PlColor)0x6C757D)
#define VT_WHITE  ((PlColor)0xFFFFFF)

static void put_text(PlCanvas* c, PlFont* f, int x, int y, int w, int h,
	const char* s, PlColor col)
{
	PlRect r = { x, y, x + w, y + h };
	pl_text(c, f, r, s, col, PL_TEXT_LEFT);
}

static void draw_card(PlCanvas* c, int x, int y, int w, int h)
{
	PlRect r = { x, y, x + w, y + h };
	pl_round_rect(c, r, CLR_SEARCH_BG, CLR_PANEL_BORDER, 8);
}

static void draw_button(VtView* v, PlCanvas* c, PlRect r, const char* label,
	int primary, int hovered)
{
	PlRect tr = { r.l + 16, r.t, r.r - 8, r.b };
	PlColor fill = primary ? CLR_ACCENT : CLR_SEARCH_BG;
	PlColor border = (primary || hovered) ? CLR_ACCENT : CLR_PANEL_BORDER;
	pl_round_rect(c, r, fill, border, 6);
	pl_text(c, v->fText, tr, label, primary ? VT_WHITE : CLR_TEXT_PRIMARY, PL_TEXT_LEFT);
}

/* filled status pill, e.g. "CLEAN" / "MALICIOUS" */
static void draw_pill(VtView* v, PlCanvas* c, int x, int y, const char* label, PlColor col)
{
	int w = (int)strlen(label) * 8 + 24;
	PlRect r = { x, y, x + w, y + 22 };
	PlRect tr = { x + 12, y, x + w, y + 22 };
	pl_round_rect(c, r, col, col, 11);
	pl_text(c, v->fText, tr, label, VT_WHITE, PL_TEXT_LEFT);
}

/* colored swatch + "Label  count" */
static void draw_legend(VtView* v, PlCanvas* c, int x, int y, PlColor col,
	const char* label, int n)
{
	char line[96];
	pl_fill(c, x, y + 5, x + 10, y + 15, col);
	snprintf(line, sizeof(line), "%s  %d", label, n);
	put_text(c, v->fText, x + 18, y, 200, 20, line, CLR_TEXT_PRIMARY);
}

/* rounded, segmented bar */
static void draw_bar(PlCanvas* c, int x, int y, int w, int h, const VtResult* r)
{
	int total = r->malicious + r->suspicious + r->harmless + r->undetected;
	int cx = x, i;
	struct { int n; PlColor col; } seg[4];
	PlRect track = { x, y, x + w, y + h };
	seg[0].n = r->malicious;  seg[0].col = VT_RED;
	seg[1].n = r->suspicious; seg[1].col = VT_AMBER;
	seg[2].n = r->harmless;   seg[2].col = VT_GREEN;
	seg[3].n = r->undetected; seg[3].col = VT_GREY;
	pl_round_rect(c, track, CLR_PANEL_BODY, CLR_PANEL_BORDER, h / 2);
	if (total <= 0) return;
	for (i = 0; i < 4; ++i) {
		int sw = (int)((long long)w * seg[i].n / total);
		if (i == 3) sw = x + w - cx;
		if (sw > 0) pl_fill(c, cx, y + 1, cx + sw, y + h - 1, seg[i].col);
		cx += sw;
	}
}

/* card with a headline and an optional second line; returns card height */
static int draw_message(VtView* v, PlCanvas* c, int x, int y, int w,
	const char* l1, PlColor c1, const char* l2)
{
	int h = l2 ? 68 : 46;
	draw_card(c, x, y, w, h);
	put_text(c, v->fText, x + 16, y + 12, w - 32, 20, l1, c1);
	if (l2) put_text(c, v->fText, x + 16, y + 36, w - 32, 20, l2, CLR_TEXT_SECONDARY);
	return h;
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	VtView* v = (VtView*)u;
	int x = 24, y = 20, tw = w - 2 * x;
	char line[320];
	PlRect btn, frame;

	v->width = w; v->height = h;
	v->has_main = v->has_browser = v->has_copy = 0;
	pl_fill(c, 0, 0, w, h, CLR_BG);
	frame.l = 0; frame.t = 0; frame.r = w - 1; frame.b = h - 1;
	pl_round_rect(c, frame, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
	if (tw < 40) return;

	/* ---- header ---- */
	put_text(c, v->fTitle, x, y, tw, 24, translate("VirusTotal"), CLR_TEXT_PRIMARY);
	y += 24;
	put_text(c, v->fText, x, y, tw, 20,
		translate("File reputation check"), CLR_TEXT_SECONDARY);
	y += 30;
	pl_fill(c, x, y, x + tw, y + 1, CLR_PANEL_BORDER);
	y += 16;

	if (v->state == VTS_NOFILE) {
		draw_message(v, c, x, y, tw,
			translate("No file to check."), CLR_TEXT_PRIMARY,
			translate("Scan a file first (Ctrl+O or drag one here)."));
		return;
	}

	/* ---- file card ---- */
	{
		const char* name = v->path, * p;
		int ch = v->hash[0] ? 62 : 40;
		for (p = v->path; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;
		draw_card(c, x, y, tw, ch);
		put_text(c, v->fText, x + 16, y + 10, tw - 32, 20, name, CLR_TEXT_PRIMARY);
		if (v->hash[0]) {
			snprintf(line, sizeof(line), "SHA-256  %s", v->hash);
			put_text(c, v->fText, x + 16, y + 34, tw - 32, 20, line, CLR_TEXT_SECONDARY);
		}
		y += ch + 16;
	}

	if (v->state == VTS_READY) {
		y += draw_message(v, c, x, y, tw,
			translate("Ready to check."), CLR_TEXT_PRIMARY,
			translate("Only the SHA-256 hash is sent to VirusTotal, not the file.")) + 20;
		btn.l = x; btn.t = y; btn.r = x + 200; btn.b = y + 34;
		draw_button(v, c, btn, translate("Check on VirusTotal"), 1, v->hover == 1);
		v->btn_main = btn; v->has_main = 1;
		return;
	}

	if (v->state == VTS_BUSY) {
		draw_message(v, c, x, y, tw,
			translate("Hashing the file and querying VirusTotal..."),
			CLR_TEXT_SECONDARY, NULL);
		return;
	}

	/* ---- result ---- */
	if (v->state == VTS_DONE) {
		const VtResult* r = &v->res;
		int total = r->malicious + r->suspicious + r->harmless + r->undetected;
		PlColor col = r->malicious > 0 ? VT_RED : (r->suspicious > 0 ? VT_AMBER : VT_GREEN);
		const char* verdict = r->malicious > 0 ? translate("MALICIOUS")
			: (r->suspicious > 0 ? translate("SUSPICIOUS") : translate("CLEAN"));
		int half = tw / 2;

		draw_card(c, x, y, tw, 168);
		snprintf(line, sizeof(line), "%d / %d", r->malicious, total);
		put_text(c, v->fBig, x + 16, y + 12, 150, 40, line, col);
		draw_pill(v, c, x + 170, y + 20, verdict, col);
		put_text(c, v->fText, x + 16, y + 54, tw - 32, 20,
			translate("security vendors flagged this file as malicious"), CLR_TEXT_SECONDARY);
		draw_bar(c, x + 16, y + 84, tw - 32, 10, r);
		draw_legend(v, c, x + 16, y + 108, VT_RED, translate("Malicious"), r->malicious);
		draw_legend(v, c, x + 16 + half, y + 108, VT_AMBER, translate("Suspicious"), r->suspicious);
		draw_legend(v, c, x + 16, y + 134, VT_GREEN, translate("Harmless"), r->harmless);
		draw_legend(v, c, x + 16 + half, y + 134, VT_GREY, translate("Undetected"), r->undetected);
		y += 168 + 20;
	}
	else if (v->state == VTS_NOTFOUND) {
		y += draw_message(v, c, x, y, tw,
			translate("VirusTotal has no record of this file yet."), CLR_TEXT_PRIMARY,
			translate("It may be new or rare. This is not a verdict either way.")) + 20;
	}
	else if (v->state == VTS_NOKEY) {
		y += draw_message(v, c, x, y, tw,
			translate("No API key set, so results cannot be shown here."), CLR_TEXT_PRIMARY,
			translate("Add your key in Options > VirusTotal.")) + 20;
	}
	else if (v->state == VTS_ERROR) {
		y += draw_message(v, c, x, y, tw, v->err, VT_RED, NULL) + 20;
	}

	/* ---- actions ---- */
	btn.l = x; btn.t = y; btn.r = x + 120; btn.b = y + 34;
	draw_button(v, c, btn, translate("Re-check"), 0, v->hover == 1);
	v->btn_main = btn; v->has_main = 1;

	if (v->hash[0]) {
		btn.l = btn.r + 10; btn.r = btn.l + 170;
		draw_button(v, c, btn, translate("Open in browser"), 0, v->hover == 2);
		v->btn_browser = btn; v->has_browser = 1;

		if (v->copy_cb) {
			btn.l = btn.r + 10; btn.r = btn.l + 150;
			draw_button(v, c, btn, translate("Copy SHA-256"), 0, v->hover == 3);
			v->btn_copy = btn; v->has_copy = 1;
		}
	}
}

static int in_rect(PlRect r, int x, int y)
{
	return x >= r.l && x < r.r && y >= r.t && y < r.b;
}

static void on_mouse_move(void* u, int x, int y)
{
	VtView* v = (VtView*)u;
	int hv = 0;
	if (v->has_main && in_rect(v->btn_main, x, y)) hv = 1;
	else if (v->has_browser && in_rect(v->btn_browser, x, y)) hv = 2;
	else if (v->has_copy && in_rect(v->btn_copy, x, y)) hv = 3;
	if (hv != v->hover) { v->hover = hv; pl_invalidate(v->w, NULL); }
}

static void on_mouse_leave(void* u)
{
	VtView* v = (VtView*)u;
	if (v->hover) { v->hover = 0; pl_invalidate(v->w, NULL); }
}

static void on_mouse_down(void* u, int x, int y)
{
	VtView* v = (VtView*)u;
	if (v->has_main && in_rect(v->btn_main, x, y)) {
		if (v->state == VTS_READY || v->state == VTS_DONE || v->state == VTS_NOTFOUND ||
			v->state == VTS_NOKEY || v->state == VTS_ERROR)
			start_check(v);
	}
	else if (v->has_browser && in_rect(v->btn_browser, x, y)) {
		vt_open_report(v->hash);
	}
	else if (v->has_copy && in_rect(v->btn_copy, x, y)) {
		if (v->copy_cb && v->hash[0]) v->copy_cb(v->copy_user, v->hash);
	}
}

static void on_resize(void* u, int w, int h)
{
	VtView* v = (VtView*)u;
	v->width = w; v->height = h;
	pl_invalidate(v->w, NULL);
}

static void on_destroy(void* u)
{
	VtView* v = (VtView*)u;
	join_worker(v);
	pl_font_destroy(v->fBig);
	pl_font_destroy(v->fTitle);
	pl_font_destroy(v->fText);
	free(v);
}

static const PlWidgetOps s_vt_ops = {
	on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize, NULL, on_destroy
};

VtView* vtview_create(void* parent_native, int x, int y, int w, int h)
{
	VtView* v = (VtView*)calloc(1, sizeof * v);
	if (!v) return NULL;
	v->state = VTS_NOFILE;
	v->width = w; v->height = h;
	v->fBig = pl_font_create(22, 1);
	v->fTitle = pl_font_create(12, 1);
	v->fText = pl_font_create(9, 0);
	v->w = pl_widget_create(parent_native, &s_vt_ops, v, x, y, w, h);
	if (!v->w) {
		pl_font_destroy(v->fBig);
		pl_font_destroy(v->fTitle);
		pl_font_destroy(v->fText);
		free(v);
		return NULL;
	}
	return v;
}

PlWidget* vtview_widget(const VtView* v) { return v->w; }

void vtview_set_callbacks(VtView* v, int (*copy)(void*, const char*), void* user)
{
	if (!v) return;
	v->copy_cb = copy;
	v->copy_user = user;
}

void vtview_set_rect(VtView* v, int x, int y, int w, int h)
{
	if (v) pl_widget_set_rect(v->w, x, y, w, h);
}

void vtview_set_file(VtView* v, const char* utf8_path)
{
	if (!v) return;
	join_worker(v);
	v->hash[0] = 0;
	v->err[0] = 0;
	v->hover = 0;
	memset(&v->res, 0, sizeof(v->res));
	if (utf8_path && utf8_path[0]) {
		snprintf(v->path, sizeof(v->path), "%s", utf8_path);
		v->state = VTS_READY;
	}
	else {
		v->path[0] = 0;
		v->state = VTS_NOFILE;
	}
	pl_invalidate(v->w, NULL);
}

void vtview_refresh(VtView* v)
{
	if (v && v->state == VTS_READY) start_check(v);
}

void vtview_tick(VtView* v)
{
	if (!v || !v->running || !v->worker_done) return;
	join_worker(v);
	v->state = v->w_state;
	memcpy(v->hash, v->w_hash, sizeof(v->hash));
	memcpy(v->err, v->w_err, sizeof(v->err));
	v->res = v->w_res;
	pl_invalidate(v->w, NULL);
}

void vtview_cancel(VtView* v)
{
	if (v) join_worker(v);
}