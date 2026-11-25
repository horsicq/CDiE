/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_RESULT_TREE_H
#define CDIE_GUI_RESULT_TREE_H
#include "gui_backend.h"
#include <xxfclib/scan/xx_scan.h>
/* Emits parent-before-child nodes. Text/category are borrowed for the callback. */
int gui_result_tree_emit(const xx_scan_result *result, int hide_unknown, gui_backend_tree_fn callback, void *user);
#endif
