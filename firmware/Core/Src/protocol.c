#include "protocol.h"
#include "engine.h"
#include "platform.h"
#include <string.h>
static uint8_t rx[247], previous[247], response[247];
static uint16_t used, previous_len, response_len;
static uint32_t last_byte, previous_at;
uint16_t protocol_crc(const uint8_t *data, uint16_t n) {
    uint16_t crc = 0xffff;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
void protocol_init(void) {
    used = previous_len = response_len = 0;
}
static void drop(void) {
    if (used) {
        used--;
        memmove(rx, rx + 1, used);
    }
}
static void parse(void) {
    while (used >= 2) {
        if (rx[0] != 0xa5 || rx[1] != 0x5a) {
            drop();
            continue;
        }
        if (used < 5)
            return;
        uint8_t len = rx[2];
        if (len > 240) {
            state.error = E_LENGTH;
            state.faults++;
            drop();
            continue;
        }
        uint16_t total = (uint16_t)len + 7;
        if (used < total)
            return;
        uint16_t crc = protocol_crc(rx + 2, (uint16_t)len + 3);
        if (rx[total - 2] != (uint8_t)crc || rx[total - 1] != (uint8_t)(crc >> 8)) {
            state.error = E_CRC;
            state.faults++;
            drop();
            continue;
        }
        if (total == previous_len && platform_ms() - previous_at < DUPLICATE_WINDOW_MS &&
            !memcmp(rx, previous, total)) {
            platform_write(response, response_len);
        } else {
            uint8_t outlen = 0;
            uint8_t err = engine_command(rx[4], rx + 5, len, response + 7, &outlen);
            if (err) {
                state.error = err;
                state.faults++;
                outlen = 0;
            }
            response[0] = 0xa5;
            response[1] = 0x5a;
            response[2] = outlen + 2;
            response[3] = rx[3];
            response[4] = err ? 129 : 128;
            response[5] = rx[4];
            response[6] = err;
            crc = protocol_crc(response + 2, outlen + 5);
            response[outlen + 7] = (uint8_t)crc;
            response[outlen + 8] = (uint8_t)(crc >> 8);
            response_len = outlen + 9;
            memcpy(previous, rx, total);
            previous_len = total;
            previous_at = platform_ms();
            platform_write(response, response_len);
        }
        used -= total;
        memmove(rx, rx + total, used);
    }
}
void protocol_byte(uint8_t byte) {
    if (used && platform_ms() - last_byte > FRAME_TIMEOUT_MS) {
        used = 0;
        state.error = E_TIMEOUT;
        state.faults++;
    }
    last_byte = platform_ms();
    if (used == sizeof(rx))
        drop();
    rx[used++] = byte;
    parse();
}
void protocol_poll(void) {
    if (platform_uart_error()) {
        used = 0;
        engine_fault(E_UART);
    }
    if (platform_oc_error())
        engine_fault(E_OVERRUN);
    if (used && platform_ms() - last_byte > FRAME_TIMEOUT_MS) {
        used = 0;
        state.error = E_TIMEOUT;
        state.faults++;
    }
    int b;
    while ((b = platform_read()) >= 0)
        protocol_byte((uint8_t)b);
}
