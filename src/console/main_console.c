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
#include <xxfclib/list/xx_list.h>
#include <xxfclib/rt/xx_rt.h>

#include "../app/cdie_app.h"


static void print_help(void)
{
    xx_rt_printf("%s v%s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
    xx_rt_printf("Detect It Easy console engine, C port\n");
    xx_rt_printf("Copyright(C) 2026 hors<horsicq@gmail.com> Web: http://ntinfo.biz\n\n");
    xx_rt_printf("Usage: cdie [options] target\n\n");
    xx_rt_printf("Options:\n");
    xx_rt_printf("  -h, --help                Displays this help.\n");
    xx_rt_printf("  -v, --version             Displays version information.\n");
    xx_rt_printf("  -r, --recursivescan       Scan directories recursively.\n");
    xx_rt_printf("  -d, --deepscan            Deep scan.\n");
    xx_rt_printf("  -u, --heuristicscan       Heuristic scan.\n");
    xx_rt_printf("  -g, --aggressivecscan     Aggressive scan.\n");
    xx_rt_printf("  -b, --verbose             Verbose output.\n");
    xx_rt_printf("  -f, --format              Format the result strings (the default).\n");
    xx_rt_printf("      --noformat            Do not format the result strings.\n");
    xx_rt_printf("      --nocolor             Disable color output (cdie never colors).\n");
    xx_rt_printf("  -U, --hideunknown         Hide unknown results.\n");
    xx_rt_printf("  -M, --messages            Show engine messages.\n");
    xx_rt_printf("  -l, --profiling           Show profiling information.\n");
    xx_rt_printf("  -j, --json                Result as JSON.\n");
    xx_rt_printf("  -x, --xml                 Result as XML.\n");
    xx_rt_printf("  -c, --csv                 Result as CSV.\n");
    xx_rt_printf("  -t, --tsv                 Result as TSV.\n");
    xx_rt_printf("  -p, --plaintext           Result as plain text.\n");
    xx_rt_printf("  -D, --database <path>     Main database path.\n");
    xx_rt_printf("  -E, --extradatabase <p>   Extra database path.\n");
    xx_rt_printf("  -C, --customdatabase <p>  Custom database path.\n");
    xx_rt_printf("  -s, --showdatabase        Show the database information.\n");
    xx_rt_printf("\n");
    xx_rt_printf("Target:\n");
    xx_rt_printf("  target                    The file or directory to open.\n");
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
        return cdie_strdup(pPath);
    }

    {
        /* $data/<name>: next to the executable, then the current directory. */
        char *pAppDir = cdie_app_dir();
        char *pCandidate = xx_fs_path_join(pAppDir, pDefaultName);

        xx_rt_free(pAppDir);

        if (xx_fs_is_dir(pCandidate)) {
            return pCandidate;
        }

        xx_rt_free(pCandidate);
    }

    return cdie_strdup(pDefaultName);
}

static void show_database(DBase *pDb, ScanOptions *pOptions)
{
    /* Same list, in the same order, as XScanEngine::getSignatureStates(). */
    static const XFileType pTypes[] = {XFT_BINARY,  XFT_COM,     XFT_MSDOS, XFT_NE,     XFT_LE,       XFT_LX,     XFT_PE,      XFT_ELF,
                                       XFT_MACHO,   XFT_PDF,     XFT_CFBF,  XFT_IMAGE,  XFT_JPEG,     XFT_PNG,    XFT_RAR,     XFT_ISO9660,
                                       XFT_ARCHIVE, XFT_ZIP,     XFT_JAR,   XFT_APK,    XFT_IPA,      XFT_DEX,    XFT_NPM,     XFT_MACHOFAT,
                                       XFT_AMIGAHUNK, XFT_ATARIST, XFT_DOS16M, XFT_DOS4G};
    size_t i = 0;

    xx_rt_printf("Main database: %s\n", pOptions->pMainDatabasePath ? pOptions->pMainDatabasePath : "");
    xx_rt_printf("Extra database: %s\n", pOptions->pExtraDatabasePath ? pOptions->pExtraDatabasePath : "");
    xx_rt_printf("Custom database: %s\n", pOptions->pCustomDatabasePath ? pOptions->pCustomDatabasePath : "");

    for (i = 0; i < sizeof(pTypes) / sizeof(pTypes[0]); i++) {
        int nCount = db_count_for_type(pDb, pTypes[i]);

        if (nCount > 0) {
            xx_rt_printf("\t%s: %d\n", xft_to_string(pTypes[i]), nCount);
        }
    }
}

/* Entry point. utils_entry.c calls this: on a no-CRT Windows build from the
 * real linker entry, otherwise from main(). */
int x_main(int argc, char *argv[])
{
    ScanOptions options;
    DBase db;
    xx_list_t vecTargets;
    xx_list_t vecFiles;
    int nResult = CR_SUCCESS;
    int i = 0;
    int bShowDatabase = 0;
    int bDatabaseLoaded = 0;

    scan_options_init(&options);
    /* diec formats the result strings by default, and printing exactly what
     * diec prints is the acceptance test, so the console front end turns the
     * option on before the command line is parsed. -f/--format stays an
     * explicit enable, --noformat is the opt-out. */
    options.bFormatResult = 1;
    xx_rt_memset(&db, 0, sizeof(db));
    xx_list_init(&vecTargets, sizeof(char *), NULL);
    xx_list_init(&vecFiles, sizeof(char *), NULL);

    for (i = 1; i < argc; i++) {
        const char *pArg = argv[i];

        if ((xx_rt_strcmp(pArg, "-h") == 0) || (xx_rt_strcmp(pArg, "--help") == 0) || (xx_rt_strcmp(pArg, "-?") == 0)) {
            print_help();
            scan_options_free(&options);
            free_string_vector(&vecTargets);
            free_string_vector(&vecFiles);

            return CR_SUCCESS;
        } else if ((xx_rt_strcmp(pArg, "-v") == 0) || (xx_rt_strcmp(pArg, "--version") == 0)) {
            xx_rt_printf("%s %s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
            scan_options_free(&options);
            free_string_vector(&vecTargets);
            free_string_vector(&vecFiles);

            return CR_SUCCESS;
        } else if ((xx_rt_strcmp(pArg, "-r") == 0) || (xx_rt_strcmp(pArg, "--recursivescan") == 0)) {
            options.bRecursiveScan = 1;
        } else if ((xx_rt_strcmp(pArg, "-d") == 0) || (xx_rt_strcmp(pArg, "--deepscan") == 0)) {
            options.bDeepScan = 1;
        } else if ((xx_rt_strcmp(pArg, "-u") == 0) || (xx_rt_strcmp(pArg, "-he") == 0) || (xx_rt_strcmp(pArg, "--heuristicscan") == 0)) {
            options.bHeuristicScan = 1;
            /* diec spells the long form "aggressivecscan"; both are taken. */
        } else if ((xx_rt_strcmp(pArg, "-g") == 0) || (xx_rt_strcmp(pArg, "--aggressivecscan") == 0) || (xx_rt_strcmp(pArg, "--aggressivescan") == 0)) {
            options.bAggressiveScan = 1;
        } else if ((xx_rt_strcmp(pArg, "-b") == 0) || (xx_rt_strcmp(pArg, "-V") == 0) || (xx_rt_strcmp(pArg, "--verbose") == 0)) {
            options.bVerbose = 1;
        } else if ((xx_rt_strcmp(pArg, "-f") == 0) || (xx_rt_strcmp(pArg, "--format") == 0)) {
            options.bFormatResult = 1;
        } else if (xx_rt_strcmp(pArg, "--noformat") == 0) {
            options.bFormatResult = 0;
            /* cdie never emits color, so diec's --nocolor is already the
             * behaviour; it is taken to keep diec command lines working. */
        } else if (xx_rt_strcmp(pArg, "--nocolor") == 0) {
            /* nothing to do */
        } else if ((xx_rt_strcmp(pArg, "-U") == 0) || (xx_rt_strcmp(pArg, "-hu") == 0) || (xx_rt_strcmp(pArg, "--hideunknown") == 0)) {
            options.bHideUnknown = 1;
        } else if ((xx_rt_strcmp(pArg, "-M") == 0) || (xx_rt_strcmp(pArg, "-m") == 0) || (xx_rt_strcmp(pArg, "--messages") == 0)) {
            options.bShowMessages = 1;
        } else if ((xx_rt_strcmp(pArg, "-l") == 0) || (xx_rt_strcmp(pArg, "--profiling") == 0)) {
            options.bProfiling = 1;
        } else if ((xx_rt_strcmp(pArg, "-j") == 0) || (xx_rt_strcmp(pArg, "--json") == 0)) {
            options.bResultAsJSON = 1;
        } else if ((xx_rt_strcmp(pArg, "-x") == 0) || (xx_rt_strcmp(pArg, "--xml") == 0)) {
            options.bResultAsXML = 1;
        } else if ((xx_rt_strcmp(pArg, "-c") == 0) || (xx_rt_strcmp(pArg, "--csv") == 0)) {
            options.bResultAsCSV = 1;
        } else if ((xx_rt_strcmp(pArg, "-t") == 0) || (xx_rt_strcmp(pArg, "--tsv") == 0)) {
            options.bResultAsTSV = 1;
        } else if ((xx_rt_strcmp(pArg, "-p") == 0) || (xx_rt_strcmp(pArg, "-P") == 0) || (xx_rt_strcmp(pArg, "--plaintext") == 0)) {
            options.bResultAsPlainText = 1;
        } else if ((xx_rt_strcmp(pArg, "-s") == 0) || (xx_rt_strcmp(pArg, "--showdatabase") == 0)) {
            bShowDatabase = 1;
        } else if (((xx_rt_strcmp(pArg, "-D") == 0) || (xx_rt_strcmp(pArg, "--database") == 0)) && (i + 1 < argc)) {
            xx_rt_free(options.pMainDatabasePath);
            options.pMainDatabasePath = cdie_strdup(argv[++i]);
        } else if (((xx_rt_strcmp(pArg, "-E") == 0) || (xx_rt_strcmp(pArg, "--extradatabase") == 0)) && (i + 1 < argc)) {
            xx_rt_free(options.pExtraDatabasePath);
            options.pExtraDatabasePath = cdie_strdup(argv[++i]);
        } else if (((xx_rt_strcmp(pArg, "-C") == 0) || (xx_rt_strcmp(pArg, "--customdatabase") == 0)) && (i + 1 < argc)) {
            xx_rt_free(options.pCustomDatabasePath);
            options.pCustomDatabasePath = cdie_strdup(argv[++i]);
        } else if (pArg[0] == '-') {
            xx_rt_fprintf(xx_rt_stderr(), "Unknown option: %s\n", pArg);
            nResult = CR_INVALIDPARAMETER;
        } else {
            {
                char *pCopy = cdie_strdup(pArg);

                if (pCopy) {
                    xx_list_append(&vecTargets, &pCopy);
                }
            }
        }
    }

    {
        char *pMain = resolve_database_path(options.pMainDatabasePath, "db");
        char *pExtra = resolve_database_path(options.pExtraDatabasePath, "db_extra");
        char *pCustom = resolve_database_path(options.pCustomDatabasePath, "db_custom");

        xx_rt_free(options.pMainDatabasePath);
        xx_rt_free(options.pExtraDatabasePath);
        xx_rt_free(options.pCustomDatabasePath);
        options.pMainDatabasePath = pMain;
        options.pExtraDatabasePath = pExtra;
        options.pCustomDatabasePath = pCustom;
    }

    if ((xx_list_count(&vecTargets) == 0) && (!bShowDatabase)) {
        print_help();
        scan_options_free(&options);
        free_string_vector(&vecTargets);
        free_string_vector(&vecFiles);

        /* An unknown option already set nResult; do not report success. */
        return nResult;
    }

    bDatabaseLoaded = db_load(&db, options.pMainDatabasePath, DB_MAIN);

    if (options.bUseExtraDatabase) {
        db_load(&db, options.pExtraDatabasePath, DB_EXTRA);
    }

    if (options.bUseCustomDatabase) {
        db_load(&db, options.pCustomDatabasePath, DB_CUSTOM);
    }

    db_sort(&db);

    if (!bDatabaseLoaded) {
        xx_rt_fprintf(xx_rt_stderr(), "Cannot load database: %s\n", options.pMainDatabasePath);
        nResult = CR_CANNOTFINDDATABASE;
    }

    if (bShowDatabase) {
        show_database(&db, &options);
    }

    for (i = 0; i < (int)xx_list_count(&vecTargets); i++) {
        const char *pTarget = string_at(&vecTargets, (size_t)i);

        if (cdie_path_exists(pTarget)) {
            cdie_find_files(pTarget, &vecFiles, options.bRecursiveScan);
        } else {
            xx_rt_printf("Cannot find: %s\n", pTarget);
            nResult = CR_CANNOTFINDFILE;
        }
    }

    for (i = 0; i < (int)xx_list_count(&vecFiles); i++) {
        const char *pFileName = string_at(&vecFiles, (size_t)i);
        ScanResult result;
        char *pOutput = NULL;

        if (xx_list_count(&vecFiles) > 1) {
            char *pNative = cdie_path_native(pFileName);

            xx_rt_printf("%s:\n", pNative);
            xx_rt_free(pNative);
        }

        if (!die_engine_scan_file(pFileName, &db, &options, &result)) {
            xx_rt_printf("Cannot open: %s\n", pFileName);
            nResult = CR_CANNOTOPENFILE;
            continue;
        }

        if (options.bResultAsJSON) {
            pOutput = die_engine_format_json(&result, &options);
        } else if (options.bResultAsXML) {
            pOutput = die_engine_format_xml(&result, &options);
        } else if (options.bResultAsCSV) {
            /* diec separates CSV fields with a semicolon, not a comma. */
            pOutput = die_engine_format_csv(&result, &options, ';');
        } else if (options.bResultAsTSV) {
            pOutput = die_engine_format_csv(&result, &options, '\t');
        } else {
            pOutput = die_engine_format_text(&result, &options);
        }

        xx_rt_printf("%s", pOutput);

        if (options.bResultAsJSON || options.bResultAsXML) {
            xx_rt_printf("\n");
        }

        xx_rt_free(pOutput);

        if (options.bShowMessages) {
            int j = 0;

            for (j = 0; j < result.nErrorCount; j++) {
                xx_rt_fprintf(xx_rt_stderr(), "%s\n", result.ppErrors[j]);
            }
        }

        xx_rt_printf("\n");
        scan_result_free(&result);
    }

    free_string_vector(&vecTargets);
    free_string_vector(&vecFiles);
    db_free(&db);
    scan_options_free(&options);

    return nResult;
}
