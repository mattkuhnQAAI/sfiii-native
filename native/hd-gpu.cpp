#include "hd-gpu.h"
#include "gpu-shader.h"
#include "gpu-residency.h"
#include <cstring>
#include <array>

namespace sf3 {
static constexpr uint32_t scene_bytes=8*1024*1024;
SDL_Renderer* HdGpu::create_renderer(SDL_Window* window) {
    const char* requested=std::getenv("SF3_GPU_BACKEND");
    for(const char* backend:{"vulkan","direct3d12","metal"}) {
        if(requested && *requested && std::strcmp(requested,backend)) continue;
        auto props=SDL_CreateProperties();
        SDL_SetStringProperty(props,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,backend);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,true);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN,true);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN,true);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,std::getenv("SF3_GPU_DEBUG")!=nullptr);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,std::getenv("SF3_GPU_LOW_POWER")!=nullptr);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_VULKAN_REQUIRE_HARDWARE_ACCELERATION_BOOLEAN,std::getenv("SF3_GPU_ALLOW_SOFTWARE")==nullptr);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN,true);
        // The presentation shader does not use these optional Vulkan features.
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN,false);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN,false);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN,false);
        SDL_SetBooleanProperty(props,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN,false);
        device_=SDL_CreateGPUDeviceWithProperties(props);SDL_DestroyProperties(props);
        if(!device_) continue;
        auto* renderer=SDL_CreateGPURenderer(device_,window);
        if(renderer) {owns_device_=true;return renderer;}
        SDL_DestroyGPUDevice(device_);device_=nullptr;
    }
    return nullptr;
}
bool HdGpu::upload(SDL_GPUBuffer* buffer,const std::vector<uint32_t>& words,uint32_t offset,bool cycle) {
    if(!buffer || words.empty() || words.size()>UINT32_MAX/4 || offset>UINT32_MAX-words.size()*4)
        return SDL_SetError("Invalid HD GPU upload size");
    auto size=uint32_t(words.size()*4);
    if(size>transfer_capacity_) {
        SDL_GPUTransferBufferCreateInfo ti{};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size=uint32_t(std::min(uint64_t(UINT32_MAX),std::max(uint64_t(size),uint64_t(transfer_capacity_)*2)));
        auto* next=SDL_CreateGPUTransferBuffer(device_,&ti);if(!next)return false;
        if(transfer_)SDL_ReleaseGPUTransferBuffer(device_,transfer_);
        transfer_=next;transfer_capacity_=ti.size;
    }
    // Cycle staging memory, not the retained artwork destination: queued draws
    // may still reference earlier uploads. GPU copies remain submission ordered.
    void* mapped=SDL_MapGPUTransferBuffer(device_,transfer_,true);
    if(!mapped) return false;
    std::memcpy(mapped,words.data(),size);SDL_UnmapGPUTransferBuffer(device_,transfer_);
    auto* command=SDL_AcquireGPUCommandBuffer(device_);
    if(!command) return false;
    auto* copy=SDL_BeginGPUCopyPass(command);
    if(!copy) {SDL_CancelGPUCommandBuffer(command);return false;}
    SDL_GPUTransferBufferLocation source{transfer_,0};SDL_GPUBufferRegion dest{buffer,offset,size};
    SDL_UploadToGPUBuffer(copy,&source,&dest,cycle);SDL_EndGPUCopyPass(copy);
    return SDL_SubmitGPUCommandBuffer(command);
}
bool HdGpu::prepare_art(const HdAssets& assets,const std::vector<size_t>& needed) {
    const bool reuse=std::getenv("SF3_AI_GPU_CACHE_DISABLE")==nullptr;
    if(resident_valid_ && (reuse?std::includes(resident_.begin(),resident_.end(),needed.begin(),needed.end()):resident_==needed)) return true;
    // Animation changes the visible set frequently. Retain available recent
    // materials within a bounded working set instead of replacing the entire
    // GPU pack whenever a sprite appears or disappears.
    constexpr size_t retained_limit=64*1024*1024/4;
    auto working=retain_gpu_materials(needed,resident_valid_ && reuse?resident_:std::vector<size_t>{},retained_limit,
        [&](size_t index){return index<assets.assets_.size()?assets.assets_[index].replacement.px.size():size_t(0);});
    // Keep existing offsets stable while the working set grows. Previously one
    // newly revealed animation texture recopied every retained texture.
    if(reuse && resident_valid_ && std::includes(working.begin(),working.end(),resident_.begin(),resident_.end())) {
        std::vector<size_t> added;
        std::set_difference(working.begin(),working.end(),resident_.begin(),resident_.end(),std::back_inserter(added));
        std::vector<uint32_t> pixels;auto offsets=offsets_;
        for(auto index:added) {
            const auto& r=assets.assets_[index].replacement;
            if(r.w<1 || r.h<1 || r.px.size()!=size_t(r.w)*r.h)return SDL_SetError("New HD material unavailable");
            if(r.px.size()>UINT32_MAX/4-art_words_ || pixels.size()>UINT32_MAX/4-art_words_-r.px.size())
                return SDL_SetError("New HD materials exceed GPU buffer limit");
            offsets[index]=art_words_+uint32_t(pixels.size());
            pixels.insert(pixels.end(),r.px.begin(),r.px.end());
        }
        if(!pixels.empty() && (uint64_t(art_words_)+pixels.size())*4<=art_capacity_) {
            if(!SDL_FlushRenderer(renderer_) || !upload(art_,pixels,art_words_*4,false))return false;
            art_words_+=uint32_t(pixels.size());offsets_=std::move(offsets);resident_=std::move(working);
            last_scene_.clear();++art_uploads_;++art_appends_;art_words_uploaded_+=pixels.size();return true;
        }
    }
    size_t words=0;
    for(auto index:working) {
        if(index>=assets.assets_.size()) return SDL_SetError("Invalid visible HD material");
        const auto& replacement=assets.assets_[index].replacement;
        size_t size=replacement.px.size();
        if(replacement.w<1 || replacement.h<1 || size!=size_t(replacement.w)*replacement.h)
            return SDL_SetError("Visible HD material pixels are unavailable");
        if(size>UINT32_MAX/4-words) return SDL_SetError("Visible HD materials exceed GPU buffer limit");
        words+=size;
    }
    std::vector<uint32_t> offsets(assets.assets_.size(),UINT32_MAX),art;
    art.reserve(std::max(size_t(1),words));
    for(auto index:working) {
        offsets[index]=uint32_t(art.size());
        const auto& pixels=assets.assets_[index].replacement.px;
        art.insert(art.end(),pixels.begin(),pixels.end());
    }
    if(art.empty()) art.push_back(0); // Keep a valid storage binding for empty scenes.
    if(art_ && !SDL_FlushRenderer(renderer_))return false;
    last_scene_.clear();
    uint32_t required=uint32_t(art.size()*4);
    if(required>art_capacity_) {
        // Grow without discarding the previous working binding on allocation failure.
        uint64_t capacity=std::max(uint64_t(required),uint64_t(art_capacity_)*2);
        if(!working.empty())capacity=std::max(capacity,uint64_t(retained_limit)*4);
        capacity=std::min(capacity,uint64_t(UINT32_MAX/4)*4);
        SDL_GPUBufferCreateInfo bi{};bi.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;bi.size=uint32_t(capacity);
        auto* buffer=SDL_CreateGPUBuffer(device_,&bi);if(!buffer) return false;
        SDL_GPUBuffer* buffers[]{scene_,buffer};SDL_GPURenderStateCreateInfo ri{};
        ri.fragment_shader=shader_;ri.num_storage_buffers=2;ri.storage_buffers=buffers;
        auto* state=SDL_CreateGPURenderState(renderer_,&ri);
        if(!state || !upload(buffer,art)) {
            if(state) SDL_DestroyGPURenderState(state);
            SDL_ReleaseGPUBuffer(device_,buffer);return false;
        }
        // render() flushed earlier draws before reaching this point. SDL defers
        // GPU buffer destruction until submitted commands have finished with it.
        if(state_) SDL_DestroyGPURenderState(state_);
        if(art_) SDL_ReleaseGPUBuffer(device_,art_);
        state_=state;art_=buffer;art_capacity_=uint32_t(capacity);
    } else {
        resident_valid_=false;
        if(!upload(art_,art)) return false;
    }
    offsets_=std::move(offsets);resident_=std::move(working);resident_valid_=true;art_words_=uint32_t(art.size());
    ++art_uploads_;art_words_uploaded_+=art.size();
    return true;
}
bool HdGpu::initialize(SDL_Renderer* renderer,const HdAssets& assets,const std::string& dir) {
    renderer_=renderer;
    auto* renderer_device=SDL_GetGPURendererDevice(renderer);
    if(!renderer_device || (device_ && renderer_device!=device_)) return SDL_SetError("GPU shader renderer not available");
    if(!device_) {device_=renderer_device;owns_device_=false;}
    if(assets.assets_.empty()) return SDL_SetError("No HD asset pack loaded");
    FragmentShader code;if(!code.read(device_,dir,"hd-assets"))return false;
    SDL_GPUShaderCreateInfo si{};si.code=code.bytes.data();si.code_size=code.bytes.size();si.entrypoint=code.entry;
    si.format=code.format;si.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;si.num_samplers=1;si.num_storage_buffers=2;
    shader_=SDL_CreateGPUShader(device_,&si);if(!shader_) return false;
    SDL_GPUBufferCreateInfo bi{};bi.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;bi.size=scene_bytes;
    scene_=SDL_CreateGPUBuffer(device_,&bi);if(!scene_) return false;
    pack_=&assets;resident_valid_=false;
    if(!prepare_art(assets,{})) return false;
    white_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,1,1);
    output_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,1440,1080);
    if(!white_ || !output_) return false;
    uint32_t white=0xFFFFFFFF;SDL_UpdateTexture(white_,nullptr,&white,4);
    SDL_SetTextureBlendMode(white_,SDL_BLENDMODE_NONE);SDL_SetTextureBlendMode(output_,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(output_,SDL_SCALEMODE_LINEAR);
    ready_=true;return true;
}
SDL_Texture* HdGpu::render(const Frame& frame,const HdAssets& assets) {
    if(!ready_) return nullptr;
    if(pack_!=&assets || offsets_.size()!=assets.assets_.size()) {
        SDL_SetError("HD asset pack changed; GPU initialization required");return nullptr;
    }
    auto* old_target=SDL_GetRenderTarget(renderer_);
    // Submit the preceding geometry draw before CPU matching work, preserving
    // GPU/CPU overlap even when the AI overlay itself is a repeated frame.
    if(!SDL_FlushRenderer(renderer_))return nullptr;
    assets.begin_frame();
    if(frame.w<1 || frame.h<1) {SDL_SetError("Invalid HD frame dimensions");return nullptr;}
    size_t pixels=size_t(frame.w)*frame.h;
    bool valid=frame.hd && frame.hd->visible.size()==pixels;
    size_t count=valid?frame.hd->quads.size():0;
    if(pixels>(scene_bytes/4-4)/8) {SDL_SetError("HD scene exceeds GPU buffer limit");return nullptr;}
    size_t quad_offset=4+pixels*8;
    if(count>(scene_bytes/4-quad_offset)/32) {SDL_SetError("HD scene exceeds GPU buffer limit");return nullptr;}
    std::vector<uint32_t> data(quad_offset+count*32);
    data[0]=frame.w;data[1]=frame.h;data[2]=uint32_t(quad_offset);data[3]=uint32_t(count);
    std::vector<size_t> needed,materials(count,SIZE_MAX);
    if(valid) {
        std::vector<uint8_t> visible(count,0);
        for(size_t i=0;i<pixels;++i) {
            auto at=4+i*8;data[at]=frame.hd->visible[i];
            if(data[at] && data[at]<=count) visible[data[at]-1]=1;
            if(frame.hd->effects.size()==pixels) {const auto& e=frame.hd->effects[i];data[at+1]=e.second;
                for(int c=0;c<3;++c) data[at+2+c]=uint32_t(int32_t(e.offset[c]));
                data[at+5]=e.ratio;data[at+6]=e.flags;}
        }
        // A texture may occur in both a character command and a world command;
        // eligibility belongs to the texture/command pair.
        struct Matches {std::array<const HdAssets::Asset*,16> asset{};uint16_t attempted=0;};
        std::unordered_map<const HdTexture*,Matches> matches;
        for(size_t i=0;i<count;++i) {
            auto at=quad_offset+i*32;const auto& q=frame.hd->quads[i];data[at+22]=UINT32_MAX;
            if(!visible[i] || !q.gpu_eligible || !q.texture) continue;
            unsigned command=(q.gpu_flags>>16)&15;
            auto it=matches.try_emplace(q.texture.get()).first;
            auto*& a=it->second.asset[command];unsigned mask=1u<<command;
            if(!(it->second.attempted&mask)) {a=assets.match(*q.texture,command);it->second.attempted|=mask;}
            if(!a) continue;
            materials[i]=size_t(a-assets.assets_.data());needed.push_back(materials[i]);
            float xy[8];std::memcpy(xy,q.xy,sizeof(xy));
            // Ownership is indexed in the expanded frame, while submitted
            // vertices retain guest screen coordinates. Project in one space.
            for(int vertex=0;vertex<4;++vertex) xy[vertex*2]+=frame.origin_x;
            std::memcpy(data.data()+at,xy,sizeof(xy));std::memcpy(data.data()+at+8,q.shade,48);
            data[at+20]=q.hflip;data[at+21]=q.vflip;
            data[at+23]=a->replacement.w;data[at+24]=a->replacement.h;
            std::memcpy(data.data()+at+25,a->uv,16);
        }
    }
    std::sort(needed.begin(),needed.end());needed.erase(std::unique(needed.begin(),needed.end()),needed.end());
    if(!prepare_art(assets,needed)) return nullptr;
    for(size_t i=0;i<count;++i) if(materials[i]!=SIZE_MAX) data[quad_offset+i*32+22]=offsets_[materials[i]];
    if(std::getenv("SF3_AI_GPU_CACHE_DISABLE")==nullptr && data==last_scene_){++cache_hits_;return output_;}
    if(!SDL_FlushRenderer(renderer_))return nullptr;
    if(!upload(scene_,data)) return nullptr;
    if(!SDL_SetRenderTarget(renderer_,output_)) return nullptr;
    SDL_SetRenderDrawColor(renderer_,0,0,0,0);SDL_RenderClear(renderer_);
    SDL_SetGPURenderState(renderer_,state_);
    bool ok=SDL_RenderTexture(renderer_,white_,nullptr,nullptr);
    SDL_SetGPURenderState(renderer_,nullptr);SDL_SetRenderTarget(renderer_,old_target);
    if(ok)last_scene_=std::move(data);
    return ok?output_:nullptr;
}
void HdGpu::release() {
    ready_=false;
    if(renderer_) SDL_FlushRenderer(renderer_);
    if(state_) SDL_DestroyGPURenderState(state_);
    if(white_) SDL_DestroyTexture(white_);if(output_) SDL_DestroyTexture(output_);
    if(device_) {if(transfer_)SDL_ReleaseGPUTransferBuffer(device_,transfer_);if(scene_) SDL_ReleaseGPUBuffer(device_,scene_);if(art_) SDL_ReleaseGPUBuffer(device_,art_);if(shader_) SDL_ReleaseGPUShader(device_,shader_);}
    state_=nullptr;white_=output_=nullptr;scene_=art_=nullptr;shader_=nullptr;offsets_.clear();
    resident_.clear();resident_valid_=false;pack_=nullptr;art_capacity_=0;
    last_scene_.clear();
    transfer_=nullptr;transfer_capacity_=0;art_words_=0;
}
void HdGpu::destroy_device() {if(device_ && owns_device_) SDL_DestroyGPUDevice(device_);device_=nullptr;owns_device_=false;}
}
