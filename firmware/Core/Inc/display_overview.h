#ifndef DISPLAY_OVERVIEW_H
#define DISPLAY_OVERVIEW_H
#include <stdint.h>

typedef struct {
    uint32_t mhz;
    uint8_t flags, note;
} DisplayMotor;
typedef struct {
    DisplayMotor motors[6];
    uint32_t started[6], off[6];
    uint8_t was_active[6];
} DisplayOverview;

/* Minimum visual lifetime is 200 ms from note onset, not an off-delay.
 * Confirmed short gaps still count as active. STEP/history never use this state. */
static void display_overview_update(DisplayOverview *view, const DisplayMotor *motors,
        uint32_t now, uint8_t have_state, uint8_t flags, uint8_t sleeping,
        uint8_t resetting, uint8_t mask) {
    for(unsigned i=0;i<6;++i) {
        int active=!!(motors[i].flags&2)||((flags&64)&&(motors[i].flags&8));
        int clear=!have_state||!(flags&1)||sleeping||resetting||
                  !(mask&(1u<<i))||(flags&4);
        if(!(flags&(32|64))) {
            view->motors[i]=motors[i];
            view->motors[i].flags&=7;
            if((flags&64)&&(motors[i].flags&8))view->motors[i].flags|=2;
            if(clear)view->motors[i].flags=0;
            view->was_active[i]=0;
        } else if(clear) {
            view->motors[i]=motors[i];view->motors[i].flags=0;
            view->was_active[i]=0;
        } else if(active) {
            if(!view->was_active[i]||view->motors[i].note!=motors[i].note)
                view->started[i]=now;
            view->motors[i]=motors[i];view->motors[i].flags=(view->motors[i].flags&7)|2;
            view->was_active[i]=1;
        } else {
            if(view->was_active[i]) {
                view->off[i]=now;
                if((int32_t)(now-view->started[i])<200)
                    view->off[i]=view->started[i]+200u;
            }
            view->was_active[i]=0;
            if((int32_t)(now-view->started[i])>=200)
                view->motors[i].flags&=(uint8_t)~2u;
        }
    }
}
#endif
