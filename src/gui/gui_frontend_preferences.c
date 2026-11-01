/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* POSIX declarations must be selected before any system header in strict C99. */
/* flock is a BSD interface hidden by the POSIX/XSI feature selection on macOS. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#endif
#include "gui_frontend_preferences.h"
#include "gui_backend.h"
#include "gui_tree.h"
#include "gui_hex_view.h"
#include "gui_disasm.h"
#include "gui_settings.h"
#include "app/cdie_translation.h"
#include <xxwidgets/xxwidgets_forms.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HANDLE instance_mutex;
static int instance_mutex_owned;
static HWND instance_window;
static wchar_t instance_name[96];
#else
#include <unistd.h>
#include <sys/file.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
static int instance_file = -1;
/* Single application: the first copy listens beside its lock; later copies
 * send their file path there (see gui_frontend_instance_poll). */
static int instance_listener = -1;
static int instance_client = -1;
static char instance_socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static char instance_message[GUI_PREFERENCE_PATH_MAX];
static size_t instance_received;
static int instance_oversized;
static time_t instance_client_since;
#endif

static int frontend_preferences_applied;

void gui_frontend_get_preferences(gui_options_values *values)
{
    gui_backend_scan_preferences scan;
    gui_preferences_get(values);
    if (frontend_preferences_applied && gui_backend_get_scan_preferences(&scan)) {
        values->scan.recursive = scan.recursive;
        values->scan.profiling = scan.profiling;
        values->scan.sort = scan.sort;
        values->scan.use_cache = scan.use_cache;
        memcpy(values->scan.engine, scan.engine, sizeof(scan.engine));
    }
    gui_disasm_get_options(&values->disasm);
}

xxwidgets_status gui_frontend_apply_preferences(xxwidgets_app *app, const gui_options_values *values)
{
    gui_backend_scan_preferences scan;
    xxwidgets_status status;
    memset(&scan, 0, sizeof(scan));
    scan.recursive = values->scan.recursive;
    scan.profiling = values->scan.profiling;
    scan.sort = values->scan.sort;
    scan.use_cache = values->scan.use_cache;
    memcpy(scan.engine, values->scan.engine, sizeof(scan.engine));
    if (!gui_backend_set_scan_preferences(&scan)) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_app_set_disasm_options(app, &values->disasm);
    if (status != XXWIDGETS_OK) return status;
    gui_disasm_set_options(&values->disasm);
    status = xxwidgets_app_set_save_backup(app, values->file.save_backup);
    if (status != XXWIDGETS_OK) return status;
    status = gui_tree_set_scan_preferences(&values->scan);
    if (status != XXWIDGETS_OK) return status;
    if (!gui_backend_set_info_path(values->info.path)) return XXWIDGETS_INVALID_ARGUMENT;
    gui_preferences_set(values);
    frontend_preferences_applied = 1;
    return XXWIDGETS_OK;
}

int gui_frontend_scan_after_open(void)
{
    gui_options_values values;
    gui_preferences_get(&values);
    return values.scan.scan_after_open;
}

char *gui_frontend_executable_directory(void)
{
    char *path = NULL, *slash;
#ifdef _WIN32
    wchar_t wide[32768];
    DWORD size = GetModuleFileNameW(NULL, wide, 32768);
    if (size && size < 32768) {
        char *utf8 = xx_str_unicode_to_utf8(wide);
        if (utf8) { size_t n = strlen(utf8) + 1; path = malloc(n); if (path) memcpy(path, utf8, n); xx_str_free(utf8); }
    }
#else
    char buffer[GUI_PREFERENCE_PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) path = realpath(buffer, NULL);
#else
    ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (size > 0) { buffer[size] = 0; path = malloc((size_t)size + 1); if (path) memcpy(path, buffer, (size_t)size + 1); }
#endif
#endif
    if (!path) return NULL;
    slash = strrchr(path, '/');
    { char *other = strrchr(path, '\\'); if (other && (!slash || other > slash)) slash = other; }
    if (!slash) { free(path); return NULL; }
    if (slash == path || (slash == path + 2 && path[1] == ':')) slash[1] = 0;
    else *slash = 0;
    return path;
}

/* History uses UTF-8 lines, escaping percent/line terminators so filenames
 * never change record boundaries. Retain at most16 most recently opened files. */
static size_t recent_decode(const char *stored, char paths[16][GUI_PREFERENCE_PATH_MAX])
{
    size_t count = 0;
    while (*stored && count < 16) {
        size_t at = 0;
        int oversized = 0;
        while (*stored && *stored != '\n') {
            char c = *stored++;
            if (c == '%' && stored[0] && stored[1]) {
                if (stored[0] == '2' && stored[1] == '5') { c = '%'; stored += 2; }
                else if (stored[0] == '0' && (stored[1] == 'A' || stored[1] == 'D')) { c = stored[1] == 'A' ? '\n' : '\r'; stored += 2; }
            }
            if (at + 1 < GUI_PREFERENCE_PATH_MAX) paths[count][at++] = c;
            else oversized = 1;
        }
        paths[count][at] = 0;
        if (at && !oversized) ++count;
        if (*stored == '\n') ++stored;
    }
    return count;
}

static int recent_path_equal(const char *a, const char *b)
{
#ifdef _WIN32
    wchar_t *left = xx_str_utf8_to_unicode(a), *right = xx_str_utf8_to_unicode(b);
    int same = left && right ? CompareStringOrdinal(left,-1,right,-1,TRUE) == CSTR_EQUAL : !strcmp(a,b);
    xx_str_wfree(left); xx_str_wfree(right);
    return same;
#else
    return !strcmp(a,b);
#endif
}

void gui_frontend_remember_file(const char *path)
{
    gui_options_values values;
    char previous[16][GUI_PREFERENCE_PATH_MAX], *absolute = NULL, *slash;
    size_t count, used = 0, retained = 0;
    if (!path || !*path || !xx_io_file_exists_a(path)) return;
#ifdef _WIN32
    { wchar_t *wide = xx_str_utf8_to_unicode(path), full[32768];
      DWORD n = wide ? GetFullPathNameW(wide, 32768, full, NULL) : 0;
      char *utf8 = n && n < 32768 ? xx_str_unicode_to_utf8(full) : NULL;
      if (utf8) { size_t len = strlen(utf8) + 1; absolute = malloc(len); if (absolute) memcpy(absolute, utf8, len); xx_str_free(utf8); }
      xx_str_wfree(wide); }
#else
    absolute = realpath(path, NULL);
#endif
    if (!absolute || strlen(absolute) >= GUI_PREFERENCE_PATH_MAX) { free(absolute); return; }
    gui_preferences_get(&values);
    if (values.file.save_recent_files) {
        count = recent_decode(values.file.recent_files, previous);
        values.file.recent_files[0] = 0;
        for (size_t item = 0; item <= count && retained < 16; ++item) {
            const char *text = item ? previous[item - 1] : absolute;
            size_t needed = 1;
            if (item && recent_path_equal(text, absolute)) continue;
            for (const char *p = text; *p; ++p) needed += (*p == '%' || *p == '\n' || *p == '\r') ? 3 : 1;
            if (needed >= sizeof(values.file.recent_files) - used) break;
            for (const char *p = text; *p; ++p) {
                const char *escape = *p == '%' ? "%25" : *p == '\n' ? "%0A" : *p == '\r' ? "%0D" : NULL;
                if (escape) { memcpy(values.file.recent_files + used, escape, 3); used += 3; }
                else values.file.recent_files[used++] = *p;
            }
            values.file.recent_files[used++] = '\n';
            values.file.recent_files[used] = 0;
            ++retained;
        }
    } else values.file.recent_files[0] = 0;
    if (values.file.save_last_directory) {
        slash = strrchr(absolute, '/');
#ifdef _WIN32
        { char *other = strrchr(absolute, '\\'); if (other && (!slash || other > slash)) slash = other; }
#endif
        if (slash) { if (slash == absolute || (slash == absolute + 2 && absolute[1] == ':')) slash[1] = 0; else *slash = 0; strcpy(values.file.last_directory, absolute); }
    } else values.file.last_directory[0] = 0;
    gui_preferences_set(&values);
    free(absolute);
}

xxwidgets_status gui_frontend_recent_dialog(xxwidgets_widget *owner, char *path, size_t capacity, int *accepted)
{
    gui_options_values values;
    char paths[16][GUI_PREFERENCE_PATH_MAX];
    const char *items[16];
    size_t count, selection = 0;
    xxwidgets_status status;
    if (!owner || !path || !capacity || !accepted) return XXWIDGETS_INVALID_ARGUMENT;
    *accepted = 0;
    gui_preferences_get(&values);
    count = values.file.save_recent_files ? recent_decode(values.file.recent_files, paths) : 0;
    for (size_t i = 0; i < count; ++i) items[i] = paths[i];
    status = xxwidgets_choice_dialog(owner, CDIE_TR("Recent files"), items, count, &selection, accepted);
    if (status == XXWIDGETS_OK && *accepted) {
        if (selection >= count || strlen(paths[selection]) >= capacity) { *accepted = 0; return XXWIDGETS_INVALID_ARGUMENT; }
        strcpy(path, paths[selection]);
    }
    return status;
}

char *gui_frontend_stored_language(int memory_only)
{
    int loaded = 0;
    xx_settings *store = gui_settings_open(memory_only, &loaded);
    /* cdie's own key; Detect It Easy's View/Lang names a .qm file (gui_settings.c). */
    const xx_settings_value *value = loaded && store ? xx_settings_get(store, "cdie/Lang") : NULL;
    char *path = NULL;
    if (!value && loaded && store) value = xx_settings_get(store, "View/Lang");
    if (value && value->type == XX_SETTINGS_VALUE_STRING && value->data.buffer.size &&
        value->data.buffer.size < GUI_PREFERENCE_PATH_MAX &&
        !memchr(value->data.buffer.data, 0, value->data.buffer.size)) {
        path = malloc(value->data.buffer.size + 1);
        if (path) { memcpy(path, value->data.buffer.data, value->data.buffer.size); path[value->data.buffer.size] = 0; }
        if (path && (!strcmp(path, "System") || !strcmp(path, "English") || !xx_io_file_exists_a(path))) { free(path); path = NULL; }
    }
    gui_settings_close(store);
    return path;
}

static uint64_t installation_hash(const char *path)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (; *path; ++path) {
        unsigned char c = (unsigned char)*path;
#ifdef _WIN32
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
#endif
        hash = (hash ^ c) * UINT64_C(1099511628211);
    }
    return hash;
}
#ifdef _WIN32
static BOOL CALLBACK find_instance(HWND window, LPARAM parameter)
{
    HWND *found = (HWND *)parameter;
    if (GetPropW(window, instance_name)) { *found = window; return FALSE; }
    return TRUE;
}
#else
static void instance_set_flags(int fd, int nonblocking)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    if (nonblocking) { flags = fcntl(fd, F_GETFL); if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
}

/* Only a socket this account created counts: /tmp is shared, and another
 * account must neither receive our file names nor have its entry removed. */
static int instance_socket_ours(const char *path)
{
    struct stat info;
    return lstat(path, &info) == 0 && S_ISSOCK(info.st_mode) && info.st_uid == getuid() &&
           !(info.st_mode & (S_IRWXG | S_IRWXO));
}

/* Called with the lock held, so an existing socket of ours is stale. Bound,
 * restricted to the owner, and only then listening: nobody can connect while
 * the mode is still the umask's. A failure leaves forwarding off, not the GUI. */
static void instance_listen(const char *path)
{
    struct sockaddr_un address;
    struct stat info;
    int fd;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path)) return;
    strcpy(address.sun_path, path);
    if (lstat(path, &info) == 0) {
        if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid() || unlink(path) != 0) return;
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    instance_set_flags(fd, 1);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0) { close(fd); return; }
    if (chmod(path, 0600) != 0 || listen(fd, 8) != 0) { close(fd); unlink(path); return; }
    instance_listener = fd;
    strcpy(instance_socket_path, path);
}

/* The running copy may still be between its lock and its listen; wait for it
 * as Windows waits on the mutex. The path is buffered by the kernel, so the
 * running copy receives it when its main loop next polls, even if a dialog
 * is open now. */
static int instance_forward(const char *path, const char *startup_file)
{
    struct sockaddr_un address;
    char *absolute = NULL;
    size_t length;
    int fd = -1, attempt, sent = 0;
    if (!startup_file || !*startup_file) return 0;
    if (strlen(path) >= sizeof(address.sun_path)) return -1;
    if (startup_file[0] == '/') {
        length = strlen(startup_file) + 1;
        absolute = malloc(length);
        if (absolute) memcpy(absolute, startup_file, length);
    } else if (!(absolute = realpath(startup_file, NULL))) {
        char directory[GUI_PREFERENCE_PATH_MAX];
        if (getcwd(directory, sizeof(directory))) {
            length = strlen(directory) + strlen(startup_file) + 2;
            absolute = malloc(length);
            if (absolute) snprintf(absolute, length, "%s/%s", directory, startup_file);
        }
    }
    if (!absolute || strlen(absolute) >= GUI_PREFERENCE_PATH_MAX) { free(absolute); return -1; }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, path);
    for (attempt = 0; attempt < 60 && fd < 0; ++attempt) {
        if (attempt) { struct timespec delay = {0, 50000000L}; nanosleep(&delay, NULL); }
        if (!instance_socket_ours(path)) continue;
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) break;
        instance_set_flags(fd, 0);
#ifdef SO_NOSIGPIPE
        { int on = 1; (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)); }
#endif
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) { close(fd); fd = -1; }
    }
    if (fd >= 0) {
        const char *at = absolute;
        size_t left = strlen(absolute) + 1;
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        while (left) {
            ssize_t n = send(fd, at, left, flags);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            at += n; left -= (size_t)n;
        }
        sent = !left;
        close(fd);
    }
    free(absolute);
    return sent ? 0 : -1;
}

static int instance_utf8_valid(const unsigned char *text)
{
    while (*text) {
        unsigned char c = *text;
        int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 && c >= 0xC2 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 && c <= 0xF4 ? 3 : -1;
        if (more < 0) return 0;
        ++text;
        while (more--) { if ((*text & 0xC0) != 0x80) return 0; ++text; }
    }
    return 1;
}
#endif
int gui_frontend_instance_start(xxwidgets_widget *window, const char *startup_file)
{
    gui_options_values values;
    char *directory;
    uint64_t hash;
    gui_preferences_get(&values);
    if (!values.appearance.single_application) return 1;
    directory = gui_frontend_executable_directory();
    if (!directory) return -1;
    hash = installation_hash(directory);
    free(directory);
#ifdef _WIN32
    {
        wchar_t mutex_name[128];
        HWND existing = NULL;
        swprintf(instance_name, 96, L"NTInfo.cdie.%016llx", (unsigned long long)hash);
        swprintf(mutex_name, 128, L"Local\\%ls", instance_name);
        /* Owning the mutex during publication makes an existing name a
         * readiness barrier rather than a reason to drop a startup file. */
        instance_mutex = CreateMutexW(NULL, TRUE, mutex_name);
        if (!instance_mutex) return -1;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            DWORD wait = WaitForSingleObject(instance_mutex, 3000);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
                CloseHandle(instance_mutex); instance_mutex = NULL; return -1;
            }
            instance_mutex_owned = 1;
            EnumWindows(find_instance, (LPARAM)&existing);
            if (existing) {
                int forwarded = 1;
                if (startup_file && *startup_file) {
                    wchar_t *wide = xx_str_utf8_to_unicode(startup_file), absolute[32768];
                    DWORD length = wide ? GetFullPathNameW(wide,32768,absolute,NULL) : 0;
                    char *utf8 = length && length < 32768 ? xx_str_unicode_to_utf8(absolute) : NULL;
                    DWORD_PTR result = 0;
                    xx_str_wfree(wide);
                    if (utf8 && strlen(utf8) < GUI_PREFERENCE_PATH_MAX) {
                        COPYDATASTRUCT data = {GUI_FRONTEND_COPYDATA_ID,(DWORD)strlen(utf8)+1,utf8};
                        forwarded = SendMessageTimeoutW(existing,WM_COPYDATA,0,(LPARAM)&data,SMTO_ABORTIFHUNG,1500,&result) != 0 && result != 0;
                    } else forwarded = 0;
                    xx_str_free(utf8);
                }
                ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing);
                ReleaseMutex(instance_mutex); instance_mutex_owned = 0;
                CloseHandle(instance_mutex); instance_mutex = NULL;
                return forwarded ? 0 : -1;
            }
            /* The preceding owner closed before publishing a window. This
             * process can publish its own window while holding the barrier. */
        } else instance_mutex_owned = 1;
        instance_window = (HWND)xxwidgets_widget_native_handle(window);
        if (!SetPropW(instance_window, instance_name, (HANDLE)(uintptr_t)1)) { gui_frontend_instance_close(); return -1; }
        ReleaseMutex(instance_mutex); instance_mutex_owned = 0;
    }
#else
    {
        char path[128], socket_path[128];
        int flags = O_CREAT | O_RDWR;
        (void)window;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
        /* A child (xdg-open, a browser) must not keep the lock after exit. */
        flags |= O_CLOEXEC;
#endif
        snprintf(path, sizeof(path), "/tmp/cdie-%lu-%016llx.lock", (unsigned long)getuid(), (unsigned long long)hash);
        snprintf(socket_path, sizeof(socket_path), "/tmp/cdie-%lu-%016llx.sock", (unsigned long)getuid(), (unsigned long long)hash);
        instance_file = open(path, flags, 0600);
        if (instance_file < 0) return -1;
        if (flock(instance_file, LOCK_EX | LOCK_NB) != 0) {
            close(instance_file); instance_file = -1;
            return instance_forward(socket_path, startup_file);
        }
        instance_listen(socket_path);
    }
#endif
    return 1;
}

int gui_frontend_instance_poll(xxwidgets_widget *window, char **path_out)
{
    (void)window;
    if (!path_out) return 0;
    *path_out = NULL;
#ifdef _WIN32
    return 0;
#else
    for (;;) {
        int done = 0;
        if (instance_client < 0) {
            if (instance_listener < 0) return 0;
            instance_client = accept(instance_listener, NULL, NULL);
            if (instance_client < 0) return 0;
            instance_set_flags(instance_client, 1);
            instance_received = 0; instance_oversized = 0;
            instance_client_since = time(NULL);
        }
        while (!done) {
            char chunk[512];
            ssize_t n = read(instance_client, chunk, sizeof(chunk));
            if (n > 0) {
                const char *end = memchr(chunk, 0, (size_t)n);
                size_t take = end ? (size_t)(end - chunk) : (size_t)n;
                if (instance_received + take >= sizeof(instance_message)) instance_oversized = 1;
                else { memcpy(instance_message + instance_received, chunk, take); instance_received += take; }
                done = end != NULL;
            } else if (n == 0) done = 1;
            else if (errno == EINTR) continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* A sender that stalls is dropped rather than waited for. */
                if (time(NULL) - instance_client_since <= 2) return 0;
                instance_oversized = 1; done = 1;
            } else { instance_oversized = 1; done = 1; }
        }
        close(instance_client); instance_client = -1;
        instance_message[instance_received] = 0;
        if (!instance_oversized && instance_received &&
            instance_utf8_valid((const unsigned char *)instance_message)) {
            *path_out = malloc(instance_received + 1);
            if (*path_out) { memcpy(*path_out, instance_message, instance_received + 1); return 1; }
        }
    }
#endif
}
void gui_frontend_instance_close(void)
{
#ifdef _WIN32
    if (instance_window) RemovePropW(instance_window, instance_name);
    instance_window = NULL;
    if (instance_mutex_owned && instance_mutex) ReleaseMutex(instance_mutex);
    instance_mutex_owned = 0;
    if (instance_mutex) CloseHandle(instance_mutex);
    instance_mutex = NULL;
#else
    if (instance_client >= 0) close(instance_client);
    instance_client = -1;
    if (instance_listener >= 0) {
        /* Before the lock goes: the next owner binds only after taking it. */
        close(instance_listener);
        if (instance_socket_ours(instance_socket_path)) unlink(instance_socket_path);
    }
    instance_listener = -1;
    instance_socket_path[0] = 0;
    if (instance_file >= 0) close(instance_file);
    instance_file = -1;
#endif
}

#ifdef _WIN32
static int directory_component_equal(const wchar_t *component, size_t length, const wchar_t *directory)
{
    size_t size = wcslen(directory);
    while (length && (component[length-1] == L'\\' || component[length-1] == L'/')) --length;
    while (size && (directory[size-1] == L'\\' || directory[size-1] == L'/')) --size;
    return length == size && _wcsnicmp(component, directory, size) == 0;
}
static wchar_t *user_path(DWORD *stored_type, LONG *result)
{
    HKEY key; DWORD bytes = 0, type = 0;
    wchar_t *path;
    LONG code = RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_QUERY_VALUE, &key);
    if (result) *result = code;
    if (stored_type) *stored_type = REG_EXPAND_SZ;
    if (code != ERROR_SUCCESS) return NULL;
    code = RegQueryValueExW(key, L"Path", NULL, &type, NULL, &bytes);
    if (code != ERROR_SUCCESS || bytes > 1024 * 1024 || bytes % sizeof(wchar_t) ||
        (type != REG_SZ && type != REG_EXPAND_SZ)) {
        if (result) *result = code == ERROR_SUCCESS ? ERROR_INVALID_DATA : code;
        RegCloseKey(key); return NULL;
    }
    if (stored_type) *stored_type = type;
    path = calloc(1, (size_t)bytes + sizeof(wchar_t));
    code = path ? RegQueryValueExW(key, L"Path", NULL, &type, (BYTE *)path, &bytes) : ERROR_NOT_ENOUGH_MEMORY;
    if (code != ERROR_SUCCESS) { free(path); path = NULL; }
    if (result) *result = code;
    RegCloseKey(key);
    return path;
}
#endif
int gui_frontend_environment_contains(const char *directory)
{
#ifdef _WIN32
    wchar_t *wide = directory ? xx_str_utf8_to_unicode(directory) : NULL, *path = user_path(NULL, NULL);
    int found = 0;
    if (wide && path) for (const wchar_t *at = path; *at;) { const wchar_t *end = wcschr(at, ';'); size_t n = end ? (size_t)(end - at) : wcslen(at); if (directory_component_equal(at, n, wide)) { found = 1; break; } at += n; if (*at) ++at; }
    xx_str_wfree(wide); free(path);
    return found;
#else
    (void)directory; return 0;
#endif
}
xxwidgets_status gui_frontend_environment_apply(const char *directory, int enabled)
{
#ifdef _WIN32
    DWORD stored_type = REG_EXPAND_SZ;
    LONG read_status = ERROR_SUCCESS;
    wchar_t *wide = directory ? xx_str_utf8_to_unicode(directory) : NULL;
    wchar_t *old = user_path(&stored_type, &read_status), *result;
    size_t capacity, used = 0; HKEY key; LONG code;
    if (!wide || wcschr(wide, L';')) { xx_str_wfree(wide); free(old); return XXWIDGETS_INVALID_ARGUMENT; }
    if (!old && read_status != ERROR_FILE_NOT_FOUND && read_status != ERROR_PATH_NOT_FOUND) {
        xx_str_wfree(wide); return XXWIDGETS_PLATFORM_ERROR;
    }
    if (gui_frontend_environment_contains(directory) == !!enabled) { xx_str_wfree(wide); free(old); return XXWIDGETS_OK; }
    capacity = (old ? wcslen(old) : 0) + wcslen(wide) + 3;
    result = calloc(capacity, sizeof(wchar_t));
    if (!result) { xx_str_wfree(wide); free(old); return XXWIDGETS_OUT_OF_MEMORY; }
    if (enabled) {
        if (old) { used = wcslen(old); memcpy(result, old, used * sizeof(wchar_t)); }
        if (used) result[used++] = L';';
        wcscpy(result + used, wide);
    } else if (old) {
        size_t retained = 0;
        for (const wchar_t *at = old;;) {
            const wchar_t *end = wcschr(at, L';');
            size_t n = end ? (size_t)(end - at) : wcslen(at);
            if (!directory_component_equal(at, n, wide)) {
                if (retained++) result[used++] = L';';
                memcpy(result + used, at, n * sizeof(wchar_t)); used += n;
            }
            if (!end) break;
            at = end + 1;
        }
    }
    code = RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL);
    if (code == ERROR_SUCCESS) {
        code = RegSetValueExW(key, L"Path", 0, stored_type, (const BYTE *)result, (DWORD)((wcslen(result)+1)*sizeof(wchar_t)));
        RegCloseKey(key);
    }
    free(result); free(old); xx_str_wfree(wide);
    if (code == ERROR_SUCCESS) { DWORD_PTR response; SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 1000, &response); }
    return code == ERROR_SUCCESS ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
#else
    (void)directory; return enabled ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_OK;
#endif
}
