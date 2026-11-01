/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_PREFERENCES_H
#define CDIE_GUI_PREFERENCES_H
#include <stdint.h>
#define GUI_PREFERENCE_PATH_MAX 4096
#define GUI_PREFERENCE_URL_MAX 2048

typedef struct gui_appearance_preferences {
    int stay_on_top, single_application;
    char style[64], qss[GUI_PREFERENCE_PATH_MAX], language[GUI_PREFERENCE_PATH_MAX];
} gui_appearance_preferences;
typedef struct gui_file_preferences {
    int save_last_directory, save_recent_files, save_backup, context_menu, set_environment;
    char last_directory[GUI_PREFERENCE_PATH_MAX], recent_files[16384];
} gui_file_preferences;
typedef struct gui_database_preferences {
    char main_path[GUI_PREFERENCE_PATH_MAX], custom_path[GUI_PREFERENCE_PATH_MAX];
    char peid_path[GUI_PREFERENCE_PATH_MAX], yara_path[GUI_PREFERENCE_PATH_MAX];
    char main_update_url[GUI_PREFERENCE_URL_MAX], peid_update_url[GUI_PREFERENCE_URL_MAX], yara_update_url[GUI_PREFERENCE_URL_MAX];
} gui_database_preferences;
typedef struct gui_signatures_preferences { char path[GUI_PREFERENCE_PATH_MAX]; } gui_signatures_preferences;
typedef struct gui_online_preferences { char virustotal_api_key[GUI_PREFERENCE_PATH_MAX]; } gui_online_preferences;
typedef struct gui_info_preferences {
    char path[GUI_PREFERENCE_PATH_MAX], update_url[GUI_PREFERENCE_URL_MAX];
} gui_info_preferences;
#endif
