#include "music.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
namespace music {
const Chord chords[9] = {
    {"major", {0,4,7,0},3}, {"minor",{0,3,7,0},3},
    {"diminished",{0,3,6,0},3}, {"augmented",{0,4,8,0},3},
    {"sus2",{0,2,7,0},3}, {"sus4",{0,5,7,0},3},
    {"dominant7",{0,4,7,10},4}, {"major7",{0,4,7,11},4}, {"minor7",{0,3,7,10},4}
};
Harmony recognize(uint16_t mask) {
    // Exact templates, then root+third hypotheses. Tie: template order, then root C..B.
    for (int k=0;k<9;++k) for (int r=0;r<12;++r) {
        uint16_t m=0;
        for (unsigned i=0;i<chords[k].count;++i) m |= 1u<<((r+chords[k].intervals[i])%12);
        if (m==mask) return {r,k,100};
    }
    for (int k=0;k<2;++k) for (int r=0;r<12;++r)
        if (mask == ((1u<<r)|(1u<<((r+chords[k].intervals[1])%12)))) return {r,k,55};
    return {};
}
bool NoteSet::operator==(const NoteSet &b) const {
    return count==b.count && !std::memcmp(notes,b.notes,count);
}
void Engine::reset() {
    for (auto &k:keys) k={};
    for (auto &t:tracks) t={};
    std::memset(sustain,0,sizeof(sustain));
    std::memset(confidence,0,sizeof(confidence));
    std::memset(selected_at,0,sizeof(selected_at));
    std::fill(groups,groups+128,-1);
    selected={}; history_head=0;
}
uint8_t Engine::assign_track(const Event &e) {
    int best=-1, cost=100000;
    for (int i=0;i<32;++i) {
        const auto &t=tracks[i];
        if (!t.used || t.channel!=e.channel || t.source!=e.source || e.timestamp<t.at || e.timestamp-t.at>cfg.track_us) continue;
        bool held=false;
        for (auto &k:keys) if(k.used && k.down && k.track==i) held=true;
        if (held && e.timestamp-t.at<=cfg.gesture_us) continue;
        int d=int(e.note)-t.note, distance=std::abs(d);
        if (distance>cfg.track_distance) continue;
        int dir=(d>0)-(d<0);
        int c=distance*cfg.distance_weight+(dir && t.direction && dir!=t.direction ? cfg.direction_penalty:0)-t.confidence*cfg.track_confidence_weight;
        if(c<cost) {cost=c;best=i;}
    }
    bool continued=best>=0;
    if(best<0) {
        // Never recycle a track still referenced by a sounding key.
        for(int i=0;i<32;++i) {
            bool occupied=false;
            for(auto &k:keys) if(k.used && k.track==i) occupied=true;
            if(!occupied && (best<0 || tracks[i].at<tracks[best].at)) best=i;
        }
    }
    if(best<0) return 255; // Dense polyphony: register analysis still applies.
    auto &t=tracks[best];
    int d=int(e.note)-t.note;
    t.confidence=continued ? std::min(cfg.max_confidence,unsigned(t.confidence)+1):0;
    t.direction=continued ? (d>0)-(d<0):0;
    t.note=e.note;t.at=e.timestamp;t.channel=e.channel;t.source=e.source;t.used=true;
    return uint8_t(best);
}
NoteSet Engine::process(Event e) {
    if(e.type==Type::Reset) {reset();return selected;}
    if(e.channel>15 || e.source>15 || e.note>127 || e.velocity>127) return selected;
    if(e.type==Type::On && !e.velocity) e.type=Type::Off;
    history[history_head++%128]=e;
    if(e.type==Type::Control) {
        if(e.note==64) sustain[e.source][e.channel]=e.velocity>=64;
        if(e.note==121) sustain[e.source][e.channel]=false;
        for(auto &k:keys) if(k.used && k.source==e.source && k.channel==e.channel) {
            if(e.note==120) k={};
            else {
                if(e.note==123) k.down=false;
                if(!k.down && !sustain[e.source][e.channel]) k={};
            }
        }
    } else {
        Key *key=nullptr;
        for(auto &k:keys) if(k.used && k.note==e.note && k.channel==e.channel && k.source==e.source) {key=&k;break;}
        if(e.type==Type::Off) {
            if(key) {key->down=false;if(!sustain[e.source][e.channel]) *key={};}
        } else if(e.type==Type::On) {
            if(key) key->down=false;
            uint8_t track=assign_track(e);
            if(!key) for(auto &k:keys) if(!k.used) {key=&k;break;}
            if(!key) {++overflow_count;reset();return selected;}
            *key={e.timestamp,e.note,e.channel,e.source,e.velocity,track,true,true};
        }
    }
    return choose(e.timestamp);
}
NoteSet Engine::choose(uint64_t now) {
    bool active[128]={}, down[128]={}, old[128]={};
    int velocity[128]={};
    std::fill(confidence,confidence+128,0);
    std::fill(groups,groups+128,-1);
    for(auto &k:keys) if(k.used) {
        active[k.note]=true;down[k.note]|=k.down;
        velocity[k.note]=std::max(velocity[k.note],int(k.velocity));
        if(k.track<32 && tracks[k.track].note==k.note)
            confidence[k.note]=std::max(confidence[k.note],unsigned(tracks[k.track].confidence));
    }
    for(unsigned i=0;i<selected.count;++i) old[selected.notes[i]]=true;
    int pitches[128],count=0;
    for(int n=0;n<128;++n) if(active[n]) pitches[count++]=n;
    // Established lines are independent even inside the accompaniment register.
    uint16_t masks[128]={};int sizes[128]={},first[128]={},last=-128,g=-1;
    for(int i=0;i<count;++i) {
        int n=pitches[i];
        if(confidence[n]>=cfg.line_threshold) continue;
        if(g<0 || n-last>=cfg.group_gap || n-first[g]>cfg.group_span) {++g;first[g]=n;}
        groups[n]=g;masks[g]|=1u<<(n%12);++sizes[g];last=n;
    }
    Harmony harmonies[128];
    for(int i=0;i<=g;++i) harmonies[i]=recognize(masks[i]);
    int score[128]={};
    for(int i=0;i<count;++i) {
        int n=pitches[i],group=groups[n],s=velocity[n]/std::max(1,cfg.velocity_divisor);
        if(confidence[n]>=cfg.line_threshold) s+=cfg.melody+int(confidence[n])*cfg.confidence_bonus+(n<cfg.bass_boundary?cfg.bass:0);
        if(group>=0) {
            if(sizes[group]==1) s+=cfg.independent;
            const auto &h=harmonies[group];
            if(h.root>=0) {
                int interval=(n%12-h.root+12)%12;
                if(!interval) s+=cfg.root;
                else if(interval==chords[h.kind].intervals[1]) s+=h.confidence==100?cfg.third:cfg.dyad_third;
                else if(chords[h.kind].count==4 && interval==chords[h.kind].intervals[3]) s+=cfg.seventh;
                else s+=cfg.fifth;
            } else if(n==first[group]) s+=cfg.root/2;
            for(int j=0;j<i;++j) if(groups[pitches[j]]==group && pitches[j]%12==n%12) {s-=cfg.duplicate;break;}
        }
        if(old[n]) s+=cfg.retained+int(std::min<uint64_t>(cfg.age,(now>=selected_at[n]?(now-selected_at[n])/std::max<uint64_t>(1,cfg.age_unit_us):0)));
        if(!down[n]) s-=cfg.released;
        score[n]=s;
    }
    // Score ties: existing note, then lower physical MIDI note. Output ascending.
    std::sort(pitches,pitches+count,[&](int a,int b){
        if(score[a]!=score[b]) return score[a]>score[b];
        if(old[a]!=old[b]) return old[a];
        return a<b;
    });
    NoteSet next;next.count=uint8_t(std::min(unsigned(count),cfg.voices));
    for(unsigned i=0;i<next.count;++i) {next.notes[i]=uint8_t(pitches[i]);if(!old[pitches[i]]) selected_at[pitches[i]]=now;}
    std::sort(next.notes,next.notes+next.count);selected=next;return next;
}
bool decode_usb(const uint8_t p[4],uint64_t t,Event &e) {
    uint8_t cin=p[0]&15,status=p[1]>>4;
    if(cin==15 && p[1]==0xff) {e={Type::Reset,0,0,0,t};return true;}
    if((cin!=8 && cin!=9 && cin!=11) || cin!=status || p[2]>127 || p[3]>127) return false;
    e={cin==8?Type::Off:cin==9?Type::On:Type::Control,uint8_t(p[1]&15),p[2],p[3],t,uint8_t(p[0]>>4)};
    if(e.type==Type::On && !e.velocity) e.type=Type::Off;
    return true;
}
bool find_endpoint(const uint8_t *d,size_t n,Endpoint &out) {
    if(n<9 || d[0]<9 || d[1]!=2) return false;
    size_t total=d[2]|(unsigned(d[3])<<8);
    if(total>n || total<9) return false;
    bool midi=false,version=false;uint8_t iface=0,alt=0;
    for(size_t i=0;i+2<=total;) {
        size_t len=d[i];if(len<2 || i+len>total) return false;
        if(d[i+1]==4) {
            if(len<9) return false;
            iface=d[i+2];alt=d[i+3];midi=d[i+5]==1 && d[i+6]==3 && d[i+7]==0;version=false;
        } else if(midi && d[i+1]==0x24 && len>=5 && d[i+2]==1) {
            version=d[i+3]==0 && d[i+4]==1;
        } else if(midi && version && d[i+1]==5 && len>=7 && (d[i+2]&0x80) && (d[i+3]&3)==2) {
            uint16_t size=d[i+4]|(uint16_t(d[i+5])<<8);
            if(!size || size>64 || size%4) return false;
            out={iface,alt,d[i+2],size};return true;
        }
        i+=len;
    }
    return false;
}
}
