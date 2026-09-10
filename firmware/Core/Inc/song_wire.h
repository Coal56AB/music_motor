#ifndef SONG_WIRE_H
#define SONG_WIRE_H
#include <stdint.h>
#define SONG_BEGIN 48u
#define SONG_DATA 49u
#define SONG_COMMIT 50u
#define SONG_STATUS 51u
#define SONG_RELAY 0x70u
#define SONG_REPLY 0x71u
#define SONG_ENGINE 0x72u
#define SONG_ENGINE_REPLY 0x73u
#define SONG_SLOT_COUNT 4u
#define SONG_SLOT_BYTES 0x30000u
#define SONG_DATA_OFFSET 4096u
#define SONG_MAX_EVENTS ((SONG_SLOT_BYTES-SONG_DATA_OFFSET)/10u)
static inline uint32_t song_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void song_put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static inline uint32_t song_crc(uint32_t crc,const uint8_t *p,unsigned n) {
    while(n--){crc^=*p++;for(unsigned i=0;i<8;i++)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
    return crc;
}
#endif
