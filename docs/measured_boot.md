# Measured Boot using wolfBoot

wolfBoot offers a simplified measured boot implementation, a way to record and
track the state of the system boot process using a Trusted Platform Module(TPM).

This record is tamper-proofed by special registers in the TPM called Platform
Configuration Register. Then, the firmware application, RTOS or rich OS(Linux),
can access that log of information by reading the PCRs of the TPM.

wolfBoot can interact with TPM2.0 chips thanks to its integration with wolfTPM.
wolfTPM has native support for Microsoft Windows and Linux, and can be used
standalone or together with wolfBoot. The combination of wolfBoot with wolfTPM
gives the developer a tamper-proof secure storage for protecting the system
during and after boot.

## Concept

Typically, systems use Secure Boot to guarantee that the correct and geniune
firmware is booted by verifying its signature. Afterwards, this knowledge is
unknown to the sytem. The application does not know if the system started in
a good known state. Sometimes, this guarantee is needed by the firmware itself.
To provide such mechanism the concept of Measured Boot exist.

Measured Boot can be used to check every start-up component, including settings
and user information(user partition). The result of the checks is then stored
into special registers called PCR. This process is called PCR Extend and is
refered to as a TPM measurement. PCR registers can be reset only on TPM power-on.

Having TPM measurements provide a way for the firmware or Operating System(OS),
like Windows or Linux, to know that the software loaded before it gained control
over system, is trustworthy and not modified.

In wolfBoot the concept is simplified to measuring a single component, the main
firmware image. However, this can easily be extended by using more PCR registers.

## Configuration

To enable measured boot add `MEASURED_BOOT=1` setting in your wolfBoot config.

It is also necessary to select the PCR (index) where the measurement will be stored.

Selection is made using the `MEASURED_BOOT_PCR_A=[index]` setting. Add this
setting in your wolfBoot config and replace `[index]` with a number between
0 and 23. Below you will find guidelines for selecting a PCR index.

Any TPM has a minimum of 24 PCR registers. Their typical use is as follows:

| Index   |      Typical use      |  Recommended to use with |
|----------|:-------------:|------:|
| 0 |  Core Root of Trust and/or BIOS measurement | bare-metal, RTOS |
| 1 |  measurement of Platform Configuration Data   | bare-metal, RTOS |
| 2-3 |  Option ROM Code measurement | bare-metal, RTOS |
| 4-5 |  Master Boot Record measurement | bare-metal, RTOS |
| 6 | State Transitions | bare-metal, RTOS |
| 7 | Vendor specific | bare-metal, RTOS |
| 8-9 | Partition measurements | bera-metal, RTOS |
| 10 | measurement of the Boot Manager | bare-metal, RTOS |
| 11 | Typically used by Microsoft Bitlocker | bare-metal, RTOS |
| 12-15 | Available for any use | bare-metal, RTOS, Linux, Windows |
| 16 | DEBUG | Use only for test purposes |
| 17 | DRTM | Trusted Bootloader |
| 18-22 | Trusted OS | Trusted Execution Environment(TEE) |
| 23 | Application | Use only for temporary measurements |

Recommendations for choosing a PCR index:

- During development it is recommended to use PCR16 that is intented for testing.
- In production, if you are running a bare-metal firmware or RTOS, you could use
almost all PCRs(PCR0-15), except the one for DRTM and Trusted OS(PCR17-23).
- If you are running Linux or Windows, PCR12-15 can be chosen for production
ready firmware, in order to avoid conflict with other software that might be
using PCRs from within Linux, like the Linux IMA or Microsoft Bitlocker.

Here is an example part of a wolfBoot .config during development:

```
MEASURED_BOOT?=1
MEASURED_PCR_A?=16
```

### Code

wolfBoot offers out-of-the-box solution. There is zero need of the developer to touch wolfBoot code
in order to use measured boot. If you would want to check the code, then look in `src/image.c` and
more specifically the `measure_boot()` function. There you would find several TPM2 native API calls
to wolfTPM. For more information about wolfTPM you can check its GitHub repository.
