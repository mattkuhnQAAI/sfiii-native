// Local diagnostic only: recompose a native video dump without running game code.
#include "saturn.h"
#include "video.h"
#include "hd-presentation.h"
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include "overlay-error.h"
#ifdef SF3_GPU_DIAGNOSTIC
#include "hd-gpu.h"
#include "gpu-renderer.h"
#endif

int main(int argc, char** argv) {
    if (argc < 3 || argc > 17) { std::fprintf(stderr, "usage: render_dump dump.bin output.png [--redraw] [--hd] [--assets PACK] [--export-textures DIR] [--bench] [--raster-bench] [--require-replacements] [--require-characters] [--render-scale 1|2|4|5] [--gpu-scale 1|2|4|5]\n"); return 2; }
    bool redraw=false, hd=false;
    bool bench=false,raster_bench=false,require_replacements=false,require_characters=false;
    int gpu_scale=1;
    std::string pack,export_textures;
    for(int i=3;i<argc;++i) {
        if(std::string(argv[i])=="--redraw") redraw=true;
        else if(std::string(argv[i])=="--hd") hd=true;
        else if(std::string(argv[i])=="--assets" && i+1<argc) pack=argv[++i];
        else if(std::string(argv[i])=="--export-textures" && i+1<argc) export_textures=argv[++i];
        else if(std::string(argv[i])=="--bench") bench=true;
        else if(std::string(argv[i])=="--raster-bench") raster_bench=true;
        else if(std::string(argv[i])=="--require-replacements") require_replacements=true;
        else if(std::string(argv[i])=="--require-characters") require_characters=true;
        else if(std::string(argv[i])=="--gpu-scale" && i+1<argc) {
            const std::string value=argv[++i];
            if(value!="1" && value!="2" && value!="4" && value!="5")return 2;
            gpu_scale=std::stoi(value);
        }
        else if(std::string(argv[i])=="--render-scale" && i+1<argc) {
            const std::string value=argv[++i];
            if(value!="1" && value!="2" && value!="4" && value!="5") return 2;
            vdp1_render_scale(std::stoi(value));
        }
        else return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    constexpr size_t v2 = 0x80000 + 0x40000 + 0x20;
    if (bytes.size() != v2 + sizeof g_vdp2_vram + sizeof g_vdp2_cram + sizeof g_vdp2_regs) {
        std::fprintf(stderr, "unexpected native video dump size: %zu\n", bytes.size()); return 2;
    }
    vdp1_init();
    vdp1_hd_collect(!pack.empty() || !export_textures.empty());
    std::memcpy(g_vdp1_vram, bytes.data(), sizeof g_vdp1_vram);
    vdp1_reg_write(0, (bytes[0xc0000] << 8) | bytes[0xc0001], 2);
    if (redraw) {
        vdp1_reg_write(4, 1, 2);
    } else {
        for (unsigned i = 0; i < 0x40000; i += 2)
            vdp1_fb_write(i, (bytes[0x80000+i] << 8) | bytes[0x80001+i], 2);
    }
    vdp1_vblank_out();
    std::memcpy(g_vdp2_vram, bytes.data()+v2, sizeof g_vdp2_vram);
    std::memcpy(g_vdp2_cram, bytes.data()+v2+sizeof g_vdp2_vram, sizeof g_vdp2_cram);
    std::memcpy(g_vdp2_regs, bytes.data()+v2+sizeof g_vdp2_vram+sizeof g_vdp2_cram, sizeof g_vdp2_regs);
    Frame frame;
    vdp2_compose(frame);
    if(!export_textures.empty()) {
        namespace fs=std::filesystem;
        fs::create_directories(export_textures);
        std::ofstream table(fs::path(export_textures)/"textures.tsv");
        table << "id\twidth\theight\tquads\tmax_area\topaque\n";
        struct Entry {std::shared_ptr<HdTexture> texture;int quads=0,max_area=0;};
        std::vector<Entry> entries;
        std::unordered_map<const HdTexture*,size_t> indices;
        if(frame.hd) for(const auto& q:frame.hd->quads) {
            if(!q.gpu_eligible || !q.texture || ((q.gpu_flags>>16)&15)!=2)continue;
            auto [it,added]=indices.emplace(q.texture.get(),entries.size());
            if(added)entries.push_back({q.texture});
            auto& e=entries[it->second];++e.quads;
            float minx=q.xy[0],maxx=q.xy[0],miny=q.xy[1],maxy=q.xy[1];
            for(int k=1;k<4;++k){minx=std::min(minx,q.xy[2*k]);maxx=std::max(maxx,q.xy[2*k]);
                miny=std::min(miny,q.xy[2*k+1]);maxy=std::max(maxy,q.xy[2*k+1]);}
            e.max_area=std::max(e.max_area,int((maxx-minx)*(maxy-miny)));
        }
        for(size_t i=0;i<entries.size();++i) {
            const auto& e=entries[i];auto name="texture-"+std::to_string(i);
            bool opaque=true;std::ofstream raw(fs::path(export_textures)/(name+".rgba"),std::ios::binary);
            for(auto p:e.texture->rgba) {
                unsigned char rgba[]={static_cast<unsigned char>(p>>16),static_cast<unsigned char>(p>>8),
                    static_cast<unsigned char>(p),static_cast<unsigned char>(p>>24)};
                opaque&=rgba[3]==255;raw.write(reinterpret_cast<const char*>(rgba),4);
            }
            table<<name<<'\t'<<e.texture->w<<'\t'<<e.texture->h<<'\t'<<e.quads<<'\t'<<e.max_area<<'\t'<<opaque<<'\n';
        }
        std::printf("Exported %zu eligible distorted-sprite textures to %s\n",entries.size(),export_textures.c_str());
    }
    if(raster_bench) {
        const auto begin=std::chrono::steady_clock::now();
        for(int i=0;i<10;++i) {vdp1_reg_write(4,1,2);vdp1_vblank_out();vdp2_compose(frame);}
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        std::printf("Internal %dx: %.2f ms/draw+compose, %dx%d (CPU raster; excludes upload/presentation)\n",vdp1_render_scale(),elapsed/10,frame.w,frame.h);
    }
    sf3::HdPresentation presentation;
    sf3::HdAssets assets;
    if(!pack.empty()) { std::printf("Loaded %zu HD assets\n",assets.load(pack));presentation.assets(&assets); }
#ifdef SF3_GPU_DIAGNOSTIC
    // Capture the complete guest video inputs after redraw. GPU residency and
    // replacement-art diagnostics must not alter guest RAM or framebuffers.
    auto guest_video=[]() {
        std::vector<unsigned char> result;
        FILE* file=std::tmpfile();if(!file)return result;
        vdp1_dump(file,true);std::fflush(file);std::fseek(file,0,SEEK_END);
        const auto length=std::ftell(file);std::rewind(file);
        if(length<0) {std::fclose(file);return result;}
        result.resize(size_t(length));
        if(std::fread(result.data(),1,result.size(),file)!=result.size())result.clear();
        std::fclose(file);if(result.empty())return result;
        result.insert(result.end(),std::begin(g_vdp2_vram),std::end(g_vdp2_vram));
        result.insert(result.end(),std::begin(g_vdp2_cram),std::end(g_vdp2_cram));
        result.insert(result.end(),std::begin(g_vdp2_regs),std::end(g_vdp2_regs));
        return result;
    };
    const auto initial_guest_video=guest_video();const auto initial_pixels=frame.px;
    if(initial_guest_video.empty()) {std::fprintf(stderr,"Guest video snapshot unavailable\n");return 18;}
    if(pack.empty() || !SDL_Init(SDL_INIT_VIDEO)) return 3;
    sf3::GpuRenderer native;auto* renderer=native.create_renderer(nullptr);
    sf3::HdGpu gpu;
    std::string shader_dir=std::string(SDL_GetBasePath())+"shaders";
    if(!renderer || !native.initialize(renderer,shader_dir) || !gpu.initialize(renderer,assets,shader_dir)) {
        std::fprintf(stderr,"GPU init: %s\n",SDL_GetError());return 4;}
    std::printf("GPU backend=%s adapter=%s\n",gpu.backend(),gpu.adapter());
    auto* scratch=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,1440,1080);
    if(!scratch || !SDL_SetRenderTarget(renderer,scratch)) return 5;
    auto* texture=gpu.render(frame,assets);
    if(!texture || !SDL_SetRenderTarget(renderer,texture)) {std::fprintf(stderr,"GPU render: %s\n",SDL_GetError());return 5;}
    auto* readback=SDL_RenderReadPixels(renderer,nullptr);
    if(!readback) {std::fprintf(stderr,"GPU readback: %s\n",SDL_GetError());return 6;}
    auto* converted=SDL_ConvertSurface(readback,SDL_PIXELFORMAT_ARGB8888);
    if(!converted) return 7;
    Frame actual;actual.w=1440;actual.h=1080;actual.px.resize(1440*1080);
    for(int y=0;y<1080;++y) std::memcpy(actual.px.data()+y*1440,(uint8_t*)converted->pixels+y*converted->pitch,1440*4);
    const auto& expected=assets.overlay(frame);size_t bad=0,different=0;int maximum=0;
    std::printf("Replacement coverage: %zu output pixels\n",assets.last_pixels());
    if(require_replacements && !assets.last_pixels())return 17;
    uint16_t single_owner=0;
    if(frame.hd)for(int y=0;y<expected.h && !single_owner;++y)for(int x=0;x<expected.w;++x) {
        if(!(expected.px[size_t(y)*expected.w+x]>>24))continue;
        const auto nx=std::min((int64_t(2*x+1)*frame.w)/(2*expected.w),int64_t(frame.w-1));
        const auto ny=std::min((int64_t(2*y+1)*frame.h)/(2*expected.h),int64_t(frame.h-1));
        single_owner=frame.hd->visible[size_t(ny)*frame.w+nx];if(single_owner)break;
    }
    write_png((std::string(argv[2])+".cpu-overlay.png").c_str(),expected);
    for(size_t i=0;i<actual.px.size();++i) {
        if(!(expected.px[i]>>24) && !(actual.px[i]>>24)) continue;
        int error=overlay_error(actual.px[i],expected.px[i]);
        maximum=std::max(maximum,error);bad+=error>2;different+=error>0;
    }
    write_png((std::string(argv[2])+".gpu-overlay.png").c_str(),actual);
    std::printf("GPU/CPU overlay (alpha and composition over black/white): %zu pixels differ, %zu exceed 2/255, maximum channel error %d\n",different,bad,maximum);
    SDL_SetRenderTarget(renderer,scratch);
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    auto* native_picture=native.render(frame,gpu_scale);
    auto* ai_picture=gpu.render(frame,assets);
    if(!native_picture || !ai_picture || !SDL_RenderTexture(renderer,native_picture,nullptr,nullptr)
       || !SDL_RenderTexture(renderer,ai_picture,nullptr,nullptr)) return 14;
    auto* combined=SDL_RenderReadPixels(renderer,nullptr);
    if(!combined)return 15;
    auto* combined_rgb=SDL_ConvertSurface(combined,SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(combined);if(!combined_rgb)return 16;
    Frame comparison;comparison.w=1440;comparison.h=1080;comparison.px.resize(1440*1080);
    for(int y=0;y<1080;++y)std::memcpy(comparison.px.data()+y*1440,(uint8_t*)combined_rgb->pixels+y*combined_rgb->pitch,1440*4);
    write_png((std::string(argv[2])+".live-composite.png").c_str(),comparison);
    SDL_DestroySurface(combined_rgb);
    SDL_DestroySurface(converted);SDL_DestroySurface(readback);SDL_SetRenderTarget(renderer,scratch);
    auto begin=std::chrono::steady_clock::now();
    auto cached_before=gpu.cache_hits(),uploads_before=gpu.art_uploads();
    for(int i=0;i<60;++i) {if(!gpu.render(frame,assets)) return 8;SDL_FlushRenderer(renderer);}
    if(gpu.cache_hits()!=cached_before+60 || gpu.art_uploads()!=uploads_before) {
        std::fprintf(stderr,"Unchanged AI frames must reuse their output and resident textures\n");return 22;
    }
    SDL_WaitForGPUIdle(SDL_GetGPURendererDevice(renderer));
    std::printf("GPU upload/projection/sampling/composition: %.3f ms/frame (60 frames, synchronized batch)\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()/60);
    // Exercise shader branches beyond this particular captured game's lighting.
    for(int test=0;test<4 && frame.hd;++test) {
        Frame variant=frame;variant.hd=std::make_shared<HdScene>(*frame.hd);
        variant.hd->geometry+=100+test;
        if(test==0) for(auto& q:variant.hd->quads) {
            q.hflip=!q.hflip;q.vflip=!q.vflip;
            for(int n=0;n<12;++n) q.shade[n]=float((n%5)-2)*4;
        }
        if(test==1) {
            variant.hd->effects.resize(variant.px.size());
            for(size_t i=0;i<variant.px.size();++i) {
                if(i%7==0) variant.hd->visible[i]=0;
                auto& e=variant.hd->effects[i];e.flags=uint8_t(1|(i%3==0?2:0)|(i%5==0?4:0));
                e.second=0x304060;e.ratio=uint8_t(i%32);e.offset[0]=-7;e.offset[1]=11;e.offset[2]=-2;
            }
        }
        if(test==2) for(auto& q:variant.hd->quads) for(auto& xy:q.xy) xy=0;
        if(test==3) variant.hd.reset();
        auto* result=gpu.render(variant,assets);
        if(!result || !SDL_SetRenderTarget(renderer,result)) return 10;
        auto* surface=SDL_RenderReadPixels(renderer,nullptr);
        if(!surface) return 11;
        auto* rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_ARGB8888);
        if(!rgba) return 12;
        const auto& reference=assets.overlay(variant);size_t errors=0;int maximum_error=0;
        for(int y=0;y<1080;++y) for(int x=0;x<1440;++x) {
            uint32_t got=((uint32_t*)((uint8_t*)rgba->pixels+y*rgba->pitch))[x],want=reference.px[y*1440+x];
            if(!(got>>24) && !(want>>24)) continue;
            int difference=overlay_error(got,want);
            maximum_error=std::max(maximum_error,difference);
            if(difference>2 && !errors)std::printf("GPU variant %d first difference at %d,%d: GPU %08X, CPU %08X\n",test,x,y,got,want);
            errors+=difference>2;
        }
        SDL_DestroySurface(rgba);SDL_DestroySurface(surface);SDL_SetRenderTarget(renderer,scratch);
        std::printf("GPU variant %d (flips/Gouraud, effects/occlusion, degenerate, absent metadata): %zu pixels exceed 2/255, maximum %d\n",test,errors,maximum_error);
        if(errors) return 13;
    }
    // Material streaming must survive changes in the visible working set.
    // The command-gating variants use exactly the captured sprite pixels:
    // character descriptors may match ordinary/scaled sprites, while world
    // descriptors and unsupported commands retain their native presentation.
    const char* variant_names[]={"empty residency","one visible material","all materials","same materials",
        "empty after populated","all after empty","character normal sprites","character scaled sprites","unsupported command"};
    gpu.release();
    if(!gpu.initialize(renderer,assets,shader_dir)) {std::fprintf(stderr,"GPU material reinitialization: %s\n",SDL_GetError());return 4;}
    uint64_t append_before=0,words_before=0;
    for(int test=0;test<9 && frame.hd;++test) {
        if(test==1 && !single_owner) {std::printf("GPU residency: one-material fixture unavailable\n");continue;}
        Frame variant=frame;variant.hd=std::make_shared<HdScene>(*frame.hd);variant.hd->geometry+=1000+test;
        if(test==0 || test==4)std::fill(variant.hd->visible.begin(),variant.hd->visible.end(),0);
        if(test==1)for(auto& owner:variant.hd->visible)if(owner!=single_owner)owner=0;
        if(test>=6)for(auto& quad:variant.hd->quads)
            quad.gpu_flags=(quad.gpu_flags&~(15u<<16))|(uint32_t(test==6?0:test==7?1:3)<<16);
        if(test==2){append_before=gpu.art_appends();words_before=gpu.art_words_uploaded();}
        auto* result=gpu.render(variant,assets);
        if(!result || !SDL_SetRenderTarget(renderer,result))return 10;
        auto* surface=SDL_RenderReadPixels(renderer,nullptr);if(!surface)return 11;
        auto* rgba=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_ARGB8888);if(!rgba)return 12;
        const auto& reference=assets.overlay(variant);const auto replacement_pixels=assets.last_pixels();
        size_t errors=0;int maximum_error=0;
        for(int y=0;y<1080;++y)for(int x=0;x<1440;++x) {
            const auto got=((uint32_t*)((uint8_t*)rgba->pixels+y*rgba->pitch))[x],want=reference.px[size_t(y)*1440+x];
            if(!(got>>24) && !(want>>24))continue;
            const int difference=overlay_error(got,want);maximum_error=std::max(maximum_error,difference);
            if(difference>2 && !errors)std::printf("GPU material variant %d first difference at %d,%d: GPU %08X, CPU %08X\n",test,x,y,got,want);
            errors+=difference>2;
        }
        SDL_DestroySurface(rgba);SDL_DestroySurface(surface);SDL_SetRenderTarget(renderer,scratch);
        std::printf("GPU material variant %d (%s): %zu replacement pixels; %zu pixels exceed 2/255, maximum %d\n",
            test,variant_names[test],replacement_pixels,errors,maximum_error);
        if(errors)return 13;
        if(test==2 && gpu.art_appends()>append_before) {
            if(gpu.art_words_uploaded()<=words_before)return 23;
            std::printf("GPU residency appended new materials without replacing existing offsets\n");
        }
        if((test==0 || test==4 || test==8) && replacement_pixels)return 19;
        if(require_replacements && test>=1 && test<=5 && test!=4 && !replacement_pixels)return 17;
        if(require_characters && (test==6 || test==7) && !replacement_pixels) {
            std::fprintf(stderr,"Character replacement fixture unavailable for command %d\n",test==6?0:1);return 20;
        }
    }
    // Submit changing scenes without an intervening readback. Each copy must
    // retain its own staging contents while subsequent frames cycle the buffer.
    std::vector<SDL_Texture*> queued;
    std::vector<Frame> references;
    for(int i=0;i<3 && frame.hd;++i) {
        Frame variant=frame;variant.hd=std::make_shared<HdScene>(*frame.hd);
        variant.hd->geometry+=2000+i;
        for(auto& q:variant.hd->quads) {
            q.hflip=(i&1)!=0;
            for(auto& shade:q.shade)shade=float(i*5-5);
        }
        auto* result=gpu.render(variant,assets);if(!result)return 24;
        auto* copy=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,1440,1080);
        if(!copy || !SDL_SetRenderTarget(renderer,copy))return 24;
        SDL_SetTextureBlendMode(result,SDL_BLENDMODE_NONE);
        if(!SDL_RenderTexture(renderer,result,nullptr,nullptr) || !SDL_FlushRenderer(renderer))return 24;
        SDL_SetTextureBlendMode(result,SDL_BLENDMODE_BLEND);
        SDL_SetRenderTarget(renderer,scratch);queued.push_back(copy);references.push_back(assets.overlay(variant));
    }
    for(size_t i=0;i<queued.size();++i) {
        if(!SDL_SetRenderTarget(renderer,queued[i]))return 24;
        auto* image=SDL_RenderReadPixels(renderer,nullptr);if(!image)return 24;
        auto* rgba=SDL_ConvertSurface(image,SDL_PIXELFORMAT_ARGB8888);if(!rgba)return 24;
        for(int y=0;y<1080;++y)for(int x=0;x<1440;++x) {
            auto got=((uint32_t*)((uint8_t*)rgba->pixels+y*rgba->pitch))[x];
            auto want=references[i].px[size_t(y)*1440+x];
            if(overlay_error(got,want)>2){std::fprintf(stderr,"Queued AI frame %zu differs at %d,%d: %08X vs %08X\n",i,x,y,got,want);return 25;}
        }
        SDL_DestroySurface(rgba);SDL_DestroySurface(image);SDL_SetRenderTarget(renderer,scratch);SDL_DestroyTexture(queued[i]);
    }
    std::printf("GPU queued AI scenes: %zu delayed readbacks preserve staging contents\n",queued.size());
    if(frame.px!=initial_pixels || guest_video()!=initial_guest_video) {
        std::fprintf(stderr,"Replacement rendering changed native pixels or guest video memory\n");return 21;
    }
    std::printf("Native frame pixels and guest VRAM/framebuffer/registers unchanged\n");
    gpu.release();native.release();SDL_DestroyTexture(scratch);SDL_DestroyRenderer(renderer);
    gpu.destroy_device();native.destroy_device();SDL_Quit();
    if(bad) return 9;
#endif
    write_png(argv[2], hd ? presentation.frame(frame) : frame);
#ifndef SF3_GPU_DIAGNOSTIC
    if(require_characters) {std::fprintf(stderr,"Character fixture validation requires render_dump_gpu\n");return 20;}
    if(!pack.empty()) std::printf("HD replacement pixels: %zu; recorded quads: %zu\n",assets.last_pixels(),frame.hd ? frame.hd->quads.size() : 0);
    if(require_replacements && !assets.last_pixels())return 17;
#endif
    if(bench) {
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<30;++i) {frame.px[0]^=1;presentation.frame(frame);}
        auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("HD full presentation benchmark: %.2f ms/changing frame (30 frames, excludes SDL upload)\n",ms/30);
        if(frame.hd) {
            start=std::chrono::steady_clock::now();
            for(int i=0;i<10;++i) {frame.px[0]^=1;++frame.hd->geometry;presentation.frame(frame);}
            ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            std::printf("HD reprojection benchmark: %.2f ms/frame (10 forced geometry changes, excludes SDL upload)\n",ms/10);
        }
    }
    return 0;
}
