@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "CMAKE_EXE="
where cmake >nul 2>nul
if not errorlevel 1 set "CMAKE_EXE=cmake"
if defined CMAKE_EXE goto :have_cmake

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_cmake

set "CMAKE_LIST=%TEMP%\asciiart_cmake_path.txt"
"%VSWHERE%" -latest -prerelease -products * -find "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" > "%CMAKE_LIST%" 2>nul
if not exist "%CMAKE_LIST%" goto :no_cmake

for /f "usebackq delims=" %%I in ("%CMAKE_LIST%") do (
    if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
)
del "%CMAKE_LIST%" >nul 2>nul
if not defined CMAKE_EXE goto :no_cmake

:have_cmake
echo Using CMake: %CMAKE_EXE%
"%CMAKE_EXE%" -S . -B build -A x64
if errorlevel 1 exit /b 1

"%CMAKE_EXE%" --build build --config Release --parallel
if errorlevel 1 exit /b 1

echo.
echo Built: build\Release\ASCIIArt.exe
echo Run:   run.bat
echo.
exit /b 0

:no_cmake
echo.
echo [ERROR] CMake was not found.
echo Install Visual Studio with the "Desktop development with C++" workload,
echo or install CMake and add it to PATH.
echo.
exit /b 1
