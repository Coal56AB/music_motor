#ifndef ESP_TX_QUEUE_H
#define ESP_TX_QUEUE_H
#include <stdint.h>
/* Main-loop-owned queue. UART ISR transmits a separate immutable HD frame. */
typedef struct {uint8_t bytes[512];uint16_t head,tail;} EspTxQueue;
static inline unsigned esp_tx_free(const EspTxQueue *q) {
    return (q->tail-q->head-1u)&511u;
}
static inline int esp_tx_push(EspTxQueue *q,const uint8_t *p,unsigned n) {
    /* Largest engine ACK is 61 bytes. Never let screen traffic consume it. */
    unsigned reserve=n>=7&&p[4]>=0x40&&p[4]<=0x45?64u:0u;
    if(n+reserve>esp_tx_free(q))return 0;
    for(unsigned i=0;i<n;++i){q->bytes[q->head]=p[i];q->head=(q->head+1u)&511u;}
    return 1;
}
static inline unsigned esp_tx_pop(EspTxQueue *q,uint8_t *p,unsigned max) {
    unsigned n=0;
    while(q->tail!=q->head&&n<max){p[n++]=q->bytes[q->tail];q->tail=(q->tail+1u)&511u;}
    return n;
}
#endif
