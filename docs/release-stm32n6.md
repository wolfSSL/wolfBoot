# wolfBoot Secure Boot on the STM32N6

wolfSSL is announcing wolfBoot support for the STM32N6 series, starting with the
NUCLEO-N657X0-Q development board (STM32N657X0H). The STM32N6 is ST's first
Cortex-M55 microcontroller, designed for high-performance edge AI workloads with
a dedicated Neural Processing Unit (NPU). wolfBoot provides cryptographic
signature verification and secure firmware updates on this new platform.

## About the STM32N6

The STM32N6 is built around the Arm Cortex-M55 core capable of running at up to
800 MHz, targeting edge AI applications such as vision, audio classification,
and anomaly detection. wolfBoot configures the CPU at 600 MHz (PLL1, Voltage
Scale 1) by default. Unlike most STM32 parts, the N6 has no internal flash —
all firmware resides on external NOR flash connected via the high-speed XSPI2
interface, with over 4.2MB of on-chip SRAM available for code execution and
data.

This flash-less architecture makes the STM32N6 a great fit for applications
that need large, updateable firmware images without being constrained by
internal flash size, while still benefiting from the performance of a modern
Cortex-M55 core with Helium vector extensions.

## wolfBoot on the STM32N6

wolfBoot serves as the First Stage Boot Loader (FSBL) on the STM32N6. The
on-chip Boot ROM copies wolfBoot from external NOR flash into SRAM and executes
it. wolfBoot then verifies the application image signature and boots the
application, which runs directly from external flash via Execute-In-Place (XIP)
memory-mapped mode.

The wolfBoot binary comes in at approximately 22KB, well within the 128KB FSBL
limit. The entire port is self-contained with no external dependencies — no
STM32Cube HAL, no CMSIS headers, no RTOS required.

Signature verification uses ECC256 with SHA-256, and the port supports A/B
firmware updates with swap-based rollback, all operating on the external NOR
flash.

## Tested Hardware

- **Board**: NUCLEO-N657X0-Q (STM32N657X0H, MB1940)
- **Flash**: Macronix MX25UM51245G, 64MB NOR on XSPI2
- **Build + Flash**: `make && make flash` using OpenOCD with ST-Link

## Resources

- [wolfBoot STM32N6 documentation](https://github.com/wolfSSL/wolfBoot/blob/master/docs/Targets.md#stm32n6)
- [wolfBoot GitHub repository](https://github.com/wolfSSL/wolfBoot)
- [NUCLEO-N657X0-Q product page](https://www.st.com/en/evaluation-tools/nucleo-n657x0-q.html)
- [wolfBoot video series with ST](https://www.wolfssl.com/st-wolfboot-video-series/)

If you have any questions or run into any issues, contact us at
facts@wolfssl.com, or call us at +1 425 245 8247.
