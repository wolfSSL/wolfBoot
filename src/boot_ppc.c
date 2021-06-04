#include <stdint.h>

#include "image.h"
#include "loader.h"
#include "wolfboot/wolfboot.h"

extern unsigned int __bss_start__;
extern unsigned int __bss_end__;

extern void main(void);

void boot_entry_C(void)
{
    register unsigned int *dst;
    /* Initialize the BSS section to 0 */
    dst = &__bss_start__;
    while (dst < (unsigned int *)&__bss_end__) {
        *dst = 0U;
        dst++;
    }

    /* Run wolfboot! */
    main();
}

#ifdef MMU
void do_boot(const uint32_t *app_offset, const uint32_t* dts_offset)
#else
void do_boot(const uint32_t *app_offset)
#endif
{
  asm volatile("mtlr %0; blr":: "r"(app_offset));
}

void arch_reboot(void) {}
