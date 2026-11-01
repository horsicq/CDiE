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

/* main_updater.c - die-update, the signature database updater.
 *
 * die-update [options]          update the database when the source is newer
 * die-update --check [options]  only report whether it is
 *
 * The exit code is the cdie_update_status of the operation (cdie_updater.h):
 * 0 up to date / installed, 1 an update is available (--check), 2 network,
 * 3 database folder, 4 invalid parameter or settings, 5 verification,
 * 6 cancelled, 7 another update is running.
 *
 * Settings come from Detect It Easy's store: die.ini beside the executable in
 * a portable installation, otherwise the native NTInfo/die settings (the
 * registry on Windows). Command-line options take precedence. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "../global.h"
#include "cdie_updater.h"

#include <xxfclib/rt/xx_rt.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

typedef enum upd_mode_e { MODE_UPDATE = 0, MODE_CHECK } upd_mode;

static volatile int g_bInterrupted = 0;
static int g_bProgressShown = 0;
static int g_nLastPercent = -1;
static char g_sLastStatus[64];

/* ---------------------------------------------------------------- output */

static void print_help(void)
{
    xx_rt_printf("%s v%s\n", X_APPLICATIONNAME, X_APPLICATIONVERSION);
    xx_rt_printf("Detect It Easy signature database updater\n");
    xx_rt_printf("Copyright(C) 2026 hors<horsicq@gmail.com> Web: http://ntinfo.biz\n\n");
    xx_rt_printf("Usage: %s [options]\n\n", X_APPLICATIONNAME);
    xx_rt_printf("Without options the database is updated when the source has a newer one.\n\n");
    xx_rt_printf("Options:\n");
    xx_rt_printf("  -h, --help                Displays this help.\n");
    xx_rt_printf("  -v, --version             Displays version information.\n");
    xx_rt_printf("  -c, --check               Only check whether an update is available\n");
    xx_rt_printf("                            (exit code 1 when one is, 0 when up to date).\n");
    xx_rt_printf("  -u, --update              Install an available update (the default).\n");
    xx_rt_printf("  -f, --force               Install even when the database is up to date.\n");
    xx_rt_printf("  -D, --database <path>     Database folder (db). Default: the stored setting,\n");
    xx_rt_printf("                            else the folder the scanners use, else ./db.\n");
    xx_rt_printf("  -U, --url <url>           Update source: a GitHub release page or a zip URL.\n");
    xx_rt_printf("                            Default: %s\n", CDIE_UPDATE_DEFAULT_URL);
    xx_rt_printf("  -s, --settings <ini>      Read the settings from this INI file.\n");
    xx_rt_printf("      --nosettings          Ignore the stored settings.\n");
    xx_rt_printf("  -j, --json                Result as JSON.\n");
    xx_rt_printf("  -q, --quiet               Print errors only.\n");
    xx_rt_printf("\n");
    xx_rt_printf("Settings (die.ini beside a portable installation, else the native NTInfo/die store):\n");
    xx_rt_printf("  %s, %s\n", CDIE_UPDATE_KEY_MAIN_PATH, CDIE_UPDATE_KEY_URL);
    xx_rt_printf("\n");
    xx_rt_printf("Exit codes: 0 up to date or updated, 1 update available (--check), 2 network,\n");
    xx_rt_printf("  3 database folder, 4 invalid parameter, 5 verification failed, 6 cancelled,\n");
    xx_rt_printf("  7 another update is running.\n");
}

static void print_json_string(const char *pText)
{
    const unsigned char *p = (const unsigned char *)(pText ? pText : "");

    xx_rt_printf("\"");
    for (; *p; ++p) {
        if (*p == '"' || *p == '\\') xx_rt_printf("\\%c", *p);
        else if (*p == '\n') xx_rt_printf("\\n");
        else if (*p == '\r') xx_rt_printf("\\r");
        else if (*p == '\t') xx_rt_printf("\\t");
        else if (*p < 0x20) xx_rt_printf("\\u%04x", (unsigned)*p);
        else xx_rt_printf("%c", *p);
    }
    xx_rt_printf("\"");
}

static void print_json_member(const char *pName, const char *pValue, int bLast)
{
    xx_rt_printf("  ");
    print_json_string(pName);
    xx_rt_printf(": ");
    if (pValue) print_json_string(pValue);
    else xx_rt_printf("null");
    xx_rt_printf(bLast ? "\n" : ",\n");
}

static void format_time(int64_t nTime, char *pBuffer, size_t nSize)
{
    /* Civil date from Unix seconds (Howard Hinnant's algorithm), UTC. */
    int64_t nDays = nTime >= 0 ? nTime / 86400 : (nTime - 86399) / 86400;
    int64_t nSeconds = nTime - nDays * 86400;
    int64_t z = nDays + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;

    if (m <= 2) ++y;
    xx_rt_snprintf(pBuffer, nSize, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ", (long long)y, (long long)m, (long long)d,
                   (long long)(nSeconds / 3600), (long long)((nSeconds / 60) % 60), (long long)(nSeconds % 60));
}

static void print_json(upd_mode mode, cdie_update_status status, const cdie_update_info *pInfo)
{
    char sTime[32] = "";
    char sSize[32] = "";
    /* Addresses are shown without any credentials they carry. */
    char *pSource = pInfo->source_url ? cdie_update_display_url(pInfo->source_url) : NULL;
    char *pArchive = pInfo->archive_url ? cdie_update_display_url(pInfo->archive_url) : NULL;

    if (pInfo->remote_time) format_time(pInfo->remote_time, sTime, sizeof(sTime));
    if (pInfo->remote_size >= 0) xx_rt_snprintf(sSize, sizeof(sSize), "%lld", (long long)pInfo->remote_size);
    xx_rt_printf("{\n");
    print_json_member("operation", mode == MODE_CHECK ? "check" : "update", 0);
    print_json_member("status", cdie_update_status_name(status), 0);
    xx_rt_printf("  \"updateAvailable\": %s,\n", pInfo->update_available ? "true" : "false");
    xx_rt_printf("  \"installed\": %s,\n", pInfo->installed ? "true" : "false");
    xx_rt_printf("  \"userFolder\": %s,\n", pInfo->used_user_folder ? "true" : "false");
    print_json_member("database", pInfo->main_path, 0);
    print_json_member("source", pSource, 0);
    print_json_member("release", pInfo->release_tag, 0);
    print_json_member("archive", pArchive, 0);
    print_json_member("archiveSize", sSize[0] ? sSize : NULL, 0);
    print_json_member("archiveTime", sTime[0] ? sTime : NULL, 0);
    print_json_member("remoteMd5", pInfo->remote_md5[0] ? pInfo->remote_md5 : NULL, 0);
    print_json_member("installedMd5", pInfo->installed_md5[0] ? pInfo->installed_md5 : NULL, 0);
    print_json_member("installedAt", pInfo->installed_at[0] ? pInfo->installed_at : NULL, 0);
    xx_rt_printf("  \"signatures\": %d,\n", pInfo->signatures);
    print_json_member("message", pInfo->message, 1);
    xx_rt_printf("}\n");
    xx_rt_free(pSource);
    xx_rt_free(pArchive);
}

static void print_text(upd_mode mode, cdie_update_status status, const cdie_update_info *pInfo, const char *pSettings)
{
    char sTime[32] = "";
    char *pSource = pInfo->source_url ? cdie_update_display_url(pInfo->source_url) : NULL;

    if (pInfo->main_path) xx_rt_printf("Database:   %s\n", pInfo->main_path);
    if (pSettings) xx_rt_printf("Settings:   %s\n", pSettings);
    if (pSource) xx_rt_printf("Source:     %s\n", pSource);
    if (pInfo->release_tag) {
        xx_rt_printf("Release:    %s%s%s%s\n", pInfo->release_name ? pInfo->release_name : pInfo->release_tag,
                     pInfo->release_name ? " (" : "", pInfo->release_name ? pInfo->release_tag : "", pInfo->release_name ? ")" : "");
    }
    if (pInfo->archive_url) {
        if (pInfo->remote_time) format_time(pInfo->remote_time, sTime, sizeof(sTime));
        xx_rt_printf("Archive:    %s", pInfo->archive_name ? pInfo->archive_name : "");
        if (pInfo->remote_size >= 0) xx_rt_printf(", %lld bytes", (long long)pInfo->remote_size);
        if (sTime[0]) xx_rt_printf(", %s", sTime);
        xx_rt_printf("\n");
    }
    if (pInfo->remote_md5[0]) xx_rt_printf("MD5:        %s\n", pInfo->remote_md5);
    if (status == CDIE_UPDATE_OK || status == CDIE_UPDATE_AVAILABLE) {
        if (pInfo->installed_md5[0]) {
            xx_rt_printf("Installed:  %s%s%s%s\n", pInfo->installed_md5, pInfo->installed_at[0] ? " (" : "", pInfo->installed_at,
                         pInfo->installed_at[0] ? ")" : "");
        } else {
            xx_rt_printf("Installed:  %s\n", pInfo->has_database ? "no update record" : "none");
        }
    }
    if (status == CDIE_UPDATE_AVAILABLE) {
        xx_rt_printf("An update is available.%s%s\n", mode == MODE_CHECK ? " Run " : "", mode == MODE_CHECK ? X_APPLICATIONNAME " to install it." : "");
    } else if (status == CDIE_UPDATE_OK) {
        xx_rt_printf("%s.\n", pInfo->message[0] ? pInfo->message : "Done");
    }
    xx_rt_free(pSource);
}

/* ---------------------------------------------------------- interruption */

#if defined(_WIN32)
static BOOL WINAPI on_console_control(DWORD nEvent)
{
    if (nEvent == CTRL_C_EVENT || nEvent == CTRL_BREAK_EVENT) {
        g_bInterrupted = 1;
        return TRUE;
    }
    return FALSE;
}
#else
static void on_signal(int nSignal)
{
    (void)nSignal;
    g_bInterrupted = 1;
}
#endif

static int stderr_is_console(void)
{
#if defined(_WIN32)
    DWORD nMode;
    return GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &nMode) != 0;
#else
    return isatty(2);
#endif
}

/* Runs on this thread after each progress update and whenever the updater
 * polls for a stop: prints the download percentage and turns Ctrl+C into a
 * cooperative stop, so a database is never left half-replaced. */
static bool on_progress(const xx_pd_struct *pPd, void *pUser)
{
    int i;

    if (pUser) {
        for (i = XX_PD_LEVELS - 1; i >= 0; --i) {
            const xx_pd_record *pRecord = &pPd->records[i];
            if (pRecord->is_busy && pRecord->total) {
                int nPercent = (int)((pRecord->current >= pRecord->total ? 100u : (pRecord->current * 100u) / pRecord->total));
                if (nPercent != g_nLastPercent || xx_rt_strcmp(g_sLastStatus, pRecord->status)) {
                    g_nLastPercent = nPercent;
                    xx_rt_snprintf(g_sLastStatus, sizeof(g_sLastStatus), "%s", pRecord->status);
                    xx_rt_fprintf(xx_rt_stderr(), "\r%-24s %3d%%", pRecord->status, nPercent);
                    xx_rt_fflush(xx_rt_stderr());
                    g_bProgressShown = 1;
                }
                break;
            }
        }
    }
    return g_bInterrupted != 0;
}

/* ------------------------------------------------------------------ main */

static int take_value(int argc, char *argv[], int *pIndex, const char *pOption, char **ppTarget)
{
    if (*pIndex + 1 >= argc) {
        xx_rt_fprintf(xx_rt_stderr(), "%s: %s needs a value\n", X_APPLICATIONNAME, pOption);
        return 0;
    }
    xx_rt_free(*ppTarget);
    ++*pIndex;
    *ppTarget = (char *)xx_rt_malloc(xx_rt_strlen(argv[*pIndex]) + 1);
    if (!*ppTarget) return 0;
    xx_rt_memcpy(*ppTarget, argv[*pIndex], xx_rt_strlen(argv[*pIndex]) + 1);
    return 1;
}

static int is_option(const char *pArg, const char *pShort, const char *pLong)
{
    return (pShort && !xx_rt_strcmp(pArg, pShort)) || (pLong && !xx_rt_strcmp(pArg, pLong));
}

int x_main(int argc, char *argv[])
{
    cdie_update_config config;
    cdie_update_info info;
    cdie_update_status status = CDIE_UPDATE_OK;
    xx_pd_struct pd = xx_pd_init();
    xx_pd_observer previous;
    upd_mode mode = MODE_UPDATE;
    char *pSettingsFile = NULL;
    char *pTimeout = NULL;
    int bModeSet = 0;
    int bForce = 0;
    int bJson = 0;
    int bQuiet = 0;
    int bNoSettings = 0;
    int bHelp = 0;
    int bVersion = 0;
    int i;

    cdie_update_config_init(&config);
    cdie_update_info_init(&info);

    for (i = 1; i < argc && status == CDIE_UPDATE_OK; ++i) {
        const char *pArg = argv[i];

        if (is_option(pArg, "-h", "--help") || !xx_rt_strcmp(pArg, "-?")) bHelp = 1;
        else if (is_option(pArg, "-v", "--version")) bVersion = 1;
        else if (is_option(pArg, "-c", "--check") || is_option(pArg, "-u", "--update")) {
            upd_mode requested = (pArg[1] == 'c' || pArg[2] == 'c') ? MODE_CHECK : MODE_UPDATE;
            if (bModeSet && requested != mode) {
                xx_rt_fprintf(xx_rt_stderr(), "%s: --check and --update are mutually exclusive\n", X_APPLICATIONNAME);
                status = CDIE_UPDATE_ERROR_ARGUMENT;
            }
            mode = requested;
            bModeSet = 1;
        } else if (is_option(pArg, "-f", "--force")) bForce = 1;
        else if (is_option(pArg, "-j", "--json")) bJson = 1;
        else if (is_option(pArg, "-q", "--quiet")) bQuiet = 1;
        else if (is_option(pArg, NULL, "--nosettings")) bNoSettings = 1;
        else if (is_option(pArg, "-D", "--database")) {
            if (!take_value(argc, argv, &i, pArg, &config.main_path)) status = CDIE_UPDATE_ERROR_ARGUMENT;
        } else if (is_option(pArg, "-E", "--extradatabase")) {
            /* The extra database is gone; refused before any settings, lock
             * or network access, with its path consumed. */
            xx_rt_fprintf(xx_rt_stderr(), "%s: %s was removed: only the main database (-D) is updated\n", X_APPLICATIONNAME, pArg);
            if (i + 1 < argc) ++i;
            status = CDIE_UPDATE_ERROR_ARGUMENT;
        } else if (is_option(pArg, "-U", "--url")) {
            if (!take_value(argc, argv, &i, pArg, &config.url)) status = CDIE_UPDATE_ERROR_ARGUMENT;
        } else if (is_option(pArg, "-s", "--settings")) {
            if (!take_value(argc, argv, &i, pArg, &pSettingsFile)) status = CDIE_UPDATE_ERROR_ARGUMENT;
        }
        /* Undocumented, for the tests: a local server instead of GitHub. */
        else if (is_option(pArg, NULL, "--api-base")) {
            if (!take_value(argc, argv, &i, pArg, &config.api_base)) status = CDIE_UPDATE_ERROR_ARGUMENT;
        } else if (is_option(pArg, NULL, "--noproxy")) config.no_proxy = 1;
        else if (is_option(pArg, NULL, "--nouserfolder")) config.user_fallback = 0;
        else if (is_option(pArg, NULL, "--timeout")) {
            if (!take_value(argc, argv, &i, pArg, &pTimeout)) status = CDIE_UPDATE_ERROR_ARGUMENT;
            else config.timeout_ms = (uint32_t)xx_rt_strtoull(pTimeout, NULL, 10);
        } else {
            xx_rt_fprintf(xx_rt_stderr(), "%s: unknown option: %s\n", X_APPLICATIONNAME, pArg);
            status = CDIE_UPDATE_ERROR_ARGUMENT;
        }
    }

    if (status == CDIE_UPDATE_OK && (bHelp || bVersion)) {
        if (bHelp) print_help();
        else xx_rt_printf("%s v%s\n", X_APPLICATIONNAME, X_APPLICATIONVERSION);
        goto done;
    }
    if (status == CDIE_UPDATE_OK && bForce && mode == MODE_CHECK) {
        xx_rt_fprintf(xx_rt_stderr(), "%s: --force applies to an update, not to --check\n", X_APPLICATIONNAME);
        status = CDIE_UPDATE_ERROR_ARGUMENT;
    }
    if (status == CDIE_UPDATE_OK && pSettingsFile && bNoSettings) {
        xx_rt_fprintf(xx_rt_stderr(), "%s: --settings and --nosettings are mutually exclusive\n", X_APPLICATIONNAME);
        status = CDIE_UPDATE_ERROR_ARGUMENT;
    }
    if (status == CDIE_UPDATE_OK && !bNoSettings) {
        char sError[512];
        status = cdie_update_config_load_settings(&config, pSettingsFile, sError, sizeof(sError));
        if (status != CDIE_UPDATE_OK) xx_rt_fprintf(xx_rt_stderr(), "%s: %s\n", X_APPLICATIONNAME, sError);
    }
    if (status != CDIE_UPDATE_OK) goto done;

#if defined(_WIN32)
    SetConsoleCtrlHandler(on_console_control, TRUE);
#else
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#endif
    previous = xx_pd_set_observer(&pd, on_progress, (!bQuiet && !bJson && stderr_is_console()) ? (void *)&pd : NULL);

    if (mode == MODE_CHECK) status = cdie_update_check(&config, &info, &pd);
    else status = cdie_update_install(&config, &info, bForce, &pd);

    (void)xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    if (g_bProgressShown) {
        xx_rt_fprintf(xx_rt_stderr(), "\n");
        xx_rt_fflush(xx_rt_stderr());
    }

    if (bJson) {
        print_json(mode, status, &info);
    } else {
        if (!bQuiet) print_text(mode, status, &info, config.settings_location);
        if (status != CDIE_UPDATE_OK && status != CDIE_UPDATE_AVAILABLE) {
            xx_rt_fprintf(xx_rt_stderr(), "%s: %s\n", X_APPLICATIONNAME, info.message[0] ? info.message : cdie_update_status_name(status));
        }
    }

done:
    xx_rt_fflush(xx_rt_stdout());
    xx_rt_free(pSettingsFile);
    xx_rt_free(pTimeout);
    cdie_update_info_free(&info);
    cdie_update_config_free(&config);
    return (int)status;
}
