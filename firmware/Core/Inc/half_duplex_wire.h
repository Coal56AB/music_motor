#ifndef HALF_DUPLEX_WIRE_H
#define HALF_DUPLEX_WIRE_H
#include <stdint.h>
#include <string.h>
#define HD_BAUD 230400u
#define HD_MAX_PAYLOAD 240u
#define HD_MAX_FRAME (HD_MAX_PAYLOAD + 7u)
#define HD_REQUEST 1u
#define HD_REPLY 2u
#define HD_REPLY_DEADLINE_MS 3u
#define HD_MASTER_TIMEOUT_MS 20u
/* C7 3A TYPE SEQ LENGTH PAYLOAD CRC16_LE. The master alone starts transactions.
 * Slave starts its reply within 3 ms of the final request byte or stays silent.
 * At 230400 the largest reply takes <10.8 ms; master waits 20 ms before retrying.
 * CRC and sequence reject echoes, corruption and stale responses. */
static inline uint16_t hd_crc(const uint8_t *p, unsigned n) {
    uint16_t c=0xffff;
    while(n--) {c^=(uint16_t)*p++<<8;for(unsigned i=0;i<8;++i)c=(uint16_t)((c<<1)^((c&0x8000)?0x1021:0));}
    return c;
}
static inline unsigned hd_encode(uint8_t *p,uint8_t type,uint8_t seq,const uint8_t *data,unsigned n) {
    if(n>HD_MAX_PAYLOAD)return 0;
    p[0]=0xc7;p[1]=0x3a;p[2]=type;p[3]=seq;p[4]=(uint8_t)n;
    if(n)memcpy(p+5,data,n);
    uint16_t c=hd_crc(p+2,n+3);p[n+5]=(uint8_t)c;p[n+6]=(uint8_t)(c>>8);return n+7;
}
typedef struct {uint8_t bytes[HD_MAX_FRAME];unsigned used;} HdParser;
/* Returned frame remains valid until the next feed. A sliding parser recovers
 * after byte loss, including a corrupt length hiding the next packet. */
static inline int hd_feed(HdParser *p,uint8_t b) {
    if(p->used==HD_MAX_FRAME) {memmove(p->bytes,p->bytes+1,--p->used);}
    p->bytes[p->used++]=b;
    for(unsigned i=0;i+7<=p->used;++i) {
        const uint8_t *q=p->bytes+i;
        unsigned n=q[4],total=n+7;
        if(q[0]!=0xc7||q[1]!=0x3a||(q[2]!=HD_REQUEST&&q[2]!=HD_REPLY)||n>HD_MAX_PAYLOAD||i+total!=p->used)continue;
        if(hd_crc(q+2,n+3)!=((uint16_t)q[n+5]|(uint16_t)q[n+6]<<8))continue;
        if(i)memmove(p->bytes,q,total);
        p->used=0;return 1;
    }
    return 0;
}
#endif
