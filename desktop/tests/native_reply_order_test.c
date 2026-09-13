#define main unused_manual_suite
#include "native_manual_test.c"
#undef main
#include "../../firmware/Core/Src/engine.c"
static unsigned reply_at, telemetry_count;
static void expensive_telemetry(const uint8_t *p,unsigned n) {
    if(n>=7&&p[4]==0x40){clock_ms+=20;++telemetry_count;}
}
static void reply_time(uint8_t sequence){(void)sequence;reply_at=clock_ms;}
int main(void) {
    engine_init();protocol_init();clock_ms=100;
    hmi_link=1;hmi_last=clock_ms;
    test_esp_sink=expensive_telemetry;test_reply_sink=reply_time;
    uint8_t wire[HD_MAX_FRAME];unsigned n=hd_encode(wire,HD_REQUEST,1,0,0);
    for(unsigned i=0;i<n;++i)protocol_esp_byte(wire[i]);
    assert(telemetry_count==1&&clock_ms==120&&reply_at==100);
    assert(HD_MASTER_TIMEOUT_MS>HD_REPLY_DEADLINE_MS+12);
    puts("Reply precedes 20 ms telemetry stall; master timeout covers maximum wire lengths");
}
