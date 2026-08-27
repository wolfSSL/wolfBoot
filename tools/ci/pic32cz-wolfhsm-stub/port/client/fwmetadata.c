/* fwmetadata.c -- CI compile-only stub of the PIC32CZ wolfHSM firmware loader.
 *
 * Reports success without loading anything. NOT a runnable implementation; see
 * tools/ci/pic32cz-wolfhsm-stub/README.md.
 */
#include "port/client/fwmetadata.h"
#include "port/mailbox.h"

int czhsm_load_hsm_firmware(uint32_t fw_addr, uint32_t fw_size)
{
    (void)fw_size;
    return mb_send_load_firmware(fw_addr);
}
