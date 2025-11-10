::!/cmd/batch
::
:: cmake_test.bat
::
:: Unlike the cmake_dev_prompt_test.bat that is expected to run in a VS 2022 Dev Prompt
:: This test manually sets paths to cmake and include files (also assumes VS 2022 is installed, but can be any suitable path)
cls

set "Path=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\bin\HostX86\x86;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\VCPackages;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TestWindow;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\bin\Roslyn;C:\Program Files (x86)\Microsoft SDKs\Windows\v10.0A\bin\NETFX 4.8 Tools\;C:\Program Files (x86)\HTML Help Workshop;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\FSharp\Tools;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\DiagnosticsHub\Collector;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\Extensions\Microsoft\CodeCoverage.Console;C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\\x86;C:\Program Files (x86)\Windows Kits\10\bin\\x86;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\\MSBuild\Current\Bin\amd64;C:\Windows\Microsoft.NET\Framework\v4.0.30319;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\;C:\Program Files (x86)\VMware\VMware Workstation\bin\;C:\Program Files\Microsoft\jdk-11.0.16.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\dotnet\;C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\;C:\Program Files\Microsoft SQL Server\150\Tools\Binn\;C:\Program Files\Git\cmd;C:\SysGCC\esp32-master\tools\riscv32-esp-elf\esp-15.2.0_20250920\riscv32-esp-elf\bin;C:\SysGCC\esp32-master\tools\xtensa-esp-elf\esp-15.2.0_20250920\xtensa-esp-elf\bin;C:\Program Files (x86)\VMware\VMware Workstation\bin\;C:\Program Files\Microsoft\jdk-11.0.16.101-hotspot\bin;C:\WINDOWS\system32;C:\WINDOWS;C:\WINDOWS\System32\Wbem;C:\WINDOWS\System32\WindowsPowerShell\v1.0\;C:\WINDOWS\System32\OpenSSH\;C:\Program Files\dotnet\;C:\Program Files\Git\cmd;C:\Users\gojimmypi\AppData\Local\Microsoft\WindowsApps;C:\Users\gojimmypi\AppData\Local\Programs\Microsoft VS Code\bin;C:\ST\STM32CubeIDE_1.14.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.1.100.202311100844\tools\bin;C:\Program Files\Git\usr\bin\;C:\Users\gojimmypi\.dotnet\tools;C:\SysGCC\esp32-master\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin;C:\Users\gojimmypi\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\VC\Linux\bin\ConnectionManagerExe"
set "INCLUDE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\include;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\ATLMFC\include;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\VS\include;C:\Program Files (x86)\Windows Kits\10\include\10.0.26100.0\ucrt;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\um;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\shared;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\winrt;C:\Program Files (x86)\Windows Kits\10\\include\10.0.26100.0\\cppwinrt;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\include\um"
set "LIB=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\ATLMFC\lib\x86;C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\lib\x86;C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8\lib\um\x86;C:\Program Files (x86)\Windows Kits\10\lib\10.0.26100.0\ucrt\x86;C:\Program Files (x86)\Windows Kits\10\\lib\10.0.26100.0\\um\x86"

:: We start in /tools/scripts, but build two directories up: from wolfBoot root

:: Begin common start directory detection
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
:: End common start directory detection


:: Is CMake installed?
where cmake >nul 2>&1
if errorlevel 1 (
    echo This test should be run from a VS2022 dev prompt.
    echo Error: CMake is not installed or not found in path.
    goto err
)



rmdir /s /q build-stm32l4

cmake --preset stm32l4


:: cmake --build --preset stm32l4 --parallel 4 -v

cmake --build --preset stm32l4

goto done

:err
popd
echo Failed.
exit /b 1

:done
popd
echo Done.
exit /b 0
