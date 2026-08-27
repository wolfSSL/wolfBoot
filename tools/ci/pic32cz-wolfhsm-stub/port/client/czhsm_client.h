/* czhsm_client.h -- CI compile-only stub of the PIC32CZ wolfHSM client port.
 *
 * NOT a runnable implementation; see tools/ci/pic32cz-wolfhsm-stub/README.md.
 */
#ifndef CZHSM_CLIENT_H
#define CZHSM_CLIENT_H

#include "wolfhsm/wh_client.h"

int czhsm_setup(whClientConfig* c_conf);
int czhsm_connect(void);

#endif /* CZHSM_CLIENT_H */
