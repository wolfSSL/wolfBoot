/* mailbox.h -- CI compile-only stub of the PIC32CZ wolfHSM port mailbox API.
 *
 * NOT a runnable implementation; see tools/ci/pic32cz-wolfhsm-stub/README.md.
 */
#ifndef MAILBOX_H
#define MAILBOX_H

#include <stdint.h>

#define MAILBOX_STATUS_OK (0x00000001)

int mb_send_load_firmware(uint32_t fw_metadata_address);
int mb_send_response_code(uint32_t code);
int mb_send_connect(void);
int mb_read_connection(void);
int mb_rx_is_ready(void);
void mb_wait_for_rx_ready_poll(void);
int mb_send_request_notify(void);
int mb_read_request(void);
int mb_send_response_notify(void);
int mb_read_response(void);
int mb_enable_rx_int(void);

#endif /* MAILBOX_H */
