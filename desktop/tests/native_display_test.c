#define MUSIC_BOX_CONTROLLER 1
#define MUSIC_BOX_PIXEL_MIDI 1
#define MUSIC_BOX_BAND 16
#include "../../esp32-diplsay-midi/DisplaySrc/src/music_box.c"
#include <assert.h>

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
  for (int y=0;y<320;y+=BAND) {clip=(Rect){0,y,480,BAND};scene();capture(0,y,480,BAND,pixels,480,0);}
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
  page=0;numeric=0;save_stage=0;saved_playing=0;notice_until=0;
  pc_range[0]=48;pc_range[1]=67;
  uint8_t *b=packet+5;
  packet[2]=50;packet[4]=0x40;
  memset(b,0,50);b[0]=1;b[1]=1|2|16|32;b[5]=63;put32(b+10,10000);
  b[14]=3;b[15]=67;put32(b+16,391995);
  process_packet();
  while(dirty_count)flush_one();
  assert(screen[36][45]==BLUE); /* Highest note fills the whole scaled bar. */
  b[1]=1;b[14]=0;put32(b+10,0);
  bar_pixels_written=0;
  process_packet();
  while(dirty_count)flush_one();
  for(unsigned y=36;y<128;++y)for(unsigned x=8;x<83;++x)
    assert(screen[y][x]==BG);
  assert(bar_pixels_written<=75*92); /* Only the changed column is repainted. */
  /* Scale changes while a note remains active also erase just the height delta. */
  b[1]=1|2|16|32;b[14]=3;put32(b+10,10000);
  process_packet();while(dirty_count)flush_one();
  assert(screen[36][45]==BLUE);
  b[1]=1|2;bar_pixels_written=0;
  process_packet();while(dirty_count)flush_one();
  unsigned top=128-bar_height(&overview[0]);
  assert(top>36);
  for(unsigned y=36;y<top;++y)assert(screen[y][45]==BG);
  for(unsigned y=top;y<128;++y)assert(screen[y][45]==BLUE);
  assert(bar_pixels_written<=75*92);
}
int main(void) {
  DisplayPlatform platform={0};platform.version=DISPLAY_API_VERSION;platform.width=480;platform.height=320;
  platform.write_rect=capture;platform.send=send_packet;
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
  Motor low={.note=48,.mhz=130813,.flags=2},high={.note=67,.mhz=391995,.flags=2};
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
  check_stop_clears_scaled_bars();
  check_timeline_pause();
  puts("Display controls, note range and sparse slots passed");
}
