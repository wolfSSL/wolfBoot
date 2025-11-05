@echo off
rem keystore_file_check.bat
rem Find files with the same name as given targets in any other local branch.

rem Defaults for wolfBoot if no args are given
set DEF1=wolfboot_signing_private_key.der
set DEF2=keystore.der
set DEF3=keystore.c

if "%~1"=="" (
    set T1=%DEF1%
    set T2=%DEF2%
    set T3=%DEF3%
    set TARGET_COUNT=3
) else (
    set T1=%~1
    set T2=%~2
    set T3=%~3
    set TARGET_COUNT=0
    if not "%~1"=="" set TARGET_COUNT=1
    if not "%~2"=="" set TARGET_COUNT=2
    if not "%~3"=="" set TARGET_COUNT=3
)

:: -------------------------------------------------------------------------------------------
:: Begin common section to start at repo root
:: -------------------------------------------------------------------------------------------
setlocal EnableExtensions EnableDelayedExpansion

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

rem === Print only if caller CWD is neither [root] nor [root]\scripts ===
if /I "%CALLER_CWD_P%"=="%REPO_ROOT_P%" goto after_print
if /I "%CALLER_CWD_P%"=="%REPO_ROOT_P%\scripts" goto after_print

echo Caller CWD = %CALLER_CWD_P%
echo SCRIPT_DIR  = %SCRIPT_DIR_P%
echo REPO_ROOT   = %REPO_ROOT_P%

:after_print
rem === Always work from repo root ===
pushd "%REPO_ROOT_P%" || goto :err
echo Starting %~nx0 from %CD%
:: -------------------------------------------------------------------------------------------
:: End common section to start at repo root
:: -------------------------------------------------------------------------------------------


rem Ensure we are in a git repo
git rev-parse --git-dir >nul 2>&1
if errorlevel 1 (
    echo Error: not inside a git repository.
    exit /b 2
)

rem Determine current branch (may be detached)
for /f "usebackq tokens=*" %%B in (`git rev-parse --abbrev-ref HEAD 2^>nul`) do set CUR_BRANCH=%%B
if "%CUR_BRANCH%"=="" set CUR_BRANCH=HEAD

rem Build list of local branches (excluding current, unless detached)
set "BR_LIST="
for /f "usebackq tokens=*" %%R in (`git for-each-ref --format^="%%(refname:short)" refs/heads/`) do (
    if /i "%CUR_BRANCH%"=="HEAD" (
        set "BR_LIST=!BR_LIST!;;%%R"
    ) else (
        if /i not "%%R"=="%CUR_BRANCH%" (
            set "BR_LIST=!BR_LIST!;;%%R"
        )
    )
)

set EXIT_CODE=0

call :PROCESS_TARGET "%T1%"
if %TARGET_COUNT% LSS 2 goto DONE
call :PROCESS_TARGET "%T2%"
if %TARGET_COUNT% LSS 3 goto DONE
call :PROCESS_TARGET "%T3%"
goto DONE

:PROCESS_TARGET
set "TARGET=%~1"
if "%TARGET%"=="" goto :eof

rem Normalize and get basename
set "TMPP=%TARGET:/=\%"
for %%G in ("%TMPP%") do set "BASENAME=%%~nxG"

echo === Searching for name: %BASENAME% ===

rem -------- FAST CURRENT WORKING TREE SCAN (tracked + untracked + ignored) --------
set CCNT=0

rem Tracked + staged + untracked (excluding ignored)
for /f "usebackq delims=" %%P in (`
  git ls-files -co --exclude-standard
`) do (
    set "PTH=%%P"
    set "PTHB=!PTH:/=\!"
    for %%Q in ("!PTHB!") do set "NM=%%~nxQ"
    if /i "!NM!"=="%BASENAME%" (
        if !CCNT! EQU 0 echo Paths in current branch:
        echo   .\!PTHB!
        set /a CCNT+=1
    )
)

rem Ignored files too (in case the file is generated and ignored)
for /f "usebackq delims=" %%P in (`
  git ls-files -i --others --exclude-standard
`) do (
    set "PTH=%%P"
    set "PTHB=!PTH:/=\!"
    for %%Q in ("!PTHB!") do set "NM=%%~nxQ"
    if /i "!NM!"=="%BASENAME%" (
        if !CCNT! EQU 0 echo Paths in current branch:
        echo   .\!PTHB!
        set /a CCNT+=1
    )
)

echo Current branch %CUR_BRANCH% has %CCNT% file(s) named %BASENAME%

rem -------- OTHER BRANCHES (tracked only) --------
set FOUND_ANY=0
for %%B in (%BR_LIST:;;= %) do (
    for /f "usebackq delims=" %%F in (`
      git ls-tree -r --name-only "%%B"
    `) do (
        set "OP=%%F"
        set "OPB=!OP:/=\!"
        for %%H in ("!OPB!") do set "ON=%%~nxH"
        if /i "!ON!"=="%BASENAME%" (
            if "!FOUND_ANY!"=="0" (
                echo Matches in other branches:
                set FOUND_ANY=1
            )
            echo   %%B:%%F
        )
    )
)

if "%FOUND_ANY%"=="0" (
    echo No matches in other branches.
) else (
    set EXIT_CODE=1
)
echo.
goto :eof

:DONE
endlocal & exit /b %EXIT_CODE%
