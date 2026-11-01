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

/* cdie_spawn.h - run another program in the foreground and wait for it.
 *
 * diec hands --checkupdate and --update to die-update this way. The child
 * shares diec's console, standard handles, working directory and environment,
 * so its output and exit code are diec's.
 *
 * CRT-free safe on Windows (KERNEL32 only), because diec links it; POSIX
 * builds use posix_spawn. Strings are xx_rt_malloc blocks, released with
 * xx_rt_free.
 */

#ifndef CDIE_SPAWN_H
#define CDIE_SPAWN_H

#include <stddef.h>

/**
 * @brief Run @p pPath with the arguments @p ppArgv and wait for it to exit.
 *
 * @p ppArgv is UTF-8, argv[0] first, NULL-terminated. On Windows it is quoted
 * into a command line that the Microsoft argv rules (core/utils_entry.c) split
 * back into the same strings. While the child runs, Ctrl+C (SIGINT and SIGQUIT
 * on POSIX) is left to the child, so this process does not return before it.
 *
 * @return 1 with *pExitCode set when the program ran -- on POSIX, a child
 *         killed by a signal reports 128 + the signal number; 0 when it could
 *         not be started, with @p pError saying why.
 */
int cdie_spawn_wait(const char *pPath, const char *const ppArgv[], int *pExitCode, char *pError, size_t nErrorSize);

/**
 * @brief The die-update executable diec runs: $CDIE_UPDATER when set (a full
 *        path, for tests and unusual layouts), else die-update[.exe] beside
 *        this executable. NULL only when out of memory.
 */
char *cdie_spawn_updater_path(void);

#endif /* CDIE_SPAWN_H */
