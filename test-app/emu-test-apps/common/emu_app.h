#ifndef EMU_APP_H
#define EMU_APP_H

#include <stdint.h>

void emu_uart_init(void);
int emu_uart_read(uint8_t *c);
void emu_uart_write(uint8_t c);

#endif /* EMU_APP_H */
