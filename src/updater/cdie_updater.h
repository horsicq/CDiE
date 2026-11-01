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

/* cdie_updater.h - checks for and installs signature database updates.
 *
 * Shared by die-update (src/updater/main_updater.c) and the GUIs, which run
 * it in-process on a worker thread. The header names xxfclib types only, so a
 * frontend that includes it does not inherit xxnetlib's or xxgithub's
 * requirements.
 *
 * THE SOURCE. A GitHub release page
 * (https://github.com/<owner>/<repo>/releases/tag/<tag>, the default being
 * horsicq/Detect-It-Easy, tag current-database), a release download URL, or
 * any http(s) URL of a zip. The archive is db.zip (or the asset a download
 * URL names); its MD5 comes from the "<archive>.md5" asset or "<url>.md5" next
 * to it when one exists. db.zip holds db/ at its top; other top-level folders
 * (such as the extra database Detect It Easy still publishes) are ignored.
 *
 * WHAT "AN UPDATE IS AVAILABLE" MEANS. The installed database records the
 * archive it came from in db/die-update.ini (an .ini file is never loaded as a
 * signature). The remote archive is compared with that record by MD5, or --
 * for a release without an .md5 asset -- by the asset's id, size and upload
 * time. A database without a record, or a source that identifies itself by
 * neither, always has an update available.
 *
 * HOW IT INSTALLS. Under a lock in the database's parent folder: download into
 * a fresh staging folder beside the target, verify size and MD5, extract with
 * per-member CRC checks, load the extracted database once to prove it works,
 * record the stamp, then rename the old folder out and the new one in, with
 * rollback if a rename fails. db_custom is never touched, and neither is an
 * extra database folder left by an earlier version (never loaded, replaced
 * or deleted). When the target is the DEFAULT folder, the installation is not
 * portable and the folder cannot be written (a package under /usr, an
 * AppImage, a macOS bundle), the update goes to cdie_user_data_dir() instead,
 * which diec and the GUIs search first.
 *
 * Strings are xx_rt_malloc blocks (release with xx_rt_free), except where a
 * field is a fixed array.
 */

#ifndef CDIE_UPDATER_H
#define CDIE_UPDATER_H

#include <stddef.h>
#include <stdint.h>

#include <xxfclib/data/xx_pd.h>

#define CDIE_UPDATE_DEFAULT_URL "https://github.com/horsicq/Detect-It-Easy/releases/tag/current-database"
/* The previous compiled default. Settings written by Detect It Easy and by
 * earlier cdie builds store it although nobody chose it, and its release has
 * no .md5 asset, so it is read as "use the default". */
#define CDIE_UPDATE_LEGACY_URL "https://github.com/horsicq/Detect-It-Easy/releases/tag/db"
#define CDIE_UPDATE_ARCHIVE_NAME "db.zip"
#define CDIE_UPDATE_STAMP_NAME "die-update.ini"

/* Settings keys, Detect It Easy's own names. A stored Extra/Path key is
 * ignored: never read, written or deleted. */
#define CDIE_UPDATE_KEY_MAIN_PATH "Scan/Die/Database/Main/Path"
#define CDIE_UPDATE_KEY_URL "Scan/Die/Database/Main/UpdateUrl"

/* Results; die-update uses them as its exit codes. */
typedef enum cdie_update_status_e {
    CDIE_UPDATE_OK = 0,              /* up to date, or the update was installed */
    CDIE_UPDATE_AVAILABLE = 1,       /* check only: an update can be installed */
    CDIE_UPDATE_ERROR_NETWORK = 2,   /* the source could not be reached or read */
    CDIE_UPDATE_ERROR_DATABASE = 3,  /* the database folder cannot be written or swapped */
    CDIE_UPDATE_ERROR_ARGUMENT = 4,  /* bad option, URL or settings */
    CDIE_UPDATE_ERROR_VERIFY = 5,    /* size, MD5, zip or database validation failed */
    CDIE_UPDATE_ERROR_CANCELLED = 6, /* stopped through the xx_pd_struct */
    CDIE_UPDATE_ERROR_BUSY = 7       /* another update holds the lock */
} cdie_update_status;

typedef struct cdie_update_config_s {
    char *main_path;      /* "db" folder; NULL/"" = the default (see cdie_paths.h, else ./db) */
    char *url;           /* NULL/"" = CDIE_UPDATE_DEFAULT_URL */
    char *api_base;       /* NULL = derived from the URL; tests point it at a local server */
    char *settings_location; /* set by cdie_update_config_load_settings, for display */
    int no_proxy;         /* connect directly (tests on 127.0.0.1) */
    int user_fallback;    /* default 1: see "HOW IT INSTALLS" */
    uint32_t timeout_ms;  /* inactivity timeout, 0 = xxnetlib's default */
} cdie_update_config;

typedef struct cdie_update_info_s {
    char *source_url;     /* the URL that was used */
    char *archive_url;    /* where db.zip is downloaded from */
    char *archive_name;
    char *release_name;   /* GitHub release, when known */
    char *release_tag;
    int64_t remote_size;  /* bytes, -1 when unknown */
    int64_t remote_time;  /* asset upload time (Unix seconds), 0 when unknown */
    char remote_md5[33];  /* lower-case hex, "" when unknown */
    char remote_asset[96];/* "<id>:<size>:<time>" of a GitHub asset, "" when unknown */

    char *main_path;      /* resolved target folder (absolute) */
    int main_is_default;  /* main_path came from the default resolution */
    int has_database;     /* main_path exists */
    char installed_md5[33];
    char installed_asset[96];
    char installed_at[32];/* ISO 8601 from the stamp, "" when none */
    char *installed_source;

    int update_available;
    int installed;        /* install: the new database is in place */
    int used_user_folder; /* install: written to cdie_user_data_dir() */
    int signatures;       /* install: signatures in the new main database */
    char message[512];    /* what happened, or why it failed */
} cdie_update_info;

void cdie_update_config_init(cdie_update_config *pConfig);
void cdie_update_config_free(cdie_update_config *pConfig);

/**
 * @brief Fill the fields still unset in @p pConfig from the stored settings.
 *
 * @p pSettingsFile names an INI file to read. NULL selects Detect It Easy's
 * store: die.ini beside the executable for a portable installation (a
 * PORTABLE or portable marker there), else its native store, which the GUIs
 * share (X_SETTINGS_ORGANIZATION/X_SETTINGS_APPLICATION in global.h): the
 * registry HKCU\Software\NTInfo\die on Windows, ~/.config/NTInfo/die.conf on
 * Linux, the biz.ntinfo.die preferences on macOS (Qt names that store after
 * the organization domain, not NTInfo). A missing store is fine.
 *
 * Paths may start with "$data", which means the executable's folder.
 * @return CDIE_UPDATE_OK, or CDIE_UPDATE_ERROR_ARGUMENT with @p pError filled
 *         when the store exists but cannot be read.
 */
cdie_update_status cdie_update_config_load_settings(cdie_update_config *pConfig, const char *pSettingsFile, char *pError, size_t nErrorSize);

void cdie_update_info_init(cdie_update_info *pInfo);
void cdie_update_info_free(cdie_update_info *pInfo);

/** @brief Compare the source with the installed database. AVAILABLE or OK on success. */
cdie_update_status cdie_update_check(const cdie_update_config *pConfig, cdie_update_info *pInfo, xx_pd_struct *pPd);

/**
 * @brief Check, then install when an update is available (or always, with
 *        @p bForce). OK when the database is current afterwards.
 */
cdie_update_status cdie_update_install(const cdie_update_config *pConfig, cdie_update_info *pInfo, int bForce, xx_pd_struct *pPd);

const char *cdie_update_status_name(cdie_update_status status);

/** @brief @p pUrl without "user:password@", for printing. Release with xx_rt_free. */
char *cdie_update_display_url(const char *pUrl);

#endif /* CDIE_UPDATER_H */
