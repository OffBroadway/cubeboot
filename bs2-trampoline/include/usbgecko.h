#include <stdint.h>

void exi_select(void);
void exi_deselect(void);
uint32_t exi_imm_read_write(uint32_t data, uint32_t len);
bool usb_transmit_byte(const uint8_t *data);
bool usb_transmit_check(void);
int usb_transmit(const void *data, int size, int minsize);
