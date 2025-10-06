# VisualGDB SDK wolfBoot Project



## Required include files

The device manufacturer files are installed by VisualGDB in `C:\Users\%USERNAME%\AppData\Local\VisualGDB`.

In the case of the STM32L4 needing `stm32l4xx_hal.h`, here:

```text
C:\Users\%USERNAME%\AppData\Local\VisualGDB\EmbeddedBSPs\arm-eabi\com.sysprogs.arm.stm32\STM32L4xxxx\STM32L4xx_HAL_Driver\Inc
```

This is in addition to the similar files installed by the STM32CubeIDE software here (for v1.18.0):

```
C:\Users\%USERNAME%\STM32Cube\Repository\STM32Cube_FW_L4_V1.18.0\Drivers\STM32L4xx_HAL_Driver\Inc
```

Project workspace directories created by STM32CubeIDE may also have include files in `[project]` directories like there:

```
C:\Users\gojimmypi\STM32CubeIDE\workspace_1.14.1\[project]\Drivers\STM32L4xx_HAL_Driver\Inc
```


When using VisualGDB files in WSL:

```bash
# Base where you found stm32l4xx_hal.h
export HAL_BASE="/mnt/c/Users/$USER/AppData/Local/VisualGDB/EmbeddedBSPs/arm-eabi/com.sysprogs.arm.stm32/STM32L4xxxx"

# HAL driver includes:
export HAL_INC="$HAL_BASE/STM32L4xx_HAL_Driver/Inc"

# (Legacy headers are sometimes needed by HAL)
export HAL_INC_LEGACY="$HAL_BASE/STM32L4xx_HAL_Driver/Inc/Legacy"

# The CMSIS *core* is usually in the Cortex pack VisualGDB ships separately:
# Find the CMSIS *device* (stm32l4xx.h) and *core* (core_cm4.h) include dirs:
export CMSIS_DEV="$(dirname "$(find "$HAL_BASE" -type f -name stm32l4xx.h | head -n1)")"
export CMSIS_CORE="$(dirname "$(find "$HAL_BASE" -type f -name core_cm4.h  | head -n1)")"

# Peek at results
echo "HAL_INC        = $HAL_INC"
echo "HAL_INC_LEGACY = $HAL_INC_LEGACY"
echo "CMSIS_DEV      = $CMSIS_DEV"
echo "CMSIS_CORE     = $CMSIS_CORE"

# Sanity check to ensure files found
ls "$HAL_INC/stm32l4xx_hal.h"
ls "$CMSIS_DEV/stm32l4xx.h"
ls "$CMSIS_CORE/core_cm4.h"

# 1) Expose the include paths to GCC (so no Makefile changes needed)
export C_INCLUDE_PATH="$HAL_INC:$HAL_INC/Legacy:$CMSIS_DEV:$CMSIS_CORE"
export CPLUS_INCLUDE_PATH="$C_INCLUDE_PATH"

echo "C_INCLUDE_PATH      = $C_INCLUDE_PATH"
echo "CPLUS_INCLUDE_PATH = $CPLUS_INCLUDE_PATH"

# 2) Build wolfBoot for STM32L4 with your exact device macro
make TARGET=stm32l4 V=1 CFLAGS+=" -DUSE_HAL_DRIVER -DSTM32L475xx"
```

The `.config` file needs to be edited. See the [visualgdb-stm32l4.config](../config/examples/visualgdb-stm32l4.config) example for the STM32-L4.

