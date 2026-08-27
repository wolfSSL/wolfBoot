/* mailbox.c -- CI compile-only stub of the PIC32CZ wolfHSM port mailbox.
 *
 * Every call is a no-op reporting success. NOT a runnable implementation; see
 * tools/ci/pic32cz-wolfhsm-stub/README.md.
 */
#include "port/mailbox.h"

int mb_send_load_firmware(uint32_t fw_metadata_address)
{
    (void)fw_metadata_address;
    return 0;
}

int mb_send_response_code(uint32_t code)
{
    (void)code;
    return 0;
}

int mb_send_connect(void)
{
    return 0;
}

int mb_read_connection(void)
{
    return MAILBOX_STATUS_OK;
}

int mb_rx_is_ready(void)
{
    return 1;
}

void mb_wait_for_rx_ready_poll(void)
{
}

int mb_send_request_notify(void)
{
    return 0;
}

int mb_read_request(void)
{
    return 0;
}

int mb_send_response_notify(void)
{
    return 0;
}

int mb_read_response(void)
{
    return 0;
}

int mb_enable_rx_int(void)
{
    return 0;
}
