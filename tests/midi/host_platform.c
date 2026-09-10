/* Host-only platform shim: run the exact firmware parser/engine in tests. */
#include "engine.h"
#include "platform.h"
#include "protocol.h"
#include <string.h>
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
static uint32_t clock_ms;
static uint8_t tx[16384];
static uint16_t count;
static uint16_t periods[6];
static uint8_t dir_levels[6];
uint32_t platform_ms(void) {
    return clock_ms;
}
void platform_init(void) {}
void platform_enable(uint8_t m, uint8_t e) {
    (void)m;
    (void)e;
}
void platform_dir(uint8_t m, uint8_t d) {
    dir_levels[m] = config_dir_level(m, d);
}
void platform_common(uint8_t r, uint8_t s, uint8_t reset) {
    (void)r;
    (void)s;
    (void)reset;
}
void platform_step(uint8_t m, uint16_t half) {
    periods[m] = half;
}
void platform_stop(uint8_t m) {
    periods[m] = 0;
}
void platform_write(const uint8_t *p, uint16_t n) {
    if (count + n <= sizeof(tx)) {
        memcpy(tx + count, p, n);
        count += n;
    }
}
int platform_read(void) {
    return -1;
}
uint8_t platform_uart_error(void) {
    return 0;
}
uint8_t platform_oc_error(void) {
    return 0;
}
uint32_t platform_lock(void) {
    return 0;
}
void platform_unlock(uint32_t k) {
    (void)k;
}
API void mm_reset(void) {
    clock_ms = count = 0;
    memset(periods, 0, sizeof(periods));
    engine_init();
    protocol_init();
}
API void mm_time(uint32_t ms) {
    clock_ms = ms;
    protocol_poll();
    engine_tick();
}
API void mm_feed(const uint8_t *p, uint16_t n) {
    for (uint16_t i = 0; i < n; i++)
        protocol_byte(p[i]);
}
API uint16_t mm_take(uint8_t *out) {
    uint16_t n = count;
    memcpy(out, tx, n);
    count = 0;
    return n;
}
API uint16_t mm_period(uint32_t f) {
    return frequency_period(f);
}
API uint32_t mm_frequency(uint16_t p) {
    return period_frequency(p);
}
API uint16_t mm_compare(uint16_t prev, uint16_t p) {
    return compare_next(prev, p);
}
API uint8_t mm_status(uint8_t *out) {
    uint8_t len;
    engine_command(C_STATUS, 0, 0, out, &len);
    return len;
}

API uint8_t mm_dir_level(uint8_t m) { return dir_levels[m]; }
