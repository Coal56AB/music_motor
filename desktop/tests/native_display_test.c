#define MUSIC_BOX_CONTROLLER 1
#define MUSIC_BOX_PIXEL_MIDI 1
#define MUSIC_BOX_BAND 16
#include "../../esp32-diplsay-midi/DisplaySrc/src/music_box.c"
#include <assert.h>
#include "../../firmware/Core/Inc/display_overview.h"


static void check_overview_delivery(void) {
  DisplayOverview stm={0};
  DisplayMotor input[6]={0};
  page=1;have_state=1;flags=1|32|64;mask=63;sleeping=resetting=0;
  uint32_t random=0x723919u;
  /* Same sampled motor states, including missing lookahead, changing notes,
   * long silences, stop/pause, manual/live mode and uint32 clock rollover. */
  for(unsigned tick=0;tick<100000;++tick) {
    now=0xffff0000u+tick*10u;
    if(tick%613==0) {
      unsigned mode=(tick/613)%7;
      flags=(uint8_t)(mode==0?1:mode==1?1|32:mode==2?1|64:mode==3?1|32|4:1|32|64);
      have_state=mode!=4;sleeping=mode==5;resetting=mode==6;
    }
    for(unsigned m=0;m<6;++m)if(tick%(37+m*11)==0) {
      random=random*1664525u+1013904223u;
      motors[m].flags=(uint8_t)(1|((random>>29)&2)|((random>>25)&8));
      motors[m].note=(uint8_t)(48+(random%30));
      motors[m].mhz=note_frequency(motors[m].note);
      mask=(uint8_t)(random>>16)&63;
    }
    for(unsigned m=0;m<6;++m) {
      input[m].mhz=motors[m].mhz;input[m].note=motors[m].note;input[m].flags=motors[m].flags;
    }
    display_overview_update(&stm,input,now,have_state,flags,sleeping,resetting,mask);
    for(unsigned m=0;m<6;++m) {
      controller_overview[m].mhz=stm.motors[m].mhz;
      controller_overview[m].note=stm.motors[m].note;
      controller_overview[m].flags=stm.motors[m].flags;
    }
    controller_overview_ready=1;
    update_overview(NULL); for(unsigned m=0;m<6;++m){assert(overview[m].flags==stm.motors[m].flags);assert(overview[m].note==stm.motors[m].note);assert(overview[m].mhz==stm.motors[m].mhz);}
  }
  puts("STM overview delivery: 100000 updates preserved on ESP");
}

static void check_note_minimum_visibility(void) {
  const unsigned durations[]={100,199,200,600,2000};
  for(unsigned wrap=0;wrap<2;++wrap)for(unsigned i=0;i<5;++i) {
    DisplayOverview view={0};DisplayMotor input[6]={0};
    uint32_t base=wrap?0xffffff00u:100u,d=durations[i];
    input[0]=(DisplayMotor){440000,3,69};
    display_overview_update(&view,input,base,1,1|32|64,0,0,63);
    input[0].flags=0;
    display_overview_update(&view,input,base+d,1,1|32|64,0,0,63);
    assert(!!(view.motors[0].flags&2)==(d<200));
    if(d<200) {
      display_overview_update(&view,input,base+199,1,1|32|64,0,0,63);
      assert(view.motors[0].flags&2);
      display_overview_update(&view,input,base+200,1,1|32|64,0,0,63);
      assert(!(view.motors[0].flags&2));
    }
  }
  DisplayOverview view={0};DisplayMotor input[6]={0};
  input[0]=(DisplayMotor){440000,3,69};
  display_overview_update(&view,input,100,1,1|32|64,0,0,63);
  input[0].flags=8; /* Known short gap remains visible after a long note. */
  display_overview_update(&view,input,2100,1,1|32|64,0,0,63);assert(view.motors[0].flags&2);
  input[0].flags=0;
  display_overview_update(&view,input,2200,1,1|32|64,0,0,63);assert(!(view.motors[0].flags&2));
  puts("Note visibility: minimum 200 ms from onset, immediate long-note release, short-gap hold passed");
}

static void process_stm_state(DisplayOverview *stm) {
  uint8_t *b=packet+5;
  DisplayMotor input[6]={0};
  for(unsigned m=0;m<6;++m) {
    input[m].flags=b[14+6*m];input[m].note=b[15+6*m];input[m].mhz=get32(b+16+6*m);
  }
  display_overview_update(stm,input,now,1,b[1],b[2],b[3],b[5]);
  b[0]=2;packet[2]=86;
  for(unsigned m=0;m<6;++m) {
    b[50+6*m]=stm->motors[m].flags;b[51+6*m]=stm->motors[m].note;
    put32(b+52+6*m,stm->motors[m].mhz);
  }
  process_packet();
}

static uint16_t screen[320][480];
static uint8_t sent[13];
static unsigned bar_pixels_written;
static void capture(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint16_t *data,uint16_t stride,void *unused) {
  (void)unused;
  for (unsigned row=0;row<h;++row) {
    memcpy(&screen[y+row][x],data+row*stride,w*2);
    if (y+row>=36 && y+row<128) bar_pixels_written+=w;
  }
}
static void send_packet(const uint8_t *b,uint16_t n) { assert(n==13);memcpy(sent,b,n); }
static uint8_t stored_settings[8];
static int settings_read(uint32_t addr,void *data,uint32_t n) {
  assert(addr==SETTINGS_ADDR&&n==8);memcpy(data,stored_settings,n);return 1;
}
static int settings_write(uint32_t addr,const void *data,uint32_t n) {
  assert(addr==SETTINGS_ADDR&&n==8);memcpy(stored_settings,data,n);return 1;
}
static int settings_erase(uint32_t addr) {assert(addr==SETTINGS_ADDR);return 1;}
static void settings_state(uint8_t raw) {
  memset(packet,0,sizeof(packet));packet[2]=50;packet[4]=0x40;
  packet[5]=1;packet[6]=1;packet[9]=raw;packet[10]=63;process_packet();
}
static void predictive_state(uint32_t stamp,uint32_t epoch,uint8_t running,uint8_t pitch) {
  memset(packet,0,sizeof(packet));packet[2]=94;packet[4]=0x40;
  uint8_t *b=packet+5;b[0]=3;b[1]=1|(running?64:0);b[5]=63;
  b[50]=pitch<128?3:0;b[51]=pitch;put32(b+52,261626);
  put32(b+86,stamp);put32(b+90,epoch);process_packet();
}
static void check_predictive_display(DisplayPlatform *platform) {
  display_init(platform);now=2000;predictive_state(1000,1,1,60);
  memset(packet,0,sizeof(packet));packet[2]=25;packet[4]=0x45;
  uint8_t *b=packet+5;put32(b,1);b[4]=2;
  put32(b+5,1050);b[9]=0;b[10]=64;put32(b+11,329628);
  put32(b+15,1100);b[19]=0;b[20]=67;put32(b+21,391995);
  process_packet();assert(upcoming_count==2);
  now=2049;apply_upcoming();assert(overview[0].note==60);
  now=2050;apply_upcoming();assert(overview[0].note==64&&upcoming_count==1);
  now=2051;predictive_state(1040,1,1,60);assert(overview[0].note==64);
  now=2099;apply_upcoming();assert(overview[0].note==64);
  now=2100;apply_upcoming();assert(overview[0].note==67);
  /* Rewind/new playback epoch cancels future events and its previous clock. */
  now=2110;predictive_state(10,2,1,60);assert(!upcoming_count&&!predicted_mask);
  memset(packet,0,sizeof(packet));packet[2]=15;packet[4]=0x45;b=packet+5;
  put32(b,1);b[4]=1;put32(b+5,60);b[9]=0;b[10]=72;put32(b+11,523251);
  process_packet();assert(!upcoming_count); /* Old epoch rejected. */
  put32(b,2);process_packet();assert(upcoming_count==1);
  now=2120;predictive_state(20,3,0,255);assert(!predictive_ready&&!upcoming_count);
  now=2200;apply_upcoming();assert(!(overview[0].flags&2));
  now=3000;predictive_state(2000,4,1,60);
  memset(packet,0,sizeof(packet));packet[2]=15;packet[4]=0x45;b=packet+5;
  put32(b,4);b[4]=1;put32(b+5,2050);b[9]=0;b[10]=72;put32(b+11,523251);
  process_packet();assert(upcoming_count==1);
  now=3200;predictive_state(2200,4,1,255);
  apply_upcoming();assert(!(overview[0].flags&2));
  display_init(platform);
}
static void check_note_history_capacity(void) {
  memset(notes,0,sizeof(notes));note_next=0;
  MidiNote *held=midi_note_slot(0);assert(held);
  *held=(MidiNote){0};held->used=1;held->open=1;
  for(unsigned i=1;i<12000;++i) {
    MidiNote *n=midi_note_slot(i);assert(n&&n!=held);
    *n=(MidiNote){0};n->used=1;n->end=i;
  }
  assert(held->used&&held->open);
  held->open=0;held->end=20000;
  for(unsigned i=12000;i<20000;++i) {
    MidiNote *n=midi_note_slot(i);assert(n&&n!=held);
    *n=(MidiNote){0};n->used=1;n->end=i;
  }
  puts("MIDI history: 20000 allocations preserve long open and recently ended notes");
}
static void check_settings_and_motor_paint(DisplayPlatform *platform) {
  platform->flash_read=settings_read;platform->flash_write=settings_write;
  platform->flash_erase=settings_erase;
  display_init(platform);saved_micro=3;show_hz=1;width_index=1;save_settings();
  display_init(platform);assert(saved_micro==3&&restore_micro&&show_hz==1&&width_index==1);
  settings_state(0);assert(saved_micro==3&&restore_micro);
  display_step(1);assert(pending&&sent[5]==7&&get32(sent+7)==3);
  assert(!music_box_screen_ready());
  pending=0;settings_state(3);assert(!restore_micro&&!settings_dirty);
  settings_state(7);assert(saved_micro==7&&settings_dirty);
  save_settings();display_init(platform);assert(saved_micro==7&&restore_micro);
  /* Previous settings format retains screen options, without forcing a step. */
  stored_settings[2]=1;uint16_t c=crc16(stored_settings,6);
  stored_settings[6]=(uint8_t)c;stored_settings[7]=(uint8_t)(c>>8);
  display_init(platform);assert(saved_micro==255&&!restore_micro&&show_hz==1);
  stored_settings[6]^=1;display_init(platform);assert(saved_micro==255&&show_hz==0);
  platform->flash_read=0;platform->flash_write=0;platform->flash_erase=0;
  display_init(platform);display_renderer_clear(&renderer);page=0;
  /* All six 75x48 indicators finish in six passes, previously eighteen. */
  for(unsigned m=0;m<6;++m)invalidate(8+m*78,150,75,48);
  for(unsigned m=0;m<6;++m){flush_one();assert(display_renderer_pending(&renderer)==5-m);}
  invalidate(0,0,480,320);flush_one();assert(renderer.pending[0].y==MUSIC_BOX_BAND);
}
static void snapshot(const char *path) {
  FILE *f=fopen(path,"wb");assert(f);
  fprintf(f,"P6\n480 320\n255\n");
  for(unsigned y=0;y<320;++y)for(unsigned x=0;x<480;++x) {
    unsigned c=screen[y][x];
    fputc(((c>>11)&31)*255/31,f);fputc(((c>>5)&63)*255/63,f);fputc((c&31)*255/31,f);
  }
  fclose(f);
}
static void render(void) {
  for (int y=0;y<320;y+=MUSIC_BOX_BAND) {
    DisplayCanvas target={{0,y,480,MUSIC_BOX_BAND},render_storage.pixels};
    paint_scene(&target,0);
    capture(0,y,480,MUSIC_BOX_BAND,target.pixels,480,0);
  }
}
static void timeline_clock(uint32_t at) {
  packet[2]=4;packet[4]=0x43;put32(packet+5,at);process_packet();
}
static void timeline_note(uint32_t at,unsigned velocity) {
  packet[2]=7;packet[4]=0x42;put32(packet+5,at);
  packet[9]=0;packet[10]=69;packet[11]=(uint8_t)velocity;process_packet();
}
static void check_timeline_pause(void) {
  have_state=1;flags=1;sleeping=resetting=0;pending=0;
  midi_now=0;midi_source_valid=midi_stop_latched=midi_input_running=0;
  midi_tail_ms=0;
  memset(notes,0,sizeof(notes));note_next=0;midi_dirty=0;
  timeline_clock(1000);timeline_clock(2000);
  assert(midi_now==0&&!midi_dirty);
  music_box_midi_input(1);timeline_clock(2050);
  assert(midi_now==50);
  timeline_note(2050,100);assert(notes[0].open&&notes[0].start==50);
  timeline_clock(2100);assert(midi_now==100);
  send_action(0,0,0);
  assert(!notes[0].open&&notes[0].end==100);
  music_box_midi_input(1); /* Connected input must not undo STOP ALL. */
  timeline_clock(2200);timeline_clock(5000);
  assert(midi_now==100);
  music_box_midi_input(0);pending=0;send_action(5,0,1);
  timeline_note(5000,100);flags=3;
  timeline_clock(5050);assert(midi_now==150);
  timeline_note(5050,0);flags=1;
  timeline_clock(5550);assert(midi_now==650); /* Half of the one-second tail. */
  timeline_clock(6050);assert(midi_now==1150);
  timeline_clock(7000);assert(midi_now==1150&&!notes[1].open);
  flags=1|64;timeline_clock(7050); /* File playback keeps moving through rests. */
  assert(midi_now==1200);
  flags=1|4|64;timeline_clock(8000);assert(midi_now==1200);
  flags=1;music_box_midi_input(1);timeline_clock(8050);assert(midi_now==1250);
  music_box_midi_input(0);timeline_clock(10000);assert(midi_now==2250);
  timeline_clock(11000);assert(midi_now==2250);
}
static void check_stop_clears_scaled_bars(void) {
  DisplayOverview stm={0};
  now=10000;
  page=0;numeric=0;save_stage=0;saved_playing=0;notice_until=0;
  pc_range[0]=48;pc_range[1]=67;
  uint8_t *b=packet+5;
  packet[2]=50;packet[4]=0x40;
  memset(b,0,50);b[0]=1;b[1]=1|2|16|32;b[5]=63;put32(b+10,10000);
  b[14]=3;b[15]=67;put32(b+16,391995);
  process_stm_state(&stm);
  while(display_renderer_pending(&renderer))flush_one();
  assert(screen[36][45]==BLUE); /* Highest note fills the whole scaled bar. */
  b[1]=1|64;b[14]=1|8;process_stm_state(&stm);
  assert(overview[0].flags&2); /* Confirmed future appointment keeps the bar. */
  b[14]=1;process_stm_state(&stm);
  assert(overview[0].flags&2); /* Historical smoothing survives missing lookahead. */
  now=10199;process_stm_state(&stm);assert(overview[0].flags&2);
  now=10200;update_overview(NULL);assert(overview[0].flags&2); /* ESP never expires it. */
  process_stm_state(&stm);assert(!(overview[0].flags&2));
  b[1]=1|2|16|32;b[14]=3;process_stm_state(&stm);while(display_renderer_pending(&renderer))flush_one();
  b[1]=1;b[14]=0;put32(b+10,0);
  bar_pixels_written=0;
  process_stm_state(&stm);
  while(display_renderer_pending(&renderer))flush_one();
  for(unsigned y=36;y<128;++y)for(unsigned x=8;x<83;++x)
    assert(screen[y][x]==BG);
  assert(bar_pixels_written<=75*92); /* Only the changed column is repainted. */
  /* Scale changes while a note remains active also erase just the height delta. */
  b[1]=1|2|16|32;b[14]=3;put32(b+10,10000);
  process_stm_state(&stm);while(display_renderer_pending(&renderer))flush_one();
  assert(screen[36][45]==BLUE);
  b[1]=1|2;bar_pixels_written=0;
  process_stm_state(&stm);while(display_renderer_pending(&renderer))flush_one();
  unsigned top=128-bar_height(&overview[0]);
  assert(top>36);
  for(unsigned y=36;y<top;++y)assert(screen[y][45]==BG);
  for(unsigned y=top;y<128;++y)assert(screen[y][45]==BLUE);
  assert(bar_pixels_written<=75*92);
}
static void check_delayed_midi_preserves_history(void) {
  memset(notes,0,sizeof(notes));note_next=0;midi_now=0;midi_source_valid=0;
  midi_stop_latched=0;midi_input_running=0;have_state=1;flags=1|2|64;sleeping=resetting=0;
  page=1;numeric=save_stage=notice_until=0;
  timeline_clock(1000);timeline_note(1010,100);timeline_note(1110,0);
  timeline_clock(1150);
  unsigned saved=note_next;assert(saved&&notes[0].used);
  timeline_note(1120,100); /* Event arrived after a newer state/clock packet. */
  assert(notes[0].used&&note_next==saved+1);
  timeline_note(1140,0);timeline_clock(1140); /* Delayed clock must not clear either. */
  assert(notes[0].used&&notes[1].used);
  timeline_clock(1200);
  full();while(display_renderer_pending(&renderer))flush_one();
  unsigned colored=0;for(unsigned y=86;y<254;++y)for(unsigned x=48;x<472;++x)colored+=screen[y][x]==voice_colors[0];
  assert(colored>=6);
  /* Incremental pixel updates retain the closed bars as the cursor advances. */
  timeline_clock(1400);invalidate(0,54,480,210);while(display_renderer_pending(&renderer))flush_one();
  unsigned after=0;for(unsigned y=86;y<254;++y)for(unsigned x=48;x<472;++x)after+=screen[y][x]==voice_colors[0];
  assert(after>=colored);
  snapshot("build/midi-tests/midi-delayed.ppm");
}
static void check_saved_loading(void) {
  static uint16_t idle[320][480],loading[320][480];
  page=0;numeric=0;save_stage=0;notice_until=0;
  music_box_saved_playing(1);render();memcpy(idle,screen,sizeof(idle));
  music_box_saved_loading(1);while(display_renderer_pending(&renderer))flush_one();
  assert(saved_playing&&saved_loading&&!save_stage);
  assert(memcmp(idle[229],screen[229],16*480*2));
  memcpy(loading,screen,sizeof(loading));
  render();assert(!memcmp(loading,screen,sizeof(loading)));
  snapshot("build/midi-tests/song-loading.ppm");
  music_box_saved_loading(0);while(display_renderer_pending(&renderer))flush_one();
  assert(saved_playing&&!saved_loading);
  assert(!memcmp(idle,screen,sizeof(idle)));
  music_box_saved_loading(1);music_box_saved_playing(0);
  assert(!saved_playing&&!saved_loading);
  puts("Saved song loading: visible preparation, incremental redraw, start and stop passed");
}
int main(void) {
  check_note_history_capacity();
  check_note_minimum_visibility();
  DisplayPlatform platform={0};platform.version=DISPLAY_API_VERSION;platform.width=480;platform.height=320;
  platform.write_rect=capture;platform.send=send_packet;
  check_settings_and_motor_paint(&platform);
  check_predictive_display(&platform);
  display_init(&platform);
  check_overview_delivery();
  display_init(&platform);
  music_box_boot_error(&platform);snapshot("build/midi-tests/boot-error.ppm");
  assert(motor_note(&(Motor){.mhz=440000})==69);
  assert(note_frequency(69)==440000);
  flags=1;show_hz=0;input_note=69;numeric=1;
  touch_numeric(20,220);assert(sent[5]==3&&get32(sent+7)==440000&&numeric==0);
  pending=0;show_hz=1;strcpy(digits,"440.5");numeric=1;
  touch_numeric(20,220);assert(get32(sent+7)==440500);
  pending=0;show_hz=0;input_note=12;numeric=1;
  touch_numeric(20,220);assert(numeric==1&&!pending);numeric=0;
  numeric=1;input_note=69;
  touch_numeric(210,55); /* C4 */
  touch_numeric(310,165); /* C#4 */
  assert(input_note==61&&!pending);
  notice_until=0;render();snapshot("build/midi-tests/note-picker.ppm");
  touch_numeric(20,220);assert(get32(sent+7)==277183&&!numeric);
  pending=0;numeric=1;touch_numeric(400,220); /* Next octave: C#5. */
  touch_numeric(20,220);assert(get32(sent+7)==554366);
  pending=0;numeric=1;touch_numeric(210,220); /* Back to C#4. */
  touch_numeric(400,55); /* E has no sharp button state. */
  touch_numeric(310,165);assert(input_note==64);
  numeric=0;
  page=4;motors[0]=(Motor){.note=255,.mhz=440000,.flags=1};
  tap(440,110);assert(get32(sent+7)==466164);
  pending=0;tap(190,110);assert(get32(sent+7)==415305);
  pending=0;show_hz=1;tap(440,110);assert(get32(sent+7)==450000);
  pending=0;show_hz=0;
  save_stage=5;mask=63;
  Motor low={.note=36,.mhz=65406,.flags=2},high={.note=79,.mhz=783991,.flags=2};
  assert(bar_height(&low)==9&&bar_height(&high)==92);
  mask=1;assert(bar_height(&low)==92);
  low.flags=0;assert(bar_height(&low)==0);save_stage=0;
  memset(saved_present,0,sizeof(saved_present));saved_selected=0;
  select_saved_song(1);assert(saved_selected==0);
  music_box_saved_song(9,"Clockwork \xe2\x80\xa2 demo",1,10000);assert(saved_selected==9);
  select_saved_song(1);assert(saved_selected==9);
  music_box_saved_song(2,"Second",1,10000);
  select_saved_song(1);assert(saved_selected==2);
  select_saved_song(-1);assert(saved_selected==9);
  music_box_saved_song(10,"invalid",1,10000);assert(saved_selected==9);
  page=2;notice[0]=0;notice_until=0;render();snapshot("build/midi-tests/songs.ppm");
  page=4;motors[0]=(Motor){.note=255,.mhz=440000,.flags=1};render();snapshot("build/midi-tests/motor-note.ppm");
  check_saved_loading();
  check_stop_clears_scaled_bars();
  check_timeline_pause();
  check_delayed_midi_preserves_history();
  puts("Display controls, note range and sparse slots passed");
}
