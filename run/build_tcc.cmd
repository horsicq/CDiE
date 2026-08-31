@echo off
rem ---------------------------------------------------------------------------
rem Build cdie with TCC (Tiny C Compiler) on Windows. Produces, under build_tcc\:
rem
rem   cdie.exe            the console scanner (ordinary main(), links msvcrt)
rem   die.dll, die.def    the die_library-compatible shared library (+ TCC .def)
rem   libdie.a            the static library
rem
rem TCC cannot emit the CRT-free MSVC layout nor an MSVC-style import .lib; the
rem .def it writes next to the DLL is the TCC equivalent of the import library.
rem
rem Usage:  run\build_tcc.cmd
rem tcc.exe must be on PATH, or set TCC to its full path:  set "TCC=C:\tcc\tcc.exe"
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "OUT=%ROOT%\build_tcc"

if "%TCC%"=="" set "TCC=tcc"
if not exist "%TCC%" (
    where "%TCC%" >nul 2>&1
    if errorlevel 1 (
        echo TCC not found. Put tcc.exe on PATH or set TCC to its full path.
        exit /b 1
    )
)

if not exist "%OUT%" mkdir "%OUT%"

rem --- Source lists ----------------------------------------------------------
rem src\gui is the Windows GUI front end (its own entry point): it must not be
rem pulled into cdie.exe or the libraries, so it is skipped by path here.
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

set "DEF=-DNDEBUG -D_CRT_SECURE_NO_WARNINGS"

rem --- 1. cdie.exe -----------------------------------------------------------
echo [1/3] cdie.exe...
"%TCC%" %DEF% -I"%SRC%" !EXE_SOURCES! -o "%OUT%\cdie.exe" -lkernel32
if errorlevel 1 ( echo BUILD FAILED ^(cdie.exe^) & exit /b 1 )

rem --- 2. die.dll + die.def (shared) ----------------------------------------
echo [2/3] die.dll + die.def (shared library)...
"%TCC%" -shared -DDIE_BUILD_SHARED %DEF% -I"%SRC%" -I"%ROOT%\lib" !LIB_SOURCES! -o "%OUT%\die.dll"
if errorlevel 1 ( echo BUILD FAILED ^(die.dll^) & exit /b 1 )

rem --- 3. libdie.a (static) --------------------------------------------------
echo [3/3] libdie.a (static library)...
if not exist "%OUT%\obj_static" mkdir "%OUT%\obj_static"
set "OBJS="
for %%f in (!LIB_SOURCES!) do (
    "%TCC%" -c -DDIE_STATIC %DEF% -I"%SRC%" -I"%ROOT%\lib" "%%~f" -o "%OUT%\obj_static\%%~nf.o"
    if errorlevel 1 ( echo BUILD FAILED ^(%%~nxf^) & exit /b 1 )
    set "OBJS=!OBJS! "%OUT%\obj_static\%%~nf.o""
)
"%TCC%" -ar rcs "%OUT%\libdie.a" !OBJS!
if errorlevel 1 ( echo BUILD FAILED ^(libdie.a^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie.exe  die.dll  die.def  libdie.a
endlocal
