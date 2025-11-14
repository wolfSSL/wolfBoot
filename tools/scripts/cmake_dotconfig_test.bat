::!/cmd/batch
::
:: cmake_dotconfig_test.bat
::
:: See similar Ubuntu workflow: .guthub\workflows\test-build-cmake-dot-config.yml
::
:: Some cmake tests from a VS 2022 dev prompt
::
:: We start in /tools/scripts, but build two directories up: from wolfBoot root
::
:: Example:
:: C:\workspace\wolfBoot>.\tools\scripts\cmake_dotconfig_test.bat
::
:: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
::
:: TODO     This DOS Batch File is currently FAILING for .config cmake
::
::                Consider cmake presets or contact support
::
:: See also working Linux / WSL:  cmake_dot_config.sh
::
:: !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
@echo off
setlocal enableextensions enabledelayedexpansion

rem === Resolve script path/dir ===
set "SCRIPT_PATH=%~f0"
for %%I in ("%~dp0") do set "SCRIPT_DIR=%%~fI"

rem === Repo root is parent of /tools/scripts ===
for %%I in ("%SCRIPT_DIR%\..\..") do set "REPO_ROOT=%%~fI"

rem === Caller's current directory ===
for %%I in ("%CD%") do set "CALLER_CWD=%%~fI"

rem === (Optional) Normalize to physical paths via PowerShell to resolve junctions/symlinks ===
rem call :physpath "%SCRIPT_DIR%" SCRIPT_DIR_P
rem call :physpath "%REPO_ROOT%" REPO_ROOT_P
rem call :physpath "%CALLER_CWD%" CALLER_CWD_P
set "SCRIPT_DIR_P=%SCRIPT_DIR%"
set "REPO_ROOT_P=%REPO_ROOT%"
set "CALLER_CWD_P=%CALLER_CWD%"

rem === Print only if caller CWD is neither <root> nor <root>\scripts ===
if /I "%CALLER_CWD_P%"=="%REPO_ROOT_P%" goto after_print
if /I "%CALLER_CWD_P%"=="%REPO_ROOT_P%\scripts" goto after_print

echo Caller CWD = %CALLER_CWD_P%
echo SCRIPT_DIR  = %SCRIPT_DIR_P%
echo REPO_ROOT   = %REPO_ROOT_P%

:after_print
rem === Always work from repo root ===
pushd "%REPO_ROOT_P%" || goto :err
echo Starting %~nx0 from %CD%

:: Is CMake installed?
where cmake >nul 2>&1
if errorlevel 1 (
    echo This test should be run from a VS2022 dev prompt.
    echo Error: CMake is not installed or not found in path.
    goto err
)


set "SRC=.\config\examples\stm32h7.config"
set "DST=.\.config"


echo Remove prior build directories...

if exist "build-stm32h7" rmdir /s /q "build-stm32h7" || goto :err

rem Exit if the .config file already exists (perhaps it is a valid file? we'll delete our copy when done here')
if exist ".config" (
    >&2 echo ERROR: .config already exists! We need to copy a new test file. Delete or save existing file.
    goto :err
)

rem cp   ./config/examples/stm32h7.config ./.config

echo "Source config file: %SRC%"
echo "Destination file:   %DST%"

rem Ensure source exists
if not exist "%SRC%" (
    >&2 echo ERROR: Source not found: "%SRC%"
    goto :err
)

echo copy %SRC% %DST%
copy "%SRC%" "%DST%"
if errorlevel 1 (
    >&2 echo ERROR: Copy failed.
    goto :err
)

rem Verify destination exists and is non-empty
if not exist "%DST%" (
    >&2 echo ERROR: Destination missing after copy: "%DST%"
    goto :err
)

echo( & rem blank line
echo Current directory:
pwd

echo( & rem blank line
dir .config
echo( & rem blank line
echo .config contents:

echo( & rem blank line
type .config
echo( & rem blank line

echo Begin dot-config test

cmake -S . -B build-stm32h7  -DUSE_DOT_CONFIG=ON  -DWOLFBOOT_TARGET=stm32h7

cmake --build build-stm32h7 --parallel 1

del .config

goto done

:err
echo Failed
exit /b 1

:done
echo Done.
exit /b 0
