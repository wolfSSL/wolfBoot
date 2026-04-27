# DICE attestation sample on MCXN947

General concept of wolfBoot in TrustZone secure domain is explained in `docs/STM32-TZ.md`.
This focuses on a sample of PSA initial attestation for MCXN947.
Sample application is implemented in `test-app/app_mcxn.c`.
This supports hardware-based DICE using EdgeLock Secure Subsystem.

## How to build
Please use `config/examples/mcxn-tz-psa-hw.config` to build wolfBoot.
You need to update the path of SDK inside the config for your build environment.
```
cp config/examples/mcxn-tz-psa-hw.config .config
make
```

## How to run
MCXN947 requires us to enable secure boot to use DICE functionality of ELS.
So, We need to prepare the specific image file of wolfBoot binary and configuration files of CMPA/CFPA for secure bootROM and ELS.
Fortunately NXP provides the [secure provisioning tool](https://www.nxp.com/design/design-center/software/development-software/mcuxpresso-software-and-tools-/mcuxpresso-secure-provisioning-tool:MCUXPRESSO-SECURE-PROVISIONING) for such purpose.

At a minimum, you shall configure the following settings and encode the image with signature:
- Enable Image verification for secure boot (SEC_BOOT_EN == 0b10)
- Enable DICE (SKIP_DICE == 0b0)
All of these can be done using the NXP secure provisioning tool.

If you use secure provisioning tool, you can download the prepared image with configuration files to hardware as well.
Regarding to application image like image_v1_signed.bin, we don't need to encode it like the wolfBoot binary because it's verified by wolfBoot as usual.
But, we still recommend you to download the application image using the flash programmer tool in the secure provisioning tool for a safe download. You can access it via `Tools > Flash Programmer` on GUI.

Once both images are downloaded successfully, you'll see the log after reboot:
```
Boot partition: 0x60000 (sz 31508, ver 0x1, type 0x601)
Partition 1 header magic 0xFFFFFFFF invalid at 0x72000
Boot partition: 0x60000 (sz 31508, ver 0x1, type 0x601)
Booting version: 0x1
Checking integrity...done
Verifying signature...done
Hello from firmware version 1
Today's lucky number: 0xAF
PSA crypto init ok
Start attestation verify test
[ATTEST] GET_TOKEN: challenge_len=32 out_len=1024
Boot partition: 0x60000 (sz 31508, ver 0x1, type 0x601)
Boot partition: 0x60000 (sz 31508, ver 0x1, type 0x601)
[ATTEST] GET_TOKEN: dice_rc=0 token_len=356
attest_verify: token_len=356
        84 43 A1 01 26 A0 59 01 19 A5 0A 58 20 01 02 03 | .C..&.Y....X ...
        04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 | ................
        14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 19 01 00 | ............ ...
        58 21 01 AD 74 46 22 0C D0 5B 7A 1B 33 5E 37 28 | X!..tF"..[z.3^7(
        0A 54 E6 DF 55 5B 40 A4 57 A0 43 0A 6D A5 AB E7 | .T..U[@.W.C.m...
        5A 2B 5F 19 09 5C 58 30 E4 F9 83 72 7F A9 C5 CE | Z+_..\X0...r....
        DD E0 79 A0 AD 0E 32 59 0A EA D3 10 43 BA 12 DD | ..y...2Y....C...
        76 47 59 D1 42 8B 3A 75 F2 7E 75 CF 7D EE A2 B9 | vGY.B.:u.~u.}...
        AA AB 7E E9 91 27 F7 21 19 09 5E 19 30 00 19 09 | ..~..'.!..^.0...
        5F 82 A3 01 67 73 68 61 2D 33 38 34 02 58 30 E4 | _...gsha-384.X0.
        F9 83 72 7F A9 C5 CE DD E0 79 A0 AD 0E 32 59 0A | ..r......y...2Y.
        EA D3 10 43 BA 12 DD 76 47 59 D1 42 8B 3A 75 F2 | ...C...vGY.B.:u.
        7E 75 CF 7D EE A2 B9 AA AB 7E E9 91 27 F7 21 05 | ~u.}.....~..'.!.
        68 77 6F 6C 66 62 6F 6F 74 A3 01 67 73 68 61 2D | hwolfboot..gsha-
        33 38 34 02 58 30 D4 F2 89 AD E4 4E 6E EB F4 C6 | 384.X0.....Nn...
        7D 24 13 AD 6C E6 DC A6 E9 FC 67 4F CC CB DE 52 | }$..l.....gO...R
        4D FD E2 73 9D 2A D7 B3 DD DF B8 51 F4 43 C5 59 | M..s.*.....Q.C.Y
        48 8A 30 46 D7 E3 05 6A 62 6F 6F 74 2D 69 6D 61 | H.0F...jboot-ima
        67 65 58 40 E8 3F 76 44 15 7C 62 76 33 56 6D F7 | geX@.?vD.|bv3Vm.
        2B E1 57 40 C4 F1 7D 21 D3 E9 90 57 25 8F A3 14 | +.W@..}!...W%...
        44 ED 1B CE 01 CA EF 52 6B 41 4C 44 DF 4E 04 D4 | D......RkALD.N..
        AA CC A8 7F 20 3D AA 39 CA 3A 9F C2 B1 C0 28 43 | .... =.9.:....(C
        DF BB 82 53                                     | ...S
[ATTEST] GET_IAK_PUBKEY: out_len=65
[ATTEST] GET_IAK_PUBKEY: dice_rc=0 key_len=65
attest_verify: IAK pubkey (65 bytes):
        04 42 9B 43 75 BE B0 E9 A0 39 61 64 68 20 1E FC | .B.Cu....9adh ..
        B6 68 F4 3E 25 74 F1 E2 CB 3D BD 09 A6 45 A7 E2 | .h.>%t...=...E..
        6F E1 54 B8 20 61 96 79 6A E8 31 83 3A 82 DE 1C | o.T. a.yj.1.:...
        87 46 E1 45 49 3D 6F 56 78 F4 14 B6 42 47 DD F1 | .F.EI=oVx...BG..
        AC                                              | .
attest_verify: IAK signature verified OK
```
