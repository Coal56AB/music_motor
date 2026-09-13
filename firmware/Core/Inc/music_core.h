#ifndef MUSIC_CORE_H
#define MUSIC_CORE_H
/* Bounded, allocation-free musical planner. Called only from the main loop.
 * Millisecond times cover a whole day; source+channel is one MIDI address. */
typedef struct {
    uint32_t at, released, order;
    uint8_t note, address, velocity, track, flags, pin, priority;
} MusicKey;
typedef struct { uint32_t at; uint8_t note, address, confidence, used; int8_t direction; } MusicTrack;
static struct {
    MusicKey keys[128];
    MusicTrack tracks[32];
    uint16_t sustain[16];
    uint32_t order;
    uint32_t instance_assignment[6];
    uint8_t selected[6], assignment[6], last_pitch[6], count, voices, strategy, mask;
} music;
/* Shared scratch avoids a large stack frame; no interrupt calls this code. */
static struct {
    uint32_t released[128], order[128];
    int16_t score[128];
    uint16_t masks[128];
    uint8_t active[128], down[128], velocity[128], confidence[128], old[128];
    uint8_t pitches[128], sizes[128], first[128], pin[128], priority[128];
    int8_t group[128], root[128], kind[128], exact[128];
} ms;
static void music_reset(void) {
    memset(&music,0,sizeof(music));
    memset(music.assignment,255,6);memset(music.last_pitch,255,6);music.voices=6;music.mask=63;
}
static uint8_t music_track(uint8_t note,uint8_t address,uint32_t at) {
    int best=-1,cost=100000;
    uint32_t occupied=0,held=0;
    for(unsigned j=0;j<128;++j){const MusicKey *k=&music.keys[j];if(k->flags&&k->track<32){occupied|=1u<<k->track;if(k->flags&2)held|=1u<<k->track;}}
    for(int i=0;i<32;++i) {
        MusicTrack *t=&music.tracks[i];
        if(!t->used||t->address!=address||at<t->at||at-t->at>600)continue;
        if((held&(1u<<i))&&at-t->at<=40)continue;
        int d=(int)note-t->note,dist=d<0?-d:d,dir=(d>0)-(d<0);
        if(dist>7)continue;
        int c=dist*10+(dir&&t->direction&&dir!=t->direction?8:0)-t->confidence*3;
        if(c<cost){cost=c;best=i;}
    }
    int continued=best>=0;
    if(best<0)for(int i=0;i<32;++i) {
        if(!(occupied&(1u<<i))&&(best<0||music.tracks[i].at<music.tracks[best].at))best=i;
    }
    if(best<0)return 255;
    MusicTrack *t=&music.tracks[best];int d=(int)note-t->note;
    t->confidence=continued?(t->confidence<4?t->confidence+1:4):0;
    t->direction=continued?(d>0)-(d<0):0;t->note=note;t->address=address;t->at=at;t->used=1;
    return (uint8_t)best;
}
static uint8_t music_event_ex(uint32_t at,uint8_t address,uint8_t type,uint8_t note,uint8_t velocity,uint8_t pin,uint8_t priority) {
    if(type==4){music_reset();return E_OK;}
    uint16_t bit=(uint16_t)(1u<<(address&15));uint16_t *pedal=&music.sustain[address>>4];
    if(type==0&&!velocity)type=1;
    if(type==2) {
        if(note==64){if(velocity>=64)*pedal|=bit;else *pedal&=~bit;}
        if(note==121)*pedal&=~bit;
        for(unsigned i=0;i<128;++i) {
            MusicKey *k=&music.keys[i];if(!k->flags||k->address!=address)continue;
            if(note==120)k->flags=0;
            else {
                if(note==123||(note>=124&&note<=127)){if(k->flags&2)k->released=at;k->flags&=~2;}
                if(!(k->flags&2)&&!(*pedal&bit))k->flags=0;
            }
        }
    } else if(type==1) {
        MusicKey *key=0;
        for(unsigned i=0;i<128;++i){MusicKey *k=&music.keys[i];if((k->flags&3)==3&&k->note==note&&k->address==address&&k->pin==pin&&(!key||k->order<key->order))key=k;}
        if(key){key->released=at;key->flags=(*pedal&bit)?1:0;}
    } else if(type==0) {
        uint8_t track=music_track(note,address,at);MusicKey *key=0;
        for(unsigned i=0;i<128;++i){MusicKey *k=&music.keys[i];if(k->flags==1&&k->note==note&&k->address==address&&k->pin==pin)k->flags=0;}
        for(unsigned i=0;i<128;++i)if(!music.keys[i].flags){key=&music.keys[i];break;}
        if(!key)return E_FULL;
        *key=(MusicKey){at,0,++music.order,note,address,velocity,track,3,pin,priority};
    }
    return E_OK;
}
static uint8_t music_event(uint32_t at,uint8_t address,uint8_t type,uint8_t note,uint8_t velocity,uint8_t pin) {
    return music_event_ex(at,address,type,note,velocity,pin,0);
}
static const uint8_t music_chords[9][4]={{0,4,7,255},{0,3,7,255},{0,3,6,255},{0,4,8,255},{0,2,7,255},{0,5,7,255},{0,4,7,10},{0,4,7,11},{0,3,7,10}};
static void music_harmony(unsigned g) {
    ms.root[g]=ms.kind[g]=-1;
    for(int k=0;k<9;++k)for(int r=0;r<12;++r){
        uint16_t mask=0;for(unsigned j=0;j<4&&music_chords[k][j]!=255;++j)mask|=1u<<((r+music_chords[k][j])%12);
        if(mask==ms.masks[g]){ms.root[g]=(int8_t)r;ms.kind[g]=(int8_t)k;ms.exact[g]=1;return;}
    }
    for(int k=0;k<2;++k)for(int r=0;r<12;++r)if(ms.masks[g]==((1u<<r)|(1u<<((r+music_chords[k][1])%12)))){ms.root[g]=(int8_t)r;ms.kind[g]=(int8_t)k;return;}
}
static int music_better(unsigned a,unsigned b) {
    if(ms.score[a]!=ms.score[b])return ms.score[a]>ms.score[b];
    if(ms.down[a]!=ms.down[b])return ms.down[a]>ms.down[b];
    if(!ms.down[a]&&ms.released[a]!=ms.released[b])return ms.released[a]>ms.released[b];
    if(!ms.down[a]&&ms.order[a]!=ms.order[b])return ms.order[a]>ms.order[b];
    if(ms.old[a]!=ms.old[b])return ms.old[a]>ms.old[b];
    return a<b;
}
static void music_choose(uint32_t at) {
    memset(&ms,0,sizeof(ms));memset(ms.group,-1,sizeof(ms.group));memset(ms.priority,255,sizeof(ms.priority));
    for(unsigned i=0;i<128;++i){MusicKey *k=&music.keys[i];if(!k->flags)continue;unsigned n=k->note;
        ms.active[n]=1;ms.down[n]|=(k->flags>>1)&1;
        if(k->velocity>ms.velocity[n])ms.velocity[n]=k->velocity;
        if(k->released>ms.released[n])ms.released[n]=k->released;
        if(k->order>ms.order[n])ms.order[n]=k->order;
        if(k->pin)ms.pin[n]=k->pin;
        if(k->priority<ms.priority[n])ms.priority[n]=k->priority;
        if(k->track<32&&music.tracks[k->track].note==n&&music.tracks[k->track].confidence>ms.confidence[n])ms.confidence[n]=music.tracks[k->track].confidence;
    }
    for(unsigned i=0;i<music.count;++i)ms.old[music.selected[i]]=1;
    int count=0,g=-1,last=-128;
    for(unsigned n=0;n<128;++n)if(ms.active[n]){
        ms.pitches[count++]=(uint8_t)n;if(ms.confidence[n]>=2)continue;
        if(g<0||(int)n-last>=13||(int)n-ms.first[g]>24){++g;ms.first[g]=(uint8_t)n;}
        ms.group[n]=(int8_t)g;ms.masks[g]|=1u<<(n%12);++ms.sizes[g];last=(int)n;
    }
    for(int i=0;i<=g;++i)music_harmony((unsigned)i);
    for(int i=0;i<count;++i){unsigned n=ms.pitches[i];int group=ms.group[n],s=ms.velocity[n]/16;
        if(ms.confidence[n]>=2)s+=400+ms.confidence[n]*20+(n<48?160:0);
        if(group>=0){
            if(ms.sizes[group]==1)s+=260;
            if(ms.root[group]>=0){int interval=((int)n%12-ms.root[group]+12)%12,k=ms.kind[group];
                s+=!interval?180:interval==music_chords[k][1]?(ms.exact[group]?110:20):interval==music_chords[k][3]?90:40;
            }else if(n==ms.first[group])s+=90;
            for(int j=0;j<i;++j)if(ms.group[ms.pitches[j]]==group&&ms.pitches[j]%12==n%12){s-=220;break;}
        }
        if(ms.old[n])s+=12;
        if(!ms.down[n]){uint32_t age=at>=ms.released[n]?at-ms.released[n]:0;s-=20+(age>=3000?240:(int)(age*80/1000));}
        /* Strategy codes mirror desktop STRATEGIES; pins always take priority. */
        if(music.strategy==1)s=(ms.old[n]?1000:0)+(int)ms.velocity[n];
        if(music.strategy==2)s=(i==0||i==count-1?1000:0)+(int)ms.velocity[n];
        if(music.strategy==3)s=ms.velocity[n];
        if(music.strategy==4)s=(int)n;
        if(music.strategy==5)s=127-(int)n;
        if(music.strategy==6)s=(127-ms.priority[n])*128+ms.velocity[n];
        if(ms.pin[n])s+=10000;
        ms.score[n]=(int16_t)s;
    }
    music.count=0;uint8_t used_pins=0;
    for(unsigned i=0;i<music.voices&&i<6;++i){int best=-1;
        for(int j=0;j<count;++j){unsigned n=ms.pitches[j];if(!ms.active[n])continue;
            if(ms.pin[n]&&(!(music.mask&(1u<<(ms.pin[n]-1)))||(used_pins&(1u<<(ms.pin[n]-1)))))continue;
            if(best<0||music_better(n,(unsigned)best))best=(int)n;
        }
        if(best<0)break;
        ms.active[best]=0;music.selected[music.count++]=(uint8_t)best;
        if(ms.pin[best])used_pins|=1u<<(ms.pin[best]-1);
    }
    for(unsigned i=0;i<music.count;++i)for(unsigned j=i+1;j<music.count;++j)if(music.selected[j]<music.selected[i]){uint8_t t=music.selected[i];music.selected[i]=music.selected[j];music.selected[j]=t;}
}
#endif
