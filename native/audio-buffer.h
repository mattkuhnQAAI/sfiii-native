#pragma once
#include <cstdint>

namespace sf3 {
// Prime with generated PCM before starting/resuming the device. Never splice
// synthetic silence between game chunks to refill an empty playback queue.
struct AudioBuffer {
    static constexpr int lead = 4410; // 100 ms at 44.1 kHz
    static constexpr int recovery_lead = 6615; // 150 ms after repeated underruns
    static constexpr int maximum = 11025;
    bool playing = false;
    int speed = 1; // Host playback ratio; machine PCM stays at 44.1 kHz.
    int target_lead = lead;
    unsigned underruns = 0;
    uint64_t device_margin_ns = 20000000; // Device read-ahead; input queue != audible output.
    uint64_t last_submission_ns = 0, buffered_ns = 0;
    bool submission_known = false;

    void reset(int ratio) { playing = false; speed = ratio; target_lead = lead; underruns = 0; submission_known = false; }

    void submitted(int queued,int frames,uint64_t now_ns) {
        if(queued<0 || frames<=0 || speed<1 || speed>3)return;
        last_submission_ns=now_ns;
        buffered_ns=(uint64_t(queued)+uint64_t(frames))*1000000000ull/(44100u*unsigned(speed));
        submission_known=true;
    }
    bool drained(int queued,uint64_t now_ns) {
        // SDL can consume the input queue in a device-sized batch while that
        // batch is still playing. Only re-prime after a genuinely late refill.
        if (!playing || queued != 0 || !submission_known || now_ns<last_submission_ns ||
            now_ns-last_submission_ns<=buffered_ns+device_margin_ns) return false;
        playing = false;
        if(underruns<3)++underruns;
        if(underruns==2)target_lead=recovery_lead;
        return true;
    }
    bool ready(int queued) {
        if (playing || queued < target_lead * speed) return false;
        playing = true;
        return true;
    }
    bool fits(int queued, int incoming) const {
        return queued >= 0 && incoming > 0 && incoming <= maximum && queued <= maximum * speed - incoming;
    }
};
}
