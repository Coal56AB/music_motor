#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>
uint16_t protocol_crc(const uint8_t *data, uint16_t n);
void protocol_init(void);
void protocol_byte(uint8_t byte);
void protocol_poll(void);
#endif
