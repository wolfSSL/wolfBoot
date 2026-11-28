/*
 * Linker script for the wolfBoot test application on NXP MCXE31B with wolfIP
 *
 * The application runs from the boot partition after the wolfBoot header.
 *
 * RAM is the 64 KB CM7_0 DTCM (0x20000000), which wolfBoot has already
 * ECC-initialized: data, the RAM_CODE flash routines, .bss, heap and stack.
 * The core does not cache the DTCM and the EMAC DMA reaches it through its
 * backdoor port, so the EMAC driver's descriptor rings and frame buffers
 * (its .bss) live there with no cache maintenance.
 *
 * The wolfIP stack state (sockets and their buffers), the TFTP receive
 * buffer and libwolfboot's flash sector buffer (NVM_CACHE) go in the 320 KB
 * SRAM (0x20400000), which the core caches and only the core uses. SRAM is
 * ECC protected and holds no valid ECC after
 * power-on. With APP_RAM_ECC_INIT the reset handler clears
 * [_start_ecc_ram, _end_ecc_ram) with 64-bit stores (SRAM ECC words are 64
 * bits) before anything reads it; that clear is also what zeroes the SRAM
 * section: it is not part of .bss.
 *
 * The hottest code runs from the 32 KB ITCM (.ramcode_itcm). The first
 * 1 KB of the ITCM is left unused, so that writes through a NULL pointer do
 * not land on code.
 */

MEMORY
{
    FLASH (rx)  : ORIGIN = @WOLFBOOT_TEST_APP_ADDRESS@, LENGTH = @WOLFBOOT_TEST_APP_SIZE@
    ITCM (rx)   : ORIGIN = 0x00000400, LENGTH = 0x7C00   /* 31 KB ITCM */
    RAM (rwx)   : ORIGIN = 0x20000000, LENGTH = 0x10000  /* 64 KB DTCM */
    SRAM (rw)   : ORIGIN = 0x20400000, LENGTH = 0x50000  /* 320 KB SRAM */
}

SECTIONS
{
    /* Hot code run from the ITCM. It comes first so that these input
     * sections are taken before the .text* rule below. */
    .ramcode_itcm : AT (_stored_ramcode_itcm)
    {
        . = ALIGN(8);
        _start_ramcode_itcm = .;
        KEEP(*string.o(.ramcode))
        *string.o(.text.memset .text.memcmp)
        *wolfip.o(.text.csum_add .text.transport_checksum)
        *wolfip.o(.text.iphdr_set_checksum .text.wolfIP_poll)
        *wolfip.o(.text.fifo_next .text.fifo_peek .text.fifo_push)
        *wolfip.o(.text.fifo_pop .text.fifo_can_push_len)
        *wolfip.o(.text.tcp_snd_nxt .text.tcp_calc_win .text.wolfIP_sock_send)
        *wolfip.o(.text.wolfIP_route_lookup_internal .text.arp_neighbor_index)
        *nxp_mcxe_emac.o(.text.eth_poll .text.eth_send)
        *nxp_mcxe_emac.o(.text.eth_poll_stats .text.eth_send_stats)
        *nxp_mcxe_emac.o(.text.nxp_mcxe_emac_irq_handler)
        . = ALIGN(8);
        _end_ramcode_itcm = .;
    } > ITCM

    .text :
    {
        _start_text = .;
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        *(.init*)
        *(.fini*)
        . = ALIGN(4);
        _end_text = .;
    } > FLASH

    .keystore :
    {
        . = ALIGN(4);
        KEEP(*(.keystore*))
    } > FLASH

    .edidx :
    {
        . = ALIGN(4);
        *(.ARM.exidx*)
    } > FLASH

    _stored_ramcode_itcm = ALIGN(8);

    _stored_data = _stored_ramcode_itcm + SIZEOF(.ramcode_itcm);

    .data : AT (_stored_data)
    {
        _start_data = .;
        KEEP(*(.data*))
        . = ALIGN(4);
        KEEP(*(.ramcode))
        . = ALIGN(4);
        _end_data = .;
    } > RAM

    /* wolfIP and network test state and the libwolfboot sector buffer,
     * zero-filled by the APP_RAM_ECC_INIT clear. It comes before .bss so
     * that these input sections are taken before the .bss* rule below. */
    .bss_sram (NOLOAD) :
    {
        . = ALIGN(8);
        *wolfip.o(.bss .bss.* COMMON)
        *wolftftp.o(.bss .bss.* COMMON)
        *wolfip_tftp_test.o(.bss .bss.* COMMON)
        *libwolfboot.o(.bss.NVM_CACHE)
        . = ALIGN(8);
    } > SRAM

    .bss (NOLOAD) :
    {
        _start_bss = .;
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _end_bss = .;
        __bss_end__ = .;
        _end = .;
    } > RAM
    . = ALIGN(4);
}

/* Partition info for libwolfboot */
_wolfboot_partition_boot_address = @WOLFBOOT_PARTITION_BOOT_ADDRESS@;
_wolfboot_partition_size = @WOLFBOOT_PARTITION_SIZE@;
_wolfboot_partition_update_address = @WOLFBOOT_PARTITION_UPDATE_ADDRESS@;
_wolfboot_partition_swap_address = @WOLFBOOT_PARTITION_SWAP_ADDRESS@;

/* Stack and heap indicators */
_start_heap = _end;
_end_stack = ORIGIN(RAM) + LENGTH(RAM);

/* ITCM cleared at reset with APP_RAMCODE_ITCM, before the code is copied */
_start_ecc_itcm = 0x00000000;
_end_ecc_itcm = 0x00008000;

/* SRAM cleared at reset with APP_RAM_ECC_INIT */
_start_ecc_ram = ORIGIN(SRAM);
_end_ecc_ram = ORIGIN(SRAM) + LENGTH(SRAM);

/* The EMAC driver's buffers must be in the DTCM */
ASSERT(_end_bss <= ORIGIN(RAM) + LENGTH(RAM), "DTCM overflow")
/* Keep room above .bss for the stack and the heap used by printf */
ASSERT(_end_stack - _end >= 0x2000, "not enough RAM left for the stack")
