/* cmsis_os.h (wolfBoot AmebaPro2 SDK shim)
 *
 * The RealTek SDK header chain (cmsis.h) unconditionally includes "cmsis_os.h",
 * which in the SDK is the CMSIS-OS wrapper over FreeRTOS. wolfBoot's use of the
 * SDK flash / log-UART / cache drivers is bare-metal and never calls any
 * CMSIS-OS API, but a few SDK headers (e.g. diag.h: "extern osMutexId
 * PrintLock_id;") reference CMSIS-OS types while being parsed.
 *
 * This stub satisfies those references with opaque types so the SDK headers
 * compile WITHOUT pulling FreeRTOS into wolfBoot's standalone build. It is
 * placed on the include path ahead of the SDK's real cmsis_os.h, and is used
 * only for hal/rtl8735b.o when HAL_BACKEND=sdk.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */
#ifndef WOLFBOOT_AMEBAPRO2_CMSIS_OS_SHIM_H
#define WOLFBOOT_AMEBAPRO2_CMSIS_OS_SHIM_H

#include <stdint.h>

typedef void    *osMutexId;
typedef void    *osSemaphoreId;
typedef void    *osThreadId;
typedef void    *osMessageQId;
typedef void    *osMailQId;
typedef void    *osTimerId;
typedef void    *osPoolId;
typedef int32_t  osStatus;

#endif /* WOLFBOOT_AMEBAPRO2_CMSIS_OS_SHIM_H */
