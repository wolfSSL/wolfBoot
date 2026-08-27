/* fwmetadata.h -- CI compile-only stub of the PIC32CZ wolfHSM firmware loader.
 *
 * NOT a runnable implementation; see tools/ci/pic32cz-wolfhsm-stub/README.md.
 */
#ifndef FW_METADATA_H
#define FW_METADATA_H

#include <stdint.h>

int czhsm_load_hsm_firmware(uint32_t fw_addr, uint32_t fw_size);

#endif /* FW_METADATA_H */
