@echo off
REM ============================================
REM CAD Locker Build Script
REM ============================================
REM 
REM This script compiles both stub.exe and builder.exe
REM 
REM Supports:
REM   - MSVC (Visual Studio cl.exe)
REM   - MinGW (gcc)
REM   - TCC (Tiny C Compiler)
REM 
REM Usage: 
REM   Simply run: build.bat
REM ============================================

setlocal enabledelayedexpansion

echo.
echo ============================================
echo    CAD Locker Build Script
echo ============================================
echo.

cd /d "%~dp0"

REM Try to find a compiler
set COMPILER=none

REM Check for MSVC
where cl >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set COMPILER=msvc
    goto :compile
)

REM Check for GCC (MinGW)
where gcc >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set COMPILER=gcc
    goto :compile
)

REM Check for TCC
where tcc >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    set COMPILER=tcc
    goto :compile
)

REM No compiler found
echo ERROR: No C compiler found!
echo.
echo Please install one of the following:
echo   - Visual Studio (with C++ tools)
echo   - MinGW-w64 (https://www.mingw-w64.org/)
echo   - TCC (https://bellard.org/tcc/)
echo.
echo Or run from Visual Studio Developer Command Prompt
goto :error

:compile
echo Using compiler: %COMPILER%
echo.

if "%COMPILER%"=="msvc" (
    echo [1/2] Compiling stub.exe with MSVC...
    cl /nologo /O2 /Fe:stub.exe stub.c /link shell32.lib advapi32.lib user32.lib
    if %ERRORLEVEL% NEQ 0 goto :error
    
    echo.
    echo [2/2] Compiling builder.exe with MSVC...
    cl /nologo /O2 /Fe:builder.exe builder.c
    if %ERRORLEVEL% NEQ 0 goto :error
    
    REM Clean up obj files
    del *.obj 2>nul
)

if "%COMPILER%"=="gcc" (
    echo [1/2] Compiling stub.exe with GCC...
    gcc -O2 -o stub.exe stub.c -lshell32 -ladvapi32 -luser32 -mwindows
    if %ERRORLEVEL% NEQ 0 goto :error
    
    echo.
    echo [2/2] Compiling builder.exe with GCC...
    gcc -O2 -o builder.exe builder.c
    if %ERRORLEVEL% NEQ 0 goto :error
)

if "%COMPILER%"=="tcc" (
    echo [1/2] Compiling stub.exe with TCC...
    tcc -o stub.exe stub.c -lshell32 -ladvapi32 -luser32
    if %ERRORLEVEL% NEQ 0 goto :error
    
    echo.
    echo [2/2] Compiling builder.exe with TCC...
    tcc -o builder.exe builder.c
    if %ERRORLEVEL% NEQ 0 goto :error
)

echo.
echo ============================================
echo    BUILD SUCCESSFUL!
echo ============================================
echo.
echo Created:
echo   - stub.exe    (template viewer)
echo   - builder.exe (designer tool)
echo.
echo Usage:
echo   1. Drag a .dwg file onto builder.exe
echo   2. Enter a suffix (e.g., _secure)
echo   3. Distribute the generated .exe to clients
echo.
goto :end

:error
echo.
echo ============================================
echo    BUILD FAILED!
echo ============================================
echo.

:end
pause
