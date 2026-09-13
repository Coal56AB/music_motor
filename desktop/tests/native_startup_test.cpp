#include "startup_link.h"
#include <cassert>
#include <cstdio>

static void state_frame(StartupLink &check, uint32_t now, bool bad_crc=false, bool invalid=false) {
    uint8_t payload[50]{}, frame[57];
    payload[0] = invalid ? 9 : 1;
    control::encode(frame, 0x40, payload, sizeof(payload));
    if (bad_crc) frame[56] ^= 1;
    for (uint8_t b : frame) check.feed(b, now);
}
int main() {
    using S = StartupLink::State;
    StartupLink good(100);
    state_frame(good, 130);
    assert(good.state(130) == S::Ready);
    StartupLink version2(100);
    uint8_t state2[86]{}, wire2[93];state2[0]=2;
    control::encode(wire2,0x40,state2,sizeof(state2));
    for(uint8_t byte:wire2)version2.feed(byte,140);
    assert(version2.state(140)==S::Ready);
    StartupLink version3(100);
    uint8_t state3[94]{},wire3[101];state3[0]=3;
    control::encode(wire3,0x40,state3,sizeof(state3));
    for(uint8_t byte:wire3)version3.feed(byte,140);
    assert(version3.state(140)==S::Ready);
    StartupLink malformed3(100);state3[50]=8;
    control::encode(wire3,0x40,state3,sizeof(state3));
    for(uint8_t byte:wire3)malformed3.feed(byte,140);
    assert(malformed3.state(140)==S::Waiting);
    StartupLink malformed2(100);
    state2[50]=8; /* Display flags are independent and must be validated. */
    control::encode(wire2,0x40,state2,sizeof(state2));
    for(uint8_t byte:wire2)malformed2.feed(byte,140);
    assert(malformed2.state(140)==S::Waiting);
    StartupLink missing(100);
    state_frame(missing, 110, true);
    state_frame(missing, 150, false, true);
    assert(missing.state(1099) == S::Waiting);
    assert(missing.state(1100) == S::Failed);
    assert(missing.probe_due(1100));
    state_frame(missing, 1101);
    assert(missing.state(1101) == S::Ready);
    StartupLink ack_only(0);
    uint8_t frame[8], ack=0;
    control::encode(frame, 0x51, &ack, 1);
    for (uint8_t b : frame) ack_only.feed(b, 20);
    assert(ack_only.state(20) == S::Waiting);
    StartupLink wrap(0xfffffff0u);
    assert(wrap.probe_due(0xfffffff0u));
    wrap.probe_sent(0xfffffff0u);
    assert(!wrap.probe_due(3));
    assert(wrap.probe_due(4));
    state_frame(wrap, 30);
    assert(wrap.state(30) == S::Ready);
    assert(!wrap.probe_due(100));
    StartupLink retry(0);
    unsigned attempts=0;
    for(uint32_t now=0;now<=1000;++now) {
        if(retry.probe_due(now)) {
            assert(now==attempts*20);
            retry.probe_sent(now);++attempts;
        }
    }
    assert(attempts==51&&retry.state(1000)==S::Failed);
    state_frame(retry,20000,true);
    assert(retry.state(20000)==S::Failed&&retry.probe_due(20000));
    retry.probe_sent(20000);
    state_frame(retry,20027);
    assert(retry.state(20027)==S::Ready&&!retry.probe_due(20040));
    StartupLink recovery(0);
    recovery.probe_sent(0);state_frame(recovery,5,true);
    assert(recovery.probe_due(20));recovery.probe_sent(20);
    state_frame(recovery,27);
    assert(recovery.state(27)==S::Ready&&!recovery.probe_due(40));
    puts("Startup link checks passed");
}
