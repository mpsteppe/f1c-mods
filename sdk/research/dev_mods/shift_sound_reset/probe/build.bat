@echo off
REM Shift Sound Probe 0.1.1 (research build). Output goes to %TEMP%\shift_sound_probe unless OUT is set.
REM Refuses to overwrite an existing DLL; move the old one aside first.
setlocal
cd /d "%~dp0"
if not defined F1C_SDK_DIR set "F1C_SDK_DIR=%~dp0..\..\..\..\tools\sdk"
REM windres runs the MinGW preprocessor, so it must be on PATH (script-local).
set "PATH=C:\msys64\mingw32\bin;%PATH%"
set "CC=C:\msys64\mingw32\bin\gcc.exe"
set "RC=C:\msys64\mingw32\bin\windres.exe"
if not exist "%F1C_SDK_DIR%\f1c_broker.c" exit /b 1
if not exist "%CC%" exit /b 1
if not defined OUT set "OUT=%TEMP%\shift_sound_probe"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\shift_sound_probe.dll" exit /b 9
"%RC%" -O coff -o "%OUT%\shift_sound_probe_meta.o" "shift_sound_probe_meta.rc"
if errorlevel 1 exit /b 1
"%CC%" -shared -O2 -s -Wall -Wextra -Werror -std=gnu11 -static-libgcc -fvisibility=hidden -I"%F1C_SDK_DIR%" -o "%OUT%\shift_sound_probe.dll" "shift_sound_probe.c" "%F1C_SDK_DIR%\f1c_broker.c" "%OUT%\shift_sound_probe_meta.o" -lkernel32
if errorlevel 1 exit /b 1
echo Built %OUT%\shift_sound_probe.dll
