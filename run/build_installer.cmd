@echo off
rem ---------------------------------------------------------------------------
rem Build the cdie GUI (xxwidgets front end) with MSVC directly (no CMake for
rem the app itself), then build the installer. The build tree lives under
rem %TEMP%\cdie_setup; only the finished cdie-setup*.exe is left in release\.
rem
rem Every .c under src except the console entry points (utils_entry.c,
rem main_console.c) is compiled in, including everything under src\gui, plus
rem the resource script.
rem
rem Dependencies (dep\xxwidgets, dep\xxfclib): if no prebuilt .lib is found,
rem they are configured and built automatically with CMake (Release, static
rem CRT /MT) into dep\<name>\build, then picked up.
rem
rem Usage (from any prompt):  run\build_msvc_gui.cmd [clean | cleanall | rebuild] [store] [noinstaller]
rem   store        build with /DCDIE_STORE_BUILD
rem   noinstaller  build the app only; by default the installer is built after it
rem   clean        delete the temp build folder
rem   cleanall     clean, and also delete dep\xxwidgets\build and dep\xxfclib\build
rem   rebuild      clean, then build
rem Overrides (optional):
rem   set "VCVARS=...\vcvars64.bat"
rem   set "XXW_LIB=C:\full\path\xxwidgets.lib"
rem   set "XXF_LIB=C:\full\path\die_engine.lib"
rem   set "CDIS_LIB=C:\full\path\cdisasm.lib"
rem   set "REBUILD_DEPS=1"     force a rebuild of both dependencies
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "WORK_ROOT=%TEMP%\cdie_setup"
set "OUT=%WORK_ROOT%\build_msvc"
set "GUI=%SRC%\gui"
set "DEP=%ROOT%\dep"
set "RES=%GUI%\resource"
set "STORE_BUILD=0"
set "BUILD_INSTALLER=1"
set "DO_REBUILD="
for %%a in (%*) do (
    if /i "%%~a"=="store"       set "STORE_BUILD=1"
    if /i "%%~a"=="installer"   set "BUILD_INSTALLER=1"
    if /i "%%~a"=="noinstaller" set "BUILD_INSTALLER=0"
    if /i "%%~a"=="rebuild"     set "DO_REBUILD=1"
)

rem --- Clean targets (no Visual Studio environment needed) -------------------
if /i "%~1"=="clean"    call :do_clean     & exit /b 0
if /i "%~1"=="cleanall" call :do_clean all & exit /b 0
if defined DO_REBUILD   call :do_clean

rem --- Visual Studio x64 environment (needed for cl and cmake) ---------------
if not defined VSCMD_VER (
    if not defined VCVARS (
        set "VSVARS_PATH="

        for /f "tokens=*" %%a in ('
            reg query "HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall" /s /f "Visual Studio" ^|
            findstr "HKEY"
        ') do (
            for /f "tokens=2*" %%b in ('
                reg query "%%a" /v InstallLocation 2^>nul ^|
                findstr /i "InstallLocation"
            ') do (
                if not "%%c"=="" (
                    if exist "%%c\VC\Auxiliary\Build\vcvars64.bat" (
                        set "VSVARS_PATH=%%c\VC\Auxiliary\Build\vcvars64.bat"
                        goto :found_vs
                    )
                )
            )
        )

        :found_vs
        if not defined VSVARS_PATH (
            echo ERROR: Visual Studio not found.
            exit /b 1
        )

        set "VCVARS=%VSVARS_PATH%"
    )

    call "!VCVARS!" >nul
)

rem --- Dependency include roots ----------------------------------------------
set "XXW_INC=%DEP%\xxwidgets\include"
set "XXF_INC=%DEP%\xxfclib\include"

if not exist "%XXW_INC%\xxwidgets\xxwidgets.h" (
    echo ERROR: missing %XXW_INC%\xxwidgets\xxwidgets.h
    exit /b 1
)
if not exist "%XXF_INC%\xxfclib\list\xx_list.h" (
    echo ERROR: missing %XXF_INC%\xxfclib\list\xx_list.h
    exit /b 1
)
if not exist "%XXF_INC%\xxfclib\die_engine\die_engine.h" (
    echo ERROR: missing %XXF_INC%\xxfclib\die_engine\die_engine.h
    exit /b 1
)

rem --- Dependency libraries: find, build if missing, find again --------------
if defined REBUILD_DEPS (
    set "XXW_LIB="
    set "XXF_LIB="
    call :build_dep "%DEP%\xxwidgets" xxwidgets || exit /b 1
    call :build_dep "%DEP%\xxfclib" die_engine || exit /b 1
)

if not defined XXW_LIB call :find_lib XXW_LIB "%DEP%\xxwidgets" "xxwidgets*.lib"
if not defined XXW_LIB (
    echo xxwidgets lib not found - building it...
    call :build_dep "%DEP%\xxwidgets" xxwidgets || exit /b 1
    call :find_lib XXW_LIB "%DEP%\xxwidgets" "xxwidgets*.lib"
)
if not defined XXW_LIB (
    echo ERROR: still no xxwidgets*.lib under %DEP%\xxwidgets after building.
    echo        Run: dir /s /b "%DEP%\xxwidgets\build\*.lib"
    echo        then set XXW_LIB to the right file.
    exit /b 1
)

if not defined XXF_LIB call :find_lib XXF_LIB "%DEP%\xxfclib" "*die_engine*.lib"
if not defined XXF_LIB (
    echo die_engine lib not found - building xxfclib...
    call :build_dep "%DEP%\xxfclib" die_engine || exit /b 1
    call :find_lib XXF_LIB "%DEP%\xxfclib" "*die_engine*.lib"
)
if not defined XXF_LIB (
    echo ERROR: still no *die_engine*.lib under %DEP%\xxfclib after building.
    echo        Run: dir /s /b "%DEP%\xxfclib\build\*.lib"
    echo        then set XXF_LIB to the right file.
    exit /b 1
)
echo Using xxwidgets lib : %XXW_LIB%
echo Using die_engine lib: %XXF_LIB%

rem die_engine.lib needs the cdisasm library (static libs do not carry deps).
rem Auto-detected under dep\ unless CDIS_LIB is already set.
set "CDIS_ARG="
if not defined CDIS_LIB call :find_lib CDIS_LIB "%DEP%" "*cdisasm*.lib"
if defined CDIS_LIB (
    echo Using cdisasm lib   : %CDIS_LIB%
    set CDIS_ARG="%CDIS_LIB%"
) else (
     echo WARNING: no *cdisasm*.lib found under %DEP% - link may fail with cdisasm_x86_* unresolved.
)

set "CDIS_INC_ARG="
set "CDIS_HDR="
if not defined CDIS_INC (
    for /r "%ROOT%" %%f in (cdisasm.h) do (
        if exist "%%f" if not defined CDIS_HDR (
            echo %%f| findstr /i /c:"\.git\" >nul || set "CDIS_HDR=%%f"
        )
    )
)
if not defined CDIS_INC if defined CDIS_HDR (
    for %%d in ("!CDIS_HDR!") do set "CDIS_HDR_DIR=%%~dpd"
    set "CDIS_HDR_DIR=!CDIS_HDR_DIR:~0,-1!"
    for %%p in ("!CDIS_HDR_DIR!") do (
        set "CDIS_HDR_NAME=%%~nxp"
        set "CDIS_HDR_UP=%%~dpp"
    )
    if /i "!CDIS_HDR_NAME!"=="cdisasm" (
        set "CDIS_INC=!CDIS_HDR_UP:~0,-1!"
    ) else (
        echo cdisasm.h is in "!CDIS_HDR_DIR!", not a "cdisasm" folder - creating a shim.
        if not exist "%OUT%\shim\cdisasm" mkdir "%OUT%\shim\cdisasm"
        > "%OUT%\shim\cdisasm\cdisasm.h" echo #include "!CDIS_HDR!"
        set "CDIS_INC=%OUT%\shim"
        set CDIS_INC_ARG=-I"!CDIS_HDR_DIR!"
    )
)
if defined CDIS_INC (
    echo Using cdisasm inc   : !CDIS_INC!
    set CDIS_INC_ARG=-I"!CDIS_INC!" !CDIS_INC_ARG!
) else (
    echo WARNING: cdisasm.h not found under %ROOT% - compile will fail.
)

if not exist "%OUT%" mkdir "%OUT%"

set "EXTRA_DEFINES="
if "%STORE_BUILD%"=="1" set "EXTRA_DEFINES=/DCDIE_STORE_BUILD"

rem --- Engine source list (engine minus entry points and the gui dir) --------
set "GUI_SOURCES="
for /r "%SRC%" %%f in (*.c) do (
    set "skip="
    echo %%~dpf| findstr /i "\\gui\\" >nul && set "skip=1"
    echo %%~dpf| findstr /i "\\installer\\" >nul && set "skip=1"
    if /i "%%~nxf"=="utils_entry.c" set "skip=1"
    if /i "%%~nxf"=="main_console.c" set "skip=1"
    if not defined skip set "GUI_SOURCES=!GUI_SOURCES! "%%f""
)

rem --- The GUI's own sources: every .c under src\gui except non-Windows backends ---
for /r "%GUI%" %%f in (*.c) do (
    set "skip="
    echo %%~nxf| findstr /i /r "_gtk\.c$ _cocoa\.c$ _macos\.c$" >nul && set "skip=1"
    if not defined skip set "GUI_SOURCES=!GUI_SOURCES! "%%f""
)

rem --- 1. resources (manifest + version) ------------------------------------
echo [1/2] cdie_gui.res (manifest + version)...
rc /nologo /i "%RES%" /i "%GUI%" /fo "%OUT%\cdie_gui.res" "%RES%\cdie_gui.rc"
if errorlevel 1 ( echo BUILD FAILED ^(cdie_gui.res^) & exit /b 1 )

rem --- 2. cdie_gui.exe (xxwidgets front end, static CRT) ---------------------
rem Matches CMake: /utf-8, /MT, /MANIFEST:NO (the .rc owns the manifest).
rem No /NODEFAULTLIB, /ENTRY or -DCDIE_NO_CRT: xxwidgets needs the hosted CRT.
rem XXWIDGETS_STATIC / XXFC_STATIC stop the headers using dllimport (__imp_ symbols)
rem when linking the static .lib files.
if "%STORE_BUILD%"=="1" (
    echo Building STORE version
) else (
    echo Building NORMAL version
)

if not exist "%OUT%\obj_gui" mkdir "%OUT%\obj_gui"
cl /nologo /O1 /GL /Gy /Gw /GS- /W3 /MT /utf-8 /DNDEBUG /DXXWIDGETS_STATIC /DXXFC_STATIC !EXTRA_DEFINES! ^
   -I"%SRC%" -I"%GUI%" -I"%RES%" -I"%XXW_INC%" -I"%XXF_INC%" !CDIS_INC_ARG! ^
   !GUI_SOURCES! "%OUT%\cdie_gui.res" /Fe:"%OUT%\cdie_gui.exe" /Fo:"%OUT%\obj_gui\\" ^
      /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF /LTCG /DEBUG:NONE /MAP:"%OUT%\cdie_gui.map" ^
   "%XXW_LIB%" "%XXF_LIB%" !CDIS_ARG! ^
      user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib dwmapi.lib uxtheme.lib msimg32.lib
if errorlevel 1 ( echo BUILD FAILED ^(cdie_gui.exe^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie_gui.exe

if "%BUILD_INSTALLER%"=="1" (
    echo.
    echo Building installer...
    if not exist "%ROOT%\build_msvc" mkdir "%ROOT%\build_msvc"
    copy /y "%OUT%\cdie_gui.exe" "%ROOT%\build_msvc\cdie_gui.exe" >nul
    call "%SRC%\installer\build_installer.cmd"
    if errorlevel 1 (
        if exist "%ROOT%\build_msvc" rmdir /s /q "%ROOT%\build_msvc"
        echo INSTALLER BUILD FAILED
        exit /b 1
    )

    set "RELEASE_DIR=%ROOT%\release"
    if not exist "!RELEASE_DIR!" mkdir "!RELEASE_DIR!"
    set "SETUP_COPIED="
    call :copy_setup "%ROOT%\build_msvc"
    call :copy_setup "%OUT%"
    call :copy_setup "%SRC%\installer"
    if exist "%ROOT%\build_msvc" rmdir /s /q "%ROOT%\build_msvc"
    if not defined SETUP_COPIED echo WARNING: no cdie-setup*.exe found
    if defined SETUP_COPIED if exist "!WORK_ROOT!" rmdir /s /q "!WORK_ROOT!"
)
endlocal
exit /b 0

rem ===========================================================================
rem Subroutines
rem ===========================================================================

rem :copy_setup <search root>
rem   Copies any cdie-setup*.exe found under the root into the release folder.
:copy_setup
for /r "%~1" %%f in (cdie-setup*.exe) do (
    copy /y "%%f" "!RELEASE_DIR!\" >nul
    echo Copied to !RELEASE_DIR!\ : %%~nxf
    set "SETUP_COPIED=1"
)
exit /b 0

rem :do_clean [all]
rem   Removes the temp build folder. With "all", also the CMake build folders of
rem   the dependencies (the next build then recompiles them, which is slow).
:do_clean
if exist "!WORK_ROOT!" (
    echo Removing !WORK_ROOT! ...
    rmdir /s /q "!WORK_ROOT!"
) else (
    echo Nothing to clean in !WORK_ROOT!
)
if exist "!ROOT!\build_msvc" (
    echo Removing !ROOT!\build_msvc ...
    rmdir /s /q "!ROOT!\build_msvc"
)
if /i "%~1"=="all" (
    for %%d in ("!DEP!\xxwidgets\build" "!DEP!\xxfclib\build") do (
        if exist "%%~d" (
            echo Removing %%~d ...
            rmdir /s /q "%%~d"
        )
    )
)
exit /b 0

rem :find_lib <VARNAME> <search root> <pattern>
rem   Sets VARNAME to the first matching .lib found recursively.
:find_lib
for /r "%~2" %%f in (%~3) do if not defined %~1 set "%~1=%%f"
exit /b 0

rem :build_dep <source dir> [target]
rem   Configure + build a CMake dependency as a Release static lib with /MT.
rem   Demos and tests are switched off (not needed, and the demo sources may
rem   not be present). If [target] is given, only that target is built.
:build_dep
if not exist "%~1\CMakeLists.txt" (
    echo ERROR: no CMakeLists.txt in %~1 - cannot auto-build.
    exit /b 1
)
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake not found on PATH. Install CMake or build %~1 manually.
    exit /b 1
)
set "GEN="
where ninja >nul 2>&1 && set "GEN=-G Ninja"
set "TGT="
if not "%~2"=="" set "TGT=--target %~2"
echo Building %~1 ...
cmake -S "%~1" -B "%~1\build" !GEN! -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DXXWIDGETS_BUILD_EXAMPLES=OFF -DXXWIDGETS_BUILD_TESTS=OFF -DXXWIDGETS_BUILD_SETTINGS=OFF -DBUILD_TESTING=OFF
if errorlevel 1 (
    echo ERROR: cmake configure failed for %~1
    exit /b 1
)
cmake --build "%~1\build" --config Release !TGT!
if errorlevel 1 (
    echo ERROR: cmake build failed for %~1
    exit /b 1
)
exit /b 0