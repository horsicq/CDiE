@echo off
setlocal
rem Repo convention:
rem - build trees and CPack staging always live under %TEMP%
rem - the repository release\ directory stores only ready-to-use packages
rem
rem cdie has no external dependencies, so unlike the Qt projects this script
rem takes no Qt root: just the target platform and a package suffix.
rem
rem   build_portable_windows.cmd <cmake-platform> <package-suffix>
rem   build_portable_windows.cmd x64 win64
rem
rem Optional environment variables:
rem   CMAKE_GENERATOR_NAME  default "Visual Studio 17 2022"
rem   CDIE_DATABASE_DIR     directory holding db, db_extra and db_custom;
rem                         set to NONE to package the binary alone

if "%~2"=="" (
    echo Usage: %~nx0 ^<cmake-platform^> ^<package-suffix^>
    echo Example: %~nx0 x64 win64
    exit /b 1
)

for %%I in ("%~dp0..\..") do set "PROJECT_ROOT=%%~fI"

set "CMAKE_PLATFORM=%~1"
set "PACKAGE_SUFFIX=%~2"
if "%CMAKE_GENERATOR_NAME%"=="" set "CMAKE_GENERATOR_NAME=Visual Studio 17 2022"

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
set "CPACK_DIR=%WORK_ROOT%\cpack"
set "CPACK_OUTPUT_DIR=%WORK_ROOT%\output"

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
    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR_NAME%" -A %CMAKE_PLATFORM% "%DB_ARG%"
) else (
    cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR_NAME%" -A %CMAKE_PLATFORM%
)
if errorlevel 1 exit /b 1

echo Building %PACKAGE_SUFFIX% Release...
cmake --build "%BUILD_DIR%" --config Release --clean-first
if errorlevel 1 exit /b 1

set "APP_EXE=%BUILD_DIR%\src\console\Release\%APP_NAME%"
set "CPACK_CONFIG=%BUILD_DIR%\CPackConfig.cmake"

if not exist "%APP_EXE%" (
    echo Built executable not found:
    echo   %APP_EXE%
    exit /b 1
)

if not exist "%CPACK_CONFIG%" (
    echo CPack config not found:
    echo   %CPACK_CONFIG%
    exit /b 1
)

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
if exist "%CPACK_DIR%" rmdir /s /q "%CPACK_DIR%"
if exist "%CPACK_OUTPUT_DIR%" rmdir /s /q "%CPACK_OUTPUT_DIR%"

echo Installing portable package folder...
cmake --install "%BUILD_DIR%" --config Release --prefix "%PACKAGE_DIR%"
if errorlevel 1 exit /b 1

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

echo Creating portable zip with CPack...
cpack --config "%CPACK_CONFIG%" -G ZIP -C Release -B "%CPACK_DIR%" -D "CPACK_OUTPUT_FILE_PREFIX=%CPACK_OUTPUT_DIR%"
if errorlevel 1 exit /b 1

set "CPACK_ZIP="
for /r "%CPACK_OUTPUT_DIR%" %%F in (*.zip) do (
    set "CPACK_ZIP=%%~fF"
    goto cpack_zip_found
)

echo CPack did not produce a zip archive in:
echo   %CPACK_OUTPUT_DIR%
exit /b 1

:cpack_zip_found
for %%I in ("%CPACK_ZIP%") do set "PACKAGE_ZIP=%RELEASE_DIR%\%%~nxI"
if exist "%PACKAGE_ZIP%" del /f /q "%PACKAGE_ZIP%"
copy /y "%CPACK_ZIP%" "%PACKAGE_ZIP%" >nul
if errorlevel 1 exit /b 1

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"

echo.
echo Portable package folder created:
echo   %PACKAGE_DIR%
echo Portable zip created by CPack:
echo   %PACKAGE_ZIP%

endlocal
