// GPU presentation only. Original Saturn memory and framebuffer remain untouched.
#pragma once
#include "hd-assets.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace sf3 {
class HdGpu {
    SDL_GPUDevice* device_=nullptr;
    SDL_Renderer* renderer_=nullptr;
    SDL_GPUBuffer *scene_=nullptr,*art_=nullptr;
    SDL_GPUTransferBuffer* transfer_=nullptr;
    uint32_t transfer_capacity_=0,art_words_=0;
    SDL_GPUShader* shader_=nullptr;
    SDL_GPURenderState* state_=nullptr;
    SDL_Texture *white_=nullptr,*output_=nullptr;
    std::vector<uint32_t> offsets_;
    std::vector<size_t> resident_;
    std::vector<uint32_t> last_scene_;
    uint64_t art_uploads_=0,cache_hits_=0,art_words_uploaded_=0,art_appends_=0;
    const HdAssets* pack_=nullptr;
    uint32_t art_capacity_=0;
    bool resident_valid_=false;
    bool ready_=false;
    bool owns_device_=false;
    bool upload(SDL_GPUBuffer*,const std::vector<uint32_t>&,uint32_t offset=0,bool cycle=true);
    bool prepare_art(const HdAssets&,const std::vector<size_t>&);
public:
    // Backend can be explicitly selected with SF3_GPU_BACKEND=vulkan/direct3d12.
    SDL_Renderer* create_renderer(SDL_Window* window);
    bool initialize(SDL_Renderer*,const HdAssets&,const std::string& shader_dir);
    SDL_Texture* render(const Frame&,const HdAssets&);
    bool ready() const {return ready_;}
    uint64_t art_uploads() const {return art_uploads_;}
    uint64_t cache_hits() const {return cache_hits_;}
    uint64_t art_words_uploaded() const {return art_words_uploaded_;}
    uint64_t art_appends() const {return art_appends_;}
    const char* backend() const {return device_?SDL_GetGPUDeviceDriver(device_):"none";}
    const char* adapter() const {return device_?SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device_),SDL_PROP_GPU_DEVICE_NAME_STRING,"unknown"):"none";}
    // Call before destroying the renderer. The device may be shared with the
    // native geometry renderer, or owned when create_renderer made it.
    void release();
    void destroy_device();
};
}
