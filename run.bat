@echo off
rem Build and start Computer Inspector on Windows.
rem
rem   run.bat           build, start the server, open the dashboard in the browser
rem   run.bat --agent   also start an agent on this computer (to test without other PCs)
rem
rem The server and agent open in their own windows; close them to stop.
rem If layout.json exists here (seat map), it is copied next to the server first.
setlocal
cd /d "%~dp0"

where cmake >nul 2>nul || (echo CMake is not installed. Install it from cmake.org and run again. & exit /b 1)
if not exist build (cmake -S . -B build || exit /b 1)
cmake --build build --config Release --target server agent || exit /b 1

rem MinGW puts the .exe in build\, Visual Studio in build\Release\
set "BIN=build"
if exist "build\Release\server.exe" set "BIN=build\Release"
if not exist "%BIN%\server.exe" (echo server.exe not found after building. & exit /b 1)
if exist layout.json copy /y layout.json "%BIN%\layout.json" >nul

start "Computer Inspector server" "%BIN%\server.exe"
if /i "%~1"=="--agent" start "Computer Inspector agent" "%BIN%\agent.exe" --host 127.0.0.1
timeout /t 2 /nobreak >nul
start "" http://127.0.0.1:8081
echo Running. Close the "Computer Inspector server" window to stop.
