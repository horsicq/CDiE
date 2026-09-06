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
#include "../engine/cdie.h"
#include "../core/cd_fs.h"
#include "../format/xft.h"

static void print_help(void)
{
    x_printf("%s v%s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
    x_printf("Detect It Easy console engine, C port\n");
    x_printf("Copyright(C) 2026 hors<horsicq@gmail.com> Web: http://ntinfo.biz\n\n");
    x_printf("Usage: cdie [options] target\n\n");
    x_printf("Options:\n");
    x_printf("  -h, --help                Displays this help.\n");
    x_printf("  -v, --version             Displays version information.\n");
    x_printf("  -r, --recursivescan       Scan directories recursively.\n");
    x_printf("  -d, --deepscan            Deep scan.\n");
    x_printf("  -u, --heuristicscan       Heuristic scan.\n");
    x_printf("  -g, --aggressivecscan     Aggressive scan.\n");
    x_printf("  -b, --verbose             Verbose output.\n");
    x_printf("  -f, --format              Format the result strings (the default).\n");
    x_printf("      --noformat            Do not format the result strings.\n");
    x_printf("      --nocolor             Disable color output (cdie never colors).\n");
    x_printf("  -U, --hideunknown         Hide unknown results.\n");
    x_printf("  -M, --messages            Show engine messages.\n");
    x_printf("  -l, --profiling           Show profiling information.\n");
    x_printf("  -j, --json                Result as JSON.\n");
    x_printf("  -x, --xml                 Result as XML.\n");
    x_printf("  -c, --csv                 Result as CSV.\n");
    x_printf("  -t, --tsv                 Result as TSV.\n");
    x_printf("  -p, --plaintext           Result as plain text.\n");
    x_printf("  -D, --database <path>     Main database path.\n");
    x_printf("  -E, --extradatabase <p>   Extra database path.\n");
    x_printf("  -C, --customdatabase <p>  Custom database path.\n");
    x_printf("  -s, --showdatabase        Show the database information.\n");
    x_printf("\n");
    x_printf("Target:\n");
    x_printf("  target                    The file or directory to open.\n");
}

/* Frees the cd_strdup'd elements and then the vector itself. */
static void free_string_vector(CDVec *pVec)
{
    size_t i = 0;

    for (i = 0; i < pVec->nSize; i++) {
        cd_free(pVec->ppData[i]);
    }

    cdvec_free(pVec);
}

static char *resolve_database_path(const char *pPath, const char *pDefaultName)
{
    if (pPath && pPath[0]) {
        return cd_strdup(pPath);
    }

    {
        /* $data/<name>: next to the executable, then the current directory. */
        char *pAppDir = cd_app_dir();
        char *pCandidate = cd_path_join(pAppDir, pDefaultName);

        cd_free(pAppDir);

        if (cd_path_is_dir(pCandidate)) {
            return pCandidate;
        }

        cd_free(pCandidate);
    }

    return cd_strdup(pDefaultName);
}

static void show_database(DBase *pDb, ScanOptions *pOptions)
{
    /* Same list, in the same order, as XScanEngine::getSignatureStates(). */
    static const XFileType pTypes[] = {XFT_BINARY,  XFT_COM,     XFT_MSDOS, XFT_NE,     XFT_LE,       XFT_LX,     XFT_PE,      XFT_ELF,
                                       XFT_MACHO,   XFT_PDF,     XFT_CFBF,  XFT_IMAGE,  XFT_JPEG,     XFT_PNG,    XFT_RAR,     XFT_ISO9660,
                                       XFT_ARCHIVE, XFT_ZIP,     XFT_JAR,   XFT_APK,    XFT_IPA,      XFT_DEX,    XFT_NPM,     XFT_MACHOFAT,
                                       XFT_AMIGAHUNK, XFT_ATARIST, XFT_DOS16M, XFT_DOS4G};
    size_t i = 0;

    x_printf("Main database: %s\n", pOptions->pMainDatabasePath ? pOptions->pMainDatabasePath : "");
    x_printf("Extra database: %s\n", pOptions->pExtraDatabasePath ? pOptions->pExtraDatabasePath : "");
    x_printf("Custom database: %s\n", pOptions->pCustomDatabasePath ? pOptions->pCustomDatabasePath : "");

    for (i = 0; i < sizeof(pTypes) / sizeof(pTypes[0]); i++) {
        int nCount = db_count_for_type(pDb, pTypes[i]);

        if (nCount > 0) {
            x_printf("\t%s: %d\n", xft_to_string(pTypes[i]), nCount);
        }
    }
}

/* Entry point. utils_entry.c calls this: on a no-CRT Windows build from the
 * real linker entry, otherwise from main(). */
int x_main(int argc, char *argv[])
{
    ScanOptions options;
    DBase db;
    CDVec vecTargets;
    CDVec vecFiles;
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
    x_memset(&db, 0, sizeof(db));
    cdvec_init(&vecTargets);
    cdvec_init(&vecFiles);

    for (i = 1; i < argc; i++) {
        const char *pArg = argv[i];

        if ((x_strcmp(pArg, "-h") == 0) || (x_strcmp(pArg, "--help") == 0) || (x_strcmp(pArg, "-?") == 0)) {
            print_help();
            scan_options_free(&options);
            free_string_vector(&vecTargets);
            free_string_vector(&vecFiles);

            return CR_SUCCESS;
        } else if ((x_strcmp(pArg, "-v") == 0) || (x_strcmp(pArg, "--version") == 0)) {
            x_printf("%s %s\n", X_APPLICATIONDISPLAYNAME, X_APPLICATIONVERSION);
            scan_options_free(&options);
            free_string_vector(&vecTargets);
            free_string_vector(&vecFiles);

            return CR_SUCCESS;
        } else if ((x_strcmp(pArg, "-r") == 0) || (x_strcmp(pArg, "--recursivescan") == 0)) {
            options.bRecursiveScan = 1;
        } else if ((x_strcmp(pArg, "-d") == 0) || (x_strcmp(pArg, "--deepscan") == 0)) {
            options.bDeepScan = 1;
        } else if ((x_strcmp(pArg, "-u") == 0) || (x_strcmp(pArg, "-he") == 0) || (x_strcmp(pArg, "--heuristicscan") == 0)) {
            options.bHeuristicScan = 1;
            /* diec spells the long form "aggressivecscan"; both are taken. */
        } else if ((x_strcmp(pArg, "-g") == 0) || (x_strcmp(pArg, "--aggressivecscan") == 0) || (x_strcmp(pArg, "--aggressivescan") == 0)) {
            options.bAggressiveScan = 1;
        } else if ((x_strcmp(pArg, "-b") == 0) || (x_strcmp(pArg, "-V") == 0) || (x_strcmp(pArg, "--verbose") == 0)) {
            options.bVerbose = 1;
        } else if ((x_strcmp(pArg, "-f") == 0) || (x_strcmp(pArg, "--format") == 0)) {
            options.bFormatResult = 1;
        } else if (x_strcmp(pArg, "--noformat") == 0) {
            options.bFormatResult = 0;
            /* cdie never emits color, so diec's --nocolor is already the
             * behaviour; it is taken to keep diec command lines working. */
        } else if (x_strcmp(pArg, "--nocolor") == 0) {
            /* nothing to do */
        } else if ((x_strcmp(pArg, "-U") == 0) || (x_strcmp(pArg, "-hu") == 0) || (x_strcmp(pArg, "--hideunknown") == 0)) {
            options.bHideUnknown = 1;
        } else if ((x_strcmp(pArg, "-M") == 0) || (x_strcmp(pArg, "-m") == 0) || (x_strcmp(pArg, "--messages") == 0)) {
            options.bShowMessages = 1;
        } else if ((x_strcmp(pArg, "-l") == 0) || (x_strcmp(pArg, "--profiling") == 0)) {
            options.bProfiling = 1;
        } else if ((x_strcmp(pArg, "-j") == 0) || (x_strcmp(pArg, "--json") == 0)) {
            options.bResultAsJSON = 1;
        } else if ((x_strcmp(pArg, "-x") == 0) || (x_strcmp(pArg, "--xml") == 0)) {
            options.bResultAsXML = 1;
        } else if ((x_strcmp(pArg, "-c") == 0) || (x_strcmp(pArg, "--csv") == 0)) {
            options.bResultAsCSV = 1;
        } else if ((x_strcmp(pArg, "-t") == 0) || (x_strcmp(pArg, "--tsv") == 0)) {
            options.bResultAsTSV = 1;
        } else if ((x_strcmp(pArg, "-p") == 0) || (x_strcmp(pArg, "-P") == 0) || (x_strcmp(pArg, "--plaintext") == 0)) {
            options.bResultAsPlainText = 1;
        } else if ((x_strcmp(pArg, "-s") == 0) || (x_strcmp(pArg, "--showdatabase") == 0)) {
            bShowDatabase = 1;
        } else if (((x_strcmp(pArg, "-D") == 0) || (x_strcmp(pArg, "--database") == 0)) && (i + 1 < argc)) {
            cd_free(options.pMainDatabasePath);
            options.pMainDatabasePath = cd_strdup(argv[++i]);
        } else if (((x_strcmp(pArg, "-E") == 0) || (x_strcmp(pArg, "--extradatabase") == 0)) && (i + 1 < argc)) {
            cd_free(options.pExtraDatabasePath);
            options.pExtraDatabasePath = cd_strdup(argv[++i]);
        } else if (((x_strcmp(pArg, "-C") == 0) || (x_strcmp(pArg, "--customdatabase") == 0)) && (i + 1 < argc)) {
            cd_free(options.pCustomDatabasePath);
            options.pCustomDatabasePath = cd_strdup(argv[++i]);
        } else if (pArg[0] == '-') {
            x_fprintf(x_stderr(), "Unknown option: %s\n", pArg);
            nResult = CR_INVALIDPARAMETER;
        } else {
            cdvec_push(&vecTargets, cd_strdup(pArg));
        }
    }

    {
        char *pMain = resolve_database_path(options.pMainDatabasePath, "db");
        char *pExtra = resolve_database_path(options.pExtraDatabasePath, "db_extra");
        char *pCustom = resolve_database_path(options.pCustomDatabasePath, "db_custom");

        cd_free(options.pMainDatabasePath);
        cd_free(options.pExtraDatabasePath);
        cd_free(options.pCustomDatabasePath);
        options.pMainDatabasePath = pMain;
        options.pExtraDatabasePath = pExtra;
        options.pCustomDatabasePath = pCustom;
    }

    if ((vecTargets.nSize == 0) && (!bShowDatabase)) {
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
        x_fprintf(x_stderr(), "Cannot load database: %s\n", options.pMainDatabasePath);
        nResult = CR_CANNOTFINDDATABASE;
    }

    if (bShowDatabase) {
        show_database(&db, &options);
    }

    for (i = 0; i < (int)vecTargets.nSize; i++) {
        const char *pTarget = (const char *)vecTargets.ppData[i];

        if (cd_path_exists(pTarget)) {
            cd_find_files(pTarget, &vecFiles, options.bRecursiveScan);
        } else {
            x_printf("Cannot find: %s\n", pTarget);
            nResult = CR_CANNOTFINDFILE;
        }
    }

    for (i = 0; i < (int)vecFiles.nSize; i++) {
        const char *pFileName = (const char *)vecFiles.ppData[i];
        ScanResult result;
        char *pOutput = NULL;

        if (vecFiles.nSize > 1) {
            char *pNative = cd_path_native(pFileName);

            x_printf("%s:\n", pNative);
            cd_free(pNative);
        }

        if (!cdie_scan_file(pFileName, &db, &options, &result)) {
            x_printf("Cannot open: %s\n", pFileName);
            nResult = CR_CANNOTOPENFILE;
            continue;
        }

        if (options.bResultAsJSON) {
            pOutput = cdie_format_json(&result, &options);
        } else if (options.bResultAsXML) {
            pOutput = cdie_format_xml(&result, &options);
        } else if (options.bResultAsCSV) {
            /* diec separates CSV fields with a semicolon, not a comma. */
            pOutput = cdie_format_csv(&result, &options, ';');
        } else if (options.bResultAsTSV) {
            pOutput = cdie_format_csv(&result, &options, '\t');
        } else {
            pOutput = cdie_format_text(&result, &options);
        }

        x_printf("%s", pOutput);

        if (options.bResultAsJSON || options.bResultAsXML) {
            x_printf("\n");
        }

        cd_free(pOutput);

        if (options.bShowMessages) {
            int j = 0;

            for (j = 0; j < result.nErrorCount; j++) {
                x_fprintf(x_stderr(), "%s\n", result.ppErrors[j]);
            }
        }

        x_printf("\n");
        scan_result_free(&result);
    }

    free_string_vector(&vecTargets);
    free_string_vector(&vecFiles);
    db_free(&db);
    scan_options_free(&options);

    return nResult;
}
