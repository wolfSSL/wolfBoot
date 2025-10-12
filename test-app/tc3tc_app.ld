/* tc3tc_app.ld */

/* Example application linker script using bare-metal startup */
OUTPUT_FORMAT("elf32-tricore")
OUTPUT_ARCH(tricore)

/* This is defined global in tc3tc_crt.S */
ENTRY(tc3tc_crt_entry)

/* Include TC3mem linker script */
INCLUDE tc3_mem.ld

/* Reallocate regions */
INCLUDE tc3_layout.ld

MEMORY {
    TEST_APP (rx) : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
}

ASSERT(ORIGIN(TEST_APP) >= ORIGIN(app_flash), "test app not located within app_flash")
ASSERT((ORIGIN(TEST_APP) + LENGTH(TEST_APP)) <= (ORIGIN(app_flash) + LENGTH(app_flash)), "test app memory region overflows app_flash")


/* BMHD sections */
REGION_ALIAS("BMHD_ORIG", ucb_bmhd_orig)
REGION_ALIAS("BMHD_COPY", ucb_bmhd_copy)

/* CRT0 Boot Code Start */
REGION_ALIAS("BOOT", TEST_APP)

/* Shared code and data */
REGION_ALIAS("CODE",     TEST_APP)
REGION_ALIAS("RODATA",   TEST_APP)
REGION_ALIAS("DATA", 	 dlmu_cpu0)
REGION_ALIAS("RAM_CODE", pspr_cpu0)

/* CPU0: Output sections to memory region mapping */
REGION_ALIAS("CODE_CPU0_",      TEST_APP)
REGION_ALIAS("RODATA_CPU0_",    TEST_APP)
REGION_ALIAS("RAMCODE_CPU0_",   pspr_cpu0)
REGION_ALIAS("DATA_DSPR_CPU0_", dspr_cpu0)
REGION_ALIAS("DATA_DLMU_CPU0_", dlmu_cpu0)

/* CPU1: Output sections to memory region mapping */
REGION_ALIAS("CODE_CPU1_",      TEST_APP)
REGION_ALIAS("RODATA_CPU1_",    TEST_APP)
REGION_ALIAS("RAMCODE_CPU1_",   pspr_cpu1)
REGION_ALIAS("DATA_DSPR_CPU1_", dspr_cpu1)
REGION_ALIAS("DATA_DLMU_CPU1_", dlmu_cpu1)

/* CPU2: Output sections to memory region mapping */
REGION_ALIAS("CODE_CPU2_",      TEST_APP)
REGION_ALIAS("RODATA_CPU2_",    TEST_APP)
REGION_ALIAS("RAMCODE_CPU2_",   pspr_cpu2)
REGION_ALIAS("DATA_DSPR_CPU2_", dspr_cpu2)
REGION_ALIAS("DATA_DLMU_CPU2_", dlmu_cpu2)

/* CPU3: Output sections to memory region mapping */
REGION_ALIAS("CODE_CPU3_",      TEST_APP)
REGION_ALIAS("RODATA_CPU3_",    TEST_APP)
REGION_ALIAS("RAMCODE_CPU3_",   pspr_cpu3)
REGION_ALIAS("DATA_DSPR_CPU3_", dspr_cpu3)
REGION_ALIAS("DATA_DLMU_CPU3_", dlmu_cpu3)

/* CPU4: Output sections to memory region mapping */
REGION_ALIAS("CODE_CPU4_",      TEST_APP)
REGION_ALIAS("RODATA_CPU4_",    TEST_APP)
REGION_ALIAS("RAMCODE_CPU4_",   pspr_cpu4)
REGION_ALIAS("DATA_DSPR_CPU4_", dspr_cpu4)
REGION_ALIAS("DATA_DLMU_CPU4_", dlmu_cpu4)

/* CPU5: Output sections to memory region mapping */
REGION_ALIAS("CODE_CPU5_",      TEST_APP)
REGION_ALIAS("RODATA_CPU5_",    TEST_APP)
REGION_ALIAS("RAMCODE_CPU5_",   pspr_cpu5)
REGION_ALIAS("DATA_DSPR_CPU5_", dspr_cpu5)
REGION_ALIAS("DATA_DLMU_CPU5_", dlmu_cpu5)


/* Local CSA per CPU.  256 max number, 64 byte entries */
CSA_SIZE    = 256 * 64;

/* Local stack per CPU. */
STACK_SIZE = 4K;

/* Shared heap.  Only use in a pinch for compatibility with stdlib */
__HEAP_SIZE = DEFINED (__HEAP_SIZE) ? __HEAP_SIZE : 4K;

/* Pickup the common sections */
INCLUDE tc3tc_sections.ld
