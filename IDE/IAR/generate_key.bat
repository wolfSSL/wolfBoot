cd ..\..
REM Build the src/keystore.c
IDE\IAR\keytools\keygen.exe --ecc256 -g wolfboot_signing_private_key.der
cd IDE\IAR
