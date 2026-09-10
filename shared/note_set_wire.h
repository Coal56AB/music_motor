#ifndef NOTE_SET_WIRE_H
#define NOTE_SET_WIRE_H
#include <stdint.h>
#define NS_SIZE 14u
#define NS_TYPE 1u
#define NS_BAUD 921600u
#define NS_HEARTBEAT_MS 50u
#define NS_TIMEOUT_MS 300u
/* D3 91 TYPE SEQ COUNT NOTE[6] RESERVED CRC16_LE.
 * CRC-16/CCITT-FALSE over bytes 2..11. Unused notes/reserved = 0.
 * Sequence is diagnostic only: snapshots are idempotent; reboot/wrap recover. */
static inline uint16_t ns_crc(const uint8_t *p, unsigned n) {
    uint16_t c = 0xffff;
    while (n--) {
        c ^= (uint16_t)*p++ << 8;
        for (unsigned b = 0; b < 8; ++b)
            c = (uint16_t)((c << 1) ^ ((c & 0x8000) ? 0x1021 : 0));
    }
    return c;
}
static inline void ns_encode(uint8_t *p, uint8_t seq, const uint8_t *notes, uint8_t count) {
    for (unsigned i = 0; i < NS_SIZE; ++i) p[i] = 0;
    p[0] = 0xd3; p[1] = 0x91; p[2] = NS_TYPE; p[3] = seq; p[4] = count;
    for (unsigned i = 0; i < count && i < 6; ++i) p[5+i] = notes[i];
    uint16_t crc = ns_crc(p+2, 10);
    p[12] = (uint8_t)crc; p[13] = (uint8_t)(crc >> 8);
}
static inline int ns_valid(const uint8_t *p) {
    if (p[0] != 0xd3 || p[1] != 0x91 || p[2] != NS_TYPE || p[4] > 6 || p[11]) return 0;
    for (unsigned i = 0; i < 6; ++i) {
        if (i >= p[4]) { if (p[5+i]) return 0; }
        else {
            if (p[5+i] > 127) return 0;
            for (unsigned j = 0; j < i; ++j) if (p[5+j] == p[5+i]) return 0;
        }
    }
    return ns_crc(p+2, 10) == ((uint16_t)p[12] | ((uint16_t)p[13] << 8));
}
#endif
