/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gui_update.h"
#include "gui_backend.h"
#include "app/cdie_app.h"
#include "app/cdie_translation.h"
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/threads/xx_threads.h>
#include <xxwidgets/xxwidgets_process.h>

#if defined(CDIE_HAVE_UPDATER)
#include "updater/cdie_updater.h"
#endif

#if defined(_WIN32)
#include <stdlib.h>
#include <wchar.h>
#else
#include <stdlib.h>
#endif

const char *gui_update_result_name(gui_update_result result)
{
    switch (result) {
        case GUI_UPDATE_UNAVAILABLE: return "unavailable";
        case GUI_UPDATE_CURRENT: return "current";
        case GUI_UPDATE_INSTALLED: return "installed";
        case GUI_UPDATE_RELOAD_FAILED: return "reload_failed";
        case GUI_UPDATE_FAILED: return "failed";
        case GUI_UPDATE_CANCELLED: return "cancelled";
    }
    return "unknown";
}

#if defined(CDIE_HAVE_UPDATER)

/* An environment override for the tests, as UTF-8; NULL when unset. */
static char *environment(const char *name)
{
#if defined(_WIN32)
    wchar_t wide_name[64];
    const wchar_t *value;
    char *converted, *result;
    size_t i;
    for (i = 0; name[i] && i + 1 < sizeof(wide_name) / sizeof(wide_name[0]); ++i) wide_name[i] = (wchar_t)name[i];
    wide_name[i] = 0;
    value = _wgetenv(wide_name);
    if (!value || !value[0]) return NULL;
    converted = xx_str_unicode_to_utf8(value);
    result = converted ? cdie_strdup(converted) : NULL;
    xx_str_free(converted);
    return result;
#else
    const char *value = getenv(name);
    return value && value[0] ? cdie_strdup(value) : NULL;
#endif
}

/* The progress dialog's first line says where the whole update is, as a step
 * of four; the updater's own levels below it name smaller steps. They cannot
 * be shown alone: "Downloading" is also the release query and the checksum,
 * and nothing is busy while connecting. So the step only moves forward, and a
 * transfer counts as the download once it is archive-sized. */
enum { UPDATE_CHECKING = 1, UPDATE_DOWNLOADING, UPDATE_VERIFYING, UPDATE_INSTALLING, UPDATE_STEPS = UPDATE_INSTALLING };
#define UPDATE_ARCHIVE_BYTES (256u * 1024u)

typedef struct update_job {
    cdie_update_config config;
    cdie_update_info info;
    cdie_update_status status;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int stop, done;
    int stage, checked; /* UPDATE_*; the new database's load check was seen */
    int install;        /* the worker installs; else it only checks */
} update_job;

/* On the worker, under the lock: every change of the levels passes here. */
static void update_stage(update_job *job, const xx_pd_struct *progress)
{
    int i, downloading = 0, verifying = 0, checking = 0;
    for (i = 0; i < XX_PD_LEVELS; ++i) {
        const xx_pd_record *record = &progress->records[i];
        if (!record->is_busy) continue;
        if (!xx_rt_strcmp(record->status, "Downloading") && (record->total ? record->total : record->current) >= UPDATE_ARCHIVE_BYTES) downloading = 1;
        else if (!xx_rt_strcmp(record->status, "Extracting")) verifying = 1;
        else if (!xx_rt_strcmp(record->status, "Checking the database")) verifying = checking = 1;
    }
    if (downloading && job->stage < UPDATE_DOWNLOADING) job->stage = UPDATE_DOWNLOADING;
    else if (!downloading && job->stage == UPDATE_DOWNLOADING) job->stage = UPDATE_VERIFYING; /* the archive's checksum */
    if (verifying && job->stage < UPDATE_VERIFYING) job->stage = UPDATE_VERIFYING; /* unpacked and loaded beside the old one */
    if (checking) job->checked = 1;
    else if (job->checked && job->stage < UPDATE_INSTALLING) job->stage = UPDATE_INSTALLING; /* it loads: swapped in */
}

static bool observe_update(const xx_pd_struct *progress, void *user)
{
    update_job *job = user;
    int stop;
    xx_mutex_lock(job->lock); job->snapshot = *progress; update_stage(job, progress); stop = job->stop; xx_mutex_unlock(job->lock);
    return stop != 0;
}

/* Copies text into a level's status, cut at a character boundary. */
static void update_status_copy(char *target, size_t size, const char *text)
{
    size_t length = xx_rt_strlen(text);
    if (length >= size) {
        length = size - 1;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    }
    xx_rt_memmove(target, text, length);
    target[length] = 0;
}

/* The updater's and the network's level texts, in the user's language. */
static const char *update_level_text(const char *status)
{
    static const char *const known[] = {CDIE_N_("Downloading"), CDIE_N_("Uploading"), CDIE_N_("Extracting"), CDIE_N_("Checking the database")};
    size_t i;
    for (i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
        if (!xx_rt_strcmp(status, known[i])) return CDIE_TR(known[i]);
    return status;
}

/* What the dialog shows: the step as "[n/4] <step>", then the levels. */
static void update_display(const xx_pd_struct *snapshot, int stage, xx_pd_struct *shown)
{
    static const char *const steps[UPDATE_STEPS] = {CDIE_N_("Checking for updates"), CDIE_N_("Downloading"), CDIE_N_("Verifying"), CDIE_N_("Installing")};
    int i;
    *shown = *snapshot;
    xx_rt_memset(shown->records, 0, sizeof(shown->records));
    if (stage < UPDATE_CHECKING || stage > UPDATE_STEPS) stage = UPDATE_CHECKING;
    shown->records[0].is_busy = true;
    shown->records[0].current = (uint64_t)stage;
    shown->records[0].total = UPDATE_STEPS;
    update_status_copy(shown->records[0].status, sizeof(shown->records[0].status), CDIE_TR(steps[stage - 1]));
    for (i = 0; i + 1 < XX_PD_LEVELS; ++i) {
        if (!snapshot->records[i].is_busy) continue;
        shown->records[i + 1] = snapshot->records[i];
        update_status_copy(shown->records[i + 1].status, sizeof(shown->records[i + 1].status), update_level_text(snapshot->records[i].status));
    }
}

static int update_worker(void *user)
{
    update_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_update, job);
    job->status = job->install ? cdie_update_install(&job->config, &job->info, 0, &job->work)
                               : cdie_update_check(&job->config, &job->info, &job->work);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock); job->snapshot = job->work; job->done = 1; xx_mutex_unlock(job->lock);
    return 0;
}

static xxwidgets_status update_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    update_job *job = user;
    xx_pd_struct snapshot;
    int stage;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    snapshot = job->snapshot; stage = job->stage; *finished = job->done;
    xx_mutex_unlock(job->lock);
    update_display(&snapshot, stage, progress);
    /* job->stop reaches the work only when the worker polls it; the network
     * watchdog's thread reads the stop flag itself, so a stalled request ends
     * now and not at the transfer timeout. */
    if (stop_requested) xx_pd_stop(&job->work);
    return XXWIDGETS_OK;
}

/* Why an update failed, for people; the updater's message is the detail. */
static const char *update_failure_text(cdie_update_status status)
{
    switch (status) {
        case CDIE_UPDATE_ERROR_NETWORK: return CDIE_TR("the update server cannot be reached, or it did not answer with a database");
        case CDIE_UPDATE_ERROR_DATABASE: return CDIE_TR("the database folder cannot be written");
        case CDIE_UPDATE_ERROR_ARGUMENT: return CDIE_TR("the update address or the settings are not valid");
        case CDIE_UPDATE_ERROR_VERIFY: return CDIE_TR("the downloaded database is damaged or incomplete");
        case CDIE_UPDATE_ERROR_BUSY: return CDIE_TR("another database update is running");
        default: return CDIE_TR("the update did not finish");
    }
}

/* The whole outcome of a failed update in a text dialog over owner. */
static void update_report(xxwidgets_widget *owner, gui_update_result result, cdie_update_status status, const char *detail, const char *line)
{
    char text[6144];
    if (!line) line = "";
    if (result == GUI_UPDATE_RELOAD_FAILED)
        xx_rt_snprintf(text, sizeof(text), "%s\n\n%s", line,
                       CDIE_TR("The previous database stays in use. Check the database folders under Options > Scan, or restart the program."));
    else if (status != CDIE_UPDATE_OK && detail && detail[0])
        cdie_translation_snprintf(text, sizeof(text), CDIE_N_("Database update failed: %s.\n\nDetails: %s\n\nThe database was not changed."),
                                  update_failure_text(status), detail);
    else xx_rt_snprintf(text, sizeof(text), "%s\n\n%s", line, CDIE_TR("The database was not changed."));
    (void)xxwidgets_text_dialog(owner, CDIE_TR("Update database"), text);
}

/* One run of the worker (a check, or the install) under the progress dialog;
 * without an owner (smoke runs) the join below simply waits for it. 0 when
 * the worker could not be started or did not finish, with message set. */
static int update_phase(update_job *job, xxwidgets_widget *owner, char *message, size_t message_size)
{
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status shown;
    xx_mutex_lock(job->lock);
    job->stop = job->done = job->checked = 0;
    job->work = xx_pd_init();
    job->snapshot = job->work;
    xx_mutex_unlock(job->lock);
    /* Loading the new database once to validate it runs scripts; give the
     * worker the scan threads' stack (macOS secondary threads get 512 KiB). */
    thread = xx_thread_create_ex(update_worker, job, 8u * 1024u * 1024u);
    if (!thread) {
        cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("Database update failed: the update could not be started."));
        return 0;
    }
    /* xxwidgets_process_dialog rejects a NULL owner. */
    shown = owner ? xxwidgets_process_dialog(owner, CDIE_TR("Updating Database"), &progress, update_progress, job) : XXWIDGETS_OK;
    if (shown != XXWIDGETS_OK) {
        xx_mutex_lock(job->lock); job->stop = 1; xx_mutex_unlock(job->lock);
        xx_pd_stop(&job->work);
    }
    if (!xx_thread_join(thread, NULL)) {
        cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("Database update failed: the update did not finish."));
        return 0;
    }
    return 1;
}

/* "YYYY-MM-DD HH:MM UTC" of a Unix time, "" when it is unknown. */
static void update_format_time(int64_t seconds, char *text, size_t size)
{
    int64_t days, rem, z, era, doe, yoe, doy, mp, day, month, year;
    text[0] = 0;
    if (seconds <= 0) return;
    days = seconds / 86400;
    rem = seconds % 86400;
    /* Civil date from days since 1970-01-01 (Howard Hinnant's algorithm). */
    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = yoe + era * 400 + (month <= 2);
    xx_rt_snprintf(text, size, "%04lld-%02lld-%02lld %02lld:%02lld UTC", (long long)year, (long long)month, (long long)day,
                   (long long)(rem / 3600), (long long)(rem % 3600 / 60));
}

/* The question before an available update is installed; 1 for yes. A dialog
 * that cannot be shown counts as no: nothing is installed unasked. */
static int update_ask(xxwidgets_widget *owner, const cdie_update_info *info)
{
    char text[1024], when[32], installed[64] = "", size[48] = "";
    const char *name = info->release_name && info->release_name[0] ? info->release_name :
                       info->release_tag && info->release_tag[0] ? info->release_tag :
                       info->archive_name && info->archive_name[0] ? info->archive_name : "";
    int accepted = 0;
    update_format_time(info->remote_time, when, sizeof(when));
    if (info->remote_size > 0)
        cdie_translation_snprintf(size, sizeof(size), CDIE_N_("Size: %.1f MB"), (double)info->remote_size / (1024.0 * 1024.0));
    if (info->installed_at[0])
        cdie_translation_snprintf(installed, sizeof(installed), CDIE_N_("Installed now: %s"), info->installed_at);
    xx_rt_snprintf(text, sizeof(text), "%s\n\n%s: %s\n%s: %s\n%s\n%s\n\n%s",
                   CDIE_TR("A database update is available."),
                   CDIE_TR("Update"), name,
                   CDIE_TR("Version/date"), when[0] ? when : CDIE_TR("unknown"),
                   size, installed,
                   CDIE_TR("Install it now?"));
    if (xxwidgets_question_dialog(owner, CDIE_TR("Update database"), text, CDIE_TR("Yes"), CDIE_TR("No"), &accepted) != XXWIDGETS_OK) return 0;
    return accepted;
}

/* The message box of a database that needs no update. */
static void update_no_updates(xxwidgets_widget *owner, const cdie_update_info *info)
{
    char text[512];
    if (info->installed_at[0])
        xx_rt_snprintf(text, sizeof(text), "%s\n\n%s\n%s: %s", CDIE_TR("No updates."), CDIE_TR("The database is up to date."),
                       CDIE_TR("Installed"), info->installed_at);
    else xx_rt_snprintf(text, sizeof(text), "%s\n\n%s", CDIE_TR("No updates."), CDIE_TR("The database is up to date."));
    (void)xxwidgets_text_dialog(owner, CDIE_TR("Update database"), text);
}

int gui_update_supported(void)
{
    return 1;
}

gui_update_result gui_update_run(xxwidgets_widget *owner, const char *url, char *message, size_t message_size)
{
    update_job job;
    gui_update_result result;
    char *override_url = environment("CDIE_UPDATE_URL");
    char *no_proxy = environment("CDIE_UPDATE_NOPROXY");
    /* Which slots are defaults is the backend's record, not a comparison with
     * the default lookup, which resolves elsewhere once a per-user copy exists. */
    int main_default = gui_backend_db_path_is_default(GUI_DB_MAIN);
    int custom_default = gui_backend_db_path_is_default(GUI_DB_CUSTOM);
    char *main_path = cdie_strdup(gui_backend_main_db_path());
    char *custom_path = cdie_strdup(gui_backend_custom_db_path());

    if (message && message_size) message[0] = 0;
    xx_rt_memset(&job, 0, sizeof(job));
    cdie_update_config_init(&job.config);
    cdie_update_info_init(&job.info);
    job.config.url = cdie_strdup(override_url ? override_url : (url ? url : ""));
    job.config.no_proxy = no_proxy != NULL;
    /* Only the main database is updated; the custom one is never touched. */
    if (!main_default) job.config.main_path = cdie_strdup(main_path);
    job.work = xx_pd_init();
    job.snapshot = job.work;
    job.stage = UPDATE_CHECKING;
    job.lock = xx_mutex_create();
    xx_rt_free(override_url);
    xx_rt_free(no_proxy);

    result = GUI_UPDATE_FAILED;
    if (!job.lock || !main_path || !custom_path) {
        cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("Database update failed: there is not enough memory."));
        goto done;
    }
    /* First a check: an up-to-date database gets its message box, an
     * available update is installed only after the question (a smoke run
     * without an owner installs without asking, as it has no dialogs). */
    if (!update_phase(&job, owner, message, message_size)) goto done;
    if (job.status == CDIE_UPDATE_AVAILABLE) {
        if (owner && !update_ask(owner, &job.info)) {
            result = GUI_UPDATE_CANCELLED;
            cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("Database update cancelled."));
            goto done;
        }
        job.install = 1;
        job.stage = UPDATE_DOWNLOADING;
        if (!update_phase(&job, owner, message, message_size)) goto done;
    }

    if (job.status == CDIE_UPDATE_ERROR_CANCELLED) {
        result = GUI_UPDATE_CANCELLED;
        cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("Database update cancelled."));
    } else if (job.status == CDIE_UPDATE_OK) {
        /* Default slots are re-resolved: this update, or an earlier diec
         * --update or die-update, may have created the per-user copy the
         * default lookup now finds. Chosen folders are reloaded where they
         * are. An up-to-date database needs a reload only in the first case. */
        int moved = gui_backend_db_path_moved(GUI_DB_MAIN) || gui_backend_db_path_moved(GUI_DB_CUSTOM);
        if (!job.info.installed && !moved) {
            result = GUI_UPDATE_CURRENT;
            cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("The database is up to date."));
        } else if (gui_backend_set_db_paths(main_default ? "" : main_path, custom_default ? "" : custom_path)) {
            result = job.info.installed ? GUI_UPDATE_INSTALLED : GUI_UPDATE_CURRENT;
            if (job.info.installed)
                cdie_translation_snprintf(message, message_size, CDIE_N_("Database updated: %d signatures."), gui_backend_signature_count());
            else
                cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("The database is up to date."));
        } else {
            result = GUI_UPDATE_RELOAD_FAILED;
            cdie_translation_snprintf(message, message_size, CDIE_N_("The database was updated in %s but could not be loaded."),
                                      job.info.main_path ? job.info.main_path : "");
        }
    } else {
        result = GUI_UPDATE_FAILED;
        cdie_translation_snprintf(message, message_size, CDIE_N_("Database update failed: %s"),
                                  job.info.message[0] ? job.info.message : update_failure_text(job.status));
    }

done:
    /* The status bar has room for one clipped line; a failure is also shown
     * whole, over the window that asked for the update. */
    if (owner && (result == GUI_UPDATE_FAILED || result == GUI_UPDATE_RELOAD_FAILED))
        update_report(owner, result, job.status, job.info.message, message);
    else if (owner && result == GUI_UPDATE_CURRENT && !job.install)
        update_no_updates(owner, &job.info);
    if (job.lock) xx_mutex_destroy(job.lock);
    cdie_update_info_free(&job.info);
    cdie_update_config_free(&job.config);
    xx_rt_free(main_path);
    xx_rt_free(custom_path);
    return result;
}

#else /* !CDIE_HAVE_UPDATER */

int gui_update_supported(void)
{
    return 0;
}

gui_update_result gui_update_run(xxwidgets_widget *owner, const char *url, char *message, size_t message_size)
{
    (void)owner;
    (void)url;
    cdie_translation_snprintf(message, message_size, "%s", CDIE_TR("This build cannot update the database."));
    return GUI_UPDATE_UNAVAILABLE;
}

#endif
