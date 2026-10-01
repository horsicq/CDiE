@echo off
rem Convenience launcher: scans a target with the Detect It Easy databases.
rem
rem   run_cdie.cmd <target> [extra cdie options]
rem
rem Set CDIE_DB_ROOT to the directory that holds db, db_extra and db_custom.

setlocal

if "%CDIE_DB_ROOT%"=="" (
    if exist "%~dp0..\dep\Detect-It-Easy\db\" (
        set "CDIE_DB_ROOT=%~dp0..\dep\Detect-It-Easy"
    ) else (
        set "CDIE_DB_ROOT=%~dp0..\..\_mylibs\Detect-It-Easy"
    )
)
set CDIE_EXE=%~dp0..\..\cdie_build\src\console\cdie.exe

if not exist "%CDIE_EXE%" (
    echo cdie.exe not found at "%CDIE_EXE%".
    echo Build it first:  cmake -S . -B ..\cdie_build ^&^& cmake --build ..\cdie_build
    exit /b 1
)

"%CDIE_EXE%" -D "%CDIE_DB_ROOT%\db" -E "%CDIE_DB_ROOT%\db_extra" -C "%CDIE_DB_ROOT%\db_custom" %*

endlocal
