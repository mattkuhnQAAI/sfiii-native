#include "vertex-precision.h"
#include "saturn.h"
#include "video.h"
#include <algorithm>
#include <cstring>

namespace {
bool enabled=false;
sf3::VertexCommandCache commands;
sf3::PrecisionQuad pending{};uint32_t pending_address=0;
uint64_t last_field=0;
sf3::VertexPrecisionStats stats;
struct ProducerCode {
    std::array<uint8_t,280> formatter{};
    std::array<uint8_t,768> projection{};
    std::array<uint8_t,32> queue{};
    const sf3::VertexProducerProfile* profile=nullptr;
} verified;
bool ram(uint32_t address,unsigned size) {
    return SH2_IS_WRAM_H(address) && (address&0xFFFFF)<=0x100000-size;
}
void reset(){commands.clear();pending_address=0;last_field=sat_vblanks();verified.profile=nullptr;}
const sf3::VertexProducerProfile* producer_profile() {
    // Only locally reviewed BOOT revisions are enabled. Other editions
    // require independent producer/caller review; matching addresses alone fail.
    static const sf3::VertexProducerProfile* profile=[]()->const sf3::VertexProducerProfile* {
        for(int i=0;i<g_sh2_nmodules;++i) {
            const auto* m=g_sh2_modules[i];
            if(m && !std::strcmp(m->name,"BOOT"))
                if(auto* found=sf3::vertex_producer_profile(m->base,m->size,m->crc))return found;
        }
        return nullptr;
    }();
    return profile;
}
bool verified_producer(const sf3::VertexProducerProfile& profile) {
    if(!ram(profile.formatter,280) || !ram(profile.projection,profile.projection_size) ||
       profile.projection_size>verified.projection.size() || !ram(profile.queue,32))return false;
    const auto* formatter=g_wram_h+(profile.formatter&0xfffff);
    const auto* projection=g_wram_h+(profile.projection&0xfffff);
    const auto* queue=g_wram_h+(profile.queue&0xfffff);
    // Keep checking every byte on every producer call. Once identified, an
    // exact comparison avoids hashing the same resident instructions per quad.
    if(verified.profile==&profile)
        return !std::memcmp(formatter,verified.formatter.data(),280) &&
               !std::memcmp(projection,verified.projection.data(),profile.projection_size) &&
               !std::memcmp(queue,verified.queue.data(),32);
    uint64_t hash=14695981039346656037ull;
    for(uint32_t a=profile.formatter;a<profile.formatter+280;++a){hash^=ld8(a);hash*=1099511628211ull;}
    if(hash!=0x888056474C98BA8Dull)return false;
    hash=14695981039346656037ull;
    for(uint32_t a=profile.projection;a<profile.projection+profile.projection_size;++a){hash^=ld8(a);hash*=1099511628211ull;}
    if(hash!=profile.projection_hash)return false;
    hash=14695981039346656037ull;
    for(uint32_t a=profile.queue;a<profile.queue+32;++a){hash^=ld8(a);hash*=1099511628211ull;}
    if(hash!=profile.queue_hash)return false;
    std::copy_n(formatter,280,verified.formatter.begin());
    std::copy_n(projection,profile.projection_size,verified.projection.begin());
    std::copy_n(queue,32,verified.queue.begin());verified.profile=&profile;
    return true;
}
}
namespace sf3 {
void vertex_precision_configure(bool value){enabled=value;reset();stats={};}
VertexPrecisionStats vertex_precision_stats(){return stats;}
}
void sf3_vertex_precision_reset(){reset();}
void sf3_vertex_precision_call(SH2Context& c,uint32_t address,bool returned) {
    if(!enabled)return;
    const auto* profile=producer_profile();if(!profile)return;
    if(address!=profile->queue && !sf3::vertex_formatter_entry(address,profile->formatter))return;
    if(!vdp1_gpu_active() || vdp1_render_scale()==1)return;
    if(returned) {
        // The post-lighting entry returns to a queue label in its caller,
        // rather than dispatching another sh2_call. Its command is now complete.
        if(address!=profile->formatter+20 || c.pc!=profile->queue || pending_address!=c.r[11])return;
        address=profile->queue;
    }
    const uint64_t field=sat_vblanks();if(field<last_field)reset();last_field=field;
    if(sf3::vertex_formatter_entry(address,profile->formatter)) {
        pending_address=0;
        if(c.pr!=profile->queue || !ram(c.gbr,920) || !ram(c.r[11],32))return;
        if(!verified_producer(*profile)){commands.clear();return;}
        sf3::PrecisionQuad q{};
        for(int i=0;i<4;++i) {
            const uint32_t a=c.r[i+1];if(!ram(a,16)){++stats.rejected;return;}
            sf3::ProjectedPoint point{ld32(a),int32_t(ld32(a+4)),int32_t(ld32(a+8)),int32_t(ld32(a+12))};
            std::array<float,2> recovered;
            if(!sf3::recover_projected_point(point,int32_t(ld32(c.gbr+108)),ld8(c.gbr+173),recovered)){++stats.rejected;return;}
            q.packed[i]=point.packed;q.precise[i*2]=recovered[0];q.precise[i*2+1]=recovered[1];
        }
        pending=q;pending_address=c.r[11];
    } else if(address==profile->queue && pending_address) {
        const uint32_t a=pending_address;pending_address=0;
        if(a!=c.r[11] || !ram(a,32))return;
        uint16_t words[16];for(int i=0;i<16;++i)words[i]=uint16_t(ld16(a+i*2));
        if((words[0]&15)!=2)return;
        sf3::PrecisionQuad ordered{};
        std::array<uint32_t,4> corners{};
        for(int i=0;i<4;++i)corners[i]=uint32_t(words[6+i*2])<<16|words[7+i*2];
        if(!sf3::order_precision_quad(pending,corners,ordered)){++stats.rejected;return;}
        commands.store(sf3::vertex_command_key(words),ordered,field);
        ++stats.captured;
    }
}
bool sf3_vertex_precision_quad(const uint8_t* cmd,float* coordinates) {
    if(!enabled || !vdp1_gpu_active() || vdp1_render_scale()==1)return false;
    uint16_t words[16];for(int i=0;i<16;++i)words[i]=uint16_t(cmd[2*i])*256+cmd[2*i+1];
    if((words[0]&15)!=2)return false;
    const uint64_t field=sat_vblanks();
    const auto* found=commands.find(sf3::vertex_command_key(words),field);
    if(!found)return false;
    const auto& q=*found;
    float corrected[8];for(int i=0;i<4;++i) {
        corrected[i*2]=coordinates[i*2]+q.precise[i*2]-int16_t(q.packed[i]>>16);
        corrected[i*2+1]=coordinates[i*2+1]+q.precise[i*2+1]-int16_t(q.packed[i]);
    }
    float sign=0;
    for(int i=0;i<4;++i) {
        int j=(i+1)%4,k=(i+2)%4;
        const float cross=(corrected[j*2]-corrected[i*2])*(corrected[k*2+1]-corrected[j*2+1])-
                          (corrected[j*2+1]-corrected[i*2+1])*(corrected[k*2]-corrected[j*2]);
        if(std::abs(cross)<0.01f || (sign && cross*sign<0))return false;sign=cross;
    }
    std::copy(corrected,corrected+8,coordinates);++stats.applied;return true;
}
