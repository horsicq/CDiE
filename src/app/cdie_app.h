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

/* cdie_app.h - the small part of cdie that xxfclib does not provide.
 *
 * The scan engine, the format parsers, the JavaScript interpreter and the
 * runtime all live in xxfclib now. What is left here is what an application
 * needs and a library has no business providing: finding its own executable,
 * walking the command line's targets, and rendering a path the way the host
 * prints it.
 *
 * These carry a cdie_ prefix rather than cdie's old cd_ one. xxfclib exports
 * its own cd_* set from the engine's compatibility layer, and although none of
 * those names collides with these four today, two independent cd_ families in
 * one link is a trap waiting to be sprung.
 *
 * ONE ALLOCATOR. Everything cdie allocates, and everything the engine hands
 * back, is an xx_rt_malloc block released with xx_rt_free. That is not a free
 * choice: die_engine_format_text() and friends return cd_malloc blocks, and
 * cd_malloc is xx_rt_malloc. xxfclib's other public string and path helpers
 * (xx_str_dup, xx_fs_path_dir, ...) allocate through xx_mem_alloc instead,
 * whose documented release is xx_mem_free. The two happen to be the same heap
 * on both platforms today, but xxfclib's own headers call that a coincidence
 * rather than a contract -- so cdie does not build on it, and every pointer
 * here is freed with xx_rt_free.
 */

#ifndef CDIE_APP_H
#define CDIE_APP_H

#include <xxfclib/list/xx_list.h>

/** @brief True when something exists at @p pPath. */
int cdie_path_exists(const char *pPath);

/** @brief xx_rt_malloc'd copy of @p pText, or NULL. Release with xx_rt_free. */
char *cdie_strdup(const char *pText);

/**
 * @brief The directory holding the running executable, for locating the
 *        signature databases beside it.
 *
 * Falls back to "." when the path cannot be determined, which is what cdie
 * has always done -- a relative lookup is better than refusing to start.
 */
char *cdie_app_dir(void);

/**
 * @brief The path as the host writes it: on Windows, forward slashes become
 *        backslashes.
 *
 * Only used for display. The engine is given the original path, so this never
 * affects what is opened -- only what is printed above each result block.
 */
char *cdie_path_native(const char *pPath);

/**
 * @brief Append every file at or under @p pPath to @p pList as `char *`.
 *
 * @p pList must have `elem_size == sizeof(char *)`.
 *
 * The ORDER here is part of cdie's printed output, because -r prints one block
 * per file in exactly this sequence. Entries are walked in plain name order and
 * a subdirectory is descended at the point its own name falls -- so a file
 * named "b" comes after everything inside a directory named "a". That is not
 * what xx_fs_find_files does: it sorts directories ahead of files and so emits
 * every subtree before the current level's files. The two differ for any tree
 * holding both, which is why this walk is here rather than delegated.
 */
void cdie_find_files(const char *pPath, xx_list_t *pList, int bRecursive);

#endif /* CDIE_APP_H */
