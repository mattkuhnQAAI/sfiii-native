// Final presentation filter. No changes to Saturn pixels or geometry.
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include "gpu-shader.h"

namespace sf3 {
enum class ImageFilter { Nearest, Bilinear, IntegerSmooth, Bicubic, Lanczos };
class ImageScaler {
    SDL_Texture* intermediate_=nullptr;
    int width_=0,height_=0;
    SDL_Renderer* gpu_renderer_=nullptr;
    SDL_GPUDevice* device_=nullptr;
    SDL_GPUShader* shader_=nullptr;
    SDL_GPURenderState* state_=nullptr;
    SDL_Texture *composed_=nullptr,*horizontal_=nullptr,*filtered_=nullptr;
    bool advanced_active_=false;
    static bool target(SDL_Renderer* renderer,SDL_Texture*& texture,int w,int h) {
        float oldw=0,oldh=0;if(texture)SDL_GetTextureSize(texture,&oldw,&oldh);
        if(int(oldw)==w && int(oldh)==h)return true;
        if(texture)SDL_DestroyTexture(texture);
        texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w,h);
        return texture && SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_NONE);
    }
    bool initialize(SDL_Renderer* renderer) {
        if(state_ && renderer==gpu_renderer_)return true;
        if(gpu_renderer_ && gpu_renderer_!=renderer)return SDL_SetError("Release display filter before changing renderer");
        device_=SDL_GetGPURendererDevice(renderer);
        if(!device_)return SDL_SetError("Bicubic and Lanczos require the GPU renderer");
        const char* override_dir=std::getenv("SF3_GPU_SHADER_DIR");
        const std::string directory=override_dir?override_dir:std::string(SDL_GetBasePath())+"shaders";
        FragmentShader code;if(!code.read(device_,directory,"display-filter"))return false;
        SDL_GPUShaderCreateInfo si{};si.code=code.bytes.data();si.code_size=code.bytes.size();si.entrypoint=code.entry;si.format=code.format;
        si.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;si.num_samplers=1;si.num_uniform_buffers=1;
        shader_=SDL_CreateGPUShader(device_,&si);if(!shader_)return false;
        SDL_GPURenderStateCreateInfo ri{};ri.fragment_shader=shader_;
        state_=SDL_CreateGPURenderState(renderer,&ri);
        if(!state_) {SDL_ReleaseGPUShader(device_,shader_);shader_=nullptr;device_=nullptr;return false;}
        gpu_renderer_=renderer;return true;
    }
    bool advanced(SDL_Renderer* renderer,SDL_Texture* base,SDL_Texture* overlay,
                  const SDL_FRect& destination,ImageFilter filter) {
        float sw=0,sh=0;if(!SDL_GetTextureSize(base,&sw,&sh))return false;
        const auto limit=SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER,8192);
        if(destination.w>limit || destination.h>limit)return SDL_SetError("Display filter dimensions exceed GPU limit");
        const int w=int(std::ceil(destination.w)),h=int(std::ceil(destination.h));
        if(w>limit || h>limit || double(w)*sh>40000000 || double(w)*h>40000000 ||
           std::max(sw/w,sh/h)>10)return SDL_SetError("Display filter exceeds GPU sampling budget");
        if(!initialize(renderer) || !target(renderer,horizontal_,w,int(sh)) || !target(renderer,filtered_,w,h))return false;
        auto* previous=SDL_GetRenderTarget(renderer);bool ok=true;
        SDL_Texture* source=base;
        if(overlay) {
            ok=target(renderer,composed_,int(sw),int(sh)) && SDL_SetRenderTarget(renderer,composed_) &&
               layers(renderer,base,overlay,{0,0,sw,sh},SDL_SCALEMODE_NEAREST);
            source=composed_;
        }
        auto pass=[&](SDL_Texture* input,SDL_Texture* output,float inw,float inh,bool horizontal) {
            const float values[]={inw,inh,float(w),horizontal?inh:float(h),horizontal?1.f:0.f,horizontal?0.f:1.f,filter==ImageFilter::Lanczos?1.f:0.f,0};
            return SDL_SetRenderTarget(renderer,output) && SDL_SetTextureScaleMode(input,SDL_SCALEMODE_NEAREST) &&
                   SDL_SetGPURenderStateFragmentUniforms(state_,0,values,sizeof values) &&
                   SDL_SetGPURenderState(renderer,state_) && SDL_RenderTexture(renderer,input,nullptr,nullptr);
        };
        if(ok)ok=pass(source,horizontal_,sw,sh,true);
        if(ok)ok=pass(horizontal_,filtered_,float(w),sh,false);
        const bool restored=SDL_SetGPURenderState(renderer,nullptr) && SDL_SetRenderTarget(renderer,previous);
        return ok && restored && SDL_SetTextureScaleMode(filtered_,SDL_SCALEMODE_LINEAR) &&
               SDL_RenderTexture(renderer,filtered_,nullptr,&destination);
    }
    static bool layers(SDL_Renderer* renderer,SDL_Texture* base,SDL_Texture* overlay,
                       const SDL_FRect& destination,SDL_ScaleMode mode) {
        if(!SDL_SetTextureScaleMode(base,mode) || !SDL_RenderTexture(renderer,base,nullptr,&destination))return false;
        return !overlay || (SDL_SetTextureScaleMode(overlay,mode) && SDL_RenderTexture(renderer,overlay,nullptr,&destination));
    }
public:
    void release() {
        if(intermediate_)SDL_DestroyTexture(intermediate_);intermediate_=nullptr;width_=height_=0;
        for(auto** texture:{&composed_,&horizontal_,&filtered_}) {if(*texture)SDL_DestroyTexture(*texture);*texture=nullptr;}
        if(state_)SDL_DestroyGPURenderState(state_);state_=nullptr;
        if(shader_)SDL_ReleaseGPUShader(device_,shader_);shader_=nullptr;device_=nullptr;gpu_renderer_=nullptr;
    }
    int intermediate_width() const {return width_;}
    int intermediate_height() const {return height_;}
    bool advanced_active() const {return advanced_active_;}
    bool draw(SDL_Renderer* renderer,SDL_Texture* base,SDL_Texture* overlay,
              const SDL_FRect& destination,ImageFilter filter) {
        if(!base || !std::isfinite(destination.w) || !std::isfinite(destination.h) || destination.w<=0 || destination.h<=0)
            return SDL_SetError("Invalid presentation dimensions");
        advanced_active_=false;
        if(filter==ImageFilter::Bicubic || filter==ImageFilter::Lanczos) {
            advanced_active_=advanced(renderer,base,overlay,destination,filter);return advanced_active_;
        }
        if(filter!=ImageFilter::IntegerSmooth) {
            release();return layers(renderer,base,overlay,destination,
                filter==ImageFilter::Nearest?SDL_SCALEMODE_NEAREST:SDL_SCALEMODE_LINEAR);
        }
        float sw=0,sh=0;if(!SDL_GetTextureSize(base,&sw,&sh) || sw<=0 || sh<=0)return false;
        // Downscaling and less-than-2x enlargement need no intermediate allocation.
        const auto limit=SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER,8192);
        double factor=std::floor(std::min(double(destination.w)/sw,double(destination.h)/sh));
        factor=std::min({factor,double(limit)/sw,double(limit)/sh,std::sqrt(16777216.0/(double(sw)*sh))});
        const int step=int(std::max(1.0,std::floor(factor)));
        if(step<2) {release();return layers(renderer,base,overlay,destination,SDL_SCALEMODE_LINEAR);}
        const int width=int(sw)*step,height=int(sh)*step;
        if(width!=width_ || height!=height_) {
            release();intermediate_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width,height);
            if(!intermediate_)return layers(renderer,base,overlay,destination,SDL_SCALEMODE_LINEAR);
            width_=width;height_=height;
            SDL_SetTextureBlendMode(intermediate_,SDL_BLENDMODE_NONE);
            SDL_SetTextureScaleMode(intermediate_,SDL_SCALEMODE_LINEAR);
        }
        auto* previous=SDL_GetRenderTarget(renderer);
        if(previous==intermediate_ || !SDL_SetRenderTarget(renderer,intermediate_))return false;
        // Base is opaque; the optional art overlay keeps its own blend mode.
        const bool drawn=layers(renderer,base,overlay,SDL_FRect{0,0,float(width),float(height)},SDL_SCALEMODE_NEAREST);
        if(!SDL_SetRenderTarget(renderer,previous))return false;
        return drawn && SDL_RenderTexture(renderer,intermediate_,nullptr,&destination);
    }
};
}
