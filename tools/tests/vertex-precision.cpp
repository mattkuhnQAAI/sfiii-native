// Invented fixed-point geometry; no game assets or instruction bytes.
#include "vertex-precision.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
static void check(bool v,const char* why){if(!v){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static uint32_t packed(int x,int y){return uint32_t(uint16_t(x))<<16|uint16_t(y);}
int main() {
    unsigned cases=0;
    const auto* modern=sf3::vertex_producer_profile(0x06004000,123940,0xCC57FA60);
    const auto* legacy=sf3::vertex_producer_profile(0x06004000,123872,0x40B12530);
    check(modern && legacy && modern->formatter!=legacy->formatter && modern->queue!=legacy->queue,"each reviewed revision selects its own producer and return site");++cases;
    const auto* first=sf3::vertex_producer_profile(0x06004000,117672,0xEBEEDDFA);
    const auto* third=sf3::vertex_producer_profile(0x06004000,124272,0xA9FE4141);
    check(first && third && first->formatter==0x06017144 && third->queue==0x06015FE4,"Scenario 1 and 3 use reviewed independent profiles");++cases;
    for(auto* profile:{first,modern,legacy,third}) {
        check(!sf3::vertex_producer_profile(0x06004002,profile->size,profile->crc) && !sf3::vertex_producer_profile(0x06004000,profile->size+1,profile->crc) && !sf3::vertex_producer_profile(0x06004000,profile->size,profile->crc^1),"profile requires exact base, length and CRC");
        for(uint32_t delta:{0u,12u,20u})check(sf3::vertex_formatter_entry(profile->formatter+delta,profile->formatter),"revision-relative formatter entries");++cases;
    }
    check(!sf3::vertex_producer_profile(0x06004000,123940,0xA9FE4141) && !sf3::vertex_producer_profile(0x06004000,1,0),"unreviewed revisions remain disabled");++cases;
    for(uint32_t address:{0x06016A38u,0x06016A44u,0x06016A4Cu}) {
        check(sf3::vertex_formatter_entry(address,modern->formatter),"reviewed formatter entry accepted");
        check(!sf3::vertex_formatter_entry(address+1,modern->formatter) && !sf3::vertex_formatter_entry(address-2,modern->formatter),"nearby code is not a formatter entry");++cases;
    }
    check(!sf3::vertex_formatter_entry(0,modern->formatter) && !sf3::vertex_formatter_entry(0x26016A38,modern->formatter),"unreviewed entry/mirror rejected by producer selector");++cases;
    check(!sf3::vertex_formatter_entry(first->formatter,modern->formatter) && !sf3::vertex_formatter_entry(third->formatter,modern->formatter),"other-edition formatter addresses require their own profile");++cases;
    for(int sign:{-1,1})for(int fraction:{0,8192,32768,57344}) {
        const int x=sign*(2*65536+fraction),y=sign*(3*65536+fraction);
        sf3::ProjectedPoint point{packed(int(std::floor(x/65536.0)),int(std::floor(y/65536.0))),x,y,8*65536};
        std::array<float,2> out{};
        check(sf3::recover_projected_point(point,8*65536,2,out),"signed fractional reconstruction");
        check(out[0]==x/65536.0f && out[1]==y/65536.0f,"recover fractions, not a smoothing estimate");++cases;
    }
    std::array<float,2> out{};
    sf3::ProjectedPoint point{packed(0,-1),32769,-32768,65536};
    check(sf3::recover_projected_point(point,65536,2,out) && out[0]==.5f && out[1]==-.5f,"clip-status bit does not move geometry");++cases;
    point.packed=packed(2,-1);check(!sf3::recover_projected_point(point,65536,2,out),"unmatched integer projection rejected");++cases;
    point={packed(0,0),0,0,1};check(!sf3::recover_projected_point(point,INT32_MAX,28,out),"quotient overflow rejected");++cases;
    for(int z:{0,-1}){point.z=z;check(!sf3::recover_projected_point(point,65536,2,out),"invalid depth rejected");++cases;}
    point={packed(16,-16),65536,-65536,65536};
    check(sf3::recover_projected_point(point,32*65536,2,out) && out[0]==16 && out[1]==-16,"near-plane clamp matches integer producer");++cases;
    for(int projection:{0,-65536})check(!sf3::recover_projected_point(point,projection,2,out),"invalid projection rejected");
    check(!sf3::recover_projected_point(point,65536,29,out),"unsafe near shift rejected");cases+=3;
    for(int sign:{1,-1}) {
        point={packed(sign>0?450:-451,sign>0?500:-501),sign*(900*65536+32768),sign*(1000*65536+32768),65536};
        check(sf3::recover_projected_point(point,65536,2,out) && out[0]==sign*450.25f && out[1]==sign*500.25f,"range limiter preserves signed fractions when native cells agree");++cases;
    }
    point={packed(512,10),513*65536+32768,10*65536+32768,65536};
    check(!sf3::recover_projected_point(point,65536,2,out),"coarse range-limiter rounding must not migrate to another native cell");++cases;
    for(int sign:{1,-1}) {
        std::array<double,2> boundary{sign*512.0,sign*512.0};sf3::limit_projected_point(boundary);
        check(boundary[0]==sign*512.0 && boundary[1]==sign*512.0,"exact range boundary is stable");++cases;
    }
    uint16_t a[16]={2,123,0x1428,0,0x1000,0x818,1,2,3,4,5,6,7,8,0x3000,0};
    auto key=sf3::vertex_command_key(a);a[0]|=0x1000;a[1]=456;a[14]=0x5000;a[15]=1234;
    check(sf3::vertex_command_key(a)==key,"relocated links and lighting table do not alter geometry key");++cases;
    for(int i:{2,3,4,5,6,7,8,9,10,11,12,13}){a[i]^=1;check(sf3::vertex_command_key(a)!=key,"material or corner changes invalidate correspondence");a[i]^=1;++cases;}
    a[0]^=0x10;check(sf3::vertex_command_key(a)!=key,"texture orientation is part of identity");++cases;
    sf3::PrecisionQuad source{{1.25f,2.5f,3.75f,4.125f,5.5f,6.25f,7.125f,8.75f},{packed(1,2),packed(3,4),packed(5,6),packed(7,8)}};
    std::array<int,4> permutation{0,1,2,3};
    do {
        std::array<uint32_t,4> corners{};for(int i=0;i<4;++i)corners[i]=source.packed[permutation[i]];
        sf3::PrecisionQuad ordered{};check(sf3::order_precision_quad(source,corners,ordered),"quad permutation resolves");
        for(int i=0;i<4;++i)check(ordered.packed[i]==corners[i] && ordered.precise[i*2]==source.precise[permutation[i]*2] && ordered.precise[i*2+1]==source.precise[permutation[i]*2+1],"fraction follows its vertex through rotations and winding changes");++cases;
    }while(std::next_permutation(permutation.begin(),permutation.end()));
    auto corners=source.packed;corners[0]=packed(99,99);auto retained=source;
    check(!sf3::order_precision_quad(source,corners,retained) && retained.precise==source.precise && retained.packed==source.packed,"missing corner keeps destination unchanged");++cases;
    source.packed[1]=source.packed[0];corners=source.packed;
    check(!sf3::order_precision_quad(source,corners,retained),"same integer point with conflicting fractions is ambiguous");++cases;
    source.precise[2]=source.precise[0];source.precise[3]=source.precise[1];
    check(sf3::order_precision_quad(source,corners,retained),"identical shared point is consistent");++cases;
    sf3::VertexCommandCache cache;sf3::PrecisionQuad initial{},second{};
    initial.precise[0]=.25f;second.precise[0]=.75f;
    cache.store(key,initial,100);cache.store(key,initial,100);
    check(cache.find(key,100) && cache.find(key,103),"identical shared vertices stay usable through displayed-field latency");++cases;
    check(!cache.find(key,99) && !cache.find(key,104),"future or stale metadata rejected");++cases;
    cache.store(key,second,100);cache.store(key,initial,100);
    check(!cache.find(key,100),"conflicting projections stay ambiguous even after another identical submission");++cases;
    cache.store(key,second,101);check(cache.find(key,101) && cache.find(key,101)->precise[0]==.75f,"next frame can refresh an unchanged integer command");++cases;
    cache.clear();check(!cache.find(key,101) && !cache.size(),"snapshot/scene reset discards prior correspondences");++cases;
    for(unsigned i=0;i<8193;++i){auto unique=key;unique[4]=uint16_t(i);cache.store(unique,initial,200);check(cache.size()<=8192,"bounded cache");}
    check(cache.size()==1 && !cache.find(key,200),"capacity rollover discards older geometry");++cases;
    std::printf("PASS: %u projection, boundary and command-identity cases\n",cases);return 0;
}
