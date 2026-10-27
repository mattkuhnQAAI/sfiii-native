// Synthetic regression: a native state taken after an overlay was loaded over an active module, but
// before the first call into it, must restore the dispatch exactly as the live run has it. No game data:
// two invented 64-byte images share one base, each with one entry.
#include "saturn.h"
#include "state.h"
#include "test.h"
#include <cstring>

static int g_ran;
static void entry_a(SH2Context&) { g_ran = 1; }
static void entry_b(SH2Context&) { g_ran = 2; }

static const uint32_t kBase = 0x06088000, kSize = 64;
static uint8_t g_image_a[kSize], g_image_b[kSize];
static const SH2FuncEntry kFuncsA[] = {{kBase + 4, entry_a}};
static const SH2FuncEntry kFuncsB[] = {{kBase + 4, entry_b}};
static SH2Module g_a{"SYNTH_A", kBase, kSize, 0, kFuncsA, 1, nullptr, nullptr, true};
static SH2Module g_b{"SYNTH_B", kBase, kSize, 0, kFuncsB, 1, nullptr, nullptr, true};

extern const SH2Module* const g_sh2_modules[] = {&g_a, &g_b};
extern const int g_sh2_nmodules = 2;
extern const uint32_t g_sh2_tasks[2] = {0, 0};
extern const SH2TaskSwitch g_sh2_task_switches[] = {{0, 0, 0, 0, 0}};
extern const int g_sh2_ntask_switches = 0;

static void load_image(const uint8_t* image) { std::memcpy(g_wram_h + (kBase - 0x06000000), image, kSize); }

static int call_entry() {
    g_ran = 0;
    SH2Context c{};
    if (SH2Func f = sh2_lookup(kBase + 4)) f(c);
    return g_ran;
}

static void setup() {
    for (uint32_t i = 0; i < kSize; ++i) { g_image_a[i] = uint8_t(i * 7 + 1); g_image_b[i] = uint8_t(i * 13 + 5); }
    g_a.crc = sh2_crc32(g_image_a, kSize);
    g_b.crc = sh2_crc32(g_image_b, kSize);
    for (int i = 0; i < sh2_active_slots(); ++i) sh2_set_active_slot(i, nullptr);
    load_image(g_image_a);
    sh2_activate(&g_a);
}

// The live run: A active, B loaded over it, then a call goes to B's code.
TEST(live_call_after_overlay_load_runs_the_loaded_module) {
    setup();
    CHECK_EQ(call_entry(), 1);
    load_image(g_image_b);
    CHECK_EQ(call_entry(), 2);
}

// The same moment through a native state: saved after B is loaded, before any call into it.
TEST(restored_call_after_overlay_load_runs_the_loaded_module) {
    setup();
    CHECK_EQ(call_entry(), 1);
    load_image(g_image_b);
    State out;
    core_state(out);
    core_heads_state(out);
    // A fresh process: nothing active, power-on RAM.
    for (int i = 0; i < sh2_active_slots(); ++i) sh2_set_active_slot(i, nullptr);
    std::memset(g_wram_h, 0, 0x100000);
    State in;
    in.loading = true;
    in.data = out.data;
    core_state(in);
    core_heads_state(in);
    CHECK_EQ(in.pos, in.data.size());
    CHECK_EQ(call_entry(), 2);
}
