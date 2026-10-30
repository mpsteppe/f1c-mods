@echo off
REM Shift Sound Reset 0.1.3 (development build). Output goes to %TEMP%\shift_sound_reset unless OUT is set.
REM Builds and runs the headless test first; refuses to overwrite an existing DLL.
setlocal
cd /d "%~dp0"
if not defined F1C_SDK_DIR set "F1C_SDK_DIR=%~dp0..\..\..\..\tools\sdk"
REM windres runs the MinGW preprocessor, so it must be on PATH (script-local).
set "PATH=C:\msys64\mingw32\bin;%PATH%"
set "CC=C:\msys64\mingw32\bin\gcc.exe"
set "RC=C:\msys64\mingw32\bin\windres.exe"
if not exist "%F1C_SDK_DIR%\f1c_broker.c" exit /b 1
if not exist "%CC%" exit /b 1
if not defined OUT set "OUT=%TEMP%\shift_sound_reset"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\shift_sound_reset.dll" exit /b 9
"%CC%" -O2 -Wall -Wextra -Werror -std=gnu11 -I"%F1C_SDK_DIR%" -o "%OUT%\ssr_test.exe" "ssr_test.c" "%F1C_SDK_DIR%\f1c_broker.c"
if errorlevel 1 exit /b 1
"%OUT%\ssr_test.exe"
if errorlevel 1 exit /b 1
"%RC%" -O coff -o "%OUT%\shift_sound_reset_meta.o" "shift_sound_reset_meta.rc"
if errorlevel 1 exit /b 1
"%CC%" -shared -O2 -s -Wall -Wextra -Werror -std=gnu11 -static-libgcc -fvisibility=hidden -I"%F1C_SDK_DIR%" -o "%OUT%\shift_sound_reset.dll" "shift_sound_reset.c" "%F1C_SDK_DIR%\f1c_broker.c" "%OUT%\shift_sound_reset_meta.o" -lkernel32
if errorlevel 1 exit /b 1
copy /y "shift_sound_reset.ini" "%OUT%\shift_sound_reset.ini" >nul
echo Built %OUT%\shift_sound_reset.dll
