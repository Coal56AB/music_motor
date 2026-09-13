#include <assert.h>
#include <stdio.h>
#include "../../firmware/Core/Src/protocol.c"

static uint32_t clock_ms;
static uint8_t physical_step[6], dir_changed_while_stepping;
static unsigned physical_starts[6];
static void (*test_esp_sink)(const uint8_t *,unsigned);
static void (*test_reply_sink)(uint8_t);
static uint8_t note_events[16][3],note_event_count,reported_note;
static uint8_t last_midi_ack;
static uint8_t reported_state[86];
static uint8_t check_history_clock;
static uint32_t checked_clock;
uint32_t platform_ms(void){return clock_ms;}
void platform_midi_mark(void){}
void platform_enable(uint8_t m,uint8_t on){(void)m;(void)on;}
void platform_dir(uint8_t m,uint8_t dir){dir_changed_while_stepping=physical_step[m];(void)dir;}
void platform_common(uint8_t raw,uint8_t sleep,uint8_t reset){(void)raw;(void)sleep;(void)reset;}
void platform_step(uint8_t m,uint16_t period){physical_step[m]=1;++physical_starts[m];(void)period;}
void platform_stop(uint8_t m){physical_step[m]=0;}
void platform_write(const uint8_t *b,uint16_t n){(void)b;(void)n;}
void platform_esp_write(const uint8_t *b,uint16_t n){
    if(test_esp_sink){test_esp_sink(b,n);return;}
    if(n==8&&b[4]==0x57)last_midi_ack=b[5];
    if(check_history_clock&&((n==14&&b[4]==0x42)||(n==11&&b[4]==0x43))){
        uint32_t at=song_u32(b+5);assert(at>=checked_clock);checked_clock=at;
    }
    if(n==14&&b[4]==0x42){
        assert(note_event_count<16);
        memcpy(note_events[note_event_count++],b+9,3);
    }
    if(n==101&&b[4]==0x40){assert(b[5]==3);reported_note=b[20];memcpy(reported_state,b+5,86);}
}
void platform_esp_reply(uint8_t seq){if(test_reply_sink)test_reply_sink(seq);}
void platform_esp_poll(void){}
int platform_read(void){return -1;}
int platform_esp_read(void){return -1;}
static uint8_t injected_uart_error;
uint8_t platform_uart_error(void){uint8_t error=injected_uart_error;injected_uart_error=0;return error;}
uint8_t platform_esp_uart_error(void){return 0;}
uint8_t platform_oc_error(void){return 0;}
uint32_t platform_lock(void){return 0;}
void platform_unlock(uint32_t key){(void)key;}

static void screen_command(uint8_t action,uint32_t value,int corrupt) {
    static uint8_t sequence;
    uint8_t frame[13]={0xa5,0x5a,6,0,0x50,action,0};
    frame[3]=++sequence;hmi_put32(frame+7,value);
    uint16_t crc=protocol_crc(frame+2,9);
    frame[11]=(uint8_t)crc;frame[12]=(uint8_t)(crc>>8);
    if(corrupt)frame[12]^=1;
    for(unsigned i=0;i<sizeof(frame);++i)hmi_byte(frame[i]);
}
static void start_manual(void) {
    clock_ms=0;engine_init();protocol_init();
    uint8_t zero=0,out[64],len=0;
    assert(engine_command(C_SLEEP,&zero,1,out,&len)==E_OK);
    assert(engine_command(C_RESET,&zero,1,out,&len)==E_OK);
    clock_ms=STARTUP_DELAY_MS+1;
    screen_command(255,1,0);
    screen_command(5,1,0);
    assert(state.motors[0].active&&hmi_manual);
}
static void tick_heartbeat(unsigned duration,int corrupt) {
    uint32_t end=clock_ms+duration;
    while(clock_ms<end) {
        clock_ms+=100;
        if(pc_active)pc_last=clock_ms;
        screen_command(255,1,corrupt);
        protocol_poll();engine_tick();
    }
}
int main(void) {
    start_manual();
    uint32_t frequency=state.motors[0].frequency;
    screen_command(6,1,0);
    assert(state.motors[0].dir==1&&!physical_step[0]&&!dir_changed_while_stepping);
    clock_ms+=STARTUP_DELAY_MS-1;engine_tick();assert(!physical_step[0]);
    ++clock_ms;engine_tick();assert(physical_step[0]&&state.motors[0].frequency==frequency);
    unsigned starts=physical_starts[0];
    screen_command(6,1,0);engine_tick();assert(physical_starts[0]==starts&&physical_step[0]);
    screen_command(6,0,0);screen_command(5,0,0);
    clock_ms+=STARTUP_DELAY_MS;engine_tick();assert(!physical_step[0]);
    screen_command(6,1,0);clock_ms+=STARTUP_DELAY_MS;engine_tick();
    assert(!physical_step[0]); /* Changing DIR while stopped never starts it. */
    start_manual();screen_command(6,1,0);screen_command(0,0,0);
    clock_ms+=STARTUP_DELAY_MS;engine_tick();assert(!physical_step[0]&&state.reset);
    start_manual();clock_ms=0xfffffffeu;screen_command(6,1,0);
    clock_ms=0xffffffffu;engine_tick();assert(!physical_step[0]);
    clock_ms=0;engine_tick();assert(physical_step[0]);
    /* START enables its driver for both terminal and screen, but not in RESET. */
    clock_ms=0;engine_init();protocol_init();
    uint8_t arg=0,out[128],len;
    assert(engine_command(C_START,&arg,1,out,&len)==E_STATE);
    assert(!state.motors[0].enabled&&!state.motors[0].active);
    assert(!engine_command(C_SLEEP,&arg,1,out,&len));
    assert(!engine_command(C_RESET,&arg,1,out,&len));
    clock_ms=STARTUP_DELAY_MS+1;
    assert(!engine_command(C_START,&arg,1,out,&len));
    assert(state.motors[0].enabled&&state.motors[0].active);
    assert(!engine_command(C_STOP,&arg,1,out,&len));
    assert(!state.motors[0].enabled&&!state.motors[0].active);
    start_manual();note_event_count=0;
    screen_command(3,440000,0); /* The screen sends frequency even for A4. */
    clock_ms+=50;hmi_poll(1);
    assert(note_event_count==1&&note_events[0][0]==0&&note_events[0][1]==69&&note_events[0][2]>0);
    assert(reported_note==69);
    uint32_t sent_at=hmi_sent;
    clock_ms+=19;hmi_poll(1);assert(hmi_sent==sent_at);
    clock_ms+=1;hmi_poll(1);assert(hmi_sent==clock_ms&&note_event_count==1);
    screen_command(3,466164,0);clock_ms+=50;hmi_poll(1);
    assert(note_event_count==3&&note_events[1][1]==69&&note_events[1][2]==0);
    assert(note_events[2][1]==70&&note_events[2][2]>0&&reported_note==70);
    screen_command(5,0,0);clock_ms+=50;hmi_poll(1);
    assert(!state.motors[0].enabled&&!state.motors[0].active);
    assert(note_event_count==4&&note_events[3][1]==70&&note_events[3][2]==0);
    start_manual();tick_heartbeat(5000,0);
    assert(state.motors[0].active&&!state.reset&&state.error==E_OK);
    /* Real loss still stops sooner than the generic engine watchdog. */
    clock_ms+=NS_TIMEOUT_MS+1;protocol_poll();engine_tick();
    assert(!state.motors[0].active&&state.reset);
    start_manual();tick_heartbeat(5000,1);
    assert(!state.motors[0].active&&state.reset);
    start_manual();screen_command(255,0,0);
    assert(!state.motors[0].active&&state.reset);
    /* Screen traffic must not keep another owner's abandoned motor alive. */
    start_manual();hmi_manual=0;tick_heartbeat(3000,0);
    assert(!state.motors[0].active&&state.error==E_TIMEOUT);
    start_manual();pc_active=1;tick_heartbeat(3000,0);
    assert(!state.motors[0].active&&state.error==E_TIMEOUT);
    puts("Manual motor heartbeat and lost-link checks passed");
    return 0;
}
