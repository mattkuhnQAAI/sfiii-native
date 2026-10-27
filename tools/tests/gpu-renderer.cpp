// Original, synthetic shader tests. Optional local video dump stays external.
#include "gpu-renderer.h"
#include "saturn.h"
#include "state.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <cstdio>
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
// VDP2 shows an RGB dot's 5-bit channels shifted left by 3, as the CPU composition does.
static uint32_t rgb555(uint16_t d){const unsigned r=d&31,g=d>>5&31,b=d>>10&31;return (r<<19)|(g<<11)|(b<<3);}
static Frame fixture(int w=4,int h=4) {
    Frame f;f.w=w;f.h=h;f.px.assign(w*h,0x112233);f.hd=std::make_shared<HdScene>();f.hd->composition.resize(w*h);
    for(auto& p:f.hd->composition){p.absent=0x112233;p.mode=1;}return f;
}
static HdQuad quad(std::initializer_list<uint16_t> raw,int w,int h,int x0,int y0,int x1,int y1) {
    HdQuad q;q.gpu_eligible=true;q.gpu_flags=0x80;q.clip[0]=511;q.clip[1]=255;
    float xy[]={float(x0),float(y0),float(x1),float(y0),float(x1),float(y1),float(x0),float(y1)};
    std::copy(xy,xy+8,q.xy);q.texture=std::make_shared<HdTexture>();q.texture->w=w;q.texture->h=h;q.texture->raw.assign(raw);return q;
}
static Frame rendered(sf3::GpuRenderer& gpu,const Frame& f,int scale) {
    check(gpu.render(f,scale)!=nullptr,"GPU render failed");Frame result;check(gpu.capture(result),"GPU readback failed");return result;
}
int main(int argc,char** argv) {
    if(argc<3 || argc>4)return 2;
    namespace fs=std::filesystem;fs::create_directories(argv[1]);
    sf3::GpuRenderer gpu;SDL_Renderer* renderer=nullptr;
    try {
        check(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization");renderer=gpu.create_renderer(nullptr);
        check(renderer!=nullptr,"GPU renderer initialization");check(gpu.initialize(renderer,argv[2]),"Geometry shader initialization");
        auto* scratch=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,16,16);
        check(scratch && SDL_SetRenderTarget(renderer,scratch),"Offscreen fixture render target");
        std::printf("GPU %s: %s\n",gpu.backend(),gpu.adapter());
        unsigned cases=0;
        for(int scale:{2,4,5})for(bool hflip:{false,true})for(bool vflip:{false,true}) {
            auto f=fixture();auto q=quad({0x801F,0x83E0,0xFC00,0xFFFF},2,2,1,1,2,2);q.hflip=hflip;q.vflip=vflip;f.hd->quads.push_back(q);
            const auto original=f.px;auto out=rendered(gpu,f,scale);
            check(out.w==4*scale && out.h==4*scale,"Output dimensions");check(f.px==original,"GPU mutated native frame");
            for(int y=0;y<out.h;++y)for(int x=0;x<out.w;++x) {
                const int cx=x/scale,cy=y/scale;uint32_t expected=0x112233;
                if(cx>=1 && cx<=2 && cy>=1 && cy<=2)expected=rgb555(q.texture->raw[(vflip?2-cy:cy-1)*2+(hflip?2-cx:cx-1)]);
                check(out.px[y*out.w+x]==expected,"Native texture sampling/flips/coverage");
            }++cases;
        }
        for(unsigned mode=0;mode<=5;++mode)for(unsigned ratio:{0u,15u,31u}) {
            auto f=fixture();f.hd->quads.push_back(quad({0x801F},1,1,0,0,3,3));
            for(auto& p:f.hd->composition){p.mode=mode;p.ratio=ratio;p.constant=0x40A0D0;p.offset[0]=-17;p.offset[1]=9;p.offset[2]=260;}
            auto out=rendered(gpu,f,4);uint32_t expected=0x40A0D0;
            if(mode) {
                expected=0;const int offset[]={-17,9,260};
                for(int c=0;c<3;++c){int sh=16-c*8;unsigned a=(0xF80000>>sh)&255,b=(0x40A0D0>>sh)&255,v=a;
                    if(mode==2)v=(a*(31-ratio)+b*(ratio+1))>>5;if(mode==3 || mode==5)v=std::min(a+b,255u);if(mode==4)v=(b*(31-ratio)+a*(ratio+1))>>5;
                    expected|=unsigned(std::clamp(int(v)+offset[c],0,255))<<sh;}
            }
            for(auto p:out.px)check(p==expected,"VDP2 ratio/addition/offset ordering");++cases;
        }
        for(int scale:{2,4,5}) {
            auto f=fixture(8,2);auto q=quad({0x801F,0x7FFF,0x83E0,0x7FFF,0xFC00,0xFC00,0xFC00,0xFC00},8,1,0,0,7,0);q.gpu_flags=0;f.hd->quads.push_back(q);
            auto out=rendered(gpu,f,scale);
            for(int x=0;x<8*scale;++x){uint32_t expected=x/scale==0?0xF80000:x/scale==2?0x00F800:0x112233;check(out.px[x]==expected,"Two distinct source end codes terminate texture row");}
            f.hd->quads[0].hflip=true;out=rendered(gpu,f,scale);
            for(int x=0;x<8*scale;++x){uint32_t expected=x/scale<4?0x0000F8:x/scale==5?0x00F800:0x112233;check(out.px[x]==expected,"Flipped end-code traversal direction and cached texture variant");}
            f.hd->quads.clear();q=quad({0},1,1,0,0,7,1);q.gpu_flags=0xC0;f.hd->quads.push_back(q);
            out=rendered(gpu,f,scale);for(auto p:out.px)check(p==0x112233,"SPD zero clears to background");
            f.hd->quads[0].texture->raw[0]=0x8000;out=rendered(gpu,f,scale);for(auto p:out.px)check(p==0,"Opaque RGB black remains black");
            for(auto& p:f.hd->composition)p.mode=0x100;
            out=rendered(gpu,f,scale);for(auto p:out.px)check(p==0x112233,"Unsupported native effects remain intact");
            ++cases;
        }
        for(int scale:{2,4,5})for(bool outside:{false,true}) {
            auto f=fixture();auto q=quad({0x801F},1,1,0,0,3,3);
            q.gpu_flags|=0x400|(outside?0x200:0);q.clip[0]=2;q.clip[1]=2;
            q.clip[2]=1;q.clip[3]=1;q.clip[4]=2;q.clip[5]=2;f.hd->quads.push_back(q);
            auto out=rendered(gpu,f,scale);
            for(int y=0;y<out.h;++y)for(int x=0;x<out.w;++x) {
                int cx=x/scale,cy=y/scale;bool inside=cx>=1 && cy>=1 && cx<=2 && cy<=2;
                check(out.px[y*out.w+x]==((cx<=2 && cy<=2 && inside!=outside)?0xF80000:0x112233),"System and user clipping boundaries");
            }++cases;
        }
        for(int scale:{2,4,5}) {
            auto f=fixture();f.hd->quads.push_back(quad({0x801F},1,1,0,0,3,3));
            f.hd->quads.push_back(quad({0,0x83E0},2,1,0,0,3,3));
            auto out=rendered(gpu,f,scale);
            for(int y=0;y<out.h;++y)for(int x=0;x<out.w;++x)
                check(out.px[y*out.w+x]==(x<out.w/2?0xF80000:0x00F800),"Transparent foreground preserves earlier primitive");
            f.hd->quads.clear();auto q=quad({0xC210},1,1,0,0,3,3);q.gpu_flags|=4;
            for(int i=0;i<4;++i){q.shade[i*3]=8;q.shade[i*3+1]=-8;q.shade[i*3+2]=0;}
            f.hd->quads.push_back(q);out=rendered(gpu,f,scale);
            for(auto p:out.px)check(p==rgb555(17|(15<<5)|(16<<10)),"Gouraud channel offsets and RGB555 expansion");++cases;
        }
        auto f=fixture(32,32);auto q=quad({0x83E0},1,1,2,2,25,25);q.gpu_flags=2<<16;
        const float xy[]={2,2,25,8,20,25,3,19};std::copy(xy,xy+8,q.xy);f.hd->quads.push_back(q);
        auto out=rendered(gpu,f,4);unsigned mixed=0;
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx)mixed+=out.px[(y*4+dy)*out.w+x*4+dx]!=out.px[(y*4)*out.w+x*4];
        check(mixed>0,"GPU must rasterize subpixel geometry, not upscale the native picture");++cases;
        write_png((fs::path(argv[1])/"synthetic-geometry.png").string(),out);
        // Translation of the viewport/tile bins must not change coverage.
        // Skewed convex quads expose spurious Newton roots outside geometry.
        for(int scale:{1,2,4,5}) {
            auto source=fixture(96,64);
            for(int i=0;i<12;++i) {
                auto shape=quad({uint16_t(0x801F+i*64)},1,1,0,0,0,0);shape.gpu_flags|=2<<16;
                float corners[]={float(i*7-5),float(i*3),float(i*7+45),float(i*3+1),float(i*7+27),float(i*3+7),float(i*7+2),float(i*3+4)};
                std::copy(corners,corners+8,shape.xy);shape.clip[0]=95;source.hd->quads.push_back(shape);
            }
            auto reference=rendered(gpu,source,scale);
            for(int origin:{1,7,16,53}) {
                auto wide=fixture(96+2*origin,64);wide.native_w=96;wide.origin_x=origin;wide.hd->quads=source.hd->quads;
                auto expanded=rendered(gpu,wide,scale);
                for(int y=0;y<reference.h;++y)for(int x=0;x<reference.w;++x)
                    check(reference.px[y*reference.w+x]==expanded.px[y*expanded.w+x+origin*scale],"Skewed quad coverage depends on tile-bin origin");
                ++cases;
            }
        }
        // Small bins must retain inclusive coverage at every edge, including
        // negative coordinates and quads crossing multiple tile boundaries.
        for(int scale:{2,4,5})for(int x0:{-4,-1,0,3,4,7,8,15,16,31,32,39}) {
            auto f=fixture(40,8);f.hd->quads.push_back(quad({0x801F},1,1,x0,2,x0+6,4));
            auto out=rendered(gpu,f,scale);
            for(int y=0;y<out.h;++y)for(int x=0;x<out.w;++x) {
                bool covered=x/scale>=x0 && x/scale<=x0+6 && y/scale>=2 && y/scale<=4;
                check(out.px[y*out.w+x]==(covered?0xF80000:0x112233),"Small-bin inclusive and clipped coverage");
            }
            ++cases;
        }
        Frame disabled;std::memset(g_vdp2_regs,0,sizeof g_vdp2_regs);check(!vdp2_compose_wide(disabled),"Display-off must retain original framing");++cases;
        {
            // At 1x the native centre is the exact CPU raster; geometry is not resampled.
            auto f=fixture();f.hd->quads.push_back(quad({0x801F},1,1,0,0,3,3));f.px[5]=0xABCDEF;
            check(rendered(gpu,f,1).px==f.px,"1x must present the exact native CPU raster");++cases;
        }
        for(int scale:{2,4,5}) {
            auto f=fixture();f.hd->quads.push_back(quad({0x801F},1,1,0,0,3,3));
            auto first=rendered(gpu,f,scale);auto draws=gpu.submissions(),hits=gpu.cache_hits();
            for(int i=0;i<3;++i)check(rendered(gpu,f,scale).px==first.px,"Repeated-field cache changed pixels");
            check(gpu.submissions()==draws && gpu.cache_hits()==hits+3,"Identical inputs must avoid GPU resubmission");
            f.hd->quads[0].texture->raw[0]=0x83E0;auto changed=rendered(gpu,f,scale);
            check(gpu.submissions()==draws+1 && changed.px[0]==0x00F800,"Texture changes must invalidate cache");
            f.hd->quads[0].clip[0]=1;changed=rendered(gpu,f,scale);
            check(gpu.submissions()==draws+2 && changed.px[3*scale]==0x112233,"Clipping changes must invalidate cache");
            f.hd->composition[0].mode=0x100;f.px[0]=0xABCDEF;changed=rendered(gpu,f,scale);
            check(gpu.submissions()==draws+3 && changed.px[0]==0xABCDEF,"Native fallback changes must invalidate cache");
            ++cases;
        }
        // Spatial AA is presentation only. Exact normal-sprite/text paths and
        // flat/axis-aligned samples must survive the extra GPU pass unchanged.
        for(int scale:{2,4,5})for(int width:{16,17,35}) {
            auto f=fixture(width,17);auto q=quad({0xFFFF},1,1,2,2,13,13);
            q.xy[0]=7;q.xy[1]=1;q.xy[2]=14;q.xy[3]=7;q.xy[4]=8;q.xy[5]=14;q.xy[6]=1;q.xy[7]=8;
            f.hd->quads.push_back(q);for(auto& p:f.hd->composition)p.owner=1;
            gpu.antialiasing(false);auto off=rendered(gpu,f,scale);
            gpu.antialiasing(true);check(rendered(gpu,f,scale).px==off.px,"AA altered protected sprite/text samples");
            f.hd->quads[0].gpu_flags|=2u<<16;
            gpu.antialiasing(false);off=rendered(gpu,f,scale);
            const auto original=f.px;gpu.antialiasing(true);auto on=rendered(gpu,f,scale);
            check(on.px!=off.px,"AA did not smooth diagonal geometry");
            check(f.px==original,"AA changed native frame");
            auto draws=gpu.submissions();check(rendered(gpu,f,scale).px==on.px && gpu.submissions()==draws,"AA repeated-field cache");
            gpu.antialiasing(false);check(rendered(gpu,f,scale).px==off.px,"Disabling AA did not restore exact pixels");
            for(auto& p:f.hd->composition)p.mode=0x100;
            for(size_t i=0;i<f.px.size();++i)f.px[i]=i%2?0xAA0011:0x0011AA;
            off=rendered(gpu,f,scale);gpu.antialiasing(true);
            check(rendered(gpu,f,scale).px==off.px,"AA changed unsupported/occluding native text layer");
            gpu.antialiasing(false);++cases;
        }
        // A visible small sprite can shade a larger geometry receiver through
        // the owner mask. Sprite pixels and the original frame stay exact.
        for(int scale:{2,4,5}) {
            auto f=fixture(16,16);
            auto ground=quad({0xFFFF},1,1,0,0,15,15);ground.gpu_flags=2u<<16;
            auto figure=quad({0x801F},1,1,4,4,6,6);
            f.hd->quads.push_back(ground);f.hd->quads.push_back(figure);
            for(auto& p:f.hd->composition)p.owner=1;
            for(int y=4;y<=6;++y)for(int x=4;x<=6;++x)f.hd->composition[y*16+x].owner=2;
            const auto native=f.px;
            gpu.ray_shadows(false);auto off=rendered(gpu,f,scale);
            gpu.ray_shadows(true);auto on=rendered(gpu,f,scale);
            auto at=[&](const Frame& frame,int x,int y){return frame.px[(y*scale+scale/2)*frame.w+x*scale+scale/2];};
            check(at(on,10,8)!=at(off,10,8),"Ray shadow missed the geometry receiver");
            check(at(on,5,5)==at(off,5,5),"Ray shadow altered protected sprite pixels");
            check(at(on,15,0)==at(off,15,0),"Ray shadow changed an unoccluded pixel");
            check(f.px==native,"Ray shadow changed the native frame");
            auto draws=gpu.submissions();check(rendered(gpu,f,scale).px==on.px && gpu.submissions()==draws,"Ray shadow repeated-field cache");
            gpu.antialiasing(true);check(rendered(gpu,f,scale).px!=off.px,"AA and ray shadows did not combine");
            gpu.antialiasing(false);gpu.ray_shadows(false);
            check(rendered(gpu,f,scale).px==off.px,"Ray shadow Off did not restore exact pixels");
            ++cases;
        }
        std::printf("PASS: %u shader cases: geometry, original textures, flips, native immutability, end codes, SPD, composition, fallback, masked AA, ray shadows and exact Off restore\n",cases);
        if(argc==4) {
            std::ifstream input(argv[3],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
            constexpr size_t v2=0xC0020;check(bytes.size()==0x141220,"Local video dump size");
            vdp1_init();vdp1_gpu_available(true);vdp1_render_scale(2);vdp1_hd_collect(true);
            std::memcpy(g_vdp1_vram,bytes.data(),0x80000);vdp1_reg_write(0,(bytes[0xC0000]<<8)|bytes[0xC0001],2);
            vdp1_reg_write(4,1,2);vdp1_vblank_out();
            std::memcpy(g_vdp2_vram,bytes.data()+v2,sizeof g_vdp2_vram);std::memcpy(g_vdp2_cram,bytes.data()+v2+sizeof g_vdp2_vram,sizeof g_vdp2_cram);
            std::memcpy(g_vdp2_regs,bytes.data()+v2+sizeof g_vdp2_vram+sizeof g_vdp2_cram,sizeof g_vdp2_regs);
            Frame source;vdp2_compose(source);check(source.w<=704 && source.h<=512,"GPU mode must retain native CPU composition size");
            State snapshot;vdp1_state(snapshot);
            auto before_state=rendered(gpu,source,4);
            auto before_state5=rendered(gpu,source,5);
            vdp1_fb_write(0,0xFC00,2);g_vdp1_vram[0x200]^=0xFF;
            snapshot.loading=true;vdp1_state(snapshot);
            Frame restored_state;vdp2_compose(restored_state);
            check(restored_state.px==source.px,"State restore changed native pixels");
            check(rendered(gpu,restored_state,4).px==before_state.px,"State restore changed GPU geometry, texture sharing or composition");
            check(rendered(gpu,restored_state,5).px==before_state5.px,"State restore changed 5x GPU geometry");
            check(snapshot.pos==snapshot.data.size(),"Incomplete VDP1 state restoration");
            std::printf("PASS: native, 4x and 5x GPU snapshot round trip\n");
            Frame reference;vdp2_compose_reference(reference);
            check(source.px==reference.px && source.hd && reference.hd && source.hd->composition.size()==reference.hd->composition.size(),"Optimized composition changed native pixels");
            check(std::memcmp(source.hd->composition.data(),reference.hd->composition.data(),source.hd->composition.size()*sizeof(GpuPixel))==0,"Optimized metadata differs from reference");
            check(source.hd->visible==reference.hd->visible && source.hd->fingerprint==reference.hd->fingerprint && source.hd->geometry==reference.hd->geometry,"Parallel raster rows changed ordered provenance");
            check(source.hd->effects.size()==reference.hd->effects.size() && std::memcmp(source.hd->effects.data(),reference.hd->effects.data(),source.hd->effects.size()*sizeof(HdEffect))==0,"Parallel raster rows changed effects");
            auto same_video=[&](const Frame& a,const Frame& b) {
                check(a.px==b.px && bool(a.hd)==bool(b.hd),"Cached native picture differs from fresh reference");
                if(a.hd)check(a.hd->composition.size()==b.hd->composition.size() &&
                    (a.hd->composition.empty() || std::memcmp(a.hd->composition.data(),b.hd->composition.data(),a.hd->composition.size()*sizeof(GpuPixel))==0),"Cached native effects differ from fresh reference");
            };
            Frame cached;auto hits=vdp2_cached_fields();vdp2_compose(cached);
            check(vdp2_cached_fields()==hits+1,"Identical video state must reuse native composition");same_video(cached,reference);
            for(int off:{4,8,10}) {
                g_vdp2_regs[off]^=1;auto old_hits=vdp2_cached_fields();Frame fresh,control;vdp2_compose(fresh);vdp2_compose_reference(control);
                check(vdp2_cached_fields()==old_hits+1,"Unused raster-status reads must not invalidate composition");same_video(fresh,control);g_vdp2_regs[off]^=1;
            }
            for(uint8_t* changed:{&g_vdp2_vram[0],&g_vdp2_vram[0x7FFFF],&g_vdp2_cram[0],&g_vdp2_regs[0x115]}) {
                *changed^=1;auto old_hits=vdp2_cached_fields();Frame fresh,control;vdp2_compose(fresh);vdp2_compose_reference(control);
                check(vdp2_cached_fields()==old_hits,"Video input change failed to invalidate native cache");same_video(fresh,control);
                *changed^=1;vdp2_compose(fresh);vdp2_compose_reference(control);same_video(fresh,control);
            }
            auto word=[&](int at,uint16_t value){g_vdp2_regs[at]=value>>8;g_vdp2_regs[at+1]=value;};
            const uint16_t cc=(g_vdp2_regs[0xEC]<<8)|g_vdp2_regs[0xED];
            for(uint16_t bits:{uint16_t(0),uint16_t(0x40),uint16_t(0x140),uint16_t(0x240),uint16_t(0x340)}) {
                word(0xEC,(cc&~0x340)|bits);Frame fast,control;vdp2_compose(fast);vdp2_compose_reference(control);same_video(fast,control);
            }
            word(0xEC,cc);vdp2_compose(source);vdp2_compose_reference(reference);
            std::printf("PASS: native cache invalidation, exact reference metadata and blend-mode variants\n");
            check(source.hd && !source.hd->composition.empty(),"GPU composition provenance missing");
            std::printf("Local scene: %zu quads, %dx%d native CPU frame\n",source.hd->quads.size(),source.w,source.h);
            auto measured=[&](const char* name,auto action,int n=20) {
                auto start=std::chrono::steady_clock::now();for(int i=0;i<n;++i)action();
                std::printf("PROFILE %s: %.3f ms/frame\n",name,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/n);
            };
            Frame composed;
            measured("native CPU composition with changing video inputs",[&]{g_vdp2_vram[0]^=1;vdp2_compose(composed);});
            measured("native unchanged-field cache",[&]{vdp2_compose(composed);});
            vdp2_compose(source);
            measured("full widescreen CPU composition",[&]{vdp2_compose_wide(composed);});
            measured("side-column CPU composition reusing native center",[&]{vdp2_compose_wide(composed,&reference);});
            measured("wide unchanged-field cache",[&]{vdp2_compose_wide(composed,&source);});
            Frame profile_wide;
            if(vdp2_compose_wide(profile_wide))for(int scale:{2,4,5}) {
                std::string label="wide GPU "+std::to_string(scale)+"x with submission/readback";
                measured(label.c_str(),[&]{profile_wide.px[0]^=1;rendered(gpu,profile_wide,scale);});
            }
            for(int scale:{2,4,5}) {
                auto result=rendered(gpu,source,scale);write_png((fs::path(argv[1])/("scene-"+std::to_string(scale)+"x.png")).string(),result);
                gpu.antialiasing(true);auto filtered=rendered(gpu,source,scale);
                write_png((fs::path(argv[1])/("scene-"+std::to_string(scale)+"x-aa.png")).string(),filtered);
                unsigned changed=0,protected_count=0;
                for(int y=0;y<filtered.h;++y)for(int x=0;x<filtered.w;++x) {
                    auto at=size_t(y)*filtered.w+x;const auto& p=source.hd->composition[size_t(y/scale)*source.w+x/scale];
                    bool eligible=p.mode && !(p.mode&0x100) && p.owner && p.owner<=source.hd->quads.size() &&
                        source.hd->quads[p.owner-1].gpu_eligible && ((source.hd->quads[p.owner-1].gpu_flags>>16)&15)==2;
                    if(!eligible){check(filtered.px[at]==result.px[at],"AA changed protected local sprite/text/background sample");++protected_count;}
                    changed+=filtered.px[at]!=result.px[at];
                }
                std::printf("AA %dx: %u changed geometry pixels, %u protected samples unchanged\n",scale,changed,protected_count);
                std::string aa_label="Light AA "+std::to_string(scale)+"x including upload/submission/readback";
                measured(aa_label.c_str(),[&]{source.px[0]^=1;rendered(gpu,source,scale);});
                gpu.antialiasing(false);check(rendered(gpu,source,scale).px==result.px,"Local AA Off restoration");
                gpu.ray_shadows(true);auto shaded=rendered(gpu,source,scale);
                write_png((fs::path(argv[1])/ ("scene-"+std::to_string(scale)+"x-ray.png")).string(),shaded);
                unsigned ray_changed=0,ray_protected=0;
                for(int y=0;y<shaded.h;++y)for(int x=0;x<shaded.w;++x) {
                    auto at=size_t(y)*shaded.w+x;const auto& p=source.hd->composition[size_t(y/scale)*source.w+x/scale];
                    bool eligible=p.mode && !(p.mode&0x100) && p.owner && p.owner<=source.hd->quads.size() &&
                        source.hd->quads[p.owner-1].gpu_eligible && ((source.hd->quads[p.owner-1].gpu_flags>>16)&15)==2;
                    if(!eligible){check(shaded.px[at]==result.px[at],"Ray shadow changed a protected local sample");++ray_protected;}
                    ray_changed+=shaded.px[at]!=result.px[at];
                }
                std::printf("Ray shadows %dx: %u changed geometry pixels, %u protected samples unchanged\n",scale,ray_changed,ray_protected);
                std::string ray_label="Ray shadows "+std::to_string(scale)+"x including upload/submission/readback";
                measured(ray_label.c_str(),[&]{source.px[0]^=1;rendered(gpu,source,scale);});
                gpu.ray_shadows(false);check(rendered(gpu,source,scale).px==result.px,"Local ray Off restoration");
                auto start=std::chrono::steady_clock::now();
                for(int i=0;i<10;++i){source.px[0]^=1;rendered(gpu,source,scale);}
                std::printf("GPU %dx: %.3f ms/frame including upload, submission and readback (%dx%d)\n",scale,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10,result.w,result.h);
                Frame wide;
                if(vdp2_compose_wide(wide)) {
                    Frame reused;check(vdp2_compose_wide(reused,&source),"Native-center reuse unavailable");
                    check(reused.px==wide.px && reused.hd->composition.size()==wide.hd->composition.size(),"Native-center reuse changed wide pixels");
                    check(std::memcmp(reused.hd->composition.data(),wide.hd->composition.data(),wide.hd->composition.size()*sizeof(GpuPixel))==0,"Native-center reuse changed effects/occlusion");
                    check(reused.hd->fingerprint==wide.hd->fingerprint && reused.hd->geometry==wide.hd->geometry,"Native-center reuse changed provenance");
                    Frame invalid=source;invalid.w+=1;Frame fallback;
                    check(vdp2_compose_wide(fallback,&invalid) && fallback.px==wide.px,"Invalid native center must use complete composition");
                    check(wide.w==(source.w*4+2)/3 && wide.h==source.h,"Expanded viewport dimensions");
                    for(int y=0;y<source.h;++y)for(int x=0;x<source.w;++x)
                        check(source.px[y*source.w+x]==wide.px[y*wide.w+x+wide.origin_x],"Expanded composition changed native center");
                    auto extended=rendered(gpu,wide,scale);
                    write_png((fs::path(argv[1])/("wide-"+std::to_string(scale)+"x.png")).string(),extended);
                    unsigned differences=0;int first_x=-1,first_y=-1;
                    for(int y=0;y<result.h;++y)for(int x=0;x<result.w;++x)
                        if(result.px[y*result.w+x]!=extended.px[y*extended.w+x+wide.origin_x*scale]){++differences;if(first_x<0){first_x=x;first_y=y;}}
                    if(differences) {
                        auto a=source.hd->composition[(first_y/scale)*source.w+first_x/scale],b=wide.hd->composition[(first_y/scale)*wide.w+first_x/scale+wide.origin_x];
                        std::printf("Center differences %u; first %d,%d metadata %u/%u owner %u/%u raw equal %d color %06X/%06X\n",differences,first_x,first_y,a.mode,b.mode,a.owner,b.owner,int(std::memcmp(&a,&b,sizeof a)==0),result.px[first_y*result.w+first_x],extended.px[first_y*extended.w+first_x+wide.origin_x*scale]);
                    }
                    check(differences==0,"Expanded GPU viewport changed original center");
                    std::printf("PASS: %dx widescreen %dx%d, native and GPU center unchanged\n",scale,extended.w,extended.h);
                } else std::printf("Original framing retained: unsupported or flat screen\n");
            }
            std::vector<uint8_t> shown_before,shown_after;
            check(vdp1_presentation_key(shown_before),"Normal video key unavailable");
            vdp1_fb_write(0,0x801F,2);check(vdp1_presentation_key(shown_after) && shown_before==shown_after,"Hidden framebuffer write must not change shown-video key");
            vdp1_reg_write(2,3,2);vdp1_vblank_out();
            check(vdp1_presentation_key(shown_after) && shown_before!=shown_after,"Framebuffer change failed to invalidate shown-video key");
            Frame swapped,control;vdp2_compose(swapped);vdp2_compose_reference(control);same_video(swapped,control);
            std::printf("PASS: CPU framebuffer upload and display-buffer switch invalidation\n");
            vdp1_gpu_available(false);vdp1_render_scale(1);
        }
        gpu.release();SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(scratch);SDL_DestroyRenderer(renderer);gpu.destroy_device();SDL_Quit();return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s; SDL: %s\n",e.what(),SDL_GetError());return 1;}
}
