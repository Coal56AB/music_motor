#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>
#include <string>
#include <cstdlib>
#include "../../firmware/Core/Inc/esp_tx_queue.h"
#include "../../esp32-diplsay-midi/DisplaySrc/esp32/song_store.cpp"
extern "C" {
void saved_stm_init(void (*)(const uint8_t *,unsigned),void (*)(uint8_t));
void saved_stm_tick(uint32_t);
void saved_stm_request(const uint8_t *,unsigned,uint8_t);
unsigned saved_stm_error(void),saved_stm_running(void),saved_stm_position(void),saved_stm_used(void);
}
namespace test {
struct Queue {unsigned capacity,size;std::deque<std::vector<uint8_t>> items;};
std::vector<uint8_t> flash((SONG_SLOT_COUNT+1)*SONG_SLOT_BYTES,255);
esp_partition_t storage{flash.size()};
std::deque<std::vector<uint8_t>> tx;
EspTxQueue stm_tx{};
std::vector<uint8_t> request,response;
uint32_t time=0,rng=1,next_io=0,turns=0,lost=0,overflows=0;
unsigned phase=0,slow=1,loss=0,max_used=0,min_used=512;
std::string failure;
bool ever_running=false,ui_playing=false;
uint32_t random(){rng=rng*1664525u+1013904223u;return rng;}
void frame(const uint8_t *p,unsigned n) {
    if(!esp_tx_push(&stm_tx,p,n))++overflows;
}
void reply(uint8_t) {
    response.clear();
    uint8_t bytes[HD_MAX_PAYLOAD];unsigned n=esp_tx_pop(&stm_tx,bytes,sizeof(bytes));
    response.assign(bytes,bytes+n);
}
void advance_wire() {
    if(time<next_io)return;
    if(phase==1) {
        saved_stm_request(request.data(),request.size(),uint8_t(turns));
        next_io=time+slow+unsigned((response.size()+7)*10*1000/HD_BAUD)+1;
        phase=2;return;
    }
    if(phase==2) {
        if(random()%100<loss){++lost;next_io=time+HD_MASTER_TIMEOUT_MS;}
        else {
            for(uint8_t byte:response)song_store_feed(byte);
            if(tx.empty())next_io=time+5;
        }
        // UI telemetry is deliberately not drained here: real loader doesn't need it.
        phase=0;return;
    }
    if(!tx.empty()){request=tx.front();tx.pop_front();}
    else request.clear();
    ++turns;phase=1;next_io=time+unsigned((request.size()+7)*10*1000/HD_BAUD)+1;
}
}
QueueHandle_t xQueueCreate(unsigned cap,unsigned size){return new test::Queue{cap,size,{}};}
int xQueueSend(QueueHandle_t q,const void *p,unsigned){auto &v=*static_cast<test::Queue*>(q);if(v.items.size()==v.capacity)return 0;v.items.emplace_back((const uint8_t*)p,(const uint8_t*)p+v.size);return 1;}
int xQueueReceive(QueueHandle_t q,void *p,unsigned){auto &v=*static_cast<test::Queue*>(q);if(v.items.empty())return 0;memcpy(p,v.items.front().data(),v.size);v.items.pop_front();return 1;}
const esp_partition_t *esp_partition_find_first(unsigned,unsigned,const char*){return &test::storage;}
int esp_partition_read(const esp_partition_t *,size_t at,void *p,size_t n){if(at+n>test::flash.size())return 1;memcpy(p,test::flash.data()+at,n);return 0;}
int esp_partition_write(const esp_partition_t *,size_t at,const void *p,size_t n){if(at+n>test::flash.size())return 1;memcpy(test::flash.data()+at,p,n);return 0;}
int esp_partition_erase_range(const esp_partition_t *,size_t at,size_t n){if(at+n>test::flash.size())return 1;memset(test::flash.data()+at,255,n);return 0;}
int esp_link_send(const uint8_t *p,unsigned n){if(test::tx.size()>=8)return 0;test::tx.emplace_back(p,p+n);return n;}
namespace ui_bridge {
void music_box_saved_song(unsigned,const char *,unsigned,uint32_t){}
void music_box_save_progress(unsigned,unsigned){}
unsigned music_box_screen_ready(){return 1;}
void music_box_playback_error(const char *s){test::failure=s;}
void music_box_saved_offset(uint32_t){}
void music_box_saved_range(uint8_t,uint8_t){}
void music_box_saved_playing(unsigned p){test::ui_playing=p;}
void music_box_saved_loading(unsigned){}
}
int main(int argc,char **argv) {
    if(argc>1)test::loss=unsigned(atoi(argv[1]));
    if(argc>2)test::slow=unsigned(atoi(argv[2]));
    if(argc>3)test::rng=unsigned(atoi(argv[3]));
    unsigned at,addr,op,value,count=0,last=0;
    while(scanf("%u %u %u %u",&at,&addr,&op,&value)==4) {
        assert(count<SONG_MAX_EVENTS);uint8_t *p=test::flash.data()+SONG_DATA_OFFSET+10*count++;
        song_put32(p,at);p[4]=uint8_t(addr);p[5]=uint8_t(op);song_put32(p+6,value);last=at;
    }
    Header h{};h.magic=magic;h.generation=1;h.slot=0;h.count=count;h.duration=last;h.mask=63;h.reserved=1;
    strcpy(h.title,"Stress song");h.check=checksum(&h,60);memcpy(test::flash.data(),&h,sizeof(h));
    saved_stm_init(test::frame,test::reply);song_store_start();song_store_action(20,0);
    for(test::time=10;test::time<last+30000;++test::time) {
        saved_stm_tick(test::time);
        if(test::time%100==0){uint8_t b[13],p[6]={255,0,1,0,0,0};control::encode(b,0x50,p,6);esp_link_send(b,13);}
        test::advance_wire();song_store_tick(test::time);
        if(saved_stm_running()) {
            test::ever_running=true;test::max_used=std::max(test::max_used,saved_stm_used());test::min_used=std::min(test::min_used,saved_stm_used());
        }
        if(!test::failure.empty()||saved_stm_error())break;
        if(test::ever_running&&!playing&&!stopping&&!saved_stm_running())break;
    }
    bool ok=test::ever_running&&!playing&&!saved_stm_running()&&test::failure.empty()&&!saved_stm_error();
    printf("%s position=%u error=%u loss=%u%% delay=%u turns=%u dropped=%u tx_overflow=%u queue=%u..%u %s\n",
        ok?"PASS":"FAIL",saved_stm_position(),saved_stm_error(),test::loss,test::slow,test::turns,test::lost,test::overflows,test::min_used,test::max_used,test::failure.c_str());
    return ok?0:1;
}
