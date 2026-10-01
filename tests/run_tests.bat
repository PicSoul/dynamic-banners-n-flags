@echo off
setlocal
rem Builds and runs the unit tests.
rem Optional argument: a game executable for the signature test (default: every game found through Steam, needs Python).
cd /d "%~dp0\.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>nul || exit /b 1
if not exist "bin" mkdir "bin"
if exist "bin\test_config.ini" del "bin\test_config.ini"

cl /nologo /O2 /MD /std:c++20 /EHsc /W3 /D_CRT_SECURE_NO_WARNINGS ^
   /I "include" /I "sdk\include" ^
   "tests\test_mock.cpp" ^
   "src\pattern_scanner.cpp" "src\config_manager.cpp" "src\ini_upgrade.cpp" "src\logger.cpp" ^
   "src\mem_safe.cpp" "src\reflection.cpp" "src\visibility_controller.cpp" ^
   /Fe:"bin\unit_tests.exe" /Fo:"bin\\" || exit /b 1

rem Signature test: the given executable, or every SCS truck game found through Steam (ATS and/or ETS2).
setlocal enabledelayedexpansion
set "GAMES="
if not "%~1"=="" set GAMES="%~1"
if "%~1"=="" for /f "delims=" %%P in ('python tools\update_check.py --print-exe 2^>nul') do set GAMES=!GAMES! "%%P"
bin\unit_tests.exe !GAMES!
