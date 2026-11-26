@echo off
REM expanded_setup_and_hud 1.0.0 build & unit test script.
REM Output goes to %TEMP%\expanded_setup_and_hud_build (never into the repo).
setlocal
set "PATH=C:\msys64\mingw32\bin;C:\msys64\usr\bin;%PATH%"
cd /d "%~dp0"
set "CC=C:\msys64\mingw32\bin\gcc.exe"
set "RC=C:\msys64\mingw32\bin\windres.exe"
if not defined OUT set "OUT=%TEMP%\expanded_setup_and_hud_build"
if not exist "%CC%" exit /b 1
if not exist "%RC%" exit /b 1
if not exist "%OUT%" mkdir "%OUT%"

"%RC%" -I "%~dp0." -O coff -o "%OUT%\expanded_setup_and_hud_meta.o" "expanded_setup_and_hud_meta.rc"
if errorlevel 1 exit /b %errorlevel%

"%CC%" -shared -O2 -s -Wall -Wextra -Werror -std=gnu11 -static-libgcc -fvisibility=hidden -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--build-id=none -I "%~dp0." -I "%~dp0src" -o "%OUT%\expanded_setup_and_hud.dll" "src\expanded_setup_and_hud.c" "%OUT%\expanded_setup_and_hud_meta.o" -lkernel32 -luser32
if errorlevel 1 exit /b %errorlevel%

"%CC%" -O2 -Wall -Wextra -Werror -std=gnu11 -static-libgcc -o "%OUT%\test_damage_color.exe" "tests\test_damage_color.c" -lm
if errorlevel 1 exit /b %errorlevel%

"%OUT%\test_damage_color.exe"
exit /b %errorlevel%
