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

/* cdie_paths.h - where the signature databases live by default.
 *
 * diec, the GUIs and die-update all resolve an unconfigured database folder
 * the same way, so an update lands where the scanners look:
 *
 *   1. the per-user copy written by die-update when the shipped database
 *      could not be updated in place (never in a portable installation);
 *   2. beside the executable (the portable layout, and every package, whose
 *      executables sit next to their data);
 *   3. on macOS, the application bundle's Contents/Resources;
 *   4. the current directory.
 *
 * Portable means a regular file named PORTABLE (or portable) beside the
 * executable -- the same marker that makes the settings an INI file there.
 * A portable installation never reads or writes per-user locations.
 *
 * Everything here is CRT-free safe (xx_rt plus KERNEL32 on Windows), because
 * diec links it. Strings are xx_rt_malloc blocks, released with xx_rt_free.
 */

#ifndef CDIE_PATHS_H
#define CDIE_PATHS_H

/** @brief Nonzero when a PORTABLE (or portable) marker sits beside the executable. */
int cdie_is_portable(void);

/**
 * @brief The per-user folder that receives an update which cannot be written
 *        beside the executable.
 *
 *   Windows  %LOCALAPPDATA%\NTInfo\cdie
 *   macOS    ~/Library/Application Support/NTInfo/cdie
 *   others   $XDG_DATA_HOME/NTInfo/cdie, else ~/.local/share/NTInfo/cdie
 *
 * The environment variable CDIE_USER_DATA_DIR replaces it (tests, side-by-side
 * setups). NULL when no home folder is known. The folder need not exist.
 * For root (POSIX) the override is ignored and HOME must belong to root, so a
 * privileged run never works in a folder another account controls.
 */
char *cdie_user_data_dir(void);

/**
 * @brief "<user data dir>/<pName>" when that is an existing directory and the
 *        installation is not portable, else NULL.
 */
char *cdie_user_database_dir(const char *pName);

/**
 * @brief The default folder of database @p pName ("db", "db_custom"), in
 *        the order described above. Only existing directories count; NULL
 *        when none exists.
 *
 * The current-directory candidate is returned relative, as @p pName itself.
 */
char *cdie_find_database_dir(const char *pName);

/**
 * @brief The folder of the bundled translation catalogs (*.po), or NULL when
 *        no such folder exists. The first existing directory of:
 *
 *   <executable dir>/translations                 Windows, the build tree and
 *                                                 the Linux FHS lib/cdie tree
 *   <executable dir>/../Resources/translations    macOS application bundle
 *   <executable dir>/../share/cdie/translations   Linux portable/install tree
 *
 * Release with xx_rt_free.
 */
char *cdie_translations_dir(void);

#endif /* CDIE_PATHS_H */
