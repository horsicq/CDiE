@echo off
rem ---------------------------------------------------------------------------
rem Build the cdie GUI (pure WinAPI) with MSVC directly (no CMake). Produces:
rem
rem   build_msvc\cdie_gui.exe   CRT-free, imports KERNEL32 + the GUI system DLLs
rem
rem The engine is reused: every .c under src except the console entry points
rem (utils_entry.c, main_console.c) and src\gui is compiled in, plus the three
rem GUI sources and the resource script (manifest + version info).
rem
rem Usage (from any prompt):  run\build_msvc_gui.cmd
rem The script loads the Visual Studio x64 environment itself; override with
rem   set "VCVARS=...\vcvars64.bat"
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "OUT=%ROOT%\build_msvc"
set "GUI=%SRC%\gui"

rem --- Visual Studio x64 environment -----------------------------------------
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

if not exist "%OUT%" mkdir "%OUT%"

rem --- Engine source list (engine minus entry points and the gui dir) --------
set "GUI_SOURCES="
for /r "%SRC%" %%f in (*.c) do (
    set "skip="
    echo %%~dpf| findstr /i "\\gui\\" >nul && set "skip=1"
    if /i "%%~nxf"=="utils_entry.c" set "skip=1"
    if /i "%%~nxf"=="main_console.c" set "skip=1"
    if not defined skip set "GUI_SOURCES=!GUI_SOURCES! "%%f""
)

rem --- The GUI's own sources -------------------------------------------------
set "GUI_SOURCES=!GUI_SOURCES! "%GUI%\gui_backend.c" "%GUI%\main_gui.c" "%GUI%\gui_entry.c""

rem --- 1. resources (manifest + version) ------------------------------------
echo [1/2] cdie_gui.res (manifest + version)...
rc /nologo /i "%GUI%" /fo "%OUT%\cdie_gui.res" "%GUI%\cdie_gui.rc"
if errorlevel 1 ( echo BUILD FAILED ^(cdie_gui.res^) & exit /b 1 )

rem --- 2. cdie_gui.exe (CRT-free, WINDOWS subsystem) ------------------------
echo [2/2] cdie_gui.exe (CRT-free, WinAPI)...
if not exist "%OUT%\obj_gui" mkdir "%OUT%\obj_gui"
cl /nologo /O2 /W3 /GS- /DNDEBUG -DCDIE_NO_CRT -I"%SRC%" ^
   !GUI_SOURCES! "%OUT%\cdie_gui.res" /Fe:"%OUT%\cdie_gui.exe" /Fo:"%OUT%\obj_gui\\" ^
   /link /NODEFAULTLIB /ENTRY:x_gui_entry_point /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF ^
   kernel32.lib user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib ole32.lib
if errorlevel 1 ( echo BUILD FAILED ^(cdie_gui.exe^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie_gui.exe
endlocal
