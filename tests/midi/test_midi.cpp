#include "music.h"
#include "note_set_wire.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <chrono>
extern "C" {
#include "engine.h"
#include "protocol.h"
void mm_reset(void);
void mm_time(uint32_t);
void mm_feed(const uint8_t *,uint16_t);
void platform_midi_mark(void) {}
}
using namespace music;
static uint64_t at=0;
static NoteSet on(Engine &e,int note,int channel=0,int source=0) {at+=3000;return e.process({Type::On,uint8_t(channel),uint8_t(note),100,at,uint8_t(source)});}
static NoteSet off(Engine &e,int note,int channel=0,int source=0) {at+=1000;return e.process({Type::Off,uint8_t(channel),uint8_t(note),0,at,uint8_t(source)});}
static NoteSet cc(Engine &e,int number,int value,int channel=0,int source=0) {return e.process({Type::Control,uint8_t(channel),uint8_t(number),uint8_t(value),++at,uint8_t(source)});}
static bool has(NoteSet s,int note) {for(unsigned i=0;i<s.count;++i)if(s.notes[i]==note)return true;return false;}
static void expect(NoteSet s,std::initializer_list<int> notes) {assert(s.count==notes.size());for(int n:notes)assert(has(s,n));}
static void packet(std::initializer_list<int> notes,uint8_t seq=0) {
    uint8_t ns[6]={},frame[NS_SIZE];unsigned i=0;for(int n:notes)ns[i++]=uint8_t(n);
    ns_encode(frame,seq,ns,uint8_t(i));assert(ns_valid(frame));mm_feed(frame,NS_SIZE);
}
static int motor(int note) {for(int m=0;m<6;++m) if(state.motors[m].active && state.motors[m].note==note)return m;return -1;}
int main() {
    Engine e;
    expect(on(e,60),{60});expect(on(e,64),{60,64});expect(on(e,67),{60,64,67});
    for(int kind=0;kind<9;++kind) for(int root=0;root<12;++root) {
        uint16_t mask=0;for(unsigned i=0;i<chords[kind].count;++i) mask|=1u<<((root+chords[kind].intervals[i])%12);
        auto h=recognize(mask);assert(h.confidence==100);
        // sus2/sus4 and augmented roots are inherently ambiguous from pitch classes.
        if(kind!=3 && kind!=4 && kind!=5) assert(h.root==root && h.kind==kind);
    }
    e.reset();on(e,64);on(e,67);expect(on(e,72),{64,67,72});
    assert(recognize((1<<0)|(1<<4)|(1<<7)).root==0);
    for(unsigned voices=1;voices<=3;++voices) {
        Config c;c.voices=voices;Engine chord(c);on(chord,60);on(chord,64);auto s=on(chord,67);
        assert(has(s,60));assert(has(s,64)==(voices>=2));assert(has(s,67)==(voices>=3));
        Engine minor(c);on(minor,60);on(minor,63);s=on(minor,67);assert(has(s,60));assert(has(s,63)==(voices>=2));
    }
    e.reset();on(e,24);on(e,28);on(e,67);assert(e.group_of(24)==e.group_of(28));assert(e.group_of(67)!=e.group_of(24));
    e.reset();for(int n:{43,47,50,54})on(e,n);
    assert(on(e,24).count==5);assert(on(e,28).count==6);
    auto critical=on(e,67);assert(critical.count==6);assert(has(critical,24));assert(has(critical,67));assert(!has(critical,28));
    std::printf("Critical C1/E1/G4: DROP 28, KEEP 24 and 67; result:");for(unsigned i=0;i<critical.count;++i)std::printf(" %u",critical.notes[i]);std::puts("");
    e.reset();int previous=-1;
    for(int n:{67,69,71,72}) {if(previous>=0)off(e,previous);at+=60000;on(e,n);previous=n;}
    assert(e.continuity(72)>=2);
    for(int n:{60,64,67,71,74,76,79})on(e,n);
    assert(has(e.output(),72)); // Melody below a new top note.
    e.reset();previous=-1;
    for(int n:{36,38,40,41}) {if(previous>=0)off(e,previous);at+=60000;on(e,n);previous=n;}
    for(int n:{48,52,55,59,60,64,67})on(e,n);
    assert(e.continuity(41)>=2 && has(e.output(),41));
    Config three;three.voices=3;Engine doubling(three);
    for(int n:{36,48,52,55})on(doubling,n);expect(doubling.output(),{36,52,55});
    e.reset();on(e,60);cc(e,64,127);expect(off(e,60),{60});expect(cc(e,64,0),{});
    on(e,60,0,0);on(e,60,1,0);off(e,60,0,0);expect(e.output(),{60});off(e,60,1,0);expect(e.output(),{});
    on(e,60,0,0);on(e,60,0,1);off(e,60,0,0);expect(e.output(),{60});off(e,60,0,1);expect(e.output(),{});
    on(e,60);on(e,60);expect(off(e,60),{}); // Retrigger replaces, does not leak a count.
    on(e,60);expect(e.process({Type::On,0,60,0,++at}),{});
    on(e,60);cc(e,64,127);cc(e,123,0);expect(e.output(),{60});expect(cc(e,121,0),{});
    on(e,60);cc(e,64,127);expect(cc(e,120,0),{});
    e.reset();Engine repeat;
    auto begin=std::chrono::steady_clock::now();
    for(int i=0;i<10000;++i) {
        music::Event a{Type::On,0,uint8_t(48+i%24),100,uint64_t(i)*2000};
        assert(e.process(a)==repeat.process(a));assert(has(e.output(),a.note));
        a.type=Type::Off;++a.timestamp;assert(e.process(a)==repeat.process(a));assert(!e.output().count);
    }
    double micros=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count();
    std::printf("40,000 host event computations: %.1f ms (host throughput only).\n",micros/1000);
    music::Event event;uint8_t p[]={0x29,0x93,60,100};assert(decode_usb(p,123,event));assert(event.source==2&&event.channel==3&&event.timestamp==123);
    p[3]=0;assert(decode_usb(p,123,event)&&event.type==Type::Off);p[0]=8;assert(!decode_usb(p,123,event));
    uint8_t descriptor[]={9,2,32,0,1,1,0,0x80,50, 9,4,7,2,1,1,3,0,0, 7,0x24,1,0,1,7,0, 7,5,0x85,2,64,0,0};
    Endpoint ep;assert(find_endpoint(descriptor,sizeof(descriptor),ep));assert(ep.interface_number==7&&ep.alternate==2&&ep.address==0x85);
    descriptor[22]=2;assert(!find_endpoint(descriptor,sizeof(descriptor),ep));descriptor[22]=1;
    descriptor[9]=0;assert(!find_endpoint(descriptor,sizeof(descriptor),ep));
    mm_reset();packet({60,64,67});mm_time(2);assert(motor(60)>=0&&motor(64)>=0&&motor(67)>=0);
    int c=motor(60);packet({60,65,69},1);assert(motor(60)==c&&motor(64)<0&&motor(65)>=0&&motor(69)>=0);
    assert(note_mhz(69)==440000);assert(state.motors[motor(69)].frequency>=439000&&state.motors[motor(69)].frequency<=441000);
    packet({0,127});assert(motor(0)>=0&&motor(127)>=0);for(auto &m:state.motors)if(m.active)assert(m.frequency>=MIN_FREQ_MHZ&&m.frequency<=MAX_FREQ_MHZ);
    uint8_t frame[NS_SIZE],notes[]={72};ns_encode(frame,255,notes,1);
    frame[12]^=1;mm_feed(frame,NS_SIZE);assert(motor(72)<0);frame[12]^=1;
    mm_feed(frame,5);mm_feed(frame+6,NS_SIZE-6);assert(motor(72)<0);mm_feed(frame,NS_SIZE);assert(motor(72)>=0);
    // Every deleted-byte location, every bit flip, and every truncation recovers.
    for(unsigned missing=0;missing<NS_SIZE;++missing) {
        mm_reset();for(unsigned i=0;i<NS_SIZE;++i)if(i!=missing)mm_feed(frame+i,1);
        mm_feed(frame,NS_SIZE);mm_time(2);assert(motor(72)>=0);
    }
    for(unsigned byte=0;byte<NS_SIZE;++byte)for(unsigned bit=0;bit<8;++bit) {
        mm_reset();frame[byte]^=1u<<bit;mm_feed(frame,NS_SIZE);assert(motor(72)<0);frame[byte]^=1u<<bit;
        mm_feed(frame,NS_SIZE);mm_time(2);assert(motor(72)>=0);
    }
    for(unsigned cut=0;cut<NS_SIZE;++cut) {mm_reset();mm_feed(frame,cut);mm_feed(frame,NS_SIZE);mm_time(2);assert(motor(72)>=0);}
    mm_time(303);assert(motor(72)<0);packet({72},0);mm_time(305);assert(motor(72)>=0); // ESP reset seq=0.
    packet({72},0);mm_time(500);packet({72},0);mm_time(700);assert(motor(72)>=0); // Duplicate heartbeat keeps link alive.
    packet({});assert(motor(72)<0);packet({60,64,67,69,71,74});assert(motor(74)>=0);
    mm_reset();packet({60,64,67});mm_time(2);assert(motor(60)>=0); // STM reset full snapshot.
    packet({});assert(motor(60)<0);packet({69});assert(motor(69)>=0); // USB disconnect/reconnect state path.
    std::puts("PASS: music A-J, templates, deterministic replay, MIDI state, descriptor/CIN validation, allocator, CRC/resync, resets and timeout.");
}
