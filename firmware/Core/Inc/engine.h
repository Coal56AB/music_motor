#ifndef ENGINE_H
#define ENGINE_H
#include "config.h"
enum {
    E_OK,
    E_LENGTH,
    E_COMMAND,
    E_VALUE,
    E_STATE,
    E_FULL,
    E_ORDER,
    E_CRC,
    E_TIMEOUT,
    E_UART,
    E_UNDERRUN,
    E_OVERRUN
};
enum {
    C_PING = 1,
    C_INFO,
    C_STATUS,
    C_ESTOP,
    C_MASK,
    C_ENABLE,
    C_START,
    C_STOP,
    C_FREQ,
    C_NOTE,
    C_DIR,
    C_MS,
    C_SLEEP,
    C_RESET,
    C_RESET_PULSE,
    C_STREAM_START,
    C_STREAM_STOP,
    C_EVENTS,
    C_CLEAR,
    C_QUEUE,
    C_BOOT_TEST,
    C_RAW_EVENTS,
    C_RAW_SEEK
};
typedef struct {
    uint32_t frequency;
    uint8_t enabled, active, dir, note;
} Motor;
typedef struct {
    uint32_t at, value;
    uint8_t motor, op, note;
} Event;
typedef struct {
    Motor motors[MOTOR_COUNT];
    uint8_t mask, sleep, reset, raw, running, error;
    uint16_t used;
    uint32_t position, faults;
} State;
extern State state;
void engine_init(void);
void engine_tick(void);
uint8_t engine_display_hold(uint8_t motor);
uint32_t engine_display_epoch(void);
uint8_t engine_display_preview(uint8_t *out, uint8_t capacity, uint32_t now);
void engine_estop(void);
void engine_fault(uint8_t error);
void engine_note_set(const uint8_t *notes, uint8_t count);
uint8_t engine_live_events(const uint8_t *data, uint8_t length, uint8_t connected);
void engine_live_gate(uint8_t enabled);
uint8_t engine_live_enabled(void);
uint8_t engine_history_read(uint8_t *event);
uint8_t engine_history_pending(void);
uint8_t engine_command(uint8_t cmd, const uint8_t *p, uint8_t len, uint8_t *out, uint8_t *outlen);
uint16_t frequency_period(uint32_t mhz);
uint32_t period_frequency(uint16_t period);
uint16_t compare_next(uint16_t previous, uint16_t period);
uint32_t note_mhz(uint8_t note);
#endif
