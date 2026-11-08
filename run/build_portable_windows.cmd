@echo off
setlocal
rem Repo convention:
rem - build trees and staging always live under %TEMP%
rem - the repository release\ directory stores only ready-to-use packages
rem
rem cdie has no external dependencies, so unlike the Qt projects this script
rem takes no Qt root: just the target platform and a package suffix.
rem
rem   build_portable_windows.cmd <cmake-platform> <package-suffix>
rem   build_portable_windows.cmd x64 win64
rem
rem The portable package is the console binary (plus docs and signature
rem database) only. Any .dll and .lib files are removed from it; build those
rem with build_dll_windows.cmd and build_lib_windows.cmd.
rem
rem Optional environment variables:
rem   CMAKE_GENERATOR_NAME  default "Ninja". Any Visual Studio generator
rem                         (e.g. "Visual Studio 17 2022") also works, but
rem                         compiles the focused die_engine archive more slowly.
rem                         With Ninja the MSVC environment for the requested
rem                         platform is set up automatically (vswhere +
rem                         vcvarsall) unless cl.exe is already on PATH.
rem   CDIE_DATABASE_DIR     directory holding db, db_extra and db_custom;
rem                         set to NONE to package the binary alone

if "%~2"=="" (
    echo Usage: %~nx0 ^<cmake-platform^> ^<package-suffix^>
    echo Example: %~nx0 x64 win64
    exit /b 1
)

for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"

set "CMAKE_PLATFORM=%~1"
set "PACKAGE_SUFFIX=%~2"
if "%CMAKE_GENERATOR_NAME%"=="" set "CMAKE_GENERATOR_NAME=Ninja"

rem Single-config generators (plain Ninja, NMake) take the build type at
rem configure time, cannot take -A, and put the binaries directly in the
rem target directory. Multi-config ones (Visual Studio, Ninja Multi-Config)
rem take -A (VS only) and add a Release\ subdirectory.
set "GEN_IS_VS="
set "GEN_IS_MULTI="
if /i "%CMAKE_GENERATOR_NAME:~0,13%"=="Visual Studio" set "GEN_IS_VS=1"
if defined GEN_IS_VS set "GEN_IS_MULTI=1"
if /i "%CMAKE_GENERATOR_NAME%"=="Ninja Multi-Config" set "GEN_IS_MULTI=1"

if defined GEN_IS_VS (
    set "GEN_ARGS=-A %CMAKE_PLATFORM%"
) else if defined GEN_IS_MULTI (
    set "GEN_ARGS="
) else (
    set "GEN_ARGS=-DCMAKE_BUILD_TYPE=Release"
)

rem The portable package is the console binary only; skip building the
rem shared/static libraries from lib\ altogether.
set "GEN_ARGS=%GEN_ARGS% -DCDIE_BUILD_LIBRARY=OFF"

rem Size-optimised build, same flags as build_msvc_gui.cmd: /O1, static CRT (/MT),
rem function/data sections, LTCG, and dead-code/duplicate folding at link time.
set "GEN_ARGS=%GEN_ARGS% -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
set "GEN_ARGS=%GEN_ARGS% "-DCMAKE_C_FLAGS_RELEASE=/O1 /Oi /Gy /GF /Gw /GL /GS- /bigobj /DNDEBUG""
set "GEN_ARGS=%GEN_ARGS% "-DCMAKE_CXX_FLAGS_RELEASE=/O1 /Oi /Gy /GF /Gw /GL /GS- /bigobj /DNDEBUG""
set "GEN_ARGS=%GEN_ARGS% "-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/LTCG /OPT:REF /OPT:ICF /DEBUG:NONE""
set "GEN_ARGS=%GEN_ARGS% "-DCMAKE_STATIC_LINKER_FLAGS_RELEASE=/LTCG""

rem A Visual Studio generator finds the compiler itself. Every other
rem generator compiles with whatever cl.exe the environment provides, so the
rem environment has to target the requested platform.
if not defined GEN_IS_VS (
    call :setup_msvc_env
    if errorlevel 1 exit /b 1
)

set "WORK_ROOT=%TEMP%\cdie_%PACKAGE_SUFFIX%"
set "BUILD_DIR=%WORK_ROOT%\build"
set "APP_NAME=cdie.exe"
set "PACKAGE_BASENAME=cdie_%PACKAGE_SUFFIX%_portable"

if not exist "%PROJECT_ROOT%\release_version.txt" (
    echo release_version.txt not found:
    echo   %PROJECT_ROOT%\release_version.txt
    exit /b 1
)

set /p RELEASE_VERSION=<"%PROJECT_ROOT%\release_version.txt"
set "PACKAGE_NAME=%PACKAGE_BASENAME%_%RELEASE_VERSION%"
set "RELEASE_DIR=%PROJECT_ROOT%\release"
set "PACKAGE_DIR=%RELEASE_DIR%\%PACKAGE_NAME%"
set "PACKAGE_ZIP=%RELEASE_DIR%\%PACKAGE_NAME%.zip"

rem DB_ARG is passed as a single quoted token at the call site below, so a
rem CDIE_DATABASE_DIR under Program Files or a profile directory survives.
set "DB_ARG="
if not "%CDIE_DATABASE_DIR%"=="" set "DB_ARG=-DCDIE_DATABASE_DIR=%CDIE_DATABASE_DIR%"

if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"
if errorlevel 1 exit /b 1

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"
mkdir "%WORK_ROOT%"
if errorlevel 1 exit /b 1

echo Configuring %PACKAGE_SUFFIX% build...
if defined DB_ARG (
    echo Signature database: %CDIE_DATABASE_DIR%
    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR_NAME%" %GEN_ARGS% "%DB_ARG%"
) else (
    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR_NAME%" %GEN_ARGS%
)
if errorlevel 1 exit /b 1

echo Building %PACKAGE_SUFFIX% Release...
cmake --build "%BUILD_DIR%" --config Release --clean-first --parallel
if errorlevel 1 exit /b 1

if defined GEN_IS_MULTI (
    set "APP_EXE=%BUILD_DIR%\src\console\Release\%APP_NAME%"
) else (
    set "APP_EXE=%BUILD_DIR%\src\console\%APP_NAME%"
)

if not exist "%APP_EXE%" (
    echo Built executable not found:
    echo   %APP_EXE%
    exit /b 1
)

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"

echo Installing portable package folder...
cmake --install "%BUILD_DIR%" --config Release --prefix "%PACKAGE_DIR%"
if errorlevel 1 exit /b 1

rem Libraries are not part of the portable package (see build_dll_windows.cmd
rem and build_lib_windows.cmd).
echo Removing .dll and .lib files from the package...
for /r "%PACKAGE_DIR%" %%F in (*.dll *.lib) do del /f /q "%%F"

rem A package that quietly lost its signatures -- a mistyped or unquoted
rem CDIE_DATABASE_DIR fails the EXISTS test in CMakeLists.txt and only prints
rem one STATUS line -- is worse than a failed build, so assert on it here.
rem Set CDIE_DATABASE_DIR=NONE to package the binary alone on purpose.
if /i not "%CDIE_DATABASE_DIR%"=="NONE" (
    if not exist "%PACKAGE_DIR%\db\" (
        echo No signature database in the package:
        echo   %PACKAGE_DIR%\db
        echo Set CDIE_DATABASE_DIR to a directory holding db, db_extra and db_custom,
        echo or to NONE to package the binary alone.
        exit /b 1
    )
)

rem The zip is made from the cleaned package folder rather than by CPack,
rem because CPack stages its own install and would put the libraries back.
echo Creating portable zip...
if exist "%PACKAGE_ZIP%" del /f /q "%PACKAGE_ZIP%"
pushd "%RELEASE_DIR%"
cmake -E tar cf "%PACKAGE_ZIP%" --format=zip "%PACKAGE_NAME%"
set "ZIP_ERR=%errorlevel%"
popd
if not "%ZIP_ERR%"=="0" (
    echo Failed to create zip:
    echo   %PACKAGE_ZIP%
    exit /b 1
)

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"

echo.
echo Portable package folder created:
echo   %PACKAGE_DIR%
echo Portable zip created:
echo   %PACKAGE_ZIP%

endlocal
exit /b 0

rem ---------------------------------------------------------------------------
rem :setup_msvc_env -- make cl.exe for %CMAKE_PLATFORM% available.
rem
rem Kept out of parenthesised blocks on purpose: the vswhere path contains
rem "(x86)", and a ")" inside a block ends the block early.
:setup_msvc_env
set "VCVARS_ARCH="
set "VC_TARGET_ARCH="
if /i "%CMAKE_PLATFORM%"=="x64"   (set "VCVARS_ARCH=x64"       & set "VC_TARGET_ARCH=x64")
if /i "%CMAKE_PLATFORM%"=="Win32" (set "VCVARS_ARCH=x64_x86"   & set "VC_TARGET_ARCH=x86")
if /i "%CMAKE_PLATFORM%"=="ARM64" (set "VCVARS_ARCH=x64_arm64" & set "VC_TARGET_ARCH=arm64")
if not defined VCVARS_ARCH goto msvc_bad_platform

where cl.exe >nul 2>&1
if errorlevel 1 goto msvc_find_vcvars

rem cl.exe is already on PATH. A Developer Command Prompt records its target
rem in VSCMD_ARG_TGT_ARCH; refuse a mismatch rather than package, say, an x86
rem build under a win64 name.
if not defined VSCMD_ARG_TGT_ARCH goto msvc_ready
if /i "%VSCMD_ARG_TGT_ARCH%"=="%VC_TARGET_ARCH%" goto msvc_ready
echo The current MSVC environment targets %VSCMD_ARG_TGT_ARCH%, but %CMAKE_PLATFORM% needs %VC_TARGET_ARCH%.
echo Run from a plain command prompt to let the script set it up, or open the
echo matching Developer Command Prompt.
exit /b 1

:msvc_find_vcvars
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto msvc_no_vs
set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL goto msvc_no_vs
set "VCVARSALL=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" goto msvc_no_vs

echo MSVC environment: %VCVARS_ARCH% from %VS_INSTALL%
rem vcvarsall's own diagnostics are discarded (it complains on stderr that
rem vswhere.exe is not on PATH even when it succeeds); the "where cl.exe"
rem below is what decides whether it worked.
call "%VCVARSALL%" %VCVARS_ARCH% >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 goto msvc_no_vs

:msvc_ready
if /i not "%CMAKE_GENERATOR_NAME:~0,5%"=="Ninja" exit /b 0
where ninja.exe >nul 2>&1
if not errorlevel 1 exit /b 0
echo ninja.exe not found on PATH. Install the "C++ CMake tools for Windows"
echo Visual Studio component, or put ninja on PATH.
exit /b 1

:msvc_bad_platform
echo Unknown platform "%CMAKE_PLATFORM%" for %CMAKE_GENERATOR_NAME%: use x64, Win32 or ARM64.
exit /b 1

:msvc_no_vs
echo No MSVC toolset found for %CMAKE_GENERATOR_NAME%.
echo Run from a Developer Command Prompt for %CMAKE_PLATFORM%, or install
echo Visual Studio with the "Desktop development with C++" workload.
exit /b 1