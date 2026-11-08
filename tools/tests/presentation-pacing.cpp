#include "presentation-pacing.h"
#include <cstdlib>
#include <cstdio>
#include <limits>
static void check(bool ok){if(!ok)std::abort();}
int main() {
    constexpr uint64_t field=16715228;
    for(int speed=1;speed<=3;++speed) {
        const int64_t limit=field/speed;
        check(!sf3::catch_up_repeated_field(true,true,-limit,field,speed));
        check(!sf3::catch_up_repeated_field(true,true,0,field,speed));
        check(!sf3::catch_up_repeated_field(true,true,limit,field,speed));
        check(sf3::catch_up_repeated_field(true,true,limit+1,field,speed));
        check(!sf3::catch_up_repeated_field(false,true,limit+1,field,speed));
        check(!sf3::catch_up_repeated_field(true,false,limit+1,field,speed));
    }
    check(!sf3::catch_up_repeated_field(true,true,100,0,1));
    check(!sf3::catch_up_repeated_field(true,true,100,field,0));
    check(!sf3::catch_up_repeated_field(true,true,100,field,4));
    check(!sf3::catch_up_repeated_field(true,true,std::numeric_limits<int64_t>::min(),field,1));
    check(sf3::catch_up_repeated_field(true,true,std::numeric_limits<int64_t>::max(),field,1));
    // 30 Hz world frames on a 60 Hz field clock: catch up on duplicates,
    // always preserve each newly drawn world frame.
    int64_t debt=0;unsigned omitted=0;
    for(int i=0;i<1200;++i) {
        debt+=1000000;
        const bool duplicate=i%2;
        if(sf3::catch_up_repeated_field(true,duplicate,debt,field,1)) {
            check(duplicate);debt-=field-1000000;++omitted;
        }
    }
    check(omitted>0 && debt<2*int64_t(field));
    std::puts("PASS: repeated-field catch-up, new-frame retention, speed, clock boundaries and sustained presentation debt");
}
