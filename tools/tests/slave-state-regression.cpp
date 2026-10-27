// Synthetic slave wait loop and native-state snapshots; no disc or game-generated inputs.
// A game's slave waits in a loop that reads its FTCSR and counts its passes in work RAM
// (SFIII's slave keeps such a counter). Taking a snapshot must park that slave where it waits,
// without running another pass, or every run that saves goes on with different RAM.
#include "saturn.h"
#include "test.h"
#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint32_t kEntry = 0x06004000u, kCounter = 0x06005000u;

void slave_wait(SH2Context& c) {
    for (;;) {
        SH2_STATE_POINT(c, kEntry);
        SH2_CYC(c, 4, 2);
        const uint32_t ftcsr = ld8(0xFFFFFE11u);             // mov.b @(FTCSR),r0
        st32(kCounter, ld32(kCounter) + 1);                   // the pass counter
        if (ftcsr & 0x80u) break;                             // tst #0x80; bt loop
        SH2_SAFE(c);
    }
    for (;;) { SH2_STATE_POINT(c, kEntry + 8); SH2_CYC(c, 1, 0); SH2_SAFE(c); }
}

uint8_t g_ran[1];
const uint16_t g_sizes[1] = {8};
const SH2FuncEntry g_funcs[] = {{kEntry, slave_wait}};
const SH2Module g_module = {"SLAVEWAIT", kEntry - 0x10, 0x20, 0, g_funcs, 1, g_ran, g_sizes};
}  // namespace

extern const SH2Module* const g_sh2_modules[] = {&g_module};
extern const int g_sh2_nmodules = 1;
extern const uint32_t g_sh2_tasks[2] = {0, 0};
extern const SH2TaskSwitch g_sh2_task_switches[] = {{0, 0, 0, 0, 0}};
extern const int g_sh2_ntask_switches = 0;
extern const bool g_sh2_native_states = true;

// The slave's thread still waits for its turn when main returns, and destroying the turn's
// condition variable under it would not return: leave without static destructors.
static bool g_passed = true;
static void leave() { std::fflush(nullptr); std::_Exit(g_passed ? 0 : 1); }

TEST(a_snapshot_parks_an_idle_slave_without_another_pass_of_its_wait) {
    std::atexit(leave);
    sh2_activate(&g_module);
    st32(0x06000250u, kEntry);
    st32(kCounter, 0);
    slave_on();                                               // boots and runs to its first wait
    const uint32_t waited = ld32(kCounter);
    CHECK_EQ(waited, 1);
    g_passed = waited == 1;
    for (int save = 0; save < 3; ++save) {                    // save, and save again
        g_state_requested = true;                             // a save is wanted (state_poll)
        const bool parked = machine_state_quiesce_slave();
        CHECK_EQ(parked, true);
        CHECK_EQ(ld32(kCounter), waited);
        g_passed = g_passed && parked && ld32(kCounter) == waited;
    }
    g_state_requested = false;
    slave_kick();                                             // SINIT: it goes on from where it waited
    CHECK_EQ(ld32(kCounter), waited + 1);
    g_passed = g_passed && ld32(kCounter) == waited + 1;
}
