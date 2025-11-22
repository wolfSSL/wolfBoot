::!/cmd/batch
::
:: cmake_dev_prompt_test.bat
::
:: Some cmake tests from a VS 2022 dev prompt
::
:: We start in /tools/scripts, but build two directories up: from wolfBoot root
::
:: Example:
:: C:\workspace\wolfBoot>.\tools\scripts\cmake_dev_prompt_test.bat
::
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


echo Remove prior build directories...
if exist "build-stm32l4" rmdir /s /q "build-stm32l4" || goto :err


:: Begin tests and examples

cmake --preset stm32l4


:: cmake --build --preset stm32l4 --parallel 4 -v

cmake --build --preset stm32l4

:: Deleting immediately may cause an anti-virus file-locking problem
:: rmdir /s /q build-stm32l4

goto done

:err
echo Failed
exit /b 1

:done
echo Done.
exit /b 0
