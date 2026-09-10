#include "protocol.h"
#include "engine.h"
#include "platform.h"
#include <string.h>
#include "song_wire.h"
#if LIVE_MIDI_MODE
#include "note_set_wire.h"
#include "half_duplex_wire.h"
static HdParser esp_wire;
static uint32_t esp_wire_at;
static uint8_t pc_active,auto_play,save_wait;
static uint32_t pc_last,pc_title_sent;
static uint8_t pc_song[57],pc_song_len,pc_action[7],pc_action_id;
static uint8_t midi_frame[NS_SIZE];
static uint8_t midi_used;
static uint32_t midi_byte_at;
static uint32_t midi_received_at;
static uint8_t midi_seen;
static void midi_byte(uint8_t b) {
    uint32_t now=platform_ms();
    if (midi_used && now-midi_byte_at>5u) midi_used=0;
    midi_byte_at=now;
    midi_frame[midi_used++]=b;
    /* Sliding fixed-size window recovers on the very next valid frame. */
    if (midi_used==NS_SIZE) {
        if (ns_valid(midi_frame)) {
            if (pc_active || auto_play || save_wait) {midi_used=0;return;}
            midi_received_at=now;midi_seen=1;
            engine_note_set(midi_frame+5,midi_frame[4]);midi_used=0;
        } else {
            memmove(midi_frame,midi_frame+1,NS_SIZE-1);--midi_used;
        }
    }
}
#endif
static uint8_t rx[247], previous[247], response[247];
static uint16_t used, previous_len, response_len;
static uint32_t last_byte, previous_at;
#if LIVE_MIDI_MODE
static uint8_t hmi_frame[13], hmi_used, hmi_link, hmi_authority, hmi_manual;
static uint8_t hmi_notes[6];
static uint32_t hmi_byte_at, hmi_last, hmi_sent;
static struct {uint8_t payload[6], seq, result, valid; uint32_t at;} hmi_recent[16];
static unsigned hmi_recent_next;
static uint8_t save_packet[200],save_length,save_seq;
static uint8_t save_status[8];
static uint32_t save_sent,save_at;
static uint8_t rpc_cache[208],rpc_cache_len,rpc_response[64],rpc_response_len;
static uint32_t rpc_cached_at;
static void hmi_byte(uint8_t b);
static void hmi_poll(uint8_t emit);
#endif
uint16_t protocol_crc(const uint8_t *data, uint16_t n) {
    uint16_t crc = 0xffff;
    for (uint16_t i = 0; i < n; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}
void protocol_init(void) {
    used = previous_len = response_len = 0;
#if LIVE_MIDI_MODE
    midi_used=midi_seen=hmi_used=hmi_link=hmi_authority=hmi_manual=0;
    pc_song_len=pc_action_id=0;memset(pc_action,0,sizeof(pc_action));pc_title_sent=0;
    pc_active=0;pc_last=0;esp_wire.used=0;esp_wire_at=0;
    hmi_sent=0;
    save_wait=save_length=auto_play=rpc_cache_len=rpc_response_len=0;
    memset(save_status,0,sizeof(save_status));
    hmi_recent_next=0;memset(hmi_recent,0,sizeof(hmi_recent));
    memset(hmi_notes,255,sizeof(hmi_notes));
#endif
}
#if LIVE_MIDI_MODE
static void hmi_send(uint8_t command,uint8_t seq,const uint8_t *payload,uint8_t length) {
    uint8_t bytes[247];uint16_t crc;
    bytes[0]=0xa5;bytes[1]=0x5a;bytes[2]=length;bytes[3]=seq;bytes[4]=command;
    if(length)memcpy(bytes+5,payload,length);
    crc=protocol_crc(bytes+2,(uint16_t)length+3);
    bytes[length+5]=(uint8_t)crc;bytes[length+6]=(uint8_t)(crc>>8);
    platform_esp_write(bytes,(uint16_t)length+7);
}
static void hmi_put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint8_t hmi_action(const uint8_t *p) {
    uint8_t command=p[0],motor=p[1],args[5],out[64],len=0,err=E_OK;
    uint32_t value=(uint32_t)p[2]|(uint32_t)p[3]<<8|(uint32_t)p[4]<<16|(uint32_t)p[5]<<24;
    if(motor>=6)return 2;
    if((command==1||command==2)&&pc_active&&pc_song_len>=9) {
        if(!++pc_action_id)++pc_action_id;
        pc_action[0]=pc_action_id;memcpy(pc_action+1,p,6);return 0;
    }
    if(command==1||command==2)return 10; /* Songs are owned by the PC, not STM32. */
    if(command>10)return 2;
    if(pc_active && command!=0)return 6;
    if(command!=0 && !hmi_authority)return 8;
    if(command!=0 && midi_seen && platform_ms()-midi_received_at<=NS_TIMEOUT_MS)return 7;
    if(command==0) {engine_estop();hmi_manual=!pc_active;return 0;}
    args[0]=motor;args[1]=(uint8_t)value;
    switch(command) {
    case 3: hmi_put32(args+1,value);err=engine_command(C_FREQ,args,5,out,&len);break;
    case 4: if(value>1)return 2;err=engine_command(C_ENABLE,args,2,out,&len);break;
    case 5:
        if(value>1)return 2;
        if(value) {args[1]=1;err=engine_command(C_ENABLE,args,2,out,&len);}
        if(!err)err=engine_command(value?C_START:C_STOP,args,1,out,&len);
        break;
    case 6: if(value>1)return 2;err=engine_command(C_DIR,args,2,out,&len);break;
    case 7:
        if(value!=0&&value!=1&&value!=2&&value!=3&&value!=7)return 2;
        args[0]=(uint8_t)value;err=engine_command(C_MS,args,1,out,&len);break;
    case 8:
        if(value>1)return 2;
        args[0]=(uint8_t)value;err=engine_command(C_SLEEP,args,1,out,&len);break;
    case 9: args[0]=1;err=engine_command(C_RESET,args,1,out,&len);break;
    case 10:
        args[0]=0;err=engine_command(C_SLEEP,args,1,out,&len);
        if(!err)err=engine_command(C_RESET,args,1,out,&len);
        break;
    default:return 2;
    }
    if(!err)hmi_manual=1;
    if(err==E_STATE) {
        if(state.reset)return 3;
        if(state.sleep)return 4;
        if(!(state.mask&(1u<<motor)))return 5;
        if(state.running || (command==6 && state.motors[motor].active))return 9;
        if(command==7)for(unsigned i=0;i<6;i++)if(state.motors[i].active)return 9;
        return 11;
    }
    return err==E_OK?0:err==E_VALUE||err==E_LENGTH?2:1;
}
static void hmi_byte(uint8_t b) {
    uint32_t now=platform_ms();
    if(hmi_used && now-hmi_byte_at>100)hmi_used=0;
    hmi_byte_at=now;hmi_frame[hmi_used++]=b;
    if(hmi_used<13)return;
    if(hmi_frame[0]==0xa5&&hmi_frame[1]==0x5a&&hmi_frame[2]==6&&hmi_frame[4]==0x50&&
       protocol_crc(hmi_frame+2,9)==((uint16_t)hmi_frame[11]|(uint16_t)hmi_frame[12]<<8)) {
        const uint8_t *p=hmi_frame+5;uint8_t result=0,cached=0;
        hmi_link=1;hmi_last=now;
        if(p[0]==255) {
            hmi_authority=p[1]==0&&p[2]==1&&p[3]==0&&p[4]==0&&p[5]==0;
            if(!hmi_authority&&hmi_manual) {engine_estop();hmi_manual=0;}
        } else {
            for(unsigned i=0;i<16;i++)if(hmi_recent[i].valid&&hmi_recent[i].seq==hmi_frame[3]&&
                now-hmi_recent[i].at<2000&&!memcmp(hmi_recent[i].payload,p,6)) {
                result=hmi_recent[i].result;cached=1;break;
            }
            if(!cached) {
                unsigned i=hmi_recent_next++%16;
                result=hmi_action(p);memcpy(hmi_recent[i].payload,p,6);
                hmi_recent[i].seq=hmi_frame[3];hmi_recent[i].result=result;hmi_recent[i].at=now;hmi_recent[i].valid=1;
            }
            hmi_send(0x51,hmi_frame[3],&result,1);
        }
        hmi_used=0;
    } else {memmove(hmi_frame,hmi_frame+1,12);hmi_used=12;}
}
static void hmi_poll(uint8_t emit) {
    uint32_t now=platform_ms();uint8_t p[50]={1,1};
    if(emit && save_wait) {
        if(now-save_sent>=150 || !save_sent) {
            hmi_send(SONG_RELAY,save_seq,save_packet,save_length);save_sent=now;
        }
        return;
    }
    if(midi_seen&&now-midi_received_at<=NS_TIMEOUT_MS)hmi_manual=0;
    if(!hmi_link)return;
    if(now-hmi_last>NS_TIMEOUT_MS) {
        if(hmi_manual)engine_estop();
        hmi_manual=hmi_link=hmi_authority=0;return;
    }
    if(!emit || now-hmi_sent<50)return;
    hmi_sent=now;
    hmi_put32(p,now);hmi_send(0x43,0,p,4);memset(p,0,sizeof(p));p[0]=1;p[1]=1;
    p[2]=state.sleep;p[3]=state.reset;p[4]=state.raw;p[5]=state.mask;
    if(auto_play)hmi_put32(p+6,state.position);
    if(pc_active&&pc_song_len>=9) {
        memcpy(p+6,pc_song+1,8);
        if(pc_song[0]&1)p[1]|=2;
        if(pc_song[0]&2)p[1]|=4;
        if(now-pc_title_sent>=500){hmi_send(0x41,0,pc_song+9,pc_song_len-9);pc_title_sent=now;}
    }
    if(pc_active)p[1]|=16; /* Valid PC protocol traffic, not mere USB cable presence. */
    if(state.running && (auto_play || (pc_active&&pc_song_len>=9)))p[1]|=32; /* Fully known file: enable LCD presentation timing. */
    if(state.running)p[1]|=64; /* Per-motor bit 3 is an authoritative display hold. */
    for(unsigned m=0;m<6;m++) {
        Motor *v=&state.motors[m];uint8_t pitch=v->active?v->note:255;
        uint8_t event[7];hmi_put32(event,now);event[4]=(uint8_t)m;
        if(hmi_notes[m]!=pitch) {
            if(hmi_notes[m]<128) {event[5]=hmi_notes[m];event[6]=0;hmi_send(0x42,0,event,7);}
            if(pitch<128) {event[5]=pitch;event[6]=100;hmi_send(0x42,0,event,7);}
            hmi_notes[m]=pitch;
        }
        p[14+m*6]=v->enabled|(v->active<<1)|(v->dir<<2)|(engine_display_hold(m)<<3);
        p[15+m*6]=v->note;
        hmi_put32(p+16+m*6,v->frequency);
        if(v->active)p[1]|=2;
    }
    if(midi_seen&&now-midi_received_at<=NS_TIMEOUT_MS)p[1]|=8;
    hmi_send(0x40,0,p,50);
}
#endif
static void drop(void) {
    if (used) {
        used--;
        memmove(rx, rx + 1, used);
    }
}
static void parse(void) {
    while (used >= 2) {
        if (rx[0] != 0xa5 || rx[1] != 0x5a) {
            drop();
            continue;
        }
        if (used < 5)
            return;
        uint8_t len = rx[2];
        if (len > 240) {
            state.error = E_LENGTH;
            state.faults++;
            drop();
            continue;
        }
        uint16_t total = (uint16_t)len + 7;
        if (used < total)
            return;
        uint16_t crc = protocol_crc(rx + 2, (uint16_t)len + 3);
        if (rx[total - 2] != (uint8_t)crc || rx[total - 1] != (uint8_t)(crc >> 8)) {
            state.error = E_CRC;
            state.faults++;
            drop();
            continue;
        }
#if LIVE_MIDI_MODE
        if((rx[4]>=C_PING && rx[4]<=C_QUEUE) || (rx[4]>=SONG_BEGIN && rx[4]<=52)) {
            if(!pc_active) {
                engine_estop();
                auto_play=0;
                midi_seen=hmi_manual=0;
                previous_len=0;
            }
            pc_active=1;pc_last=platform_ms();
        }
#endif
        if (total == previous_len && platform_ms() - previous_at < DUPLICATE_WINDOW_MS &&
            !memcmp(rx, previous, total)) {
            platform_write(response, response_len);
        } else {
            uint8_t outlen = 0;
            uint8_t err;
#if LIVE_MIDI_MODE
            if(rx[4]==52) { /* PC song position/title and reliable LCD action mailbox. */
                err=(len<10||len>58)?E_LENGTH:E_OK;
                if(!err) {
                    if(rx[5]==pc_action[0])memset(pc_action,0,sizeof(pc_action));
                    pc_song_len=len-1;memcpy(pc_song,rx+6,pc_song_len);
                    outlen=7;memcpy(response+7,pc_action,7);
                }
            } else if(rx[4]==SONG_STATUS) {
                err=len?E_LENGTH:E_OK;outlen=8;memcpy(response+7,save_status,8);
            } else if(rx[4]>=SONG_BEGIN && rx[4]<=SONG_COMMIT) {
                err=E_OK;
                if(len>190)err=E_LENGTH;
                else if(save_wait)err=E_FULL;
                else {
                    engine_estop();auto_play=0;midi_seen=hmi_manual=0;
                    save_packet[0]=rx[4];memcpy(save_packet+1,rx+5,len);
                    save_length=len+1;save_seq=rx[3];save_wait=1;save_at=platform_ms();save_sent=0;
                    save_status[0]=rx[4];save_status[1]=255;
                    if(rx[4]==SONG_BEGIN){memset(save_status+2,0,6);save_status[2]=1;}
                }
            } else
#endif
            err = engine_command(rx[4], rx + 5, len, response + 7, &outlen);
            if (err) {
                state.error = err;
                state.faults++;
                outlen = 0;
            }
            response[0] = 0xa5;
            response[1] = 0x5a;
            response[2] = outlen + 2;
            response[3] = rx[3];
            response[4] = err ? 129 : 128;
            response[5] = rx[4];
            response[6] = err;
            crc = protocol_crc(response + 2, outlen + 5);
            response[outlen + 7] = (uint8_t)crc;
            response[outlen + 8] = (uint8_t)(crc >> 8);
            response_len = outlen + 9;
            memcpy(previous, rx, total);
            previous_len = total;
            previous_at = platform_ms();
            platform_write(response, response_len);
        }
        used -= total;
        memmove(rx, rx + total, used);
    }
}
void protocol_byte(uint8_t byte) {
    if (used && platform_ms() - last_byte > FRAME_TIMEOUT_MS) {
        used = 0;
        state.error = E_TIMEOUT;
        state.faults++;
    }
    last_byte = platform_ms();
    if (used == sizeof(rx))
        drop();
    rx[used++] = byte;
    parse();
}
void protocol_esp_byte(uint8_t byte) {
#if LIVE_MIDI_MODE
    uint32_t now=platform_ms();
    if(now-esp_wire_at>5)esp_wire.used=0;
    esp_wire_at=now;
    if(hd_feed(&esp_wire,byte) && esp_wire.bytes[2]==HD_REQUEST) {
        const uint8_t *b=esp_wire.bytes+5;unsigned n=esp_wire.bytes[4];
        uint8_t service=n>=7&&b[0]==0xa5&&b[1]==0x5a&&b[2]+7u==n&&
            protocol_crc(b+2,n-4)==((uint16_t)b[n-2]|(uint16_t)b[n-1]<<8);
        if(service && b[4]==SONG_REPLY && b[2]==8) {
            if(save_wait && b[3]==save_seq && b[5]==save_packet[0]) {
                memcpy(save_status,b+5,8);save_at=now;
                if(b[6]!=255)save_wait=0;
            }
        } else if(service && b[4]==SONG_ENGINE && b[2]>=1 && n<=208) {
            if(rpc_cache_len==n&&now-rpc_cached_at<2000&&!memcmp(rpc_cache,b,n))
                hmi_send(SONG_ENGINE_REPLY,b[3],rpc_response,rpc_response_len);
            else {
                uint8_t outlen=0,err;
                if(pc_active)err=12; /* Autonomous playback: PC owns the engine. */
                else {
                    if(!auto_play){engine_estop();auto_play=1;midi_seen=hmi_manual=0;}
                    err=engine_command(b[5],b+6,b[2]-1,rpc_response+2,&outlen);
                    if(b[5]==C_ESTOP)auto_play=0;
                }
                rpc_response[0]=b[5];rpc_response[1]=err;rpc_response_len=outlen+2;
                memcpy(rpc_cache,b,n);rpc_cache_len=n;rpc_cached_at=now;
                hmi_send(SONG_ENGINE_REPLY,b[3],rpc_response,rpc_response_len);
            }
        } else for(unsigned i=0;i<n;++i) {
            midi_byte(esp_wire.bytes[5+i]);hmi_byte(esp_wire.bytes[5+i]);
        }
        hmi_poll(1);
        platform_esp_reply(esp_wire.bytes[3]);
    }
#else
    (void)byte;
#endif
}
void protocol_poll(void) {
#if LIVE_MIDI_MODE
    if(save_wait && platform_ms()-save_at>10000) {save_wait=0;save_status[1]=E_TIMEOUT;save_status[2]=6;}
    if(pc_active && platform_ms()-pc_last>LINK_TIMEOUT_MS) {
        engine_estop();pc_active=0;pc_song_len=0;memset(pc_action,0,sizeof(pc_action));previous_len=0;
    }
#endif
    if (platform_uart_error()) {
        used = 0;
        engine_fault(E_UART);
    }
    if (platform_oc_error())
        engine_fault(E_OVERRUN);
    if (used && platform_ms() - last_byte > FRAME_TIMEOUT_MS) {
        used = 0;
        state.error = E_TIMEOUT;
        state.faults++;
    }
    int b;
    while ((b = platform_read()) >= 0)
        protocol_byte((uint8_t)b);
#if LIVE_MIDI_MODE
    if(platform_esp_uart_error()) {
        midi_used=hmi_used=0;esp_wire.used=0;
        /* A damaged single-wire frame is discarded and retried using its CRC/sequence.
         * Do not reset drivers for a recoverable framing/overrun error. The MIDI
         * heartbeat, engine command watchdog and queue underrun still stop on loss. */
        state.faults++;
    }
    while((b=platform_esp_read())>=0)protocol_esp_byte((uint8_t)b);
    hmi_poll(0);
    platform_esp_poll();
#endif
}
