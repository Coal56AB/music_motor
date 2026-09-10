#include "engine.h"
#include "platform.h"
#include <string.h>
#include "../../../shared/note_set_wire.h"
State state;
static Event queue[NOTES_BUFFER_DEPTH];
static uint16_t head, tail;
static uint32_t origin, last_at, ready_at, reset_release, last_command;
static uint8_t end_queued, pulse_pending;
#if LIVE_MIDI_MODE
static uint8_t midi_notes[6], midi_count, midi_pending, midi_link;
static uint8_t midi_assignment[6]; /* Original pitches, before octave folding. */
static uint32_t midi_last;
#endif
static const uint32_t octave_mhz[12] = {261626, 277183, 293665, 311127, 329628, 349228,
                                        369994, 391995, 415305, 440000, 466164, 493883};
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v) {
    for (uint8_t i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
uint16_t frequency_period(uint32_t f) {
    if (f < MIN_FREQ_MHZ || f > MAX_FREQ_MHZ)
        return 0;
    return (uint16_t)((TIMER_HZ * 500u + f / 2u) / f);
}
uint32_t period_frequency(uint16_t p) {
    return p ? (TIMER_HZ * 500u + p / 2u) / p : 0;
}
uint16_t compare_next(uint16_t prev, uint16_t half) {
    return (uint16_t)(prev + half);
}
uint32_t note_mhz(uint8_t n) {
    uint32_t f = octave_mhz[n % 12];
    int shift = (int)(n / 12) - 5;
    return shift >= 0 ? f << shift : f >> (-shift);
}
static uint8_t closest_note(uint32_t frequency) {
    uint32_t best = 0xffffffffu;
    uint8_t note = 0;
    for (uint8_t n = 0; n < 128; n++) {
        uint32_t f = note_mhz(n), delta = frequency > f ? frequency - f : f - frequency;
        if (delta < best) {
            best = delta;
            note = n;
        }
    }
    return note;
}
static void stop(uint8_t m) {
    platform_stop(m);
    state.motors[m].active = 0;
}
static void clear(void) {
    head = tail = state.used = 0;
    last_at = 0;
    end_queued = 0;
}
static void stream_stop(uint8_t disable) {
    state.running = 0;
    clear();
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        stop(i);
        if (disable) {
            state.motors[i].enabled = 0;
            platform_enable(i, 0);
        }
    }
}
void engine_estop(void) {
#if LIVE_MIDI_MODE
    midi_link = midi_pending = midi_count = 0;
    memset(midi_assignment, 255, sizeof(midi_assignment));
#endif
    stream_stop(1);
    state.sleep = state.reset = 1;
    pulse_pending = 0;
    platform_common(state.raw, 1, 1);
}
void engine_fault(uint8_t error) {
    engine_estop();
    state.error = error;
    state.faults++;
}
void engine_init(void) {
    origin = last_at = ready_at = reset_release = 0;
    end_queued = pulse_pending = 0;
    memset(&state, 0, sizeof(state));
    state.mask = DEFAULT_INSTALLED_MASK;
#if LIVE_MIDI_MODE
    state.mask = MIDI_INSTALLED_MASK;
#endif
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        state.motors[i].frequency = note_mhz(DEFAULT_NOTE);
        state.motors[i].note = DEFAULT_NOTE;
        platform_dir(i, 0);
    }
    engine_estop();
    last_command = platform_ms();
}
static uint8_t start(uint8_t m) {
    Motor *v = &state.motors[m];
    if (!(state.mask & (1u << m)) || !v->enabled || state.sleep || state.reset ||
        (int32_t)(platform_ms() - ready_at) < 0)
        return E_STATE;
    uint16_t half = frequency_period(v->frequency);
    if (!half)
        return E_VALUE;
    platform_step(m, half);
    v->frequency = period_frequency(half);
    v->active = 1;
    return E_OK;
}
static uint8_t status(uint8_t *out) {
    out[0] = state.mask;
    out[1] = state.sleep;
    out[2] = state.reset;
    out[3] = state.raw;
    out[4] = state.running;
    out[5] = state.error;
    out[6] = (uint8_t)state.used;
    out[7] = (uint8_t)(state.used >> 8);
    put32(out + 8, state.position);
    put32(out + 12, state.faults);
    for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
        Motor *m = &state.motors[i];
        uint8_t *p = out + 16 + 6 * i;
        p[0] = m->enabled | (m->active << 1) | (m->dir << 2);
        p[1] = m->note;
        put32(p + 2, m->frequency);
    }
    return 52;
}
uint8_t engine_command(uint8_t cmd, const uint8_t *p, uint8_t len, uint8_t *out, uint8_t *outlen) {
    static const uint8_t lengths[] = {0, 0, 0, 0, 0, 1, 2, 1,   1, 5, 2,
                                      2, 1, 1, 1, 0, 0, 1, 255, 0, 0};
    *outlen = 0;
    if (cmd < 1 || cmd > 20)
        return E_COMMAND;
    if (cmd != C_EVENTS && len != lengths[cmd])
        return E_LENGTH;
    last_command = platform_ms();
    uint8_t motor = len ? p[0] : 0;
    if (cmd >= C_ENABLE && cmd <= C_DIR) {
        if (motor >= MOTOR_COUNT)
            return E_VALUE;
        if (!(state.mask & (1u << motor)))
            return E_STATE;
        if (state.running)
            return E_STATE;
    }
    if (state.running && (cmd == C_MASK || cmd == C_MS || cmd == C_STREAM_START))
        return E_STATE;
    switch (cmd) {
    case C_PING:
        out[0] = 'O';
        out[1] = 'K';
        *outlen = 2;
        break;
    case C_INFO: {
        static const uint8_t info[] =
            "MusicMotor " FIRMWARE_VERSION "; protocol=1; motors=6; timer="
            CONFIG_STRING(TIMER_HZ) "; queue=" CONFIG_STRING(NOTES_BUFFER_DEPTH);
        *outlen = (uint8_t)(sizeof(info) - 1);
        memcpy(out, info, *outlen);
        break;
    }
    case C_STATUS:
        *outlen = status(out);
        break;
    case C_ESTOP:
        engine_estop();
        state.error = 0;
        break;
    case C_MASK:
        if (motor > 63)
            return E_VALUE;
        clear();
        state.mask = motor;
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
            if (!(motor & (1u << i))) {
                stop(i);
                state.motors[i].enabled = 0;
                platform_enable(i, 0);
            }
        break;
    case C_ENABLE:
        if (p[1] > 1)
            return E_VALUE;
        if (!p[1])
            stop(motor);
        state.motors[motor].enabled = p[1];
        platform_enable(motor, p[1]);
        break;
    case C_START:
        return start(motor);
    case C_STOP:
        stop(motor);
        break;
    case C_FREQ:
    case C_NOTE: {
        if (cmd == C_NOTE && p[1] > 127)
            return E_VALUE;
        uint32_t freq = cmd == C_NOTE ? note_mhz(p[1]) : u32(p + 1);
        uint16_t half = frequency_period(freq);
        if (!half)
            return E_VALUE;
        state.motors[motor].frequency = period_frequency(half);
        state.motors[motor].note = cmd == C_NOTE ? p[1] : 255;
        if (state.motors[motor].active)
            platform_step(motor, half);
        break;
    }
    case C_DIR:
        if (p[1] > 1)
            return E_VALUE;
        if (state.motors[motor].active)
            return E_STATE;
        state.motors[motor].dir = p[1];
        platform_dir(motor, p[1]);
        ready_at = platform_ms() + STARTUP_DELAY_MS;
        break;
    case C_MS:
        if (motor > 7)
            return E_VALUE;
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
            if (state.motors[i].active)
                return E_STATE;
        state.raw = motor;
        platform_common(state.raw, state.sleep, state.reset);
        break;
    case C_SLEEP:
    case C_RESET:
        if (motor > 1)
            return E_VALUE;
        if (motor)
            stream_stop(0);
        if (cmd == C_SLEEP)
            state.sleep = motor;
        else {
            state.reset = motor;
            pulse_pending = 0;
        }
        platform_common(state.raw, state.sleep, state.reset);
        ready_at = platform_ms() + STARTUP_DELAY_MS;
        break;
    case C_RESET_PULSE:
        stream_stop(0);
        state.reset = 1;
        platform_common(state.raw, state.sleep, 1);
        pulse_pending = 1;
        reset_release = platform_ms() + RESET_PULSE_MS;
        break;
    case C_STREAM_START:
        if (state.sleep || state.reset || !state.used || (int32_t)(platform_ms() - ready_at) < 0)
            return E_STATE;
        for (uint8_t i = 0; i < MOTOR_COUNT; i++)
            stop(i);
        state.position = 0;
        origin = platform_ms() + STREAM_START_DELAY_MS;
        state.running = 1;
        state.error = 0;
        break;
    case C_STREAM_STOP:
        if (motor > 1)
            return E_VALUE;
        stream_stop(motor);
        break;
    case C_CLEAR:
        if (state.running)
            return E_STATE;
        clear();
        break;
    case C_QUEUE:
        out[0] = (uint8_t)(NOTES_BUFFER_DEPTH - state.used);
        out[1] = (uint8_t)((NOTES_BUFFER_DEPTH - state.used) >> 8);
        *outlen = 2;
        break;
    case C_EVENTS: {
        if (!len || len % 10)
            return E_LENGTH;
        uint8_t count = len / 10;
        if (count > NOTES_BUFFER_DEPTH - state.used)
            return E_FULL;
        uint32_t previous = last_at;
        uint8_t end = end_queued;
        /* Validate entire batch before committing: no partial execution on NAK. */
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *e = p + 10 * i;
            uint32_t at = u32(e), v = u32(e + 6);
            uint8_t m = e[4], op = e[5];
            if (end || at < previous || at > 86400000u || (state.running && at < state.position))
                return E_ORDER;
            if (op > 2)
                return E_VALUE;
            if (op == 2) {
                if (m != 255 || v)
                    return E_VALUE;
                end = 1;
            } else if (m >= MOTOR_COUNT || !(state.mask & (1u << m)))
                return E_STATE;
            else if ((op == 1 && !frequency_period(v)) || (op == 0 && v))
                return E_VALUE;
            previous = at;
        }
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *e = p + 10 * i;
            queue[tail] = (Event){u32(e), u32(e + 6), e[4], e[5]};
            tail = (tail + 1) % NOTES_BUFFER_DEPTH;
            state.used++;
        }
        last_at = previous;
        end_queued = end;
        out[0] = (uint8_t)(NOTES_BUFFER_DEPTH - state.used);
        out[1] = (uint8_t)((NOTES_BUFFER_DEPTH - state.used) >> 8);
        *outlen = 2;
        break;
    }
    default:
        return E_COMMAND;
    }
    return E_OK;
}
void engine_note_set(const uint8_t *notes, uint8_t count) {
#if LIVE_MIDI_MODE
    if (count > 6) return;
    for (uint8_t i=0; i<count; ++i) {
        if (notes[i] > 127) return;
        for (uint8_t j=0; j<i; ++j) if (notes[j]==notes[i]) return;
    }
    uint32_t now = platform_ms();
    midi_last = last_command = now;
    midi_link = 1;
    memcpy(midi_notes, notes, count);
    midi_count = count;
    midi_pending = 1;
    /* Wake only after startup/fault, never delay an ordinary Note On. */
    if (state.sleep || state.reset) {
        state.sleep = state.reset = 0;
        pulse_pending = 0;
        platform_common(state.raw, 0, 0);
        ready_at = now + STARTUP_DELAY_MS;
    }
    engine_tick();
#else
    (void)notes; (void)count;
#endif
}
#if LIVE_MIDI_MODE
static void midi_apply(void) {
    uint8_t matched[6] = {0};
    uint8_t marked = 0;
    midi_pending = 0;
    for (uint8_t m=0; m<6; ++m) {
        uint8_t keep=0;
        for (uint8_t i=0; i<midi_count; ++i)
            if (midi_assignment[m]==midi_notes[i] && state.motors[m].active) {
                matched[i]=keep=1; break;
            }
        if (!keep && midi_assignment[m]!=255) {
            if (!marked) {platform_midi_mark();marked=1;}
            stop(m); state.motors[m].enabled=0; platform_enable(m,0);
            midi_assignment[m]=255;
        }
    }
    for (uint8_t i=0; i<midi_count; ++i) if (!matched[i]) {
        for (uint8_t m=0; m<6; ++m) if ((state.mask & (1u<<m)) && midi_assignment[m]==255) {
            uint32_t f=note_mhz(midi_notes[i]);
            while (f<MIN_FREQ_MHZ) f*=2;
            while (f>MAX_FREQ_MHZ) f/=2;
            if (!frequency_period(f)) { engine_fault(E_VALUE); return; }
            if (!marked) {platform_midi_mark();marked=1;}
            state.motors[m].frequency=f;
            state.motors[m].note=midi_notes[i];
            state.motors[m].enabled=1;platform_enable(m,1);
            uint8_t err=start(m);
            if (err) {engine_fault(err);return;}
            midi_assignment[m]=midi_notes[i];
            break;
        }
    }
}
#endif
void engine_tick(void) {
    uint32_t now = platform_ms();
#if LIVE_MIDI_MODE
    if (midi_link && now-midi_last>NS_TIMEOUT_MS) {engine_fault(E_TIMEOUT);return;}
    if (midi_pending && (int32_t)(now-ready_at)>=0) midi_apply();
    return;
#endif
    if (pulse_pending && (int32_t)(now - reset_release) >= 0) {
        pulse_pending = 0;
        state.reset = 0;
        platform_common(state.raw, state.sleep, 0);
        ready_at = now + STARTUP_DELAY_MS;
    }
    uint8_t any = state.running;
    for (uint8_t i = 0; i < MOTOR_COUNT; i++)
        any |= state.motors[i].enabled;
    if (any && now - last_command > LINK_TIMEOUT_MS) {
        engine_fault(E_TIMEOUT);
        return;
    }
    if (!state.running || (int32_t)(now - origin) < 0)
        return;
    state.position = now - origin;
    while (state.used && queue[head].at <= state.position) {
        Event e = queue[head];
        head = (head + 1) % NOTES_BUFFER_DEPTH;
        state.used--;
        if (e.op == 2) {
            stream_stop(0);
            return;
        }
        if (e.op == 0)
            stop(e.motor);
        else {
            state.motors[e.motor].frequency = period_frequency(frequency_period(e.value));
            state.motors[e.motor].note = closest_note(e.value);
            uint8_t error = start(e.motor);
            if (error) {
                engine_fault(error);
                return;
            }
        }
    }
    if (!state.used)
        engine_fault(E_UNDERRUN);
}
