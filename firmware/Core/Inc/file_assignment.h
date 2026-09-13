#ifndef FILE_ASSIGNMENT_H
#define FILE_ASSIGNMENT_H
/* Stable file allocation mirrors desktop allocator.place, including unisons.
 * A key instance is distinct from its pitch and may need a separate motor. */
static uint32_t file_motor_cost(unsigned key,unsigned motor,const uint8_t *trial) {
    const MusicKey *n=&music.keys[key];
    int distance=music.last_pitch[motor]==255?0:(int)music.last_pitch[motor]-n->note;
    if(distance<0)distance=-distance;
    return (music.instance_assignment[motor]!=n->order?1u<<24:0) |
        (trial[motor]!=255?1u<<23:0) |
        (music.assignment[motor]!=n->note?1u<<22:0) | (uint32_t)(distance*8+motor);
}
static int file_place(unsigned key,uint8_t *trial,uint8_t *visited) {
    uint8_t ordered[6],count=0;
    for(unsigned m=0;m<6;++m)if((music.mask&(1u<<m))&&(!music.keys[key].pin||music.keys[key].pin==m+1)) {
        unsigned at=count++;
        while(at&&file_motor_cost(key,m,trial)<file_motor_cost(key,ordered[at-1],trial)) {
            ordered[at]=ordered[at-1];--at;
        }
        ordered[at]=(uint8_t)m;
    }
    for(unsigned i=0;i<count;++i) {
        unsigned m=ordered[i];if(*visited&(1u<<m))continue;
        *visited|=(uint8_t)(1u<<m);
        unsigned old=trial[m];
        if(old==255||(!music.keys[old].pin&&file_place(old,trial,visited))) {
            trial[m]=(uint8_t)key;return 1;
        }
    }
    return 0;
}
static void file_stable_assignment(uint8_t *notes,uint32_t *instances) {
    uint8_t chosen[6],seen[128]={0};unsigned count=0;
    memset(chosen,255,6);
    for(unsigned attempt=0;attempt<128&&count<music.voices;++attempt) {
        int best=-1;
        for(unsigned i=0;i<128;++i)if(music.keys[i].flags&&!seen[i]) {
            const MusicKey *a=&music.keys[i],*b=best<0?a:&music.keys[best];
            if(best<0||!!a->pin>!!b->pin|| (!!a->pin==!!b->pin&&
               (a->velocity>b->velocity||(a->velocity==b->velocity&&
               (a->at<b->at||(a->at==b->at&&a->order<b->order))))))best=(int)i;
        }
        if(best<0)break;
        seen[best]=1;uint8_t trial[6],visited=0;memcpy(trial,chosen,6);
        if(file_place((unsigned)best,trial,&visited)){memcpy(chosen,trial,6);++count;}
    }
    for(unsigned m=0;m<6;++m) {
        notes[m]=chosen[m]==255?255:music.keys[chosen[m]].note;
        instances[m]=chosen[m]==255?0:music.keys[chosen[m]].order;
    }
}
#endif
