@echo off
rem ---------------------------------------------------------------------------
rem Build cdie with MSVC directly (no CMake). Produces, under build_msvc\:
rem
rem   cdie.exe         the console scanner - CRT-free, imports KERNEL32 only
rem   die.dll, die.lib the die_library-compatible shared library + import lib
rem   die_static.lib   the static library (link the whole engine in)
rem
rem Usage (from any prompt):  run\build_msvc.cmd
rem The script loads the Visual Studio x64 environment itself; override with
rem   set "VCVARS=...\vcvars64.bat"
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "OUT=%ROOT%\build_msvc"

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

rem --- Source lists ----------------------------------------------------------
rem Console: every .c under src (core, js, format, engine, console).
rem Library: the same, minus the two entry points, plus lib\die.c.
rem src\gui is the Windows GUI front end (its own WinMain / entry point and
rem _fltused/mem* copies): it must NOT go into cdie.exe or the libraries, so it
rem is skipped by path here and built separately by run\build_msvc_gui.cmd.
set "EXE_SOURCES="
set "LIB_SOURCES="
for /r "%SRC%" %%f in (*.c) do (
    set "skip="
    echo %%~dpf| findstr /i "\\gui\\" >nul && set "skip=1"
    if not defined skip (
        set "EXE_SOURCES=!EXE_SOURCES! "%%f""
        set "keep=1"
        if /i "%%~nxf"=="utils_entry.c" set "keep="
        if /i "%%~nxf"=="main_console.c" set "keep="
        if defined keep set "LIB_SOURCES=!LIB_SOURCES! "%%f""
    )
)
set "LIB_SOURCES=!LIB_SOURCES! "%ROOT%\lib\die.c""

rem --- 1. cdie.exe (CRT-free, KERNEL32 only) ---------------------------------
echo [1/3] cdie.exe (CRT-free, KERNEL32 only)...
if not exist "%OUT%\obj_exe" mkdir "%OUT%\obj_exe"
cl /nologo /O2 /W3 /GS- /DNDEBUG -DCDIE_NO_CRT -I"%SRC%" ^
   !EXE_SOURCES! /Fe:"%OUT%\cdie.exe" /Fo:"%OUT%\obj_exe\\" ^
   /link /NODEFAULTLIB /ENTRY:x_entry_point /SUBSYSTEM:CONSOLE kernel32.lib
if errorlevel 1 ( echo BUILD FAILED ^(cdie.exe^) & exit /b 1 )

rem --- 2. die_static.lib (static, CRT-free, DIE_STATIC) ----------------------
echo [2/3] die_static.lib (static library, CRT-free)...
if not exist "%OUT%\obj_static" mkdir "%OUT%\obj_static"
cl /nologo /O2 /W3 /Zl /GS- /kernel /GR- /EHsc- /DNDEBUG -DDIE_STATIC -I"%SRC%" -I"%ROOT%\lib" ^
   /c !LIB_SOURCES! /Fo"%OUT%\obj_static\\"
if errorlevel 1 ( echo BUILD FAILED ^(die_static compile^) & exit /b 1 )
lib /nologo /OUT:"%OUT%\die_static.lib" "%OUT%\obj_static\*.obj"
if errorlevel 1 ( echo BUILD FAILED ^(die_static.lib^) & exit /b 1 )

rem --- 3. die.dll + die.lib (shared, default CRT, DIE_BUILD_SHARED) ----------
rem The shared library links the default CRT (matching lib/CMakeLists.txt: the
rem CRT-free flags there are static-only). A CRT-free DLL would need its own
rem DllMain / _fltused / mem* - which live in the entry files the libraries
rem deliberately exclude - so the DLL keeps the CRT and the loader-provided
rem _DllMainCRTStartup.
echo [3/3] die.dll + die.lib (shared library, default CRT)...
if not exist "%OUT%\obj_shared" mkdir "%OUT%\obj_shared"
cl /nologo /O2 /W3 /MD /GS- /DNDEBUG -DDIE_BUILD_SHARED -I"%SRC%" -I"%ROOT%\lib" ^
   /c !LIB_SOURCES! /Fo"%OUT%\obj_shared\\"
if errorlevel 1 ( echo BUILD FAILED ^(die_shared compile^) & exit /b 1 )
link /nologo /DLL /OUT:"%OUT%\die.dll" /IMPLIB:"%OUT%\die.lib" ^
   "%OUT%\obj_shared\*.obj"
if errorlevel 1 ( echo BUILD FAILED ^(die.dll^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie.exe  die.dll  die.lib  die_static.lib
endlocal
