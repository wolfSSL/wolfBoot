echo off

if "%~1"=="" goto fail

keytools\sign.exe --ecc256 --sha256 Debug\Exe\wolfboot-test-app.bin ..\..\ecc256.der %1

goto out

:fail
 echo please specify a version number.

:out

