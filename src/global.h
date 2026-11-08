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

#ifndef GLOBAL_H
#define GLOBAL_H

#ifndef X_APPLICATIONDISPLAYNAME
#define X_APPLICATIONDISPLAYNAME "cdie"
#endif
#ifndef X_APPLICATIONNAME
#define X_APPLICATIONNAME        "cdie"
#endif
#ifndef X_APPLICATIONVERSION
#define X_APPLICATIONVERSION     "4.1.0"
#endif
#define X_ORGANIZATIONNAME       "NTInfo"
#define X_ORGANIZATIONDOMAIN     "ntinfo.biz"

/* Detect It Easy's settings store, which the GUI, the TUI and die-update
 * share with it: die.ini beside a portable executable, else QSettings'
 * native store for organization/application. On macOS Qt names that store
 * after the organization domain, reversed: biz.ntinfo.die. */
#ifdef __APPLE__
#define X_SETTINGS_ORGANIZATION  "biz.ntinfo"
#else
#define X_SETTINGS_ORGANIZATION  X_ORGANIZATIONNAME
#endif
#define X_SETTINGS_APPLICATION   "die"

/* Exit codes 0-4 mirror the original console tool. */
#define CR_SUCCESS            0
#define CR_CANNOTFINDFILE     1
#define CR_CANNOTOPENFILE     2
#define CR_CANNOTFINDDATABASE 3
#define CR_INVALIDPARAMETER   4
#define CR_CANNOTWRITEOUTPUT  5
/* --checkupdate and --update exit with die-update's own code; this one, the
 * shell's "command not found", when die-update cannot be started. */
#define CR_CANNOTRUNUPDATER   127

/* The program entry point. src/core/utils_entry.c calls this from either
 * main() or the CRT-free x_entry_point; src/console/main_console.c defines it.
 * Declared here so neither side has to assume a signature. */
int x_main(int nArgc, char *ppArgv[]);

#endif /* GLOBAL_H */
