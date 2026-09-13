#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>
#include <vector>
#include "../../esp32-diplsay-midi/DisplaySrc/esp32/ui_bridge.h"
#include "../../esp32-diplsay-midi/DisplaySrc/src/music_box_control.h"
static std::atomic<bool> hold(false),entered(false),release_ui(false);
static std::vector<unsigned> rendered;
static unsigned ack=0,source=0,playing=0,loading=0,songs=0,stage=0;
static char error_text[128];
static char device_name[49];
extern "C" {
void music_box_control_frame(const uint8_t *p,unsigned n) {
    assert(n>=7);
    if(p[4]==0x46){memcpy(device_name,p+5,p[2]);device_name[p[2]]=0;return;}
    if(hold.exchange(false)){entered=true;while(!release_ui.load())std::this_thread::yield();}
    if(p[4]==0x51)ack=p[3];else rendered.push_back(p[5]);
}
void music_box_control_source(unsigned v){source=v;}
void music_box_control_connections(unsigned){}
void music_box_midi_input(unsigned){}
void music_box_saved_song(unsigned,const char *,unsigned,uint32_t){++songs;}
void music_box_save_progress(unsigned s,unsigned){stage=s;}
void music_box_playback_error(const char *s){strcpy(error_text,s);}
void music_box_saved_offset(uint32_t){}
void music_box_saved_range(uint8_t,uint8_t){}
void music_box_saved_playing(unsigned v){playing=v;}
void music_box_saved_loading(unsigned v){loading=v;}
}
static void post(unsigned v) {uint8_t p[8]={0xa5,0x5a,1,0,0x42,(uint8_t)v,0,0};ui_bridge::music_box_control_frame(p,8);}
int main() {
    ui_bridge::set_screen_ready(true);assert(ui_bridge::music_box_screen_ready());
    ui_bridge::music_box_control_source(1);post(1);
    ui_bridge::music_box_midi_name("Test keyboard");
    ui_bridge::music_box_control_source(2);post(2);ui_bridge::drain();
    assert(source==2&&rendered==std::vector<unsigned>{2});rendered.clear();
    assert(!strcmp(device_name,"Test keyboard"));
    for(unsigned i=0;i<10;++i)ui_bridge::music_box_saved_song(i,"song",1,1000);
    ui_bridge::music_box_saved_playing(1);ui_bridge::music_box_saved_loading(1);
    ui_bridge::music_box_saved_playing(0);ui_bridge::drain();assert(songs==10&&!playing&&!loading);
    // A deliberately stalled UI callback must not retain the mailbox lock.
    hold=true;post(3);std::thread ui([]{ui_bridge::drain();});
    while(!entered.load())std::this_thread::yield();
    auto producer=std::async(std::launch::async,[]{
        for(unsigned i=0;i<10000;++i)post(i&255);
        uint8_t p[8]={0xa5,0x5a,1,77,0x51,0,0,0};
        ui_bridge::music_box_control_frame(p,8);
        ui_bridge::music_box_save_progress(7,0);ui_bridge::music_box_playback_error("E10");
    });
    assert(producer.wait_for(std::chrono::seconds(2))==std::future_status::ready);
    producer.get();release_ui=true;ui.join();ui_bridge::drain();
    assert(ack==77&&stage==7&&!strcmp(error_text,"E10"));
    assert(source==2&&rendered.size()<=129);
    puts("UI bridge: stalled rendering does not block producer; bounded overflow, ACK, source and stop passed");
}
