#include <assert.h>
#include <stdio.h>
#include "../../firmware/Core/Src/protocol.c"

static uint32_t clock_ms;
static uint8_t note_events[16][3],note_event_count,reported_note;
uint32_t platform_ms(void){return clock_ms;}
void platform_midi_mark(void){}
void platform_enable(uint8_t m,uint8_t on){(void)m;(void)on;}
void platform_dir(uint8_t m,uint8_t dir){(void)m;(void)dir;}
void platform_common(uint8_t raw,uint8_t sleep,uint8_t reset){(void)raw;(void)sleep;(void)reset;}
void platform_step(uint8_t m,uint16_t period){(void)m;(void)period;}
void platform_stop(uint8_t m){(void)m;}
void platform_write(const uint8_t *b,uint16_t n){(void)b;(void)n;}
void platform_esp_write(const uint8_t *b,uint16_t n){
    if(n==14&&b[4]==0x42){
        assert(note_event_count<16);
        memcpy(note_events[note_event_count++],b+9,3);
    }
    if(n==57&&b[4]==0x40)reported_note=b[20];
}
void platform_esp_reply(uint8_t seq){(void)seq;}
void platform_esp_poll(void){}
int platform_read(void){return -1;}
int platform_esp_read(void){return -1;}
uint8_t platform_uart_error(void){return 0;}
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
    start_manual();note_event_count=0;
    screen_command(3,440000,0); /* The screen sends frequency even for A4. */
    clock_ms+=50;hmi_poll(1);
    assert(note_event_count==1&&note_events[0][0]==0&&note_events[0][1]==69&&note_events[0][2]>0);
    assert(reported_note==69);
    clock_ms+=50;hmi_poll(1);assert(note_event_count==1);
    screen_command(3,466164,0);clock_ms+=50;hmi_poll(1);
    assert(note_event_count==3&&note_events[1][1]==69&&note_events[1][2]==0);
    assert(note_events[2][1]==70&&note_events[2][2]>0&&reported_note==70);
    screen_command(5,0,0);clock_ms+=50;hmi_poll(1);
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
}
