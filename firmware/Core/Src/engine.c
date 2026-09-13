#include "engine.h"
#include "platform.h"
#include <string.h>
#include "note_set_wire.h"
#include "music_core.h"
#include "file_assignment.h"
State state;
/* 8 bytes instead of 12: timestamps cover 24 h, frequencies fit in 24 bits.
   Marker motor 255 is stored as 7; it is never used as a motor index. */
typedef struct {
    uint32_t at:27, motor:3, op:2;
    uint32_t value:24, note:8;
} QueuedEvent;
_Static_assert(sizeof(QueuedEvent)==8,"Compact event queue");
static QueuedEvent queue[NOTES_BUFFER_DEPTH];
static uint16_t head, tail;
static uint32_t origin, last_at, ready_at, reset_release, last_command;
static uint8_t end_queued, pulse_pending;
static uint8_t playback_fault;
static uint8_t raw_mode, raw_open, raw_seek_ready, live_enabled=1;
static uint32_t raw_at, raw_seek;
static uint8_t boot_test,boot_index;
static uint8_t direction_wait;
static uint32_t direction_ready[MOTOR_COUNT];
static void step_unless_turning(uint8_t motor, uint16_t half_period) {
    if (!(direction_wait & (1u << motor))) platform_step(motor, half_period);
}
static uint32_t boot_at;
static uint32_t display_epoch;
static uint32_t display_until[MOTOR_COUNT];
static uint32_t display_ended[MOTOR_COUNT];
static uint8_t display_pending;
static struct {uint32_t at;uint8_t motor,note,velocity;} history[32];
static uint8_t history_head,history_tail,history_notes[6];
static void history_push(uint8_t motor,uint8_t note,uint8_t velocity) {
    uint8_t next=(history_tail+1)&31;
    if(next==history_head)history_head=(history_head+1)&31;
    history[history_tail].at=platform_ms();history[history_tail].motor=motor;
    history[history_tail].note=note;history[history_tail].velocity=velocity;history_tail=next;
}
static void history_voice(uint8_t motor,uint8_t note) {
    if(history_notes[motor]==note)return;
    if(history_notes[motor]<128)history_push(motor,history_notes[motor],0);
    if(note<128)history_push(motor,note,100);
    history_notes[motor]=note;
}
uint8_t engine_history_pending(void){return history_head!=history_tail;}
uint8_t engine_history_read(uint8_t *out) {
    if(!engine_history_pending())return 0;
    for(unsigned i=0;i<4;++i)out[i]=(uint8_t)(history[history_head].at>>(i*8));
    out[4]=history[history_head].motor;out[5]=history[history_head].note;out[6]=history[history_head].velocity;
    history_head=(history_head+1)&31;return 1;
}
/* Presentation only: never extends STEP pulses or the musical note. */
uint8_t engine_display_hold(uint8_t motor) {
    return motor < MOTOR_COUNT && state.running && !state.sleep && !state.reset &&
        (state.mask & (1u << motor)) &&
        display_until[motor] > state.position;
}
static void preview_gap(uint8_t motor, uint32_t ended_at) {
    for (uint16_t n=0, i=head; n<state.used; ++n, i=(i+1)%NOTES_BUFFER_DEPTH) {
        const QueuedEvent *next=&queue[i];
        if(next->at-ended_at > 500u || next->op==2)break;
        if(next->motor==motor && next->op==1) {
            display_until[motor]=next->at;
            break;
        }
    }
    /* Unknown future is never guessed: without a queued next note, go dark. */
}
static void refresh_display_gaps(void) {
    /* Recheck only on buffer refill, not in the high-frequency motor loop. */
    for(uint8_t m=0;m<MOTOR_COUNT;++m)if(display_pending&(1u<<m)) {
        if(state.position-display_ended[m]>500u)
            display_pending &= (uint8_t)~(1u<<m);
        else if(!display_until[m])preview_gap(m,display_ended[m]);
    }
}
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
    display_until[m] = 0;
    display_pending &= (uint8_t)~(1u << m);
    platform_stop(m);
    state.motors[m].active = 0;
    history_voice(m,255);
}
static void clear(void) {
    ++display_epoch;
    head = tail = state.used = 0;
    last_at = 0;
    end_queued = 0;
    raw_mode=raw_open=raw_seek_ready=0;raw_at=raw_seek=0;music_reset();
}
uint32_t engine_display_epoch(void) {return display_epoch;}
uint8_t engine_display_preview(uint8_t *out,uint8_t capacity,uint32_t now) {
    uint8_t count=0;
    if(!state.running)return 0;
    for(unsigned n=0,i=head;n<state.used&&count<capacity;++n,i=(i+1)%NOTES_BUFFER_DEPTH) {
        const QueuedEvent *e=&queue[i];uint32_t at=origin+e->at;
        if(e->op==2 || (int32_t)(at-now)>100)break;
        if(e->op!=1 || (int32_t)(at-now)<0)continue;
        uint8_t *p=out+10*count++;
        put32(p,at);p[4]=e->motor;p[5]=e->note<128?e->note:closest_note(e->value);
        put32(p+6,period_frequency(frequency_period(e->value)));
    }
    return count;
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
    boot_test=0;
    direction_wait=0;
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
    playback_fault = error;
    state.error = error;
    state.faults++;
}
void engine_init(void) {
    playback_fault = 0;
    origin = last_at = ready_at = reset_release = 0;
    end_queued = pulse_pending = 0;
    memset(&state, 0, sizeof(state));
    history_head=history_tail=0;memset(history_notes,255,sizeof(history_notes));
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
    step_unless_turning(m, half);
    v->frequency = period_frequency(half);
    v->active = 1;
    history_voice(m,v->note);
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
        p[0] = m->enabled | (m->active << 1) | (m->dir << 2) | (engine_display_hold(i)<<3);
        p[1] = m->note;
        put32(p + 2, m->frequency);
    }
    return 52;
}
/* At most six releases, six starts and one timeline marker per input event.
 * Credits are conservative so an accepted batch can never partially overflow. */
#define RAW_EXPANSION 13u
static void raw_push(uint32_t at,uint8_t motor,uint8_t op,uint32_t value,uint8_t note) {
    queue[tail]=(QueuedEvent){at,motor&7u,op,value,note};tail=(tail+1)%NOTES_BUFFER_DEPTH;++state.used;
}
static void raw_plan(uint32_t at) {
    uint8_t next[6],matched[6]={0};memset(next,255,6);
    uint32_t instances[6]={0};
    if(music.strategy==1)file_stable_assignment(next,instances);
    else {
    /* Pins first; preserve all remaining assignments before filling free motors. */
    for(unsigned i=0;i<music.count;++i)if(ms.pin[music.selected[i]]){next[ms.pin[music.selected[i]]-1]=music.selected[i];matched[i]=1;}
    for(unsigned m=0;m<6;++m)if((music.mask&(1u<<m))&&next[m]==255)
        for(unsigned i=0;i<music.count;++i)if(!matched[i]&&music.assignment[m]==music.selected[i]){next[m]=music.selected[i];matched[i]=1;break;}
    /* Match the desktop's pitch-history preference instead of always reusing
     * the first free motor. Unused motors have zero pitch-change cost. */
    for(unsigned i=0;i<music.count;++i)if(!matched[i]) {
        int best=-1,cost=128;
        for(unsigned m=0;m<6;++m)if((music.mask&(1u<<m))&&next[m]==255) {
            int distance=music.last_pitch[m]==255?0:(int)music.last_pitch[m]-music.selected[i];
            if(distance<0)distance=-distance;
            if(distance<cost){cost=distance;best=(int)m;}
        }
        if(best>=0)next[best]=music.selected[i];
    }
    }
    for(unsigned m=0;m<6;++m)if(next[m]!=music.assignment[m]) {
        if(music.assignment[m]!=255)raw_push(at,(uint8_t)m,0,0,255);
        if(next[m]!=255){uint32_t f=note_mhz(next[m]);while(f<MIN_FREQ_MHZ)f*=2;while(f>MAX_FREQ_MHZ)f/=2;raw_push(at,(uint8_t)m,1,f,next[m]);}
    }
    memcpy(music.assignment,next,6);
    memcpy(music.instance_assignment,instances,sizeof(instances));
    for(unsigned m=0;m<6;++m)if(next[m]!=255)music.last_pitch[m]=next[m];
    raw_push(at,255,3,0,255); /* Known silence is still buffered time. */
}
static uint8_t raw_events(const uint8_t *p,uint8_t len,uint8_t *out,uint8_t *outlen) {
    if(!len||len%10)return E_LENGTH;
    /* A refill after a fault must report its cause, not hide it behind E_STATE
       after engine_estop() has cleared the planner. Explicit ESTOP acknowledges it. */
    if(playback_fault)return playback_fault;
    if(!raw_mode&&(p[5]&127)!=5)return E_STATE;
    unsigned count=len/10;if(count+1>(NOTES_BUFFER_DEPTH-state.used)/RAW_EXPANSION)return E_FULL;
    uint32_t previous=raw_at;uint8_t open=raw_open,ended=end_queued;
    if(!raw_mode&&state.used)return E_STATE;
    for(unsigned i=0;i<count;++i){const uint8_t *e=p+i*10;uint32_t at=u32(e);uint8_t type=e[5]&127;
        if(ended||at<previous||at>86400000u||(open&&at!=previous)||(state.running&&at<raw_seek+state.position))return E_ORDER;
        if(type>5||type==4||e[9]>127||e[6]>127||e[7]>127||e[8]>6)return E_VALUE;
        if(type==3){if(open)return E_ORDER;if(!(e[5]&128)||e[4]||u32(e+6))return E_VALUE;ended=1;}
        if(type==5){if(raw_mode||i||state.running||at||e[4]>63||!e[6]||e[6]>6||e[7]>7||e[8]||e[9]||!(e[5]&128))return E_VALUE;}
        previous=at;open=!(e[5]&128);
    }
    raw_mode=1;
    for(unsigned i=0;i<count;++i){const uint8_t *e=p+i*10;uint32_t at=u32(e);uint8_t type=e[5]&127;
        if(type==5){music.mask=e[4]&state.mask;music.voices=e[6];music.strategy=e[7];unsigned voices=0;for(unsigned m=0;m<6;++m)voices+=(music.mask>>m)&1;if(music.voices>voices)music.voices=(uint8_t)voices;continue;}
        if(!raw_seek_ready&&at>=raw_seek){raw_seek_ready=1;if(raw_seek){music_choose(raw_seek);raw_plan(0);}}
        if(type==3){raw_push(at>=raw_seek?at-raw_seek:0,255,2,0,255);end_queued=1;continue;}
        uint8_t err=music_event_ex(at,e[4],type,e[6],e[7],e[8],e[9]);
        if(err){engine_fault(err);return err;}
        if(e[5]&128){music_choose(at);if(at>=raw_seek)raw_plan(at-raw_seek);}
    }
    raw_at=previous;raw_open=open;last_at=previous>=raw_seek?previous-raw_seek:0;
    refresh_display_gaps();
    unsigned free=(NOTES_BUFFER_DEPTH-state.used)/RAW_EXPANSION;
    out[0]=(uint8_t)(free?free-1:0);out[1]=0;*outlen=2;
    return E_OK;
}
void engine_live_gate(uint8_t enabled) {
    engine_estop();live_enabled=enabled==2?!live_enabled:enabled;
}
uint8_t engine_live_enabled(void){return live_enabled;}
uint8_t engine_live_events(const uint8_t *p,uint8_t len,uint8_t connected) {
    if(len%10)return E_LENGTH;
    if(!connected){
#if LIVE_MIDI_MODE
        if(midi_link)engine_estop();
#endif
        music_reset();return E_OK;
    }
    if(!live_enabled)return E_OK;
    uint32_t now=platform_ms();
    for(unsigned i=0;i<len;i+=10){const uint8_t *e=p+i;uint8_t type=e[5]&127;
        if(type>4||type==3||e[6]>127||e[7]>127||e[8]||e[9])return E_VALUE;
    }
    for(unsigned i=0;i<len;i+=10){const uint8_t *e=p+i;uint8_t err=music_event(now,e[4],e[5]&127,e[6],e[7],0);
        if(err){engine_fault(err);return err;}
        if(e[5]&128)music_choose(now);
    }
    engine_note_set(music.selected,music.count);return E_OK;
}
uint8_t engine_command(uint8_t cmd, const uint8_t *p, uint8_t len, uint8_t *out, uint8_t *outlen) {
    static const uint8_t lengths[] = {0, 0, 0, 0, 0, 1, 2, 1,   1, 5, 2,
                                      2, 1, 1, 1, 0, 0, 1, 255, 0, 0, 0};
    *outlen = 0;
    if(cmd==C_RAW_EVENTS){last_command=platform_ms();return raw_events(p,len,out,outlen);}
    if(cmd==C_RAW_SEEK){if(len!=4)return E_LENGTH;if(state.running||state.used||raw_mode)return E_STATE;uint32_t at=u32(p);if(at>86400000u)return E_VALUE;raw_seek=at;return E_OK;}
    if (cmd < 1 || cmd > 21)
        return E_COMMAND;
    if (cmd != C_EVENTS && len != lengths[cmd])
        return E_LENGTH;
    last_command = platform_ms();
    if(boot_test && cmd!=C_PING && cmd!=C_STATUS)engine_estop();
    uint8_t motor = len ? p[0] : 0;
    if (cmd >= C_ENABLE && cmd <= C_DIR) {
        if (motor >= MOTOR_COUNT)
            return E_VALUE;
        if (!(state.mask & (1u << motor)))
            return E_STATE;
        if (state.running && cmd != C_DIR)
            return E_STATE;
    }
    if (state.running && (cmd == C_MASK || cmd == C_MS || cmd == C_STREAM_START))
        return E_STATE;
    switch (cmd) {
    case C_BOOT_TEST:
        engine_estop();state.sleep=state.reset=0;platform_common(state.raw,0,0);
        boot_test=1;boot_index=0;boot_at=platform_ms()+STARTUP_DELAY_MS;ready_at=boot_at;
        break;
    case C_PING:
        out[0] = 'O';
        out[1] = 'K';
        *outlen = 2;
        break;
    case C_INFO: {
        static const uint8_t info[] =
            "MusicMotor " FIRMWARE_VERSION "; protocol=1; motors=6; timer="
            CONFIG_STRING(TIMER_HZ) "; queue=" CONFIG_STRING(NOTES_BUFFER_DEPTH) "; raw_midi=1; max_hz=1200";
        *outlen = (uint8_t)(sizeof(info) - 1);
        memcpy(out, info, *outlen);
        break;
    }
    case C_STATUS:
        *outlen = status(out);
        break;
    case C_ESTOP:
        engine_estop();
        playback_fault = 0;
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
        /* Manual START from either UART or HMI enables its driver itself.
         * Reject invalid starts before applying current to the windings. */
        if(state.sleep || state.reset || (int32_t)(platform_ms()-ready_at)<0)
            return E_STATE;
        if(!frequency_period(state.motors[motor].frequency))return E_VALUE;
        state.motors[motor].enabled=1;
        platform_enable(motor,1);
        return start(motor);
    case C_STOP:
        stop(motor);
        state.motors[motor].enabled=0;
        platform_enable(motor,0);
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
        /* Frequency controls also need a pitch for the LCD note history.
           Label the actual STEP frequency without snapping it to a semitone. */
        state.motors[motor].note = cmd == C_NOTE ? p[1] : closest_note(state.motors[motor].frequency);
        if (state.motors[motor].active) {
            step_unless_turning(motor, half);
            history_voice(motor,state.motors[motor].note);
        }
        break;
    }
    case C_DIR:
        if (p[1] > 1)
            return E_VALUE;
        if (state.motors[motor].dir == p[1]) break;
        /* Stop physical STEP but retain the requested note/run state. MIDI
         * Note Off, STOP or a fault during the pause can still cancel it. */
        platform_stop(motor);
        direction_wait |= (uint8_t)(1u << motor);
        state.motors[motor].dir = p[1];
        platform_dir(motor, p[1]);
        direction_ready[motor] = platform_ms() + STARTUP_DELAY_MS;
        break;
    case C_MS:
        if (motor > 7)
            return E_VALUE;
#if LIVE_MIDI_MODE
        /* Include pending and sustained notes, even before STEP starts. */
        if (midi_link) {
            if (midi_count) return E_STATE;
            for (unsigned i = 0; i < 128; ++i)
                if (music.keys[i].flags) return E_STATE;
        }
#endif
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
        if (state.sleep || state.reset || !state.used || raw_open || (int32_t)(platform_ms() - ready_at) < 0)
            return E_STATE;
        for (uint8_t i = 0; i < MOTOR_COUNT; i++) {
            stop(i);
            state.motors[i].enabled=0;
            platform_enable(i,0);
        }
        state.position = 0;
        origin = platform_ms() + STREAM_START_DELAY_MS;
        ++display_epoch;
        state.running = 1;
        playback_fault = 0;
        state.error = 0;
        break;
    case C_STREAM_STOP:
        if (motor > 1)
            return E_VALUE;
        stream_stop(1); /* Legacy byte 0 is accepted, but STOP always releases ENABLE. */
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
        if(raw_mode)return E_STATE;
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
            queue[tail] = (QueuedEvent){u32(e), e[4]&7u, e[5], u32(e + 6),255};
            tail = (tail + 1) % NOTES_BUFFER_DEPTH;
            state.used++;
        }
        last_at = previous;
        end_queued = end;
        refresh_display_gaps();
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
static void engine_tick_update(void) {
    uint32_t now = platform_ms();
#if LIVE_MIDI_MODE
    if (midi_link && now-midi_last>NS_TIMEOUT_MS) {engine_fault(E_TIMEOUT);return;}
    if (midi_pending && (int32_t)(now-ready_at)>=0) midi_apply();
    if (midi_link) return;
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
    if (any && !boot_test && now - last_command > LINK_TIMEOUT_MS) {
        engine_fault(E_TIMEOUT);
        return;
    }
    if(boot_test) {
        static const uint8_t chord[6]={36,55,64,69,74,79};
        if((int32_t)(now-boot_at)>=0) {
            if(boot_index==6) {
                /* Keep EN asserted while slowing down; update the running timer
                   without restarting STEP or changing direction. */
                uint32_t elapsed=now-boot_at;
                if(elapsed>=400u){engine_estop();return;}
                elapsed=(elapsed/10u)*10u;
                for(unsigned m=0;m<MOTOR_COUNT;++m)if(state.motors[m].active) {
                    uint32_t f=MIN_FREQ_MHZ+(note_mhz(chord[m])-MIN_FREQ_MHZ)*(400u-elapsed)/400u;
                    uint16_t half=frequency_period(f);
                    f=period_frequency(half);
                    if(state.motors[m].frequency!=f) {
                        state.motors[m].frequency=f;
                        step_unless_turning(m,half);
                    }
                }
            }
            else {
                unsigned m=boot_index++;
                if(state.mask&(1u<<m)) {
                    state.motors[m].enabled=1;platform_enable(m,1);
                    state.motors[m].note=chord[m];state.motors[m].frequency=note_mhz(chord[m]);
                    uint8_t err=start(m);if(err){engine_fault(err);return;}
                }
                boot_at=now+(boot_index==6?2000u:400u);
            }
        }
        return;
    }
    if (!state.running || (int32_t)(now - origin) < 0)
        return;
    state.position = now - origin;
    while (state.used && queue[head].at <= state.position) {
        QueuedEvent e = queue[head];
        head = (head + 1) % NOTES_BUFFER_DEPTH;
        state.used--;
        if (e.op == 2) {
            stream_stop(1);
            return;
        }
        if(e.op==3)continue;
        if (e.op == 0) {
            uint8_t was_active=state.motors[e.motor].active;
            if(was_active) {
                stop(e.motor);
                state.motors[e.motor].enabled=0;
                platform_enable(e.motor,0);
                display_ended[e.motor]=e.at;
                display_pending|=(uint8_t)(1u<<e.motor);
                preview_gap(e.motor,e.at);
            }
        } else {
            display_until[e.motor]=0;
            display_pending &= (uint8_t)~(1u << e.motor);
            state.motors[e.motor].frequency = period_frequency(frequency_period(e.value));
            state.motors[e.motor].note = e.note<128?e.note:closest_note(e.value);
            state.motors[e.motor].enabled=1;
            platform_enable(e.motor,1);
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
void engine_tick(void) {
    engine_tick_update();
    /* Process due Note Off/STOP/faults before considering a restart. */
    uint32_t now = platform_ms();
    for (uint8_t m=0; m<MOTOR_COUNT; ++m) {
        if (!(direction_wait & (1u << m)) || (int32_t)(now-direction_ready[m]) < 0) continue;
        direction_wait &= (uint8_t)~(1u << m);
        if (state.motors[m].active && state.motors[m].enabled && !state.sleep && !state.reset)
            platform_step(m, frequency_period(state.motors[m].frequency));
    }
}
