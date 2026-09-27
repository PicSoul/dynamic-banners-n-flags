@echo off
setlocal enabledelayedexpansion
rem Installs (or removes) the plugin in American Truck Simulator\bin\win_x64\plugins\
rem   install.bat                      build if needed and install (ATS is found through Steam)
rem   install.bat /uninstall           remove the plugin
rem   install.bat "D:\...\win_x64"     use this ATS bin\win_x64 folder instead of searching
cd /d "%~dp0"

set "ATS_DIR="
if not "%~1"=="" if /i not "%~1"=="/uninstall" set "ATS_DIR=%~1"
if "%ATS_DIR%"=="" for /f "delims=" %%P in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\find_ats.ps1"') do set "ATS_DIR=%%P"

if "%ATS_DIR%"=="" (
    echo [ERROR] Could not find American Truck Simulator through Steam.
    echo Run:  install.bat "^<ATS install^>\bin\win_x64"
    echo or copy bin\dynamic_banners.dll into ^<ATS install^>\bin\win_x64\plugins\ yourself.
    exit /b 1
)
set "PLUGINS_DIR=%ATS_DIR%\plugins"
echo American Truck Simulator: "%ATS_DIR%"

if /i "%~1"=="/uninstall" (
    del /q "%PLUGINS_DIR%\dynamic_banners.dll" "%PLUGINS_DIR%\dynamic_banners.ini" "%PLUGINS_DIR%\dynamic_banners.log" 2>nul
    echo Dynamic Banners-N-Flags removed.
    exit /b 0
)

if not exist "bin\dynamic_banners.dll" (
    call "%~dp0build.bat" || exit /b 1
)

if not exist "%PLUGINS_DIR%" mkdir "%PLUGINS_DIR%"
copy /Y "bin\dynamic_banners.dll" "%PLUGINS_DIR%\" >nul || (echo [ERROR] Copy failed - is the game running? & exit /b 1)
if not exist "%PLUGINS_DIR%\dynamic_banners.ini" copy "config\dynamic_banners.ini" "%PLUGINS_DIR%\" >nul

echo Installed to "%PLUGINS_DIR%" (an existing dynamic_banners.ini is kept).
echo Start ATS, accept the SDK plugin prompt, and look for "[Dynamic Banners] ... active" in the console (~).
