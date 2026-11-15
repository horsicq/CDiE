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
rem CRT /MT) into dep\<name>\build_<arch>, then picked up.
rem
rem Usage (cmd, PowerShell or any Developer prompt; the script switches the
rem Visual Studio environment to the requested arch by itself):
rem   build_msvc_gui.cmd [clean | cleanall | rebuild] [store] [noinstaller] [x64 | x86 | arm64]
rem   store        build with /DCDIE_STORE_BUILD
rem   noinstaller  build the app only; by default the installer is built after it
rem   x64|x86|arm64  target architecture (default x64)
rem   clean        delete the temp build folders (all architectures)
rem   cleanall     clean, and also delete dep\xxwidgets\build* and dep\xxfclib\build*
rem   rebuild      clean, then build
rem Examples:
rem   build_msvc_gui.cmd store noinstaller x64
rem   build_msvc_gui.cmd store noinstaller arm64
rem   build_msvc_gui.cmd x86
rem Overrides (optional):
rem   set "VCVARS=...\vcvarsall.bat"
rem   set "XXW_LIB=C:\full\path\xxwidgets.lib"
rem   set "XXF_LIB=C:\full\path\die_engine.lib"
rem   set "CDIS_LIB=C:\full\path\cdisasm.lib"     (must match the target arch)
rem   set "REBUILD_DEPS=1"     force a rebuild of both dependencies
rem
rem The installer script receives the target arch in %CDIE_ARCH% and should
rem name its output cdie-setup-%CDIE_ARCH%.exe so archs don't overwrite each other.
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "WORK_ROOT=%TEMP%\cdie_setup"
set "GUI=%SRC%\gui"
set "DEP=%ROOT%\dep"
set "RES=%GUI%\resource"
set "STORE_BUILD=0"
set "BUILD_INSTALLER="
set "DO_REBUILD="
set "ARCH=x64"
for %%a in (%*) do (
    if /i "%%~a"=="store"       set "STORE_BUILD=1"
    if /i "%%~a"=="installer"   set "BUILD_INSTALLER=1"
    if /i "%%~a"=="noinstaller" set "BUILD_INSTALLER=0"
    if /i "%%~a"=="rebuild"     set "DO_REBUILD=1"
    if /i "%%~a"=="x64"         set "ARCH=x64"
    if /i "%%~a"=="x86"         set "ARCH=x86"
    if /i "%%~a"=="arm64"       set "ARCH=arm64"
)
set "OUT=%WORK_ROOT%\build_msvc_%ARCH%"
set "CDIE_ARCH=%ARCH%"

rem default: normal build makes an installer, Store build does not (unless "installer" is given)
if not defined BUILD_INSTALLER (
    if "%STORE_BUILD%"=="1" ( set "BUILD_INSTALLER=0" ) else ( set "BUILD_INSTALLER=1" )
)

rem --- Clean targets (no Visual Studio environment needed) -------------------
if /i "%~1"=="clean"    call :do_clean     & exit /b 0
if /i "%~1"=="cleanall" call :do_clean all & exit /b 0
if defined DO_REBUILD   call :do_clean

rem --- Visual Studio environment for the requested arch ----------------------
rem If the prompt already has a VS environment for another arch, clear it and
rem re-run vcvarsall for the requested one. setlocal keeps this change local to
rem the script, so the calling shell is left untouched.
set "NEED_VCVARS="
if not defined VSCMD_VER set "NEED_VCVARS=1"
if defined VSCMD_VER if /i not "!VSCMD_ARG_TGT_ARCH!"=="%ARCH%" (
    echo Prompt is set up for !VSCMD_ARG_TGT_ARCH!, switching to %ARCH%...
    if not defined VCVARS if exist "!VSINSTALLDIR!VC\Auxiliary\Build\vcvarsall.bat" set "VCVARS=!VSINSTALLDIR!VC\Auxiliary\Build\vcvarsall.bat"
    if defined __VSCMD_PREINIT_PATH set "PATH=!__VSCMD_PREINIT_PATH!"
    for %%v in (VSCMD_VER VSCMD_ARG_app_plat VSCMD_ARG_HOST_ARCH VSCMD_ARG_TGT_ARCH INCLUDE LIB LIBPATH EXTERNAL_INCLUDE UCRTVersion UniversalCRTSdkDir WindowsSdkDir WindowsSDKVersion WindowsSDKLibVersion WindowsSdkBinPath WindowsSdkVerBinPath WindowsLibPath Platform VCINSTALLDIR VCToolsInstallDir VCToolsRedistDir DevEnvDir) do set "%%v="
    set "NEED_VCVARS=1"
)
if defined NEED_VCVARS (
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
                    if exist "%%c\VC\Auxiliary\Build\vcvarsall.bat" (
                        set "VSVARS_PATH=%%c\VC\Auxiliary\Build\vcvarsall.bat"
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

        set "VCVARS=!VSVARS_PATH!"
    )

    rem vcvarsall needs host_target for cross builds (e.g. x64_arm64 on an x64 PC)
    set "VCARG=%ARCH%"
    set "HOSTARCH=%PROCESSOR_ARCHITECTURE%"
    if defined PROCESSOR_ARCHITEW6432 set "HOSTARCH=%PROCESSOR_ARCHITEW6432%"
    if /i "!HOSTARCH!"=="AMD64" if /i "%ARCH%"=="arm64" set "VCARG=x64_arm64"
    if /i "!HOSTARCH!"=="ARM64" if /i "%ARCH%"=="x64" set "VCARG=arm64_x64"
    if /i "!HOSTARCH!"=="ARM64" if /i "%ARCH%"=="x86" set "VCARG=arm64_x86"
    echo Host !HOSTARCH!, vcvarsall arg: !VCARG!
    call "!VCVARS!" !VCARG! >nul
    if errorlevel 1 (
        echo ERROR: vcvarsall.bat failed for !VCARG! - is that toolchain installed?
        exit /b 1
    )
    where cl >nul 2>&1
    if errorlevel 1 (
        echo ERROR: cl.exe not found on PATH after vcvarsall !VCARG!.
        exit /b 1
    )
)
echo Target architecture : %ARCH%

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

if defined REBUILD_DEPS (
    set "XXW_LIB="
    set "XXF_LIB="
    call :build_dep "%DEP%\xxwidgets" xxwidgets || exit /b 1
    call :build_dep "%DEP%\xxfclib" die_engine || exit /b 1
)

if not defined XXW_LIB call :find_lib XXW_LIB "%DEP%\xxwidgets\build_%ARCH%" "xxwidgets*.lib"
if not defined XXW_LIB (
    echo xxwidgets lib not found for %ARCH% - building it...
    call :build_dep "%DEP%\xxwidgets" xxwidgets || exit /b 1
    call :find_lib XXW_LIB "%DEP%\xxwidgets\build_%ARCH%" "xxwidgets*.lib"
)
if not defined XXW_LIB (
    echo ERROR: still no xxwidgets*.lib under %DEP%\xxwidgets\build_%ARCH% after building.
    echo        Run: dir /s /b "%DEP%\xxwidgets\build_%ARCH%\*.lib"
    echo        then set XXW_LIB to the right file.
    exit /b 1
)

if not defined XXF_LIB call :find_lib XXF_LIB "%DEP%\xxfclib\build_%ARCH%" "*die_engine*.lib"
if not defined XXF_LIB (
    echo die_engine lib not found for %ARCH% - building xxfclib...
    call :build_dep "%DEP%\xxfclib" die_engine || exit /b 1
    call :find_lib XXF_LIB "%DEP%\xxfclib\build_%ARCH%" "*die_engine*.lib"
)
if not defined XXF_LIB (
    echo ERROR: still no *die_engine*.lib under %DEP%\xxfclib\build_%ARCH% after building.
    echo        Run: dir /s /b "%DEP%\xxfclib\build_%ARCH%\*.lib"
    echo        then set XXF_LIB to the right file.
    exit /b 1
)
echo Using xxwidgets lib : %XXW_LIB%
echo Using die_engine lib: %XXF_LIB%

rem die_engine.lib needs the cdisasm library (static libs do not carry deps).
rem It is built next to the dependencies, so look ONLY in the per-arch build
rem folders (never fall back to a lib from another arch) unless CDIS_LIB is set.
set "CDIS_ARG="
if not defined CDIS_LIB call :find_lib CDIS_LIB "%DEP%\xxfclib\build_%ARCH%" "*cdisasm*.lib"
if not defined CDIS_LIB call :find_lib CDIS_LIB "%DEP%\xxwidgets\build_%ARCH%" "*cdisasm*.lib"
if not defined CDIS_LIB (
    echo ERROR: no %ARCH% cdisasm.lib found under dep\xxfclib\build_%ARCH% or dep\xxwidgets\build_%ARCH%.
    echo        Build it for %ARCH% or set CDIS_LIB to the right file.
    exit /b 1
)

rem sanity check: the lib must be the target machine type
set "MACH=x86"
if /i "%ARCH%"=="x64"   set "MACH=x64"
if /i "%ARCH%"=="arm64" set "MACH=ARM64"
dumpbin /headers "!CDIS_LIB!" | findstr /i /c:"machine (!MACH!)" >nul
if errorlevel 1 (
    echo ERROR: !CDIS_LIB! is not a %ARCH% library.
    exit /b 1
)
echo Using cdisasm lib   : %CDIS_LIB%
set CDIS_ARG="%CDIS_LIB%"

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
    echo Building STORE version ^(%ARCH%^)
) else (
    echo Building NORMAL version ^(%ARCH%^)
)

if not exist "%OUT%\obj_gui" mkdir "%OUT%\obj_gui"
cl /nologo /O1 /GL /Gy /Gw /GS- /W3 /MT /utf-8 /DNDEBUG /DXXWIDGETS_STATIC /DXXFC_STATIC !EXTRA_DEFINES! ^
   -I"%SRC%" -I"%GUI%" -I"%RES%" -I"%XXW_INC%" -I"%XXF_INC%" !CDIS_INC_ARG! ^
   !GUI_SOURCES! "%OUT%\cdie_gui.res" /Fe:"%OUT%\cdie_gui.exe" /Fo:"%OUT%\obj_gui\\" ^
      /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF /LTCG /DEBUG:NONE /MAP:"%OUT%\cdie_gui.map" ^
   "%XXW_LIB%" "%XXF_LIB%" !CDIS_ARG! ^
      user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib ole32.lib advapi32.lib dwmapi.lib uxtheme.lib msimg32.lib winhttp.lib bcrypt.lib crypt32.lib
if errorlevel 1 ( echo BUILD FAILED ^(cdie_gui.exe, %ARCH%^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie_gui.exe

if "%BUILD_INSTALLER%"=="0" (
    if not exist "%ROOT%\release" mkdir "%ROOT%\release"
    if "%STORE_BUILD%"=="1" (
        copy /y "%OUT%\cdie_gui.exe" "%ROOT%\release\cdie_gui_store_%ARCH%.exe" >nul
        echo Copied to %ROOT%\release\ : cdie_gui_store_%ARCH%.exe
    ) else (
        copy /y "%OUT%\cdie_gui.exe" "%ROOT%\release\cdie_gui_%ARCH%.exe" >nul
        echo Copied to %ROOT%\release\ : cdie_gui_%ARCH%.exe
    )
)

if "%BUILD_INSTALLER%"=="1" (
    echo.
    echo Building installer ^(%ARCH%^)...
    if not exist "%ROOT%\build_msvc" mkdir "%ROOT%\build_msvc"
    copy /y "%OUT%\cdie_gui.exe" "%ROOT%\build_msvc\cdie_gui.exe" >nul
    rem build_installer.cmd sees CDIE_ARCH (inherited environment variable)
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
    rem remove only this arch's temp folder; drop the parent only if now empty
    if defined SETUP_COPIED (
        if exist "!OUT!" rmdir /s /q "!OUT!"
        rmdir "!WORK_ROOT!" 2>nul
    )
)
endlocal
exit /b 0

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
rem   Removes the temp build folder (all archs). With "all", also the CMake build
rem   folders of the dependencies (the next build then recompiles them, which is slow).
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
    for %%d in ("!DEP!\xxwidgets\build" "!DEP!\xxwidgets\build_x64" "!DEP!\xxwidgets\build_x86" "!DEP!\xxwidgets\build_arm64" ^
                "!DEP!\xxfclib\build"   "!DEP!\xxfclib\build_x64"   "!DEP!\xxfclib\build_x86"   "!DEP!\xxfclib\build_arm64") do (
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
rem   Configure + build a CMake dependency as a Release static lib with /MT into
rem   <source dir>\build_<arch>. Uses Ninja if available, otherwise NMake Makefiles:
rem   both use the cl on PATH (the arch set up by vcvarsall), unlike the default
rem   Visual Studio generator, which would target the host arch.
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
set GEN=-G "NMake Makefiles"
where ninja >nul 2>&1 && set "GEN=-G Ninja"
set "TGT="
if not "%~2"=="" set "TGT=--target %~2"
echo Building %~1 for %ARCH% ...
cmake -S "%~1" -B "%~1\build_%ARCH%" !GEN! -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DXXWIDGETS_BUILD_EXAMPLES=OFF -DXXWIDGETS_BUILD_TESTS=OFF -DXXWIDGETS_BUILD_SETTINGS=OFF -DBUILD_TESTING=OFF -DCMAKE_C_FLAGS_RELEASE="/O1 /Oi /Gy /GF /Gw /GL /DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="/O1 /Oi /Gy /GF /Gw /GL /DNDEBUG" -DCMAKE_STATIC_LINKER_FLAGS_RELEASE=/LTCG
if errorlevel 1 (
    echo ERROR: cmake configure failed for %~1
    rmdir /s /q "%~1\build_%ARCH%" 2>nul
    exit /b 1
)
cmake --build "%~1\build_%ARCH%" --config Release !TGT!
if errorlevel 1 (
    echo ERROR: cmake build failed for %~1
    exit /b 1
)
exit /b 0