@echo off
rem Build and start Computer Inspector on Windows.
rem
rem   run.bat           build, start the server, open the dashboard in the browser
rem   run.bat --agent   also start an agent on this computer (to test without other PCs)
rem
rem The server and agent open in their own windows; close them to stop.
rem Builds with Ninja, so run it where a compiler is on PATH: MinGW g++, or the
rem "x64 Native Tools Command Prompt for VS" for Visual Studio.
rem If layout.json exists here (seat map) and is newer, it is copied next to the server first.
setlocal
cd /d "%~dp0"

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
rem /d: copy only when newer, so a map saved from the dashboard editor is kept
if exist layout.json xcopy /d /y /q layout.json "%BIN%\" >nul

start "Computer Inspector server" "%BIN%\server.exe"
if /i "%~1"=="--agent" start "Computer Inspector agent" "%BIN%\agent.exe" --host 127.0.0.1
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:8081
echo Running. Close the "Computer Inspector server" window to stop.
