#define main manual_tests
#include "native_manual_test.c"
#undef main
#include "../../firmware/Core/Src/engine.c"
#include <stdlib.h>

static void setup(void) {
    clock_ms=0;engine_init();protocol_init();
    uint8_t out[128],n,p[2]={0};
    assert(!engine_command(C_SLEEP,p,1,out,&n));assert(!engine_command(C_RESET,p,1,out,&n));
    for(unsigned m=0;m<6;++m){p[0]=(uint8_t)m;p[1]=1;assert(!engine_command(C_ENABLE,p,2,out,&n));}
    clock_ms=10;
}
static uint8_t raw_event(uint32_t at,uint8_t addr,uint8_t op,uint32_t value) {
    uint8_t p[10],out[8],n;put32(p,at);p[4]=addr;p[5]=op;put32(p+6,value);
    return engine_command(C_RAW_EVENTS,p,10,out,&n);
}
static void run_at(uint32_t at) {clock_ms=origin+at;last_command=clock_ms;engine_tick();}
static void begin(void) {uint8_t out[8],n;assert(!engine_command(C_STREAM_START,0,0,out,&n));}
static void live_packet(uint8_t sequence,uint8_t op) {
    uint8_t p[18]={0xa5,0x5a,11,0,0x56,1},frame[32];p[3]=sequence;
    p[11]=op;p[12]=60;p[13]=op==0x80?100:0;
    uint16_t crc=protocol_crc(p+2,14);p[16]=(uint8_t)crc;p[17]=(uint8_t)(crc>>8);
    unsigned length=hd_encode(frame,HD_REQUEST,sequence,p,sizeof(p));
    for(unsigned i=0;i<length;++i)protocol_esp_byte(frame[i]);
}
static void print_state(uint32_t at) {
    printf("%u",at);
    for(unsigned m=0;m<6;++m)printf(" %u",state.motors[m].active?state.motors[m].note:255);
    printf("\n");
}
int main(int argc,char **argv) {
    if(argc>1&&!strcmp(argv[1],"live-wire")) {
        setup();unsigned length,value;
        while(scanf("%u",&length)==1) {
            assert(length<=208);uint8_t packet[208],wire[HD_MAX_FRAME];
            for(unsigned i=0;i<length;++i){assert(scanf("%u",&value)==1&&value<=255);packet[i]=(uint8_t)value;}
            unsigned n=hd_encode(wire,HD_REQUEST,packet[3],packet,length);
            for(unsigned i=0;i<n;++i)protocol_esp_byte(wire[i]);
            assert(!last_midi_ack);clock_ms+=3;engine_tick();
            unsigned count=0;for(unsigned m=0;m<6;++m)count+=!!state.motors[m].active;
            printf("%u",count);
            for(unsigned pitch=0;pitch<128;++pitch)for(unsigned m=0;m<6;++m)
                if(state.motors[m].active&&state.motors[m].note==pitch)printf(" %u",pitch);
            printf("\n");
        }
        return 0;
    }
    if(argc>1&&!strcmp(argv[1],"paced")) {
        /* Replay four-event ESP refills at a bounded transport/UI cadence. */
        uint8_t *events=malloc(200000);assert(events);unsigned total=0,at,addr,op,value;
        while(scanf("%u %u %u %u",&at,&addr,&op,&value)==4) {
            assert(total<20000);uint8_t *e=events+10*total++;
            put32(e,at);e[4]=(uint8_t)addr;e[5]=(uint8_t)op;put32(e+6,value);
        }
        setup();unsigned cadence=argc>2?atoi(argv[2]):10,index=0,credits=NOTES_BUFFER_DEPTH/13-1;
        uint8_t out[8],n,micro=argc>3?(uint8_t)atoi(argv[3]):0;
        assert(!engine_command(C_MS,&micro,1,out,&n));
        while(index<total&&credits) {
            unsigned count=total-index;if(count>4)count=4;if(count>credits)count=credits;
            assert(!engine_command(C_RAW_EVENTS,events+10*index,count*10,out,&n));
            index+=count;credits=out[0];
            if(!raw_open&&last_at>=2000)break;
        }
        begin();unsigned next_send=0,poll=0;
        for(unsigned t=0;t<90000&&state.running;++t) {
            run_at(t);
            if(!state.running)break;
            if(argc>4&&t>=8000&&t<8000+(unsigned)atoi(argv[4]))continue;
            if(t>=next_send) {
                if(index<total&&credits>0) {
                    unsigned count=total-index;if(count>4)count=4;if(count>credits)count=credits;
                    unsigned error=engine_command(C_RAW_EVENTS,events+10*index,count*10,out,&n);
                    if(error){fprintf(stderr,"refill error=%u at=%u index=%u\n",error,t,index);free(events);return 1;}
                    index+=count;credits=out[0];next_send=t+cadence;
                } else if(t>=poll) {
                    unsigned free=(NOTES_BUFFER_DEPTH-state.used)/RAW_EXPANSION;
                    credits=free?free-1:0;poll=t+10;next_send=t+cadence;
                }
            }
        }
        fprintf(stderr,"paced error=%u position=%u events=%u/%u micro=%u\n",state.error,state.position,index,total,state.raw);
        assert(state.raw==micro);
        free(events);return state.error||state.running?1:0;
    }
    if(argc>1&&!strcmp(argv[1],"stream")) {
        unsigned type,ch,n,v,src,last;unsigned long long t;music_reset();
        while(scanf("%u %u %u %u %llu %u %u",&type,&ch,&n,&v,&t,&src,&last)==7){
            assert(!music_event((uint32_t)(t/1000),(uint8_t)((src<<4)|ch),(uint8_t)(type==3?4:type),(uint8_t)n,(uint8_t)v,0));
            if(last){music_choose((uint32_t)(t/1000));for(unsigned i=0;i<music.count;++i)printf("%u ",music.selected[i]);puts("");}
        }
        return 0;
    }
    if(argc>1&&!strcmp(argv[1],"device")) {
        setup();uint8_t p[4],out[8],n;uint32_t seek=argc>2?(uint32_t)strtoul(argv[2],0,10):0;
        put32(p,seek);assert(!engine_command(C_RAW_SEEK,p,4,out,&n));
        unsigned at,addr,op,value;int started=0;
        while(scanf("%u %u %u %u",&at,&addr,&op,&value)==4){
            uint8_t result;
            while((result=raw_event(at,(uint8_t)addr,(uint8_t)op,value))==E_FULL){
                if(!started){begin();started=1;}
                assert(state.used&&state.running);uint32_t next=queue[head].at;run_at(next);print_state(next);
            }
            assert(!result);
        }
        if(!started)begin();
        while(state.running){assert(state.used);uint32_t next=queue[head].at;run_at(next);print_state(next);}
        assert(!state.error);return 0;
    }
    manual_tests();
    setup();screen_command(255,1,0);live_packet(1,0x80);
    clock_ms+=3;engine_tick();assert(physical_step[0]);
    uint8_t dir_action[6]={6,0,1,0,0,0};
    assert(!hmi_action(dir_action)&&!physical_step[0]&&!hmi_manual);
    clock_ms+=STARTUP_DELAY_MS;engine_tick();assert(physical_step[0]);
    dir_action[2]=0;assert(!hmi_action(dir_action)&&!physical_step[0]);
    live_packet(2,0x81);clock_ms+=STARTUP_DELAY_MS;engine_tick();
    assert(!physical_step[0]&&!state.motors[0].active);
    /* Buffered playback keeps its queue and other voices through a turn. */
    setup();
    raw_push(0,0,1,note_mhz(60),60);raw_push(0,1,1,note_mhz(64),64);
    raw_push(100,0,0,0,255);raw_push(200,7,2,0,255);
    begin();run_at(0);assert(physical_step[0]&&physical_step[1]);
    uint8_t dir_args[2]={0,1},dir_out[8],dir_len;
    uint16_t queued=state.used;
    assert(!engine_command(C_DIR,dir_args,2,dir_out,&dir_len));
    assert(state.used==queued&&state.running&&!physical_step[0]&&physical_step[1]);
    run_at(STARTUP_DELAY_MS);assert(physical_step[0]&&physical_step[1]);
    /* Empty live MIDI heartbeats allow MS changes without taking ownership. */
    setup();
    screen_command(255,1,0);
    live_packet(1,0x81);
    uint8_t ms_action[6]={7,0,3,0,0,0};
    assert(midi_seen&&midi_link&&!midi_count&&!hmi_manual);
    assert(!hmi_action(ms_action)&&state.raw==3&&!hmi_manual&&live_enabled);
    clock_ms+=3;engine_tick();
    live_packet(2,0x80);
    ms_action[2]=7;
    assert(hmi_action(ms_action)!=0&&state.raw==3);
    live_packet(3,0x81);
    assert(!hmi_action(ms_action)&&state.raw==7&&midi_link&&!hmi_manual);
    live_packet(4,0x80);
    assert(state.motors[0].active); /* The next note still starts normally. */
    setup();
    screen_command(255,1,0);
    ready_at=clock_ms+10;
    live_packet(1,0x80);
    assert(!state.motors[0].active&&midi_pending);
    assert(hmi_action(ms_action)!=0&&state.raw==0);
    setup();
    screen_command(255,1,0);
    live_packet(1,0x81);
    music.keys[0].flags=1; /* A sustained/unselected note also blocks MS. */
    assert(hmi_action(ms_action)!=0&&state.raw==0);
    setup();assert(sizeof(queue)==4096);
    raw_push(86399999,5,1,1200000,127);
    assert(queue[head].at==86399999&&queue[head].value==1200000);
    assert(queue[head].motor==5&&queue[head].note==127&&queue[head].op==1);
    setup();
    for(unsigned i=0;i<NOTES_BUFFER_DEPTH;++i) {
        uint8_t e[10]={0},out[8],n;put32(e,i);e[4]=i+1==NOTES_BUFFER_DEPTH?255:0;
        e[5]=i+1==NOTES_BUFFER_DEPTH?2:0;
        assert(!engine_command(C_EVENTS,e,10,out,&n));
        assert((unsigned)(out[0]|out[1]<<8)==NOTES_BUFFER_DEPTH-i-1);
    }
    assert(state.used==512);begin();run_at(511);assert(!state.running&&!state.error);
    /* A refill after stop/fault must retain the actual failure reason. */
    for(unsigned error=E_TIMEOUT;error<=E_OVERRUN;++error) {
        setup();assert(!raw_event(0,63,0x85,6));
        assert(!raw_event(0,0,0x80,60|(100u<<8)));
        assert(!raw_event(1000,0,0x83,0));begin();run_at(0);
        engine_fault((uint8_t)error);
        assert(raw_event(100,0,0x81,60)==error);
        for(unsigned m=0;m<6;++m)assert(!state.motors[m].enabled&&!state.motors[m].active);
        uint8_t out[8],n;assert(!engine_command(C_ESTOP,0,0,out,&n));
        assert(!raw_event(0,63,0x85,6));
    }
    /* Errors on an idle PC UART must not stop an ESP-owned song. */
    setup();auto_play=1;
    assert(!raw_event(0,63,0x85,6));assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(1000,0,0x83,0));begin();run_at(0);
    injected_uart_error=1;protocol_poll();assert(state.running&&!state.error&&state.faults==1);
    pc_active=1;pc_last=clock_ms;injected_uart_error=1;protocol_poll();
    assert(!state.running&&state.error==E_UART);
    /* Logical parser capacity only: this does not test the hardware reply deadline.
       ESP uses four-event batches and can refill after an early start. */
    setup();uint8_t batch[160]={0},batch_out[8],batch_len;
    batch[4]=1;batch[5]=0x85;batch[6]=1;
    for(unsigned i=1;i<16;++i) {
        uint8_t *e=batch+10*i;put32(e,(i-1)*40);
        e[5]=(i&1)?0x80:0x81;e[6]=60;e[7]=(i&1)?100:0;
    }
    assert(!engine_command(C_RAW_EVENTS,batch,sizeof(batch),batch_out,&batch_len));
    begin();run_at(0);assert(state.motors[0].active);
    run_at(400);assert(state.running&&!state.error);
    assert(!raw_event(600,0,0x81,60));assert(!raw_event(700,0,0x83,0));
    run_at(700);assert(!state.running&&!state.error&&!state.motors[0].enabled);
    /* Init adds a voice every 400 ms, then holds all six for 2000 ms. */
    setup();uint8_t boot_out[8],boot_len;
    const uint8_t boot_notes[6]={36,55,64,69,74,79};
    assert(!engine_command(C_BOOT_TEST,0,0,boot_out,&boot_len));
    uint32_t first_boot=boot_at;
    for(unsigned i=0;i<6;++i){
        clock_ms=first_boot+i*400;engine_tick();
        for(unsigned m=0;m<6;++m){
            assert(state.motors[m].enabled==(m<=i));
            assert(state.motors[m].active==(m<=i));
            if(m<=i)assert(state.motors[m].note==boot_notes[m]&&state.motors[m].frequency<=1200000);
        }
        clock_ms=first_boot+i*400+399;engine_tick();
        if(i<5)assert(!state.motors[i+1].enabled);
    }
    clock_ms=first_boot+3999;engine_tick();
    for(unsigned m=0;m<6;++m)assert(state.motors[m].active&&state.motors[m].enabled);
    uint32_t previous[6];
    for(unsigned m=0;m<6;++m)previous[m]=state.motors[m].frequency;
    for(unsigned dt=0;dt<400;dt+=10){
        clock_ms=first_boot+4000+dt;engine_tick();
        for(unsigned m=0;m<6;++m){
            assert(state.motors[m].active&&state.motors[m].enabled);
            assert(state.motors[m].frequency<=previous[m]+1);
            assert(state.motors[m].frequency>=MIN_FREQ_MHZ);
            previous[m]=state.motors[m].frequency;
        }
    }
    for(unsigned m=0;m<6;++m)assert(previous[m]<40000);
    clock_ms=first_boot+4400;engine_tick();
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].active&&!state.motors[m].enabled);
    /* Explicit emergency stop must still interrupt the ramp immediately. */
    setup();assert(!engine_command(C_BOOT_TEST,0,0,boot_out,&boot_len));
    first_boot=boot_at;
    for(unsigned i=0;i<6;++i){clock_ms=first_boot+i*400;engine_tick();}
    clock_ms=first_boot+4200;engine_tick();engine_estop();
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].active&&!state.motors[m].enabled);
    /* Buffering and initial silence never energize drivers. Each note owns EN. */
    setup();uint8_t disable=1,setup_out[8],setup_len;
    assert(!engine_command(C_STREAM_STOP,&disable,1,setup_out,&setup_len));
    assert(!raw_event(0,1,0x85,1));
    assert(!raw_event(500,0,0x80,60|(100u<<8)));
    assert(!raw_event(600,0,0x81,60));
    assert(!raw_event(900,0,0x80,62|(100u<<8)));
    assert(!raw_event(1000,0,0x83,0));
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].enabled);
    begin();clock_ms=origin-1;engine_tick();
    uint8_t future[160];uint32_t first_epoch=engine_display_epoch();
    assert(engine_display_preview(future,16,origin+399)==0);
    assert(engine_display_preview(future,16,origin+400)==1);
    assert(u32(future)==origin+500&&future[4]==0&&future[5]==60);
    assert(!state.motors[0].enabled&&!state.motors[0].active);
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].enabled);
    run_at(499);assert(!state.motors[0].enabled&&!state.motors[0].active);
    run_at(500);assert(state.motors[0].enabled&&state.motors[0].active);
    run_at(600);assert(!state.motors[0].enabled&&!state.motors[0].active&&engine_display_hold(0));
    run_at(900);assert(state.motors[0].enabled&&state.motors[0].active);
    run_at(1000);assert(!state.motors[0].enabled);
    assert(engine_display_epoch()!=first_epoch);
    assert(!engine_display_preview(future,16,clock_ms));
    /* A global microstep setting survives song start, end and emergency stop. */
    setup();uint8_t micro=3,micro_out[8],micro_len;
    assert(!engine_command(C_MS,&micro,1,micro_out,&micro_len));
    assert(!raw_event(0,63,0x85,6));assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(100,0,0x83,0));begin();run_at(0);assert(state.raw==3);
    run_at(100);assert(state.raw==3);
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].enabled&&!state.motors[m].active);
    engine_estop();assert(state.raw==3);
    setup();uint8_t keep_enable=0;
    assert(!engine_command(C_STREAM_STOP,&keep_enable,1,micro_out,&micro_len));
    for(unsigned m=0;m<6;++m)assert(!state.motors[m].enabled);
    setup();assert(frequency_period(1200000));assert(!frequency_period(1200001));
    assert(!raw_event(0,1,0x85,1)); /* Gap tests constrain successive notes to M1. */
    assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(100,0,0x81,60));
    assert(!raw_event(500,0,0x80,62|(100u<<8)));
    assert(!raw_event(600,0,0x81,62));
    assert(!raw_event(1200,0,0x80,64|(100u<<8)));
    assert(!raw_event(1300,0,0x83,0));
    begin();run_at(0);assert(state.motors[0].active&&state.motors[0].note==60);
    run_at(100);assert(!state.motors[0].active&&engine_display_hold(0));
    run_at(499);assert(engine_display_hold(0));run_at(500);assert(state.motors[0].active&&state.motors[0].note==62);
    run_at(600);assert(!state.motors[0].active&&!engine_display_hold(0));
    run_at(1300);assert(!state.running&&!state.error);
    /* A repeated release must not cancel a confirmed short-gap hold. */
    setup();assert(!raw_event(0,1,0x85,1));
    assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(100,0,0x81,60));
    assert(!raw_event(500,0,0x80,62|(100u<<8)));
    assert(!raw_event(1000,0,0x83,0));begin();run_at(0);run_at(100);
    assert(engine_display_hold(0));
    /* Legacy streams may carry redundant Note Off commands. */
    queue[(head+NOTES_BUFFER_DEPTH-1)%NOTES_BUFFER_DEPTH]=(QueuedEvent){150,0,0,0,255};
    head=(head+NOTES_BUFFER_DEPTH-1)%NOTES_BUFFER_DEPTH;++state.used;
    run_at(150);assert(engine_display_hold(0));
    /* Future data received during a gap must update the presentation decision. */
    setup();assert(!raw_event(0,1,0x85,1));
    assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(100,0,0x81,60));
    assert(!raw_event(200,0,0x82,1));begin();run_at(0);run_at(100);
    assert(!engine_display_hold(0));
    assert(!raw_event(500,0,0x80,62|(100u<<8)));
    assert(!raw_event(1000,0,0x83,0));run_at(101);
    assert(!state.motors[0].active&&engine_display_hold(0));
    run_at(499);assert(engine_display_hold(0));
    run_at(500);assert(state.motors[0].active);
    /* Actual STM STATE v2 separates silent hardware from held display state. */
    setup();note_event_count=0;
    assert(!raw_event(0,63,0x85,6));
    assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(100,0,0x81,60));assert(!raw_event(5000,0,0x83,0));
    begin();run_at(50);hmi_link=1;hmi_last=clock_ms;hmi_poll(1);
    uint32_t presentation_start=clock_ms;
    assert((reported_state[14]&2)&&(reported_state[50]&2));
    run_at(100);hmi_last=clock_ms;hmi_poll(1);
    assert(!(reported_state[14]&2)&&(reported_state[50]&2));
    assert(!state.motors[0].active&&!engine_display_hold(0));
    state.motors[0].note=65;state.motors[0].frequency=note_mhz(65);
    run_at(150);hmi_last=clock_ms;hmi_poll(1);
    assert(reported_state[15]==65&&reported_state[51]==60);
    assert(song_u32(reported_state+16)!=song_u32(reported_state+52));
    run_at(249);assert(clock_ms==presentation_start+199);
    hmi_last=clock_ms;hmi_sent=clock_ms-50;hmi_poll(1);assert(reported_state[50]&2);
    run_at(250);hmi_last=clock_ms;hmi_sent=clock_ms-50;hmi_poll(1);
    assert(!(reported_state[50]&2)&&!state.motors[0].active);
    /* Sustain reconstruction at seek: prefix is analysed by STM without STEP. */
    setup();uint8_t p[4],out[8],n;put32(p,300);assert(!engine_command(C_RAW_SEEK,p,4,out,&n));
    assert(!raw_event(0,63,0x85,6));assert(!raw_event(0,0,0x80,60|(100u<<8)));
    assert(!raw_event(50,0,0x82,64|(127u<<8)));assert(!raw_event(100,0,0x81,60));
    assert(!raw_event(700,0,0x82,64));assert(!raw_event(1000,0,0x83,0));
    begin();run_at(0);assert(state.motors[0].active);run_at(400);assert(!state.motors[0].active);
    /* All channels/ports and repeated attacks remain independently releasable. */
    music_reset();assert(!music_event(0,0,0,60,100,0));assert(!music_event(1,0,0,60,100,0));
    assert(!music_event(2,16,0,60,100,0));music_event(3,0,1,60,0,0);music_choose(3);assert(music.count==1);
    music_event(4,0,1,60,0,0);music_choose(4);assert(music.count==1);
    music_event(5,16,1,60,0,0);music_choose(5);assert(!music.count);
    /* Selection occurs once per complete group, including packet boundaries. */
    setup();assert(!raw_event(0,63,0x85,6));
    for(unsigned i=0;i<8;++i)assert(!raw_event(0,0,i==7?0x80:0,48+i*3|(100u<<8)));
    assert(music.count==6);assert(!raw_event(1000,0,0x83,0));begin();run_at(0);
    unsigned active=0;for(unsigned m=0;m<6;++m)active+=state.motors[m].active;assert(active==6);
    /* Whole malformed batch must leave planner and queue untouched. */
    setup();assert(!raw_event(0,63,0x85,6));uint8_t bad[20]={0};bad[5]=0x80;bad[6]=60;bad[7]=100;bad[15]=0x80;bad[16]=255;
    assert(engine_command(C_RAW_EVENTS,bad,20,out,&n)==E_VALUE);assert(!state.used&&!music.order);
    /* Live events are never predicted; timeout releases every motor. */
    setup();engine_live_gate(1);uint8_t live[10]={0};live[5]=0x80;live[6]=60;live[7]=100;
    assert(!engine_live_events(live,10,1));clock_ms+=3;engine_tick();assert(state.motors[0].active);
    clock_ms+=NS_TIMEOUT_MS+1;engine_tick();assert(state.reset&&!state.motors[0].active);
    setup();live_packet(1,0x80);assert(!last_midi_ack&&music.order==1);
    live_packet(1,0x80);assert(!last_midi_ack&&music.order==1); /* Lost ACK retry. */
    clock_ms+=3;engine_tick();assert(state.motors[0].active);
    live_packet(2,0x81);assert(!last_midi_ack&&!state.motors[0].active);
    pc_active=1;live_packet(3,0x80);assert(last_midi_ack==12&&!state.motors[0].active);
    setup();engine_live_gate(1);live[6]=127;assert(!engine_live_events(live,10,1));
    clock_ms+=3;engine_tick();assert(state.motors[0].active&&state.motors[0].frequency<=1200000);
    /* Display clocks must not overtake history split across reply windows. */
    setup();note_event_count=0;checked_clock=0;check_history_clock=1;
    for(unsigned i=0;i<10;++i){clock_ms=10+i;history_push(0,60,(i&1)?0:100);}
    clock_ms=100;hmi_link=1;hmi_last=clock_ms;hmi_poll(1);
    assert(note_event_count==8&&checked_clock==17);
    clock_ms=101;hmi_poll(1);assert(note_event_count==10&&checked_clock==19);
    check_history_clock=0;
    printf("STM32 MIDI planner, seek, gap, polyphony and timeout passed. Planner RAM: %u bytes\n",(unsigned)(sizeof(music)+sizeof(ms)));
    return 0;
}
