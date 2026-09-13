#include "usb_role.h"
#include "controller_link.h"
#include <cassert>
#include <cstdio>
static void ignore_frame(const uint8_t*,unsigned) {}
static void ignore_source(control::Source) {}
static void ignore_send(control::Source,const uint8_t*,unsigned) {}
int main() {
    UsbRole role;
    role.begin(true,0);
    assert(role.tick(1,true,true)==UsbRole::None);
    assert(role.tick(501,false,true)==UsbRole::None); // Ignore stale initial SOF.
    assert(role.tick(1500,false,true)==UsbRole::StartHost);
    uint32_t now=0xfffffff0u;
    // Repeated hot swaps, including tick wrap, slow teardown and brief dropouts.
    for(unsigned cycle=0;cycle<1000;++cycle) {
        role.begin(true,now);
        assert(role.tick(now+1499,false,true)==UsbRole::None);
        now+=1500;assert(role.tick(now,false,true)==UsbRole::StartHost);
        now+=2500;assert(role.tick(now,true,false)==UsbRole::None);
        now+=100000;assert(role.tick(now,true,false)==UsbRole::None);
        assert(role.tick(now+499,false,false)==UsbRole::None);
        now+=500;assert(role.tick(now,false,false)==UsbRole::StopHost);
        now+=10000;assert(role.tick(now,false,false)==UsbRole::None);
        assert(role.current()==UsbRole::Stopping);
        assert(role.tick(now,false,true)==UsbRole::None);
        assert(role.tick(now+599,false,true)==UsbRole::None);
        now+=600;assert(role.tick(now,false,true)==UsbRole::StartSerial);
        now+=100;assert(role.tick(now,true,true)==UsbRole::None);
        now+=100000;assert(role.tick(now,true,true)==UsbRole::None);
        assert(role.tick(now+499,false,true)==UsbRole::None);
        now+=499;assert(role.tick(now,true,true)==UsbRole::None);
    }
    role.begin(false,0);
    assert(role.tick(2999,false,false)==UsbRole::None);
    assert(role.tick(3000,false,false)==UsbRole::StopHost);
    // An old partial PC packet cannot complete in the new USB session.
    control::Link link(ignore_frame,ignore_source,ignore_send);
    uint8_t data[50]{},frame[57];data[0]=1;data[1]=9;
    control::encode(frame,0x40,data,sizeof(data));
    for(unsigned i=0;i<20;++i)link.feed(control::Usb,frame[i],1);
    link.disconnect(control::Usb,2);
    for(unsigned i=20;i<sizeof(frame);++i)link.feed(control::Usb,frame[i],3);
    assert(!link.source_flags(control::Usb,3));
    for(uint8_t b:frame)link.feed(control::Usb,b,4);
    assert(link.source_flags(control::Usb,4)==9);
    link.disconnect(control::Usb,5);
    assert(!link.source_flags(control::Usb,5)&&link.active()==control::None);
    puts("USB role: 1000 hot swaps, disconnect debounce, teardown gate, clock wrap and session cleanup passed");
}
