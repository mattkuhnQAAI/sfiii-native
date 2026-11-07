// Presentation-only reconstruction from retained fixed-point projection data.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>

namespace sf3 {
struct VertexProducerProfile {uint32_t size,crc,formatter,queue,projection;uint64_t projection_hash;};
inline const VertexProducerProfile* vertex_producer_profile(uint32_t base,uint32_t size,uint32_t crc) {
    static constexpr VertexProducerProfile profiles[]={
        {123940,0xCC57FA60,0x06016A38,0x06015FEC,0x06015E12,0xACC79B5D28DDFB5Cull},
        {123872,0x40B12530,0x060169F0,0x06015F9C,0x06015DC2,0x1DCE2733D8154C3Cull}
    };
    if(base!=0x06004000)return nullptr;
    for(const auto& profile:profiles)if(profile.size==size && profile.crc==crc)return &profile;
    return nullptr;
}
inline bool scenario2_vertex_formatter_entry(uint32_t address,uint32_t base=0x06016A38) {
    return address==base || address==base+12 || address==base+20;
}
struct ProjectedPoint { uint32_t packed; int32_t x,y,z; };
inline bool recover_projected_point(const ProjectedPoint& p,int32_t projection,unsigned near_shift,
                                    std::array<float,2>& result) {
    if(projection<=0 || near_shift>28 || p.z<=0)return false;
    const int32_t near=projection>>(near_shift+2);
    const int32_t depth=p.z<near?near:p.z;
    if(depth<=0)return false;
    const int64_t ratio=(int64_t(projection)*65536)/depth;
    if(ratio>std::numeric_limits<int32_t>::max())return false;
    // The producer replaces X bit zero with clip status. Dropping it leaves
    // at most one 16.16 unit of uncertainty, never an inferred whole pixel.
    const double xy[]={double(int64_t(p.x&~1)*ratio)/4294967296.0,
                       double(int64_t(p.y)*ratio)/4294967296.0};
    const int native[]={int16_t(p.packed>>16),int16_t(p.packed)};
    for(int c=0;c<2;++c) {
        if(!std::isfinite(xy[c]) || native[c]<-4096 || native[c]>4095 || std::floor(xy[c])!=native[c])return false;
        result[c]=float(xy[c]);
        // A float conversion at a boundary must not migrate to another cell.
        if(std::floor(result[c])!=native[c])return false;
    }
    return true;
}
using VertexCommandKey=std::array<uint16_t,13>;
struct PrecisionQuad {std::array<float,8> precise;std::array<uint32_t,4> packed;};
inline bool order_precision_quad(const PrecisionQuad& source,const std::array<uint32_t,4>& corners,PrecisionQuad& result) {
    PrecisionQuad ordered{};
    for(int i=0;i<4;++i) {
        int pick=-1;
        for(int j=0;j<4;++j)if(source.packed[j]==corners[i]) {
            if(pick>=0 && (source.precise[pick*2]!=source.precise[j*2] || source.precise[pick*2+1]!=source.precise[j*2+1]))return false;
            pick=j;
        }
        if(pick<0)return false;
        ordered.packed[i]=corners[i];ordered.precise[i*2]=source.precise[pick*2];ordered.precise[i*2+1]=source.precise[pick*2+1];
    }
    result=ordered;return true;
}
class VertexCommandCache {
    struct Entry {PrecisionQuad quad;uint64_t field;bool ambiguous=false;};
    std::map<VertexCommandKey,Entry> entries_;
public:
    void clear(){entries_.clear();}
    size_t size()const{return entries_.size();}
    void store(const VertexCommandKey& key,const PrecisionQuad& quad,uint64_t field) {
        auto found=entries_.find(key);
        if(found!=entries_.end() && found->second.field==field) {
            if(found->second.quad.precise!=quad.precise)found->second.ambiguous=true;
        } else {
            if(entries_.size()>=8192)entries_.clear();
            entries_[key]={quad,field,false};
        }
    }
    const PrecisionQuad* find(const VertexCommandKey& key,uint64_t field)const {
        auto found=entries_.find(key);
        if(found==entries_.end() || found->second.ambiguous || field<found->second.field || field-found->second.field>3)return nullptr;
        return &found->second.quad;
    }
};
inline VertexCommandKey vertex_command_key(const uint16_t* words) {
    VertexCommandKey key{};key[0]=words[0]&0x0FFF;
    for(int i=2;i<14;++i)key[i-1]=words[i];
    return key; // link, Gouraud address and unused word are not geometry identity.
}
void vertex_precision_configure(bool enabled);
struct VertexPrecisionStats {uint64_t captured=0,applied=0,rejected=0;};
VertexPrecisionStats vertex_precision_stats();
}
