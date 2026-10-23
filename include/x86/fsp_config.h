#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

#include <stdint.h>

#ifndef FSP_HYPERTHREADING
#define FSP_HYPERTHREADING 0
#endif
#if FSP_HYPERTHREADING
/* The MADT and MP table list four single-thread cores (APIC IDs 0/2/4/6). */
#error "FSP_HYPERTHREADING=1 needs sibling threads in the MADT and MP table"
#endif

#ifndef FSP_INTEL_SPEED_STEP
#define FSP_INTEL_SPEED_STEP 0
#endif

#ifndef FSP_INTEL_SPEED_SHIFT
#define FSP_INTEL_SPEED_SHIFT 0
#endif

#define FSP_BOOT_FREQ_MAX_NO_TURBO 0x1
#ifndef FSP_BOOT_PERFORMANCE_MODE
#define FSP_BOOT_PERFORMANCE_MODE FSP_BOOT_FREQ_MAX_NO_TURBO
#endif

#ifndef FSP_C_STATES
#define FSP_C_STATES 0
#endif

#ifndef FSP_TXT
#define FSP_TXT 0
#endif

#ifndef FSP_IN_BAND_ECC
#define FSP_IN_BAND_ECC 1
#endif

/* TM1/TM2 throttling at the thermal-control limit, the FSP default */
#ifndef FSP_THERMAL_THROTTLING
#define FSP_THERMAL_THROTTLING 1
#endif

#endif
