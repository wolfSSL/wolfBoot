#ifndef FSP_CONFIG_H
#define FSP_CONFIG_H

#include <stdint.h>

#ifndef FSP_HYPERTHREADING
#define FSP_HYPERTHREADING 0
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

#ifndef FSP_THERMAL_THROTTLING
#define FSP_THERMAL_THROTTLING 0
#endif

#ifndef TEMP_LOWER_LIMIT_DEF
#define TEMP_LOWER_LIMIT_DEF -40
#endif

#ifndef TEMP_UPPER_LIMIT_DEF
#define TEMP_UPPER_LIMIT_DEF 70
#endif

#define TDP_LEVEL_UP 0x2

#define FSP_CONFIG_SIZE (32)
struct fsp_config {
    uint32_t magic;
    int32_t tdp;
    int32_t gpu_enable;
    int32_t gpu_mem_size;
    int32_t gtt_mem_size;
    int32_t gpu_aperture_size;
    int32_t temp_lower_limit;
    int32_t temp_upper_limit;
} __attribute__((packed));

#endif
