#include "hd-assets.h"
#include <cstdio>
#include "overlay-error.h"

int main(int argc,char** argv) {
    if(sf3::HdAssets::recommended_cache_bytes(-1)!=size_t(256)*1024*1024 ||
       sf3::HdAssets::recommended_cache_bytes(16383)!=size_t(256)*1024*1024 ||
       sf3::HdAssets::recommended_cache_bytes(16384)!=size_t(512)*1024*1024 ||
       sf3::HdAssets::recommended_cache_bytes(24575)!=size_t(512)*1024*1024 ||
       sf3::HdAssets::recommended_cache_bytes(24576)!=size_t(1024)*1024*1024)return 31;
    if(overlay_error(0x00FFFFFF,0)!=0 || overlay_error(0x01897821,0)!=1
       || overlay_error(0xFF030000,0xFF000000)!=3
       || overlay_error(0x03000000,0)!=3
       || overlay_error(0x80080000,0x80000000)<=2
       || overlay_error(0x80000000,0x7D000000)<=2)return 20;
    if(argc!=2) return 2;
    namespace fs=std::filesystem;auto root=fs::path(argv[1]);auto fixture=root/"invented-material";
    fs::create_directories(fixture);
    { std::ofstream f(fixture/"asset.txt");f<<"SF3HDASSET1\n1 1\n4 4\n"; }
    { std::ofstream f(fixture/"original.rgba",std::ios::binary);const char rgba[]={char(248),0,0,char(255)};f.write(rgba,4); }
    { std::ofstream f(fixture/"replacement.rgba",std::ios::binary);
      for(int i=0;i<16;++i) { const char rgba[]={0,char(i&1 ? 255 : 0),char(i&1 ? 0 : 255),char(255)};f.write(rgba,4); } }
    sf3::HdAssets assets;if(assets.load(root.string())!=1) return 3;
    Frame source;source.w=source.h=2;source.px.assign(4,0xF80000);
    source.hd=std::make_shared<HdScene>();source.hd->visible={1,0,1,0};
    auto t=std::make_shared<HdTexture>();t->w=t->h=1;t->rgba={0xFFF80000};
    HdQuad quad;const float xy[]={0,0,1,0,1,1,0,1};std::copy(xy,xy+8,quad.xy);quad.texture=t;
    quad.gpu_eligible=true;quad.gpu_flags=2u<<16;
    source.hd->quads.push_back(quad);source.hd->effects.resize(4);
    for(auto& e:source.hd->effects) { e.flags=1;e.ratio=15;e.second=0xFFFFFF; }
    Frame output;output.w=output.h=8;output.px.assign(64,0xF80000);
    assets.apply(source,output);
    if(output.px[0]!=0x7F7FFF || assets.last_pixels()!=32) return 4;
    hd_fingerprint(t->fingerprint,uint64_t(1)<<32|1);
    hd_fingerprint(t->fingerprint,t->rgba[0]);
    ++source.hd->geometry;output.px.assign(64,0xF80000);assets.apply(source,output);
    if(output.px[0]!=0x7F7FFF || assets.last_pixels()!=32)return 28;
    // A decoder fingerprint is a lookup hint, never permission to replace
    // different source bytes or dimensions (including a stale/colliding hint).
    t->rgba[0]^=1;++source.hd->geometry;assets.apply(source,output);
    if(assets.last_pixels())return 29;
    t->rgba[0]^=1;t->w=2;++source.hd->geometry;assets.apply(source,output);
    if(assets.last_pixels())return 30;
    t->w=1;t->fingerprint=0;++source.hd->geometry;
    source.hd->quads[0].gpu_flags=0;++source.hd->geometry;
    output.px.assign(64,0xF80000);assets.apply(source,output);
    if(assets.last_pixels() || output.px[0]!=0xF80000)return 14; // Ordinary sprites/UI stay native.
    source.hd->quads[0].gpu_flags=2u<<16;++source.hd->geometry;
    output.px.assign(64,0xF80000);assets.apply(source,output);
    for(int y=0;y<8;++y) for(int x=4;x<8;++x) if(output.px[y*8+x]!=0xF80000) return 5;
    for(auto p:source.px) if(p!=0xF80000) return 6;
    for(int i=0;i<4;++i) source.hd->quads[0].shade[i*3+1]=8;
    ++source.hd->geometry;
    for(auto& e:source.hd->effects) {e.flags|=4;e.offset[0]=-10;}
    output.px.assign(64,0xF80000);assets.apply(source,output);
    if(output.px[0]!=0x3A417F) return 7;
    const auto matched_geometry=source.hd->geometry;
    t->rgba[0]^=1;++source.hd->geometry;output.px.assign(64,0xF80000);assets.apply(source,output);
    if(assets.last_pixels()) return 8; // Different source bytes must not match.
    t->rgba[0]^=1;
    // Return to an earlier scene/cache identity after an unmatched frame. Its
    // projection was cleared, so retaining the old cache key would erase detail.
    source.hd->geometry=matched_geometry;
    output.px.assign(64,0xF80000);assets.apply(source,output);
    if(output.px[0]!=0x3A417F || assets.last_pixels()!=32)return 28;
    { std::ofstream f(fixture/"asset.txt");f<<"SF3HDASSET1\n1 1\n4 4\n0.333333333 0 0.666666667 1\n"; }
    if(assets.load(root.string())!=1) return 10;
    for(auto& e:source.hd->effects) e=HdEffect{};
    for(auto& s:source.hd->quads[0].shade) s=0;
    output.px.assign(64,0xF80000);assets.apply(source,output);
    if(output.px[0]!=0x00FF00) return 11; // Exact atlas region starts at green column.
    source.hd->quads[0].hflip=true;++source.hd->geometry;output.px.assign(64,0xF80000);assets.apply(source,output);
    if(output.px[0]!=0x0000FF) return 12;
    source.hd->quads[0].gpu_flags=0;output.px.assign(64,0xF80000);assets.apply(source,output);
    if(assets.last_pixels())return 21; // World packs cannot enhance an ordinary sprite/menu command.
    { std::ofstream f(fixture/"asset.txt");f<<"SF3HDASSET1\n1 1\n4 4\n0 0 1 1\ncharacter\n"; }
    if(assets.load(root.string())!=1 || assets.cached_replacement_bytes()!=0)return 22;
    ++source.hd->geometry;assets.apply(source,output);
    if(!assets.last_pixels() || assets.cached_replacement_bytes()!=64)return 23;
    source.hd->quads[0].gpu_flags=1u<<16;++source.hd->geometry;assets.apply(source,output);
    if(!assets.last_pixels())return 24;
    for(auto& coordinate:source.hd->quads[0].xy) coordinate=0;
    ++source.hd->geometry;
    output.px.assign(64,0xF80000);assets.apply(source,output);
    if(assets.last_pixels()) return 13;
    { std::ofstream f(fixture/"replacement.rgba",std::ios::binary);f<<"truncated"; }
    if(assets.load(root.string())!=0) return 9;
    // Animate three exact source frames with a one-frame CPU budget. Evicted
    // artwork must reload correctly; sprites with changed original bytes fall back.
    auto animation=root/"animation";fs::create_directories(animation);
    for(int frame=0;frame<3;++frame) {
        auto dir=animation/std::to_string(frame);fs::create_directories(dir);
        {std::ofstream f(dir/"asset.txt");f<<"SF3HDASSET1\n1 1\n4 4\n0 0 1 1\ncharacter\n";}
        {std::ofstream f(dir/"original.rgba",std::ios::binary);char p[]={char(248-frame*8),0,0,char(255)};f.write(p,4);}
        {std::ofstream f(dir/"replacement.rgba",std::ios::binary);for(int i=0;i<16;++i){char p[]={0,char(100+frame),0,char(255)};f.write(p,4);}}
    }
    sf3::HdAssets frames(64);if(frames.load(animation.string())!=3 || frames.cached_replacement_bytes())return 25;
    std::copy(xy,xy+8,source.hd->quads[0].xy);source.hd->quads[0].hflip=false;
    source.hd->quads[0].gpu_flags=0;
    for(int frame:{0,1,2,0,2,1}) {
        t->rgba[0]=0xFF000000|uint32_t(248-frame*8)<<16;++source.hd->geometry;
        output.px.assign(64,0);frames.apply(source,output);
        if(output.px[0]!=uint32_t(100+frame)<<8 || frames.cached_replacement_bytes()!=64)return 26;
    }
    sf3::HdAssets warm(64);if(!warm.configure_cache_budget(192) || warm.cache_budget()!=192 || warm.configure_cache_budget(0) || warm.load(animation.string())!=3)return 32;
    for(int frame:{0,1,2,0,2,1}) {
        t->rgba[0]=0xFF000000|uint32_t(248-frame*8)<<16;++source.hd->geometry;
        output.px.assign(64,0);warm.apply(source,output);
        if(output.px[0]!=uint32_t(100+frame)<<8 || warm.cached_replacement_bytes()>192)return 33;
    }
    if(warm.cached_replacement_bytes()!=192 || warm.configure_cache_budget(64))return 34;
    t->rgba[0]^=1;++source.hd->geometry;frames.apply(source,output);if(frames.last_pixels())return 27;
    std::puts("PASS: replacement detail, exact original match, native occlusion mask, source immutability, colour calculation, Gouraud, offset/shadow, damaged pack rejection");
}
