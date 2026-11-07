// Invented pixel patterns exercise actual SDL filtering; no game data.
#include "image-scaler.h"
#include "gpu-renderer.h"
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc,char** argv) {
    const bool gpu=argc==2 && std::strcmp(argv[1],"--gpu")==0;
    if(!SDL_Init(SDL_INIT_VIDEO))return 2;
    auto* window=SDL_CreateWindow("Image scaling tests",64,64,SDL_WINDOW_HIDDEN);
    sf3::GpuRenderer backend;
    auto* renderer=gpu?backend.create_renderer(window):SDL_CreateRenderer(window,nullptr);
    if(!renderer){std::printf("Renderer unavailable: %s\n",SDL_GetError());return 3;}
    auto* source=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,2,2);
    auto* overlay=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,2,2);
    auto* output=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,7,7);
    const uint32_t pixels[]={0xFF000000,0xFFFFFFFF,0xFF000000,0xFFFFFFFF};
    const uint32_t art[]={0x8000FF00,0,0x8000FF00,0};
    if(!source || !overlay || !output || !SDL_UpdateTexture(source,nullptr,pixels,8) || !SDL_UpdateTexture(overlay,nullptr,art,8))return 4;
    SDL_SetTextureBlendMode(source,SDL_BLENDMODE_NONE);SDL_SetTextureBlendMode(overlay,SDL_BLENDMODE_BLEND);
    sf3::ImageScaler scaler;
    auto read=[&]() {
        auto* surface=SDL_RenderReadPixels(renderer,nullptr);if(!surface)return std::vector<uint32_t>{};
        auto* rgb=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(surface);
        if(!rgb)return std::vector<uint32_t>{};
        std::vector<uint32_t> result(rgb->w*rgb->h);
        for(int y=0;y<rgb->h;++y)std::memcpy(result.data()+y*rgb->w,static_cast<uint8_t*>(rgb->pixels)+y*rgb->pitch,rgb->w*4);
        SDL_DestroySurface(rgb);return result;
    };
    std::vector<std::vector<uint32_t>> samples;
    for(auto mode:{sf3::ImageFilter::Nearest,sf3::ImageFilter::Bilinear,sf3::ImageFilter::IntegerSmooth}) {
        SDL_SetRenderTarget(renderer,output);SDL_SetRenderDrawColor(renderer,255,0,255,255);SDL_RenderClear(renderer);
        if(!scaler.draw(renderer,source,nullptr,{1,1,5,5},mode) || SDL_GetRenderTarget(renderer)!=output)return 5;
        samples.push_back(read());if(samples.back().size()!=49)return 6;
        for(int y=0;y<7;++y)for(int x=0;x<7;++x)
            if((x==0 || y==0 || x==6 || y==6) && (samples.back()[y*7+x]&0xFFFFFF)!=0xFF00FF)return 7;
        std::printf("mode %d:",int(mode));for(int x=1;x<=5;++x)std::printf(" %u",(samples.back()[3*7+x]>>16)&255);std::puts("");
    }
    auto red=[&](int mode,int x){return int(samples[mode][3*7+1+x]>>16&255);};
    if(red(0,1)!=0 || red(0,3)!=255)return 8;
    if(red(1,1)<10 || red(1,1)>45 || red(1,3)<210 || red(1,3)>245)return 9;
    if(red(2,1)>3 || red(2,3)<252 || red(2,2)<120 || red(2,2)>135)return 10;
    if(scaler.intermediate_width()!=4 || scaler.intermediate_height()!=4)return 11;
    if(!scaler.draw(renderer,source,overlay,{1,1,5,5},sf3::ImageFilter::IntegerSmooth))return 12;
    auto blended=read();if(blended.size()!=49 || ((blended[3*7+1]>>8)&255)<120 || (blended[3*7+1]&255)>3)return 13;
    // An integer enlargement stays crisp; downsampling releases its scratch target.
    if(!scaler.draw(renderer,source,nullptr,{1,1,4,4},sf3::ImageFilter::IntegerSmooth))return 14;
    if(!scaler.draw(renderer,source,nullptr,{1,1,1,1},sf3::ImageFilter::IntegerSmooth) || scaler.intermediate_width()!=0)return 15;
    if(scaler.draw(renderer,source,nullptr,{0,0,0,5},sf3::ImageFilter::Nearest))return 16;
    scaler.release();SDL_SetRenderTarget(renderer,nullptr);
    SDL_DestroyTexture(output);SDL_DestroyTexture(overlay);SDL_DestroyTexture(source);
    SDL_DestroyRenderer(renderer);backend.destroy_device();SDL_DestroyWindow(window);SDL_Quit();
    std::puts("PASS: nearest, bilinear, integer-smooth, alpha overlay, bounds, target restoration and downsampling");
    return 0;
}
