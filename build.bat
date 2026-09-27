@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo   Building SCS Dynamic Banners-N-Flags (x64 telemetry plugin)
echo ======================================================================

set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VS_PATH%" (
    echo [ERROR] Visual Studio 2022 vcvarsall.bat not found at "%VS_PATH%"
    exit /b 1
)
call "%VS_PATH%" x64 >nul 2>nul || exit /b 1

if not exist "bin" mkdir "bin"

cl /nologo /O2 /MD /std:c++20 /EHsc /W3 /D_WINDOWS /D_USRDLL /D_CRT_SECURE_NO_WARNINGS ^
   /I "include" /I "sdk\include" /I "libs\minhook\include" /I "libs\minhook\src" ^
   "src\main.cpp" ^
   "src\cloth_hook.cpp" ^
   "libs\minhook\src\buffer.c" "libs\minhook\src\hook.c" "libs\minhook\src\trampoline.c" "libs\minhook\src\hde\hde64.c" ^
   "src\logger.cpp" ^
   "src\config_manager.cpp" ^
   "src\pattern_scanner.cpp" ^
   "src\mem_safe.cpp" ^
   "src\game_layout.cpp" ^
   "src\visibility_controller.cpp" ^
   "src\telemetry_bridge.cpp" ^
   /LD /Fe:"bin\dynamic_banners.dll" /Fo:"bin\\" ^
   /link /EXPORT:scs_telemetry_init /EXPORT:scs_telemetry_shutdown

if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed!
    exit /b %errorlevel%
)

copy /Y "config\dynamic_banners.ini" "bin\dynamic_banners.ini" >nul

echo ======================================================================
echo   BUILD SUCCESSFUL: bin\dynamic_banners.dll + bin\dynamic_banners.ini
echo ======================================================================
