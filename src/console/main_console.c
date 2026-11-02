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

/* main_console.c - command line front end of cdie. */

#include "../global.h"
#include <xxfclib/die_engine/die_engine.h>
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/global/xx_global.h>
#include <xxfclib/list/xx_list.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/terminal/xx_terminal.h>
#include <xxfclib/scan/xx_scan_console.h>

#include "../app/cdie_app.h"
#include "../app/cdie_file_info.h"
#include "../app/cdie_entropy.h"
#include "../app/cdie_hash.h"
#include "../app/cdie_paths.h"
#include "../app/cdie_spawn.h"
#include "../app/cdie_strings.h"

static int g_bStdoutFailed = 0;

/* Format once, then compare the requested byte count with the stream write.
 * This catches a short WriteFile/fwrite as well as an outright failure. */
static int cdie_printf(const char *pFormat, ...)
{
    char sStack[1024];
    char *pText = sStack;
    XX_RT_VA_LIST args;
    XX_RT_VA_LIST copy;
    int nSize = 0;
    int nFormatted = 0;
    int nWritten = 0;

    if (g_bStdoutFailed) return -1;

    XX_RT_VA_START(args, pFormat);
    XX_RT_VA_COPY(copy, args);
    nSize = xx_rt_vsnprintf(sStack, sizeof(sStack), pFormat, copy);
    XX_RT_VA_END(copy);

    if (nSize < 0) {
        XX_RT_VA_END(args);
        g_bStdoutFailed = 1;
        return -1;
    }

    if ((size_t)nSize >= sizeof(sStack)) {
        pText = (char *)xx_rt_malloc((size_t)nSize + 1);
        if (pText == NULL) {
            XX_RT_VA_END(args);
            g_bStdoutFailed = 1;
            return -1;
        }
        nFormatted = xx_rt_vsnprintf(pText, (size_t)nSize + 1, pFormat, args);
    } else {
        nFormatted = nSize;
    }
    XX_RT_VA_END(args);

    if (nFormatted != nSize) {
        if (pText != sStack) xx_rt_free(pText);
        g_bStdoutFailed = 1;
        return -1;
    }

    nWritten = xx_rt_fprintf(xx_rt_stdout(), "%s", pText);
    if (pText != sStack) xx_rt_free(pText);
    if (nWritten != nSize) {
        g_bStdoutFailed = 1;
        return -1;
    }

    return nWritten;
}

static int cdie_finish_result(int nResult)
{
    if (xx_rt_fflush(xx_rt_stdout()) != 0) g_bStdoutFailed = 1;
    if ((nResult == CR_SUCCESS) && g_bStdoutFailed) return CR_CANNOTWRITEOUTPUT;
    return nResult;
}

static int select_output_format(cdie_output_format *pCurrent,
                                cdie_output_format requested,
                                const char *pOption)
{
    if (*pCurrent != CDIE_OUTPUT_TEXT) {
        xx_rt_fprintf(xx_rt_stderr(),
                      *pCurrent == requested
                          ? "Output format specified more than once: %s\n"
                          : "Output formats are mutually exclusive: %s\n",
                      pOption);
        return 0;
    }

    *pCurrent = requested;
    return 1;
}

static xxfc_status_t cdie_structured_sink(void *pContext,
                                          const char *pData,
                                          size_t nSize)
{
    int nWritten;

    (void)pContext;
    if (nSize > 0x7fffffffU) {
        g_bStdoutFailed = 1;
        return XXFC_ERR_IO;
    }
    nWritten = cdie_printf("%.*s", (int)nSize, pData);
    return (nWritten == (int)nSize) ? XXFC_OK : XXFC_ERR_IO;
}

static void print_json_string(const char *pText)
{
    xx_terminal_writer writer;

    xx_terminal_writer_init(&writer, cdie_structured_sink, NULL);
    if (xx_terminal_writer_json_string(&writer, pText) != XXFC_OK) {
        g_bStdoutFailed = 1;
    }
}

/* Every XML value emitted by this frontend is an attribute value. */
static void print_xml_string(const char *pText)
{
    xx_terminal_writer writer;

    xx_terminal_writer_init(&writer, cdie_structured_sink, NULL);
    if (xx_terminal_writer_xml_attribute(&writer, pText) != XXFC_OK) {
        g_bStdoutFailed = 1;
    }
}

/* Range of --buffersize and --filebuffersize. 64-bit builds take the GUI's
 * Optimization choices, 1 KiB to 1 GiB. The library allocates a full file
 * buffer for every buffered file and decoder, whatever the input size, and a
 * failed allocation changes results without an error; a 32-bit diec has 2 GiB
 * of address space (it is not linked /LARGEADDRESSAWARE), so there the cap is
 * the library's own 32-bit memory budget, XX_DEFAULT_FILE_MEMORY_LIMIT. */
#define CDIE_BUFFER_SIZE_MIN ((uint64_t)1024)
#if !defined(SIZE_MAX)
#error SIZE_MAX (stdint.h) is needed to choose the buffer size range
#elif SIZE_MAX > 0xFFFFFFFFu
#define CDIE_BUFFER_SIZE_MAX ((uint64_t)1024 * 1024 * 1024)
#define CDIE_BUFFER_SIZE_MAX_TEXT "1G"
#else
#define CDIE_BUFFER_SIZE_MAX ((uint64_t)64 * 1024 * 1024)
#define CDIE_BUFFER_SIZE_MAX_TEXT "64M"
#endif

static void print_help(void)
{
    cdie_printf("%s v%s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
    cdie_printf("Detect It Easy console engine, C port\n");
    cdie_printf("Copyright(C) 2026 hors<horsicq@gmail.com> Web: http://ntinfo.biz\n\n");
    cdie_printf("Usage: %s [options] target\n\n", X_APPLICATIONNAME);
    cdie_printf("Options:\n");
    cdie_printf("  -h, --help                Displays this help.\n");
    cdie_printf("  -v, --version             Displays version information.\n");
    cdie_printf("  -r, --recursivescan       Scan directories recursively, with resources and overlays.\n");
    cdie_printf("  -a, --alltypes            Scan applicable parent file types.\n");
    cdie_printf("  -O, --overlayscan         Scan trailing overlays.\n");
    cdie_printf("  -R, --resourcesscan       Scan PE/ELF resource payloads.\n");
    cdie_printf("  -A, --archivesscan        Scan ZIP/JAR/APK/IPA members and nested archives.\n");
    cdie_printf("  -d, --deepscan            Deep scan.\n");
    cdie_printf("  -u, --heuristicscan       Heuristic scan.\n");
    cdie_printf("  -g, --aggressivescan      Aggressive scan.\n");
    cdie_printf("  -b, --verbose             Verbose output.\n");
    cdie_printf("  -f, --format              Format the result strings (the default).\n");
    cdie_printf("      --noformat            Do not format the result strings.\n");
    cdie_printf("      --color, --nocolor    Enable/disable terminal colors (default: on).\n");
#if defined(_M_IX86) || (defined(_M_X64) && !defined(_M_ARM64EC)) || defined(__i386__) || \
    (defined(__x86_64__) && !defined(_M_ARM64EC))
    cdie_printf("      --sse2, --nosse2      Enable/disable SSE2 acceleration.\n");
    cdie_printf("      --sse42, --nosse42    Enable/disable SSE4.2 acceleration.\n");
    cdie_printf("      --avx2, --noavx2      Enable/disable AVX2 acceleration.\n");
    cdie_printf("      --aes, --noaes        Enable/disable AES-NI acceleration.\n");
    cdie_printf("      --sha, --nosha        Enable/disable SHA-NI for SHA-1/SHA-256.\n");
    cdie_printf("      --vpclmulqdq, --novpclmulqdq\n");
    cdie_printf("                            Enable/disable VPCLMULQDQ for CRC64.\n");
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC) || defined(__arm__) || defined(__aarch64__)
    cdie_printf("      --neon, --noneon      Enable/disable Arm NEON acceleration.\n");
    cdie_printf("      --armcrc32, --noarmcrc32\n");
    cdie_printf("                            Enable/disable Arm CRC32 acceleration.\n");
    cdie_printf("      --armcrypto, --noarmcrypto\n");
    cdie_printf("                            Enable/disable Arm AES/SHA acceleration.\n");
    cdie_printf("      --armdotprod, --noarmdotprod\n");
    cdie_printf("      --armpmull, --noarmpmull\n");
    cdie_printf("                            Enable/disable Arm dot-product acceleration.\n");
#endif
#if defined(_M_IX86) || defined(_M_X64) || defined(_M_ARM) || defined(_M_ARM64) || defined(_M_ARM64EC) || \
    defined(__i386__) || defined(__x86_64__) || defined(__arm__) || defined(__aarch64__)
    cdie_printf("                            Default: on when supported; unsupported keys are ignored.\n");
#endif
    cdie_printf("      --buffersize <size>   Read buffer size (default %uK): how much --hash,\n",
                (unsigned)(XX_DEFAULT_BUFFER_SIZE / 1024));
    cdie_printf("                            --entropy, --strings and the scan's hash,\n");
    cdie_printf("                            checksum and search loops read at a time.\n");
    cdie_printf("      --filebuffersize <size>\n");
    cdie_printf("                            File buffer size (default %uK): archive members\n",
                (unsigned)(XX_DEFAULT_FILE_BUFFER_SIZE / 1024));
    cdie_printf("                            smaller than this are decoded in memory, others in\n");
    cdie_printf("                            a temporary file. Each open file and decoder takes\n");
    cdie_printf("                            a buffer this large, even for a small input.\n");
    cdie_printf("                            <size> for both: 1K.." CDIE_BUFFER_SIZE_MAX_TEXT ", in bytes or with a K, M\n");
    cdie_printf("                            or G suffix (4096, 64K, 16M); the last one wins.\n");
    cdie_printf("  -U, --hideunknown         Hide unknown results.\n");
    cdie_printf("  -M, --messages            Show engine messages.\n");
    cdie_printf("  -l, --profiling           Show profiling information.\n");
    cdie_printf("  -j, --json                Result as JSON.\n");
    cdie_printf("  -x, --xml                 Result as XML.\n");
    cdie_printf("  -c, --csv                 Result as CSV.\n");
    cdie_printf("  -t, --tsv                 Result as TSV.\n");
    cdie_printf("  -p, --plaintext           Result as plain text.\n");
    cdie_printf("      --format <layout>     text, json, xml, csv or tsv (as Qt diec).\n");
    cdie_printf("  -D, --database <path>     Main database path.\n");
    cdie_printf("  -C, --customdatabase <p>  Custom database path.\n");
    cdie_printf("      --includetags <list>  Run signatures matching any ';'-separated tag.\n");
    cdie_printf("      --excludetags <list>  Skip signatures matching any ';'-separated tag.\n");
    cdie_printf("  -s, --showdatabase        Show the database information.\n");
    cdie_printf("      --strings             Extract ASCII/UTF-8 and UTF-16 strings.\n");
    cdie_printf("      --strings-min <n>     Minimum decoded characters (1..1024, default 4).\n");
    cdie_printf("      --strings-encoding <e> ascii, utf8, utf16le, utf16be or all (default).\n");
    cdie_printf("      --hash                Show CRC32, MD5, SHA1 and SHA256 hashes.\n");
    cdie_printf("                            Select one with -S Hash#SHA256.\n");
    cdie_printf("  -e, --entropy             Show Shannon entropy of the file and its regions.\n");
    cdie_printf("  -i, --info                Show native PE/ELF/Mach-O DataStructs and fields.\n");
    cdie_printf("  -S, --struct <name>       Show NAME[#FIELD], optionally FORMAT::NAME.\n");
    cdie_printf("  -w, --showstructs         List DataStructs (target optional).\n");
    cdie_printf("      --createtar <path>    Create TAR archive from database.\n");
    cdie_printf("      --createprecompiledtar <p> Create precompiled bytecode TAR archive.\n");
    cdie_printf("      --checkupdate         Check for a database update (exit code 1: available).\n");
    cdie_printf("      --update              Download and install a database update.\n");
    cdie_printf("                            Both run die-update, passing -D and -j on.\n");
    cdie_printf("\n");
    cdie_printf("Target:\n");
    cdie_printf("  target                    The file or directory to open.\n");
}

static void print_structured_information(cdie_output_format outputFormat,
                                         int bIncludeUsage)
{
    if (outputFormat == CDIE_OUTPUT_JSON) {
        cdie_printf("{\"tool\":");
        print_json_string(X_APPLICATIONDISPLAYNAME);
        cdie_printf(",\"version\":");
        print_json_string(X_APPLICATIONVERSION);
        if (bIncludeUsage) {
            cdie_printf(",\"usage\":");
            print_json_string("[options] target");
        }
        cdie_printf("}\n");
    } else if (outputFormat == CDIE_OUTPUT_XML) {
        cdie_printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<cdie version=\"");
        print_xml_string(X_APPLICATIONVERSION);
        if (bIncludeUsage) {
            cdie_printf("\"><usage>[options] target</usage></cdie>\n");
        } else {
            cdie_printf("\"/>\n");
        }
    }
}

/* -E/--extradatabase named the extra database, which cdie no longer has (the
 * main and custom databases remain, as in Detect It Easy). The option is
 * refused rather than ignored, so a script that relied on it fails instead of
 * quietly scanning with other rules. */
static void report_removed_option(const char *pOption)
{
    xx_rt_fprintf(xx_rt_stderr(), "%s was removed: cdie uses only the main (-D) and custom (-C) databases\n", pOption);
}

/* --buffersize and --filebuffersize: a byte count from 1 KiB to
 * CDIE_BUFFER_SIZE_MAX, optionally followed by K, M or G (KiB, MiB, GiB;
 * either case). 0 is refused, as xx_set_buffer_size() and
 * xx_set_file_buffer_size() would take it for the default. The value is
 * checked digit by digit, so no input can overflow. @p pText is NULL when
 * the option is the last argument; *pSize changes only on success. */
static int parse_buffer_size(const char *pOption, const char *pText, size_t *pSize)
{
    const char *p = pText ? pText : "";
    uint64_t nValue = 0;
    int bValid = (*p >= '0') && (*p <= '9');

    for (; bValid && (*p >= '0') && (*p <= '9'); p++) {
        nValue = nValue * 10 + (uint64_t)(*p - '0');
        if (nValue > CDIE_BUFFER_SIZE_MAX) bValid = 0;
    }
    if (bValid && *p) {
        if ((*p == 'K') || (*p == 'k')) nValue <<= 10;
        else if ((*p == 'M') || (*p == 'm')) nValue <<= 20;
        else if ((*p == 'G') || (*p == 'g')) nValue <<= 30;
        else bValid = 0;
        if (p[1]) bValid = 0;
    }
    if (bValid && ((nValue < CDIE_BUFFER_SIZE_MIN) || (nValue > CDIE_BUFFER_SIZE_MAX))) bValid = 0;

    if (!bValid) {
        if (pText) {
            xx_rt_fprintf(xx_rt_stderr(), "%s requires a size from 1K to " CDIE_BUFFER_SIZE_MAX_TEXT
                          " (bytes, or with a K, M or G suffix), not '%s'\n", pOption, pText);
        } else {
            xx_rt_fprintf(xx_rt_stderr(), "%s requires a size from 1K to " CDIE_BUFFER_SIZE_MAX_TEXT
                          " (bytes, or with a K, M or G suffix)\n", pOption);
        }
        return 0;
    }

    *pSize = (size_t)nValue;
    return 1;
}

/* Frees the cdie_strdup'd elements and then the list itself. xx_list_t stores
 * elements by value, so a list of strings holds char* and each slot has to be
 * dereferenced through. */
static void free_string_vector(xx_list_t *pList)
{
    size_t i = 0;
    size_t nCount = xx_list_count(pList);

    for (i = 0; i < nCount; i++) {
        xx_rt_free(*(char **)xx_list_at(pList, i));
    }

    xx_list_cleanup(pList);
}

/* The element at @p i of a list of strings. */
static const char *string_at(const xx_list_t *pList, size_t i)
{
    return *(const char *const *)xx_list_at(pList, i);
}

static char *resolve_database_path(const char *pPath, const char *pDefaultName)
{
    if (pPath && pPath[0]) {
        return cdie_path_native(pPath);
    }

    {
        /* The copy die-update installs for this user when the shipped folder
         * cannot be written (never in a portable installation). */
        char *pUser = cdie_user_database_dir(pDefaultName);

        if (pUser) {
            char *pNative = cdie_path_native(pUser);
            xx_rt_free(pUser);
            return pNative;
        }
    }

    {
        /* $data/<name>: next to the executable, then the current directory. */
        char *pAppDir = cdie_app_dir();
        char *pCandidate = xx_fs_path_join(pAppDir, pDefaultName);

        xx_rt_free(pAppDir);

        if (xx_fs_is_dir(pCandidate) || xx_fs_is_file(pCandidate)) {
            char *pNative = cdie_path_native(pCandidate);
            xx_rt_free(pCandidate);
            return pNative;
        }

        {
            char sTar[512];
            xx_rt_snprintf(sTar, sizeof(sTar), "%s.tar", pCandidate);
            if (xx_fs_is_file(sTar)) {
                xx_rt_free(pCandidate);
                return cdie_path_native(sTar);
            }
        }

        xx_rt_free(pCandidate);
    }

    if (xx_fs_is_dir(pDefaultName) || xx_fs_is_file(pDefaultName)) {
        return cdie_path_native(pDefaultName);
    }

    {
        char sTar[512];
        xx_rt_snprintf(sTar, sizeof(sTar), "%s.tar", pDefaultName);
        if (xx_fs_is_file(sTar)) {
            return cdie_path_native(sTar);
        }
    }

    return cdie_path_native(pDefaultName);
}

/* --checkupdate and --update hand over to die-update beside this executable
 * (CDIE_UPDATER, a full path, overrides that for the tests). Its exit code is
 * diec's: 0 up to date or updated, 1 an update is available, 2 and up an
 * error, CR_CANNOTRUNUPDATER when it cannot be started. */
static int run_updater(int bCheck, const char *pDatabase, int bJson)
{
    const char *apArgs[6];
    char sError[512];
    char *pUpdater = cdie_spawn_updater_path();
    int nArgs = 0;
    int nExitCode = CR_CANNOTRUNUPDATER;

    if (!pUpdater || !xx_fs_is_file(pUpdater)) {
        xx_rt_fprintf(xx_rt_stderr(), "Cannot find the database updater: %s\n", pUpdater ? pUpdater : "die-update");
        xx_rt_free(pUpdater);
        return CR_CANNOTRUNUPDATER;
    }

    apArgs[nArgs++] = pUpdater;
    if (bCheck) apArgs[nArgs++] = "--check";
    if (pDatabase && pDatabase[0]) {
        apArgs[nArgs++] = "--database";
        apArgs[nArgs++] = pDatabase;
    }
    if (bJson) apArgs[nArgs++] = "--json";
    apArgs[nArgs] = NULL;

    /* The updater writes to the same streams, after anything still buffered. */
    if (xx_rt_fflush(xx_rt_stdout()) != 0) g_bStdoutFailed = 1;
    xx_rt_fflush(xx_rt_stderr());
    if (!cdie_spawn_wait(pUpdater, apArgs, &nExitCode, sError, sizeof(sError))) {
        xx_rt_fprintf(xx_rt_stderr(), "Cannot run %s: %s\n", pUpdater, sError);
        nExitCode = CR_CANNOTRUNUPDATER;
    }
    xx_rt_free(pUpdater);
    return nExitCode;
}

static void show_database(DBase *pDb, xx_scan_options *pOptions,
                          cdie_output_format outputFormat)
{
    /* Same list, in the same order, as XScanEngine::getSignatureStates(). */
    static const XFileType pTypes[] = {XFT_BINARY,  XFT_COM,     XFT_MSDOS, XFT_NE,     XFT_LE,       XFT_LX,     XFT_PE,      XFT_ELF,
                                       XFT_MACHO,   XFT_PDF,     XFT_CFBF,  XFT_IMAGE,  XFT_JPEG,     XFT_PNG,    XFT_RAR,     XFT_ISO9660,
                                       XFT_ARCHIVE, XFT_ZIP,     XFT_JAR,   XFT_APK,    XFT_IPA,      XFT_DEX,    XFT_NPM,     XFT_MACHOFAT,
                                       XFT_AMIGAHUNK, XFT_ATARIST, XFT_DOS16M, XFT_DOS4G};
    size_t i = 0;
    int bFirst = 1;

    if (outputFormat == CDIE_OUTPUT_JSON) {
        cdie_printf("  \"database\": {\"main\":");
        print_json_string(pOptions->main_database_path);
        cdie_printf(",\"custom\":");
        print_json_string(pOptions->custom_database_path);
        cdie_printf(",\"signatures\":[");
    } else if (outputFormat == CDIE_OUTPUT_XML) {
        cdie_printf("  <database main=\"");
        print_xml_string(pOptions->main_database_path);
        cdie_printf("\" custom=\"");
        print_xml_string(pOptions->custom_database_path);
        cdie_printf("\">\n");
    } else {
        cdie_printf("Main database: %s\n", pOptions->main_database_path ? pOptions->main_database_path : "");
        cdie_printf("Custom database: %s\n", pOptions->custom_database_path ? pOptions->custom_database_path : "");
    }

    for (i = 0; i < sizeof(pTypes) / sizeof(pTypes[0]); i++) {
        int nCount = db_count_for_type(pDb, pTypes[i]);

        if (nCount > 0) {
            if (outputFormat == CDIE_OUTPUT_JSON) {
                if (!bFirst) cdie_printf(",");
                cdie_printf("{\"type\":");
                print_json_string(xft_to_string(pTypes[i]));
                cdie_printf(",\"count\":%d}", nCount);
                bFirst = 0;
            } else if (outputFormat == CDIE_OUTPUT_XML) {
                cdie_printf("    <signature type=\"");
                print_xml_string(xft_to_string(pTypes[i]));
                cdie_printf("\" count=\"%d\"/>\n", nCount);
            } else {
                cdie_printf("\t%s: %d\n", xft_to_string(pTypes[i]), nCount);
            }
        }
    }

    if (outputFormat == CDIE_OUTPUT_JSON) {
        cdie_printf("]}");
    } else if (outputFormat == CDIE_OUTPUT_XML) {
        cdie_printf("  </database>\n");
    }
}

/* File analysis bypasses signature loading and keeps multi-file JSON/XML
 * well formed even when one target fails. The native collector is read-only. */
static int show_file_analysis(xx_list_t *targets, int recursive, const char *selector,
    int inventory, int entropy, int hash, const cdie_strings_options *strings, cdie_output_format format)
{
    xx_list_t files;
    size_t i, written = 0;
    int result = CR_SUCCESS, wrapper;
    xx_list_init(&files, sizeof(char *), NULL);
    if (!xx_list_count(targets)) {
        char *text = NULL, *error = NULL;
        if (!inventory) {
            xx_rt_fprintf(xx_rt_stderr(), "File analysis requires a target\n");
            result = CR_INVALIDPARAMETER;
        } else if (cdie_file_info(NULL, NULL, 1, format, &text, &error)) cdie_printf("%s", text);
        else {
            xx_rt_fprintf(xx_rt_stderr(), "%s\n", error ? error : "Cannot list DataStructs");
            result = CR_CANNOTOPENFILE;
        }
        xx_rt_free(text); xx_rt_free(error);
        free_string_vector(&files);
        return result;
    }
    for (i = 0; i < xx_list_count(targets); ++i) {
        const char *target = string_at(targets, i);
        if (cdie_path_exists(target)) cdie_find_files(target, &files, recursive);
        else {
            xx_rt_fprintf(xx_rt_stderr(), "Cannot find: %s\n", target);
            result = CR_CANNOTFINDFILE;
        }
    }
    wrapper = xx_list_count(&files) != 1;
    if (wrapper && format == CDIE_OUTPUT_JSON) cdie_printf("{\"files\":[");
    else if (wrapper && format == CDIE_OUTPUT_XML) cdie_printf("<files>\n");
    for (i = 0; i < xx_list_count(&files); ++i) {
        const char *path = string_at(&files, i);
        char *text = NULL, *error = NULL;
        if (strings ? cdie_strings_report(path, strings, format, &text, &error) :
            hash ? cdie_hash_report(path, selector, format, &text, &error) :
            entropy ? cdie_entropy_report(path, format, &text, &error) :
            cdie_file_info(path, selector, inventory, format, &text, &error)) {
            const char *output = text;
            if (wrapper && format == CDIE_OUTPUT_JSON && written) cdie_printf(",");
            if (!entropy && !hash && written && (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV)) {
                const char *header_end = xx_rt_strchr(output, '\n');
                if (header_end) output = header_end + 1;
            }
            cdie_printf("%s", output);
            ++written;
        } else {
            xx_rt_fprintf(xx_rt_stderr(), "%s: %s\n", path, error ? error : "Cannot analyze file");
            result = CR_CANNOTOPENFILE;
        }
        xx_rt_free(text); xx_rt_free(error);
    }
    if (wrapper && format == CDIE_OUTPUT_JSON) cdie_printf("]}\n");
    else if (wrapper && format == CDIE_OUTPUT_XML) cdie_printf("</files>\n");
    free_string_vector(&files);
    return result;
}

/* Entry point. utils_entry.c calls this: on a no-CRT Windows build from the
 * real linker entry, otherwise from main(). */
int x_main(int argc, char *argv[])
{
    xx_scan_options options;
    DBase db;
    xx_list_t vecTargets;
    xx_list_t vecFiles;
    int nResult = CR_SUCCESS;
    int i = 0;
    int bShowDatabase = 0;
    int bShowHelp = 0;
    int bShowVersion = 0;
    int bShowInfo = 0, bShowStructs = 0, bEntropy = 0, bHash = 0, bStrings = 0, bStringsConfig = 0;
    int bCheckUpdate = 0, bUpdate = 0;
    size_t nBufferSize = 0, nFileBufferSize = 0; /* 0: not given, keep the default */
    cdie_strings_options stringsOptions = cdie_strings_defaults();
    int bDatabaseLoaded = 0;
    int bEndOfOptions = 0;
    int bStructuredWrapper = 0;
    int nStructuredFiles = 0;
    cdie_output_format outputFormat = CDIE_OUTPUT_TEXT;
    const char *pCreateTarPath = NULL;
    const char *pCreateBcTarPath = NULL;
    const char *pStruct = NULL;

    g_bStdoutFailed = 0;
    xx_scan_options_init(&options);
    /* diec formats the result strings by default, and printing exactly what
     * diec prints is the acceptance test, so the console front end turns the
     * option on before the command line is parsed. -f/--format stays an
     * explicit enable, --noformat is the opt-out. */
    options.format_result = 1;
    xx_rt_memset(&db, 0, sizeof(db));
    xx_list_init(&vecTargets, sizeof(char *), NULL);
    xx_list_init(&vecFiles, sizeof(char *), NULL);

    for (i = 1; i < argc; i++) {
        const char *pArg = argv[i];

        if (!bEndOfOptions && (xx_rt_strcmp(pArg, "--") == 0)) {
            bEndOfOptions = 1;
            continue;
        }

        if (!bEndOfOptions && xx_terminal_handle_option(pArg)) {
            continue;
        }

        if (!bEndOfOptions && pArg[0] == '-' && pArg[1] == '-') {
            /* Long options starting with -- */
            if (xx_rt_strcmp(pArg, "--help") == 0) {
                bShowHelp = 1;
            } else if (xx_rt_strcmp(pArg, "--version") == 0) {
                bShowVersion = 1;
            } else if (xx_rt_strcmp(pArg, "--recursivescan") == 0) {
                options.recursive_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--alltypes") == 0) {
                options.all_types_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--overlayscan") == 0) {
                options.overlay_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--resourcesscan") == 0) {
                options.resources_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--archivesscan") == 0) {
                options.archives_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--deepscan") == 0) {
                options.deep_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--heuristicscan") == 0) {
                options.heuristic_scan = 1;
            } else if ((xx_rt_strcmp(pArg, "--aggressivecscan") == 0) || (xx_rt_strcmp(pArg, "--aggressivescan") == 0)) {
                options.aggressive_scan = 1;
            } else if (xx_rt_strcmp(pArg, "--verbose") == 0) {
                options.verbose = 1;
            } else if ((xx_rt_strcmp(pArg, "--format") == 0) && (i + 1 < argc) &&
                       (!xx_rt_strcmp(argv[i + 1], "text") || !xx_rt_strcmp(argv[i + 1], "json") || !xx_rt_strcmp(argv[i + 1], "xml") ||
                        !xx_rt_strcmp(argv[i + 1], "csv") || !xx_rt_strcmp(argv[i + 1], "tsv"))) {
                /* Qt diec's --format <layout>: text keeps the default output. */
                const char *pLayout = argv[++i];
                cdie_output_format requested = !xx_rt_strcmp(pLayout, "json") ? CDIE_OUTPUT_JSON :
                                               !xx_rt_strcmp(pLayout, "xml") ? CDIE_OUTPUT_XML :
                                               !xx_rt_strcmp(pLayout, "csv") ? CDIE_OUTPUT_CSV :
                                               !xx_rt_strcmp(pLayout, "tsv") ? CDIE_OUTPUT_TSV : CDIE_OUTPUT_TEXT;
                if (requested == CDIE_OUTPUT_TEXT) {
                    options.format_result = 1;
                } else if (select_output_format(&outputFormat, requested, pArg)) {
                    options.result_as_json = requested == CDIE_OUTPUT_JSON;
                    options.result_as_xml = requested == CDIE_OUTPUT_XML;
                    options.result_as_csv = requested == CDIE_OUTPUT_CSV;
                    options.result_as_tsv = requested == CDIE_OUTPUT_TSV;
                } else {
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if (xx_rt_strcmp(pArg, "--format") == 0) {
                options.format_result = 1;
            } else if (xx_rt_strcmp(pArg, "--noformat") == 0) {
                options.format_result = 0;
            } else if (xx_rt_strcmp(pArg, "--hideunknown") == 0) {
                options.hide_unknown = 1;
            } else if (xx_rt_strcmp(pArg, "--messages") == 0) {
                options.show_messages = 1;
            } else if (xx_rt_strcmp(pArg, "--profiling") == 0) {
                options.profiling = 1;
            } else if (xx_rt_strcmp(pArg, "--json") == 0) {
                if (select_output_format(&outputFormat, CDIE_OUTPUT_JSON, pArg)) {
                    options.result_as_json = 1;
                } else {
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if (xx_rt_strcmp(pArg, "--xml") == 0) {
                if (select_output_format(&outputFormat, CDIE_OUTPUT_XML, pArg)) {
                    options.result_as_xml = 1;
                } else {
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if (xx_rt_strcmp(pArg, "--csv") == 0) {
                if (select_output_format(&outputFormat, CDIE_OUTPUT_CSV, pArg)) {
                    options.result_as_csv = 1;
                } else {
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if (xx_rt_strcmp(pArg, "--tsv") == 0) {
                if (select_output_format(&outputFormat, CDIE_OUTPUT_TSV, pArg)) {
                    options.result_as_tsv = 1;
                } else {
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if ((xx_rt_strcmp(pArg, "--plaintext") == 0)) {
                if (select_output_format(&outputFormat, CDIE_OUTPUT_PLAIN, pArg)) {
                    options.result_as_plain_text = 1;
                } else {
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if (xx_rt_strcmp(pArg, "--showdatabase") == 0) {
                bShowDatabase = 1;
            } else if (xx_rt_strcmp(pArg, "--strings") == 0) {
                bStrings = 1;
            } else if (xx_rt_strcmp(pArg, "--strings-min") == 0) {
                unsigned value = 0;
                const char *digits = i + 1 < argc ? argv[++i] : "";
                bStringsConfig = 1;
                if (!*digits) nResult = CR_INVALIDPARAMETER;
                for (; *digits; ++digits) {
                    if (*digits < '0' || *digits > '9' || value > 1024) { nResult = CR_INVALIDPARAMETER; break; }
                    value = value * 10 + (unsigned)(*digits - '0');
                }
                if (value < 1 || value > 1024) nResult = CR_INVALIDPARAMETER;
                if (nResult != CR_SUCCESS) xx_rt_fprintf(xx_rt_stderr(), "--strings-min requires 1..1024\n");
                stringsOptions.minimum_length = value;
            } else if (xx_rt_strcmp(pArg, "--strings-encoding") == 0) {
                bStringsConfig = 1;
                stringsOptions.encodings = cdie_strings_parse_encoding(i + 1 < argc ? argv[++i] : NULL);
                if (!stringsOptions.encodings) {
                    xx_rt_fprintf(xx_rt_stderr(), "Unknown strings encoding (ascii, utf8, utf16le, utf16be, all)\n");
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if (xx_rt_strcmp(pArg, "--hash") == 0) {
                bHash = 1;
            } else if (xx_rt_strcmp(pArg, "--entropy") == 0) {
                bEntropy = 1;
            } else if (xx_rt_strcmp(pArg, "--info") == 0) {
                bShowInfo = 1;
            } else if (xx_rt_strcmp(pArg, "--showstructs") == 0) {
                bShowStructs = 1;
            } else if (xx_rt_strcmp(pArg, "--struct") == 0) {
                if (i + 1 < argc && argv[i + 1][0]) pStruct = argv[++i];
                else {
                    xx_rt_fprintf(xx_rt_stderr(), "Option --struct requires a structure name\n");
                    nResult = CR_INVALIDPARAMETER;
                }
            } else if ((xx_rt_strcmp(pArg, "--database") == 0) && (i + 1 < argc)) {
                xx_rt_free(options.main_database_path);
                options.main_database_path = cdie_strdup(argv[++i]);
            } else if (xx_rt_strcmp(pArg, "--extradatabase") == 0) {
                /* Its path is consumed, so it is not taken for a target. */
                report_removed_option(pArg);
                if (i + 1 < argc) ++i;
                nResult = CR_INVALIDPARAMETER;
            } else if ((xx_rt_strcmp(pArg, "--customdatabase") == 0) && (i + 1 < argc)) {
                xx_rt_free(options.custom_database_path);
                options.custom_database_path = cdie_strdup(argv[++i]);
            } else if (((xx_rt_strcmp(pArg, "--includetags") == 0) ||
                        (xx_rt_strcmp(pArg, "--include-tags") == 0)) && (i + 1 < argc)) {
                options.include_tags = argv[++i];
            } else if (((xx_rt_strcmp(pArg, "--excludetags") == 0) ||
                        (xx_rt_strcmp(pArg, "--exclude-tags") == 0)) && (i + 1 < argc)) {
                options.exclude_tags = argv[++i];
            } else if ((xx_rt_strcmp(pArg, "--createtar") == 0) && (i + 1 < argc)) {
                pCreateTarPath = argv[++i];
            } else if ((xx_rt_strcmp(pArg, "--createprecompiledtar") == 0) && (i + 1 < argc)) {
                pCreateBcTarPath = argv[++i];
            } else if (xx_rt_strcmp(pArg, "--buffersize") == 0) {
                /* The next argument is the size, consumed even when invalid. */
                if (!parse_buffer_size(pArg, i + 1 < argc ? argv[++i] : NULL, &nBufferSize)) nResult = CR_INVALIDPARAMETER;
            } else if (xx_rt_strcmp(pArg, "--filebuffersize") == 0) {
                if (!parse_buffer_size(pArg, i + 1 < argc ? argv[++i] : NULL, &nFileBufferSize)) nResult = CR_INVALIDPARAMETER;
            } else if (xx_rt_strcmp(pArg, "--checkupdate") == 0) {
                bCheckUpdate = 1;
            } else if (xx_rt_strcmp(pArg, "--update") == 0) {
                bUpdate = 1;
            } else {
                xx_rt_fprintf(xx_rt_stderr(), "Unknown option: %s\n", pArg);
                nResult = CR_INVALIDPARAMETER;
            }
        } else if (!bEndOfOptions && pArg[0] == '-' && pArg[1] != '\0') {
            /* Short options (supports clustering like -rdu, -rd, etc.) */
            if (xx_rt_strcmp(pArg, "-he") == 0) {
                options.heuristic_scan = 1;
            } else if (xx_rt_strcmp(pArg, "-hu") == 0) {
                options.hide_unknown = 1;
            } else {
                size_t k = 0;
                for (k = 1; pArg[k] != '\0'; k++) {
                    char c = pArg[k];
                    switch (c) {
                        case 'h':
                        case '?':
                            bShowHelp = 1;
                            break;

                        case 'v':
                            bShowVersion = 1;
                            break;

                        case 'r': options.recursive_scan = 1; break;
                        case 'A': options.archives_scan = 1; break;
                        case 'O': options.overlay_scan = 1; break;
                        case 'R': options.resources_scan = 1; break;
                        case 'a': options.all_types_scan = 1; break;
                        case 'd': options.deep_scan = 1; break;
                        case 'u': options.heuristic_scan = 1; break;
                        case 'g': options.aggressive_scan = 1; break;
                        case 'b':
                        case 'V': options.verbose = 1; break;
                        case 'f': options.format_result = 1; break;
                        case 'U': options.hide_unknown = 1; break;
                        case 'M':
                        case 'm': options.show_messages = 1; break;
                        case 'l': options.profiling = 1; break;
                        case 'j':
                            if (select_output_format(&outputFormat, CDIE_OUTPUT_JSON, "-j")) options.result_as_json = 1;
                            else nResult = CR_INVALIDPARAMETER;
                            break;
                        case 'x':
                            if (select_output_format(&outputFormat, CDIE_OUTPUT_XML, "-x")) options.result_as_xml = 1;
                            else nResult = CR_INVALIDPARAMETER;
                            break;
                        case 'c':
                            if (select_output_format(&outputFormat, CDIE_OUTPUT_CSV, "-c")) options.result_as_csv = 1;
                            else nResult = CR_INVALIDPARAMETER;
                            break;
                        case 't':
                            if (select_output_format(&outputFormat, CDIE_OUTPUT_TSV, "-t")) options.result_as_tsv = 1;
                            else nResult = CR_INVALIDPARAMETER;
                            break;
                        case 'p':
                        case 'P':
                            if (select_output_format(&outputFormat, CDIE_OUTPUT_PLAIN, "-p")) options.result_as_plain_text = 1;
                            else nResult = CR_INVALIDPARAMETER;
                            break;
                        case 's': bShowDatabase = 1; break;
                        case 'e': bEntropy = 1; break;
                        case 'i': bShowInfo = 1; break;
                        case 'w': bShowStructs = 1; break;
                        case 'S': {
                            if (pArg[k + 1]) {
                                pStruct = &pArg[k + 1];
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc && argv[i + 1][0]) pStruct = argv[++i];
                            else {
                                xx_rt_fprintf(xx_rt_stderr(), "Option -S requires a structure name\n");
                                nResult = CR_INVALIDPARAMETER;
                            }
                            break;
                        }

                        case 'D': {
                            const char *pVal = NULL;
                            if (pArg[k + 1] != '\0') {
                                pVal = &pArg[k + 1];
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc) {
                                pVal = argv[++i];
                            }
                            if (pVal) {
                                xx_rt_free(options.main_database_path);
                                options.main_database_path = cdie_strdup(pVal);
                            } else {
                                xx_rt_fprintf(xx_rt_stderr(), "Option -D requires an argument\n");
                                nResult = CR_INVALIDPARAMETER;
                            }
                            break;
                        }

                        case 'E': {
                            /* -E<path> or -E <path>: the path is consumed. */
                            if (pArg[k + 1] != '\0') {
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc) {
                                ++i;
                            }
                            report_removed_option("-E");
                            nResult = CR_INVALIDPARAMETER;
                            break;
                        }

                        case 'C': {
                            const char *pVal = NULL;
                            if (pArg[k + 1] != '\0') {
                                pVal = &pArg[k + 1];
                                k = xx_rt_strlen(pArg) - 1;
                            } else if (i + 1 < argc) {
                                pVal = argv[++i];
                            }
                            if (pVal) {
                                xx_rt_free(options.custom_database_path);
                                options.custom_database_path = cdie_strdup(pVal);
                            } else {
                                xx_rt_fprintf(xx_rt_stderr(), "Option -C requires an argument\n");
                                nResult = CR_INVALIDPARAMETER;
                            }
                            break;
                        }

                        default:
                            xx_rt_fprintf(xx_rt_stderr(), "Unknown option: -%c\n", c);
                            nResult = CR_INVALIDPARAMETER;
                            break;
                    }
                    if (nResult != CR_SUCCESS) {
                        break;
                    }
                }
            }
        } else {
            char *pCopy = cdie_strdup(pArg);

            if (pCopy) {
                xx_list_append(&vecTargets, &pCopy);
            }
        }
    }

    if (nResult != CR_SUCCESS) {
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return cdie_finish_result(nResult);
    }

    /* Before any engine work, so every mode below uses them. */
    if (nBufferSize) xx_set_buffer_size(nBufferSize);
    if (nFileBufferSize) xx_set_file_buffer_size(nFileBufferSize);

    /* As in XScanEngine::scanProcess, a recursive scan also scans the
     * resources and the overlay of every file it walks. */
    if (options.recursive_scan) {
        options.resources_scan = 1;
        options.overlay_scan = 1;
    }

    if (bShowHelp || bShowVersion) {
        if (bShowHelp && bShowVersion) {
            xx_rt_fprintf(xx_rt_stderr(), "--help and --version are mutually exclusive\n");
            nResult = CR_INVALIDPARAMETER;
        } else if ((outputFormat == CDIE_OUTPUT_JSON) ||
                   (outputFormat == CDIE_OUTPUT_XML)) {
            print_structured_information(outputFormat, bShowHelp);
        } else if (bShowHelp) {
            print_help();
        } else {
            cdie_printf("%s %s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return cdie_finish_result(nResult);
    }

    /* Database update: a mode of its own, like --createtar. The database is
     * not loaded; die-update reads the one it replaces. */
    if (bCheckUpdate || bUpdate) {
        if (bCheckUpdate && bUpdate) {
            xx_rt_fprintf(xx_rt_stderr(), "--checkupdate and --update are mutually exclusive\n");
            nResult = CR_INVALIDPARAMETER;
        } else if (xx_list_count(&vecTargets) || bStrings || bStringsConfig || bHash || bEntropy || bShowInfo ||
                   bShowStructs || pStruct || bShowDatabase || pCreateTarPath || pCreateBcTarPath) {
            xx_rt_fprintf(xx_rt_stderr(), "--checkupdate and --update take no target and no other mode\n");
            nResult = CR_INVALIDPARAMETER;
        } else if ((outputFormat != CDIE_OUTPUT_TEXT) && (outputFormat != CDIE_OUTPUT_PLAIN) &&
                   (outputFormat != CDIE_OUTPUT_JSON)) {
            xx_rt_fprintf(xx_rt_stderr(), "--checkupdate and --update report as text or JSON only\n");
            nResult = CR_INVALIDPARAMETER;
        } else {
            nResult = run_updater(bCheckUpdate, options.main_database_path, outputFormat == CDIE_OUTPUT_JSON);
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return cdie_finish_result(nResult);
    }

    if (pStruct && !xx_rt_strcmp(pStruct, "Strings")) { bStrings = 1; pStruct = NULL; }
    /* Hash is a byte operation for every format, separate from DataStructs. */
    if (pStruct && (!xx_rt_strcmp(pStruct, "Hash") || !xx_rt_strncmp(pStruct, "Hash#", 5))) {
        bHash = 1; pStruct = pStruct[4] == '#' ? pStruct + 5 : NULL;
        if (pStruct && !*pStruct) {
            xx_rt_fprintf(xx_rt_stderr(), "Hash# requires an algorithm\n");
            nResult = CR_INVALIDPARAMETER;
        }
    }
    if (bStrings || bStringsConfig || bHash || bEntropy || bShowInfo || bShowStructs || pStruct) {
        if (nResult != CR_SUCCESS || (bStringsConfig && !bStrings) ||
            (bStrings && (bHash || bEntropy || bShowInfo || bShowStructs || pStruct)) || (bHash && (bEntropy || bShowInfo || bShowStructs || cdie_hash_algorithm_index(pStruct) == -2)) ||
            (bEntropy && (bShowInfo || bShowStructs || pStruct)) || bShowDatabase ||
            pCreateTarPath || pCreateBcTarPath || (bShowStructs && (bShowInfo || pStruct))) {
            xx_rt_fprintf(xx_rt_stderr(), "Choose one analysis mode and a supported selector (hashes: CRC32, MD5, SHA1, SHA256)\n");
            nResult = CR_INVALIDPARAMETER;
        } else nResult = show_file_analysis(&vecTargets, options.recursive_scan, pStruct, bShowStructs, bEntropy, bHash, bStrings ? &stringsOptions : NULL, outputFormat);
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return cdie_finish_result(nResult);
    }

    {
        /* Only the main and custom databases: an extra database folder left
         * by an earlier version is never looked up or loaded. */
        char *pMain = resolve_database_path(options.main_database_path, "db");
        char *pCustom = resolve_database_path(options.custom_database_path, "db_custom");

        xx_rt_free(options.main_database_path);
        xx_rt_free(options.custom_database_path);
        options.main_database_path = pMain;
        options.custom_database_path = pCustom;
    }

    if (pCreateTarPath) {
        if (!db_create_tar(options.main_database_path, pCreateTarPath)) {
            xx_rt_fprintf(xx_rt_stderr(), "Failed to create TAR database from '%s' to '%s'\n",
                          options.main_database_path, pCreateTarPath);
            nResult = CR_CANNOTFINDDATABASE;
        } else if ((outputFormat != CDIE_OUTPUT_JSON) && (outputFormat != CDIE_OUTPUT_XML)) {
            cdie_printf("Successfully created TAR database: %s\n", pCreateTarPath);
        }
        if (outputFormat == CDIE_OUTPUT_JSON) {
            cdie_printf("{\"operation\":\"create-tar\",\"path\":");
            print_json_string(pCreateTarPath);
            cdie_printf(",\"success\":%s}\n", nResult == CR_SUCCESS ? "true" : "false");
        } else if (outputFormat == CDIE_OUTPUT_XML) {
            cdie_printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<create-tar path=\"");
            print_xml_string(pCreateTarPath);
            cdie_printf("\" success=\"%s\"/>\n", nResult == CR_SUCCESS ? "true" : "false");
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return cdie_finish_result(nResult);
    }

    if (pCreateBcTarPath) {
        if (!db_create_tar_precompiled(options.main_database_path, pCreateBcTarPath)) {
            xx_rt_fprintf(xx_rt_stderr(), "Failed to create precompiled TAR database from '%s' to '%s'\n",
                          options.main_database_path, pCreateBcTarPath);
            nResult = CR_CANNOTFINDDATABASE;
        } else if ((outputFormat != CDIE_OUTPUT_JSON) && (outputFormat != CDIE_OUTPUT_XML)) {
            cdie_printf("Successfully created precompiled TAR database: %s\n", pCreateBcTarPath);
        }
        if (outputFormat == CDIE_OUTPUT_JSON) {
            cdie_printf("{\"operation\":\"create-precompiled-tar\",\"path\":");
            print_json_string(pCreateBcTarPath);
            cdie_printf(",\"success\":%s}\n", nResult == CR_SUCCESS ? "true" : "false");
        } else if (outputFormat == CDIE_OUTPUT_XML) {
            cdie_printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<create-precompiled-tar path=\"");
            print_xml_string(pCreateBcTarPath);
            cdie_printf("\" success=\"%s\"/>\n", nResult == CR_SUCCESS ? "true" : "false");
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);
        return cdie_finish_result(nResult);
    }

    if ((xx_list_count(&vecTargets) == 0) && (!bShowDatabase)) {
        if ((outputFormat == CDIE_OUTPUT_JSON) ||
            (outputFormat == CDIE_OUTPUT_XML)) {
            print_structured_information(outputFormat, 1);
        } else {
            print_help();
        }
        xx_scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);

        /* An unknown option already set nResult; do not report success. */
        return cdie_finish_result(nResult);
    }

    bDatabaseLoaded = db_load(&db, options.main_database_path, DB_MAIN);

    if (options.use_custom_database) {
        db_load(&db, options.custom_database_path, DB_CUSTOM);
    }

    db_sort(&db);

    if (!bDatabaseLoaded) {
        xx_rt_fprintf(xx_rt_stderr(), "Cannot load database: %s\n", options.main_database_path);
        nResult = CR_CANNOTFINDDATABASE;
    }

    for (i = 0; i < (int)xx_list_count(&vecTargets); i++) {
        const char *pTarget = string_at(&vecTargets, (size_t)i);

        if (cdie_path_exists(pTarget)) {
            cdie_find_files(pTarget, &vecFiles, options.recursive_scan);
        } else {
            if ((outputFormat == CDIE_OUTPUT_JSON) || (outputFormat == CDIE_OUTPUT_XML)) {
                xx_rt_fprintf(xx_rt_stderr(), "Cannot find: %s\n", pTarget);
            } else {
                cdie_printf("Cannot find: %s\n", pTarget);
            }
            nResult = CR_CANNOTFINDFILE;
        }
    }

    bStructuredWrapper = ((outputFormat == CDIE_OUTPUT_JSON) ||
                          (outputFormat == CDIE_OUTPUT_XML)) &&
                         (bShowDatabase || (xx_list_count(&vecFiles) != 1));
    if (bStructuredWrapper) {
        if (outputFormat == CDIE_OUTPUT_JSON) {
            cdie_printf("{\n");
            if (bShowDatabase) {
                show_database(&db, &options, outputFormat);
                cdie_printf(",\n");
            }
            cdie_printf("  \"files\": [");
        } else {
            cdie_printf("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<cdie>\n");
            if (bShowDatabase) show_database(&db, &options, outputFormat);
            cdie_printf("  <files>\n");
        }
    } else if (bShowDatabase) {
        show_database(&db, &options, outputFormat);
    }

    for (i = 0; i < (int)xx_list_count(&vecFiles); i++) {
        const char *pFileName = string_at(&vecFiles, (size_t)i);
        xx_scan_result result;
        char *pOutput = NULL;

        if ((xx_list_count(&vecFiles) > 1) && !bStructuredWrapper) {
            char *pNative = cdie_path_native(pFileName);

            cdie_printf("%s:\n", pNative);
            xx_rt_free(pNative);
        }

        if (!die_engine_scan_file(pFileName, &db, &options, &result)) {
            if ((outputFormat == CDIE_OUTPUT_JSON) || (outputFormat == CDIE_OUTPUT_XML)) {
                xx_rt_fprintf(xx_rt_stderr(), "Cannot open: %s\n", pFileName);
            } else {
                cdie_printf("Cannot open: %s\n", pFileName);
            }
            nResult = CR_CANNOTOPENFILE;
            /* Failed expansion can leave an owned diagnostic result. */
            {
                int error_index;
                for (error_index = 0; error_index < result.nErrorCount; ++error_index)
                    xx_rt_fprintf(xx_rt_stderr(), "%s\n", result.ppErrors[error_index]);
            }
            xx_scan_result_free(&result);
            continue;
        }

        if (options.result_as_json) {
            pOutput = xx_scan_format_json(&result, &options);
        } else if (options.result_as_xml) {
            pOutput = xx_scan_format_xml(&result, &options);
        } else if (options.result_as_csv) {
            /* diec separates CSV fields with a semicolon, not a comma. */
            pOutput = xx_scan_format_csv(&result, &options, ';');
        } else if (options.result_as_tsv) {
            pOutput = xx_scan_format_csv(&result, &options, '\t');
        } else if (outputFormat != CDIE_OUTPUT_TEXT) {
            pOutput = xx_scan_format_text(&result, &options);
        }

        if (bStructuredWrapper && (outputFormat == CDIE_OUTPUT_JSON)) {
            if (nStructuredFiles++) cdie_printf(",");
            cdie_printf("\n    {\"path\":");
            print_json_string(pFileName);
            cdie_printf(",\"result\":");
        } else if (bStructuredWrapper && (outputFormat == CDIE_OUTPUT_XML)) {
            cdie_printf("    <file path=\"");
            print_xml_string(pFileName);
            cdie_printf("\">");
        }

        if (outputFormat == CDIE_OUTPUT_TEXT) {
            /* Native Windows colors act on direct console writes. Flush the
             * runtime's buffered headers first, then restore colors/mode before
             * diagnostics or the next file. Redirected streams remain plain. */
            if (xx_rt_fflush(xx_rt_stdout()) != 0) g_bStdoutFailed = 1;
            if (!g_bStdoutFailed) {
                xx_terminal_state terminal = xx_terminal_init(XX_TERMINAL_STDOUT);
                if (xx_scan_print_text(&result, &options, &terminal, NULL) != XXFC_OK)
                    g_bStdoutFailed = 1;
                if (xx_terminal_flush(&terminal) != XXFC_OK) g_bStdoutFailed = 1;
                xx_terminal_finish(&terminal);
            }
        } else {
            /* diec's XML document has no final newline of its own. */
            if (pOutput && !bStructuredWrapper && (outputFormat == CDIE_OUTPUT_XML)) {
                size_t nLength = xx_rt_strlen(pOutput);
                if (nLength && (pOutput[nLength - 1] == '\n')) pOutput[nLength - 1] = '\0';
            }
            cdie_printf("%s", pOutput ? pOutput :
                         (outputFormat == CDIE_OUTPUT_JSON ? "{}" : ""));
        }

        if (bStructuredWrapper && (outputFormat == CDIE_OUTPUT_JSON)) {
            cdie_printf("}");
        } else if (bStructuredWrapper && (outputFormat == CDIE_OUTPUT_XML)) {
            cdie_printf("    </file>\n");
        }

        /* diec prints each structured document with printf("%s\n"). */
        if ((options.result_as_json || options.result_as_xml || options.result_as_csv || options.result_as_tsv ||
             options.result_as_plain_text) && !bStructuredWrapper) {
            cdie_printf("\n");
        }

        xx_rt_free(pOutput);

        {
            int j = 0;

            /* diec reports script/progress errors (and the "Last error" of a
             * damaged archive member) after the detections on stdout, in
             * every output mode, regardless of --messages, and still exits
             * successfully. Only cdie's multi-file JSON/XML wrapper keeps them
             * on stderr, outside its single document. */
            for (j = 0; j < result.nErrorCount; j++) {
                if (bStructuredWrapper && ((outputFormat == CDIE_OUTPUT_JSON) || (outputFormat == CDIE_OUTPUT_XML))) {
                    xx_rt_fprintf(xx_rt_stderr(), "%s\n", result.ppErrors[j]);
                } else {
                    cdie_printf("%s\n", result.ppErrors[j]);
                }
            }
        }

        if (!bStructuredWrapper) cdie_printf("\n");
        xx_scan_result_free(&result);
    }

    if (bStructuredWrapper) {
        if (outputFormat == CDIE_OUTPUT_JSON) {
            cdie_printf("\n  ]\n}\n");
        } else {
            cdie_printf("  </files>\n</cdie>\n");
        }
    }

    free_string_vector(&vecTargets);
    free_string_vector(&vecFiles);
    db_free(&db);
    xx_scan_options_free(&options);

    return cdie_finish_result(nResult);
}
