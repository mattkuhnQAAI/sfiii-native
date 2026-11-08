#pragma once
#include <cstdint>

namespace sf3 {
// When presentation falls behind the audio clock, omit only a repeated VDP1
// field. The machine still executes that field, including all sound and input.
inline bool catch_up_repeated_field(bool paced,bool duplicate,int64_t late_ns,
                                    uint64_t field_ns,int speed) {
    return paced && duplicate && speed>=1 && speed<=3 && field_ns>0 &&
           late_ns>0 && uint64_t(late_ns)>field_ns/unsigned(speed);
}
}
