#include "native_saved_stm.c"
static void discard_frame(const uint8_t *p,unsigned n){(void)p;(void)n;}
static void live(unsigned seq,unsigned count,unsigned off) {
    uint8_t packet[208]={0xa5,0x5a};unsigned length=1+count*10;
    packet[2]=(uint8_t)length;packet[3]=(uint8_t)seq;packet[4]=0x56;packet[5]=1;
    for(unsigned i=0;i<count;++i) {
        uint8_t *e=packet+6+i*10;put32(e,clock_ms);e[4]=0;e[5]=(uint8_t)(128+off);
        e[6]=(uint8_t)(60+i);e[7]=off?0:100;
    }
    uint16_t crc=protocol_crc(packet+2,length+3);packet[length+5]=(uint8_t)crc;packet[length+6]=(uint8_t)(crc>>8);
    saved_stm_request(packet,length+7,(uint8_t)seq);engine_tick();assert(!state.error);
}
int main(void) {
    saved_stm_init(discard_frame,0);
    unsigned seq=0;
    for(unsigned i=0;i<1000;++i){clock_ms+=5;live(++seq,20,i&1);}
    clock_ms+=5;live(++seq,1,0);
    for(unsigned i=0;i<300;++i){clock_ms+=100;live(++seq,0,0);}
    unsigned sounding=0;for(unsigned m=0;m<6;++m)sounding+=state.motors[m].active;
    assert(sounding==1);
    clock_ms+=5;live(++seq,1,1);
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].active);
    puts("20000 live MIDI events, sequence wrap, then held note for 30 seconds and Note Off passed");
}
