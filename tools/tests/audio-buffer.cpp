#include "audio-buffer.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <initializer_list>

void check(bool ok) { if (!ok) std::abort(); }
int main() {
    sf3::AudioBuffer q;
    constexpr uint64_t ms=1000000,initial=1000000000;
    check(!q.playing && !q.drained(0,initial));
    check(!q.ready(0) && !q.ready(q.lead-1));
    q.submitted(0,q.lead,initial);check(q.ready(q.lead));
    check(!q.drained(0,initial+1) && !q.drained(0,initial+120*ms));
    check(q.drained(0,initial+120*ms+1) && !q.playing && q.underruns==1);
    check(!q.drained(0,initial+200*ms));
    check(!q.ready(q.lead-1));q.submitted(0,q.lead,initial+200*ms);check(q.ready(q.lead));
    check(q.drained(0,initial+320*ms+1) && q.target_lead==q.recovery_lead && q.underruns==2);
    check(!q.ready(q.recovery_lead-1));q.submitted(0,q.recovery_lead,initial+400*ms);check(q.ready(q.recovery_lead));
    check(q.drained(0,initial+570*ms+1) && q.underruns==3);
    q.reset(1);check(q.target_lead==q.lead && q.underruns==0 && !q.submission_known);
    // Device read-ahead can empty the input queue before audible output ends.
    for(int speed=1;speed<=3;++speed) {
        q.reset(speed);uint64_t now=initial;
        q.submitted(0,q.lead*speed,now);check(q.ready(q.lead*speed));
        for(int chunk:{128,441,736,1024})for(int cycle=0;cycle<1000;++cycle) {
            now+=uint64_t(chunk)*1000000000/(44100*speed);
            check(!q.drained(0,now));q.submitted(0,chunk,now);
            check(q.playing && q.underruns==0);
        }
        check(!q.drained(0,now-1));
        check(!q.drained(-1,now+1000*ms));
        check(q.drained(0,now+1000*ms) && !q.drained(0,now+2000*ms));
        check(q.fits(q.maximum*speed-1,1) && !q.fits(q.maximum*speed,1));
    }
    q.reset(1);q.device_margin_ns=80*ms;
    q.submitted(0,q.lead,initial);check(q.ready(q.lead));
    check(!q.drained(0,initial+180*ms) && q.drained(0,initial+180*ms+1));
    // Elapsed-time comparison must not wrap an absolute deadline.
    q.reset(1);q.submitted(0,q.lead,UINT64_MAX-100*ms);check(q.ready(q.lead));
    check(!q.drained(0,UINT64_MAX));
    q.reset(1);check(!q.ready(q.lead-1));q.submitted(-1,441,initial);check(!q.submission_known);
    check(q.fits(10000,1025) && !q.fits(10000,1026));
    check(!q.fits(-1,1) && !q.fits(0,0) && !q.fits(0,11026));
    check(!q.fits(INT_MAX,1) && !q.fits(INT_MIN,1) && !q.fits(0,INT_MAX));
    for(int before=1;before<=3;++before)for(int after=1;after<=3;++after) {
        q.reset(before);q.submitted(0,q.lead*before,initial);check(q.ready(q.lead*before));
        q.reset(after);check(!q.playing && !q.submission_known && q.speed==after);
        q.submitted(0,q.lead*after,initial);check(q.ready(q.lead*after));
        check(!q.drained(0,initial+100*ms));
    }
    std::puts("PASS: 12000 timely empty-queue refills, genuine late refill, device read-ahead, clock boundaries, speed resets and bounded queue");
}
