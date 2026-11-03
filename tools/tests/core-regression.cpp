// Synthetic modules only: no Saturn game code or data.
#include "saturn/sh2.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>

// Snapshot module-table interface, implemented in core.cpp.
int sh2_active_slots();
void sh2_set_active_slot(int, const SH2Module*);

static SH2Module modules[19];
static SH2FuncEntry entries[19][2];
extern const SH2Module* const g_sh2_modules[] = {
    &modules[0], &modules[1], &modules[2], &modules[3], &modules[4], &modules[5],
    &modules[6], &modules[7], &modules[8], &modules[9], &modules[10], &modules[11], &modules[12], &modules[13], &modules[14],
    &modules[15], &modules[16], &modules[17], &modules[18]};
extern const int g_sh2_nmodules = 19;
static int calls, starts;
static void entry(SH2Context&) { ++calls; }
static void replacement(SH2Context&) { calls += 100; }
static void check(bool yes, const char* why) { if (!yes) { std::fprintf(stderr, "%s\n", why); std::exit(1); } }
void sh2_program_start(SH2Context&, uint32_t) { ++starts; }
void sh2_call_unknown(SH2Context&, uint32_t) { check(false, "unknown call"); }
uint32_t sh2_mmio_read(uint32_t, int) { return 0; }
void sh2_mmio_write(uint32_t, uint32_t, int) {}

int main() {
    // Work RAM aliases must agree on byte order and remain independent.
    for(uint32_t base : {0x00200000u,0x06000000u}) {
        for(uint32_t offset : {0u,0x1234u,0xFFFFCu}) {
            st32(base+offset,0x12345678);
            check(ld8(base+offset)==0x12 && ld16(base+offset+2)==0x5678,"big-endian RAM widths");
            check(ld32((base|0x20000000)+offset)==0x12345678,"cache-through alias reads same RAM");
            st16((base|0x20000000)+offset,0xABCD);
            check(ld32(base+offset)==0xABCD5678,"cache-through stores share underlying RAM");
        }
    }
    st32(0x00200000,0x13572468);st32(0x06000000,0x24681357);
    check(ld32(0x00200000)==0x13572468 && ld32(0x06000000)==0x24681357,"high and low work RAM must be independent");
    st32(0x061FFFFC,0xF1E2D3C4);
    check(ld32(0x060FFFFC)==0xF1E2D3C4 && ld32(0x27FFFFFC)==0xF1E2D3C4,"high RAM mirrors agree at final aligned word");
    uint8_t across[8]={1,2,3,4,5,6,7,8},readback[8]{};
    check(sh2_mem_write(0x060FFFFC,across,8) && sh2_mem_read(0x260FFFFC,readback,8),"bulk RAM access across physical high-RAM mirror");
    for(int i=0;i<8;++i) check(readback[i]==across[i],"bulk mirror must retain every byte");
    check(!sh2_mem_read(0x00100000,readback,1),"unmapped bulk read rejected");
    for (unsigned i = 0; i < 12; ++i) {
        auto base = 0x06010000u + i * 0x100;
        uint8_t data[80];
        for (unsigned j = 0; j < sizeof data; ++j) data[j] = static_cast<uint8_t>(i + j);
        check(sh2_mem_write(base, data, sizeof data), "write RAM");
        entries[i][0] = {base, entry};
        entries[i][1] = {base + 8, entry};
        modules[i] = {"synthetic", base, sizeof data, sh2_crc32(data, sizeof data), entries[i], 2, nullptr, nullptr, true};
    }
    uint8_t newer[8] = {90,91,92,93,94,95,96,97};
    entries[12][0] = {modules[0].base + 8, replacement};
    modules[12] = {"replacement", modules[0].base + 8, 8, sh2_crc32(newer, 8), entries[12], 1, nullptr, nullptr, true};
    uint8_t alternate[80]={42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57};
    entries[13][0]={modules[1].base,replacement};
    entries[13][1]={modules[1].base+8,replacement};
    modules[13]={"same-base-alternate",modules[1].base,80,sh2_crc32(alternate,80),entries[13],2,nullptr,nullptr,true};
    // A smaller overlay may replace only an interior region, leaving the old
    // header intact. Register both bases before dispatch builds its base cache.
    uint8_t parent_image[256], interior_image[16];
    for(unsigned j=0;j<256;++j)parent_image[j]=uint8_t(j*7+3);
    for(unsigned j=0;j<16;++j)interior_image[j]=uint8_t(j*11+91);
    for(int bank=0;bank<2;++bank) {
        int parent=15+bank*2,child=parent+1;
        uint32_t base=bank ? 0x00290000u : 0x06090000u;
        entries[parent][0]={base,entry};entries[parent][1]={base+128,entry};
        modules[parent]={"partial-parent",base,256,sh2_crc32(parent_image,256),entries[parent],2,nullptr,nullptr,true};
        entries[child][0]={base+128,replacement};entries[child][1]={base+136,replacement};
        modules[child]={"partial-child",base+128,16,sh2_crc32(interior_image,16),entries[child],2,nullptr,nullptr,true};
    }
    for (unsigned i = 0; i < 12; ++i) {
        auto& m = modules[i];
        check(sh2_identify(m.base) == &m, "exact image identification");
        sh2_activate(&m);
    }
    for (unsigned i = 0; i < 12; ++i) check(sh2_lookup(modules[i].base) == entry, "more than eight resident modules");
    uint8_t changed = 0xFF;
    check(sh2_mem_write(modules[0].base + 79, &changed, 1), "mutate synthetic data");
    check(sh2_identify(modules[0].base) == nullptr, "whole-image CRC must notice mutation");
    SH2Context cpu{};
    sh2_call(cpu, modules[0].base);
    check(calls == 1 && starts == 0, "active returning module must not become a program restart after data mutation");
    check(sh2_mem_write(modules[12].base, newer, 8), "load overlapping replacement");
    sh2_call(cpu, modules[12].base);
    check(calls == 101 && starts == 0, "exact replacement must take priority over an old interior entry");
    check(sh2_lookup(modules[0].base) == nullptr, "overlapping resident must deactivate");
    check(sh2_mem_write(modules[13].base,alternate,sizeof alternate),"replace overlay at same base");
    sh2_call(cpu,modules[13].base+8);
    check(calls==201 && starts==0,"interior call must identify replacement even when old overlay has that entry");
    check(sh2_lookup(modules[1].base)==replacement,"same-base stale entry must be replaced by the new image's entry");
    check(sh2_lookup(modules[13].base+8+0x20000000)==replacement,"native lookup through cache-through alias");
    for(int i=0;i<64;++i) {
        uint8_t original[80];for(unsigned j=0;j<80;++j) original[j]=uint8_t(1+j);
        check(sh2_mem_write(modules[1].base,original,80),"restore original overlay");
        sh2_call(cpu,modules[1].base+8);
        check(sh2_lookup(modules[1].base+8)==entry,"repeated original overlay activation");
        check(sh2_mem_write(modules[13].base,alternate,80),"restore alternate overlay");
        sh2_call(cpu,modules[13].base+8+0x20000000);
        check(sh2_lookup(modules[13].base+8)==replacement,"repeated alternate overlay activation");
    }
    // A restored snapshot names the currently active overlay. The next disc
    // load can replace it at the same entry address (e.g. battle/menu code).
    // Test both directions, including an identical first 64 bytes: a cached
    // header alone cannot identify which complete image was loaded.
    for(bool shared_header : {false,true}) for(uint32_t alias : {0u,0x20000000u}) {
        uint8_t original[80],next[80];
        for(unsigned j=0;j<80;++j) original[j]=uint8_t(1+j);
        std::memcpy(next,alternate,80);
        if(shared_header) std::memcpy(next,original,64);
        modules[13].crc=sh2_crc32(next,80);
        for(bool reverse : {false,true}) {
            const auto* old=reverse ? &modules[13] : &modules[1];
            const auto* fresh=reverse ? &modules[1] : &modules[13];
            sh2_mem_write(old->base,reverse ? next : original,80);
            for(int slot=0;slot<sh2_active_slots();++slot) sh2_set_active_slot(slot,nullptr);
            sh2_set_active_slot(0,old);
            sh2_mem_write(fresh->base,reverse ? original : next,80);
            int before=calls;
            sh2_call(cpu,fresh->base+alias);
            check(calls==before+(reverse ? 1 : 100) && starts==0,"restored overlay must dispatch newly loaded same-base image by full identity");
            check(sh2_lookup(fresh->base+8+alias)==(reverse ? entry : replacement),"both exports must follow replacement after snapshot restore");
        }
    }
    uint8_t unrecognized=0xF0;sh2_mem_write(modules[13].base,&unrecognized,1);
    check(sh2_lookup(modules[13].base+8)==nullptr,"an unrecognized changed overlay header must invalidate stale entries");
    sh2_activate(&modules[2]);
    modules[2].callable_entry=false;
    sh2_mem_write(modules[2].base,&unrecognized,1);
    check(sh2_lookup(modules[2].base+8)==entry,"a resident boot image may retain writable startup data in its header");
    check(sh2_lookup(0x060F1234)==nullptr,"unmapped function must not resolve spuriously");
    // A synthetic instruction-test image or another revision can have the same
    // entry address as real code. Address uniqueness is not image identity.
    entries[14][0]={0x06044800,replacement};
    uint8_t unrelated[16]={1,3,5,7,9,11,13,15};
    modules[14]={"unrelated-image",0x06044800,16,sh2_crc32(unrelated,16),entries[14],1,nullptr,nullptr,true};
    uint8_t actual[16]={2,4,6,8,10,12,14,16};sh2_mem_write(0x06044800,actual,16);
    bool exact=true;
    check(sh2_identify_containing(0x06044800,&exact)==nullptr && !exact,"unique entry in an unrecognized image must not activate unrelated compiled code");
    // Same-base alternatives retain table order, complete-image identity, and
    // freshly read memory on every search, even after an earlier miss.
    uint8_t original[80];for(unsigned j=0;j<80;++j)original[j]=uint8_t(1+j);
    modules[13].crc=sh2_crc32(alternate,80);
    sh2_mem_write(modules[1].base,alternate,80);exact=false;
    check(sh2_identify_containing(modules[1].base+8,&exact)==&modules[13] && exact,"later same-base image must be identified");
    sh2_mem_write(modules[1].base,original,80);exact=false;
    check(sh2_identify_containing(modules[1].base+8,&exact)==&modules[1] && exact,"memory replacement must invalidate prior search result");
    sh2_mem_write(modules[1].base+79,&unrecognized,1);exact=true;
    check(!sh2_identify_containing(modules[1].base+8,&exact) && !exact,"unknown full-image identity must remain unresolved");
    sh2_mem_write(modules[1].base,alternate,80);exact=false;
    check(sh2_identify_containing(modules[1].base+8,&exact)==&modules[13] && exact,"previous miss must not be cached across calls");
    // A shorter, earlier matching image wins base identification even when it
    // does not contain the requested address; do not select a later image.
    auto saved_first=modules[1];auto saved_second=modules[13];
    modules[1].size=16;modules[1].crc=sh2_crc32(original,16);
    modules[13].crc=sh2_crc32(original,80);sh2_mem_write(modules[1].base,original,80);exact=true;
    check(!sh2_identify_containing(modules[1].base+32,&exact) && !exact,"earlier short matching image must preserve base-identity ordering");
    modules[1]=saved_first;modules[13]=saved_second;
    for(int bank=0;bank<2;++bank)for(uint32_t alias:{0u,0x20000000u}) {
        int parent=15+bank*2,child=parent+1;
        check(sh2_mem_write(modules[parent].base,parent_image,256),"load partial parent");
        sh2_activate(&modules[parent]);
        check(sh2_lookup(modules[child].base)==entry,"parent initially owns interior call");
        check(sh2_mem_write(modules[child].base,interior_image,16),"load interior overlay past header");
        uint8_t head[64];sh2_mem_read(modules[parent].base,head,64);
        check(std::memcmp(head,parent_image,64)==0,"partial replacement must leave parent header unchanged");
        int before=calls;sh2_call(cpu,modules[child].base+alias);
        check(calls==before+100 && starts==0,"known interior overlay base must supersede unchanged parent header");
        check(sh2_lookup(modules[parent].base)==nullptr,"partial overlap must retire parent entries");
        check(sh2_lookup(modules[child].base+8+alias)==replacement,"interior replacement exports retain correct owner");
    }
    std::puts("PASS: 12 residents, data mutation, overlapping replacement, partial overlays past intact headers in both RAM banks, aliases and stale-entry invalidation");
}
