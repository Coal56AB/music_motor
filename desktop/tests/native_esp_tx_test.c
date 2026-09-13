#include <assert.h>
#include <stdio.h>
#include "../../firmware/Core/Inc/esp_tx_queue.h"
#include "../../firmware/Core/Inc/half_duplex_wire.h"
int main(void) {
    EspTxQueue q={0};uint8_t visual[172]={0xa5,0x5a,165,0,0x45},ack[61]={0xa5,0x5a,54,0,0x73};
    uint8_t out[512];unsigned dropped=0;
    for(unsigned turn=0;turn<10000;++turn) {
        /* 1000 telemetry attempts per turn, followed by the largest status ACK. */
        for(unsigned i=0;i<1000;++i)if(!esp_tx_push(&q,visual,sizeof(visual)))++dropped;
        assert(esp_tx_push(&q,ack,sizeof(ack)));
        unsigned n=esp_tx_pop(&q,out,sizeof(out));assert(n>=61);
        assert(!memcmp(out+n-61,ack,61));assert(esp_tx_free(&q)==511);
    }
    puts("10 million telemetry packets: bounded queue, every control ACK retained");
    assert(dropped);
}
