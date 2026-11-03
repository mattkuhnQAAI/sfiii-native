#pragma once

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

    void reset(int ratio) { playing = false; speed = ratio; target_lead = lead; underruns = 0; }

    bool drained(int queued) {
        if (!playing || queued != 0) return false;
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
