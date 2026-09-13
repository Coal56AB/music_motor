#include "controller_link.h"
#include <cassert>
#include <cstring>
#include <cstdio>
static uint8_t sent[208];
static unsigned sent_size,sent_count;
static control::Source destination;
static void ui(const uint8_t*,unsigned) {}
static void changed(control::Source) {}
static void send(control::Source source,const uint8_t *p,unsigned n) {
    destination=source;sent_size=n;++sent_count;memcpy(sent,p,n);
}
static void feed(control::Link &link,control::Source source,const uint8_t *p,unsigned n) {
    for(unsigned i=0;i<n;++i)link.feed(source,p[i],100);
}
int main() {
    control::Link link(ui,changed,send);
    uint8_t payload[201]{1},frame[208];
    payload[6]=0x80;payload[7]=60;payload[8]=100;
    unsigned n=control::encode(frame,0x56,payload,201,42);
    feed(link,control::Usb,frame,n);
    assert(sent_count==1&&destination==control::Uart&&sent_size==208&&!memcmp(frame,sent,n));
    frame[n-1]^=1;feed(link,control::Usb,frame,n);assert(sent_count==1);
    n=control::encode(frame,0x56,payload,2,43);feed(link,control::Usb,frame,n);assert(sent_count==1);
    n=control::encode(frame,0x57,payload+1,1,42);feed(link,control::Uart,frame,n);
    assert(sent_count==2&&destination==control::Usb&&sent[3]==42&&sent[5]==0);
    uint8_t state[50]{1,1};n=control::encode(frame,0x40,state,50);feed(link,control::Uart,frame,n);
    assert(link.active()==control::Uart);
    n=control::encode(frame,0x56,payload,11,44);feed(link,control::Usb,frame,n);
    assert(sent_count==3&&destination==control::Uart&&link.active()==control::Uart);
    puts("MIDI routing, ACK, CRC, full batch and UART UI priority passed");
}
