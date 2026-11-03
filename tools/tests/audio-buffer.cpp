#include "audio-buffer.h"
#include <cstdio>
#include <cstdlib>
#include <climits>

void check(bool ok) { if (!ok) std::abort(); }
int main() {
    sf3::AudioBuffer q;
    check(!q.playing && !q.drained(0));
    check(!q.ready(0) && !q.ready(4409));
    check(q.ready(4410) && q.playing);
    check(!q.ready(4410) && !q.drained(1));
    check(q.drained(0) && !q.playing);
    check(!q.drained(0) && !q.ready(2205));
    check(q.ready(4500));
    check(q.drained(0) && q.target_lead==q.recovery_lead && q.underruns==2);
    check(!q.ready(q.recovery_lead-1) && q.ready(q.recovery_lead));
    check(q.drained(0) && q.target_lead==q.recovery_lead && q.underruns==3);
    check(q.ready(q.recovery_lead) && q.drained(0) && q.underruns==3);
    q.reset(1);check(q.target_lead==q.lead && q.underruns==0);
    check(q.fits(10000, 1025) && !q.fits(10000, 1026));
    check(!q.fits(-1, 1) && !q.fits(0, 0) && !q.fits(0, 11026));
    check(!q.fits(INT_MAX,1) && !q.fits(INT_MIN,1) && !q.fits(0,INT_MAX));
    check(q.fits(0,q.maximum) && q.fits(q.maximum-1,1) && !q.fits(q.maximum,1));
    for(int cycle=0;cycle<1000;++cycle) {
        q.reset(1);int queued=0;
        const int chunk=cycle%3==0 ? 128 : cycle%3==1 ? 512 : 1024;
        while(queued<q.lead) {
            check(!q.ready(queued));check(q.fits(queued,chunk));queued+=chunk;
        }
        check(q.ready(queued) && !q.ready(queued));
        while(queued>0) {
            check(!q.drained(queued));queued=queued>137 ? queued-137 : 0;
        }
        check(q.drained(0) && !q.drained(0) && !q.ready(0));
    }
    for(int before=1;before<=3;++before)for(int after=1;after<=3;++after) {
        q.reset(before);check(q.ready(q.lead*before));
        q.reset(after);check(!q.playing && q.speed==after);
        check(!q.ready(q.lead*after-1));check(q.ready(q.lead*after));
        check(q.fits(q.maximum*after-1,1) && !q.fits(q.maximum*after,1));
        check(q.drained(0) && !q.ready(0));
        check(q.ready(q.lead*after) && q.drained(0));
        check(!q.ready(q.recovery_lead*after-1) && q.ready(q.recovery_lead*after));
        q.reset(after);check(q.target_lead==q.lead && q.underruns==0);
    }
    std::puts("PASS: startup priming, refill, stable playback, bounded queue");
}
