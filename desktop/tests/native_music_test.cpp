#include "music.h"
#include "live_controller.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace music;
static bool has(const NoteSet &s, int n) {
    for(unsigned i=0;i<s.count;++i) if(s.notes[i]==n)return true;
    return false;
}
static Event on(int n,uint64_t t=0,int ch=0,int src=0) {return {Type::On,uint8_t(ch),uint8_t(n),100,t,uint8_t(src)};}
static Event off(int n,uint64_t t=0,int ch=0,int src=0) {return {Type::Off,uint8_t(ch),uint8_t(n),0,t,uint8_t(src)};}
static Event cc(int n,int v,uint64_t t=0,int ch=0,int src=0) {return {Type::Control,uint8_t(ch),uint8_t(n),uint8_t(v),t,uint8_t(src)};}
int main(int argc,char **argv) {
    if(argc>1 && std::string(argv[1])=="stream") {
        Engine engine; int type,ch,n,v,source,last; uint64_t t;
        while(std::cin>>type>>ch>>n>>v>>t>>source>>last) {
            auto s=engine.process({Type(type),uint8_t(ch),uint8_t(n),uint8_t(v),t,uint8_t(source),bool(last)});
            if(last) {for(unsigned i=0;i<s.count;++i)std::cout<<int(s.notes[i])<<' ';std::cout<<'\n';}
        }
        return 0;
    }
    Engine e;
    e.process(on(60)); e.process(cc(64,127));
    assert(has(e.process(off(60,100)),60));
    e.process(on(60,200));
    assert(has(e.process(cc(64,0,300)),60));
    assert(e.process(off(60,400)).count==0);
    e.process(on(60)); e.process(on(60,1));
    assert(has(e.process(off(60,2)),60));
    assert(e.process(off(60,3)).count==0);
    e.process(cc(64,127)); e.process(on(60));
    assert(has(e.process(cc(123,0,100)),60));
    assert(e.process(cc(121,0,200)).count==0);
    e.process(on(60)); e.process(cc(64,127));
    assert(e.process(cc(120,0)).count==0);
    e.process(on(60)); e.process(on(60,0,1));
    e.process(cc(120,0)); assert(has(e.output(),60));
    e.process(cc(120,0,0,1)); assert(e.output().count==0);
    e.reset(); e.process(on(60,0,0,1)); e.process(off(60)); assert(has(e.output(),60));
    auto zero=on(60,1,0,1);zero.velocity=0;assert(e.process(zero).count==0);
    e.reset(); auto first=on(60);first.batch_end=false;
    assert(e.process(first).count==0); assert(e.process(on(64)).count==2);
    e.reset();e.process(cc(64,127));
    for(unsigned i=0;i<1000;++i) {e.process(on(60,i*100));e.process(off(60,i*100+50));}
    assert(e.overflow_count==0);assert(e.process(cc(64,0,100001)).count==0);
    Config cfg;cfg.voices=1;Engine mono(cfg);
    mono.process(on(60));mono.process(cc(64,127));mono.process(off(60,100));
    assert(has(mono.process(on(84,4000000)),84)); // Old pedal root loses to fresh independent note.
    mono.reset();mono.process(on(84));assert(has(mono.process(on(60,4000000)),84)); // Held key never ages out.
    e.reset();
    const int chord[]={24,28,31,36,40,43};
    for(int n:chord)e.process(on(n));
    assert(has(e.process(on(79,100000)),79));assert(e.output().count==6);
    live::Controller controller;controller.connection(true);controller.event(on(60));
    bool reset=false;controller.action(1,0,0,0,0,&reset);
    assert(reset && controller.output().count==0);
    controller.action(2,1,0,0,1,&reset);assert(controller.output().count==0);
    controller.event(on(64));assert(has(controller.output(),64));
    controller.connection(false);assert(controller.output().count==0);
    std::cout<<"Native MIDI state, chronology, batch, and STOP checks passed\n";
}
