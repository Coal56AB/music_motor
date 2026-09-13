#define main unused_manual_suite
#include "native_manual_test.c"
#undef main
#include "../../firmware/Core/Src/engine.c"
void saved_stm_init(void (*sink)(const uint8_t *,unsigned),void (*reply)(uint8_t)) {
    clock_ms=0;engine_init();protocol_init();test_esp_sink=sink;test_reply_sink=reply;
}
void saved_stm_tick(uint32_t at){clock_ms=at;protocol_poll();engine_tick();}
void saved_stm_request(const uint8_t *p,unsigned n,uint8_t seq) {
    uint8_t wire[HD_MAX_FRAME];unsigned count=hd_encode(wire,HD_REQUEST,seq,p,n);
    for(unsigned i=0;i<count;++i)protocol_esp_byte(wire[i]);
}
unsigned saved_stm_error(void){return state.error;}
unsigned saved_stm_running(void){return state.running;}
unsigned saved_stm_position(void){return state.position;}
unsigned saved_stm_used(void){return state.used;}
