@echo off
rem Build and start Computer Inspector on Windows.
rem
rem   run.bat           build, start the server, open the dashboard in the browser
rem   run.bat --agent   also start an agent on this computer (to test without other PCs)
rem   run.bat --set-password   set a new teacher password before starting (asked first time anyway)
rem
rem The server and agent open in their own windows; close them to stop.
rem Builds with Ninja, so run it where a compiler is on PATH: MinGW g++, or the
rem "x64 Native Tools Command Prompt for VS" for Visual Studio.
rem If layout.json exists here (seat map) and is newer, it is copied next to the server first.
setlocal
cd /d "%~dp0"

set "AGENT=0"
set "SETPW=0"
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="--agent" (set "AGENT=1") else if /i "%~1"=="--set-password" (set "SETPW=1") else (echo Unknown option: %~1 & exit /b 1)
shift
goto args
:argsdone

where cmake >nul 2>nul || (echo CMake is not installed. Install it from cmake.org and run again. & exit /b 1)
where ninja >nul 2>nul || (echo Ninja is not installed. Install it with: winget install Ninja-build.Ninja  - or in MSYS2: pacman -S mingw-w64-ucrt-x86_64-ninja - then run again. & exit /b 1)

rem Ninja instead of CMake's default generator (NMake / Visual Studio). A build\ folder set up
rem with another generator is switched in place: only CMake's own cache is removed, so
rem layout.json and machines.json saved next to the server are kept.
rem Setup runs whenever build.ninja is missing, so a setup that failed earlier (for example
rem no compiler found) is tried again instead of building from nothing.
set "GEN="
if exist build\CMakeCache.txt for /f "tokens=2 delims==" %%g in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" build\CMakeCache.txt') do set "GEN=%%g"
if exist build\CMakeCache.txt if /i not "%GEN%"=="Ninja" (
  echo build\ was set up with %GEN%, switching it to Ninja...
  del /q build\CMakeCache.txt
  if exist build\CMakeFiles rmdir /s /q build\CMakeFiles
)
if not exist build\build.ninja (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release || (echo CMake could not set up the build. If it found no C++ compiler: add MinGW g++ to PATH, or run this from the x64 Native Tools Command Prompt for VS. & exit /b 1)
)
cmake --build build --target server agent || exit /b 1

rem Ninja puts the .exe files in build\
set "BIN=build"
if not exist "%BIN%\server.exe" (echo server.exe not found after building. & exit /b 1)
rem the teacher password: asked the first time (or with --set-password), typing is hidden
set "ASK=%SETPW%"
if not exist "%BIN%\auth.json" set "ASK=1"
if "%ASK%"=="1" (
  if not exist "%BIN%\auth.json" echo No teacher password yet. It protects the dashboard; set it now.
  set "TRIES=0"
  goto askpw
)
goto pwdone
:askpw
"%BIN%\server.exe" --set-password && goto pwdone
set /a TRIES+=1
if %TRIES% geq 3 (echo Password not set. Run run.bat --set-password to try again. & exit /b 1)
echo Try again.
goto askpw
:pwdone
rem /d: copy only when newer, so a map saved from the dashboard editor is kept
if exist layout.json xcopy /d /y /q layout.json "%BIN%\" >nul

start "Computer Inspector server" "%BIN%\server.exe"
if "%AGENT%"=="1" start "Computer Inspector agent" "%BIN%\agent.exe" --host 127.0.0.1 --pin-file "%BIN%\server.crt" --join-key-file "%BIN%\agents.json"
timeout /t 2 /nobreak >nul
start "" https://127.0.0.1:8081
echo Running. The browser warns about the certificate the first time: it is this server's own, accept it once.
echo Close the "Computer Inspector server" window to stop.
