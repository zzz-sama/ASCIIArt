@echo off
setlocal
cd /d "%~dp0"

if not exist "build\Release\ASCIIArt.exe" (
    call build.bat
    if errorlevel 1 exit /b 1
)

if "%~1"=="" (
    "build\Release\ASCIIArt.exe"
    exit /b %errorlevel%
)

"build\Release\ASCIIArt.exe" %*
set "RESULT=%errorlevel%"
echo.
pause
exit /b %RESULT%
