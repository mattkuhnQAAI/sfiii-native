#include "gpu-renderer.h"
#include "gpu-shader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace sf3 {
static constexpr uint32_t capacity=32*1024*1024;
static constexpr uint32_t tile_size=4;
SDL_Renderer* GpuRenderer::create_renderer(SDL_Window* window) {
    const char* requested=std::getenv("SF3_GPU_BACKEND");
    for(const char* backend:{"vulkan","direct3d12","metal"}) {
        if(requested && *requested && std::strcmp(requested,backend)) continue;
        auto p=SDL_CreateProperties();
        SDL_SetStringProperty(p,SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING,backend);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN,true);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN,true);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN,true);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN,std::getenv("SF3_GPU_DEBUG")!=nullptr);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN,std::getenv("SF3_GPU_LOW_POWER")!=nullptr);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_VULKAN_REQUIRE_HARDWARE_ACCELERATION_BOOLEAN,std::getenv("SF3_GPU_ALLOW_SOFTWARE")==nullptr);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN,true);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN,false);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN,false);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN,false);
        SDL_SetBooleanProperty(p,SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN,false);
        device_=SDL_CreateGPUDeviceWithProperties(p);SDL_DestroyProperties(p);
        if(!device_) continue;
        if(auto* r=SDL_CreateGPURenderer(device_,window)) return r;
        SDL_DestroyGPUDevice(device_);device_=nullptr;
    }
    return nullptr;
}
bool GpuRenderer::upload(SDL_GPUBuffer* buffer,const std::vector<uint32_t>& words) {
    uint32_t bytes=uint32_t(words.size()*4);
    SDL_GPUTransferBufferCreateInfo ti{};ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;ti.size=bytes;
    auto* transfer=SDL_CreateGPUTransferBuffer(device_,&ti);if(!transfer)return false;
    auto* data=SDL_MapGPUTransferBuffer(device_,transfer,false);
    if(!data){SDL_ReleaseGPUTransferBuffer(device_,transfer);return false;}
    std::memcpy(data,words.data(),bytes);SDL_UnmapGPUTransferBuffer(device_,transfer);
    auto* command=SDL_AcquireGPUCommandBuffer(device_);
    if(!command){SDL_ReleaseGPUTransferBuffer(device_,transfer);return false;}
    auto* copy=SDL_BeginGPUCopyPass(command);
    SDL_GPUTransferBufferLocation from{transfer,0};SDL_GPUBufferRegion to{buffer,0,bytes};
    SDL_UploadToGPUBuffer(copy,&from,&to,true);SDL_EndGPUCopyPass(copy);
    bool ok=SDL_SubmitGPUCommandBuffer(command);SDL_ReleaseGPUTransferBuffer(device_,transfer);return ok;
}
bool GpuRenderer::initialize(SDL_Renderer* renderer,const std::string& dir) {
    renderer_=renderer;
    if(!device_ || SDL_GetGPURendererDevice(renderer)!=device_)return SDL_SetError("GPU shader renderer unavailable");
    FragmentShader code;if(!code.read(device_,dir,"geometry"))return false;
    SDL_GPUShaderCreateInfo si{};si.code=code.bytes.data();si.code_size=code.bytes.size();si.entrypoint=code.entry;si.format=code.format;
    si.stage=SDL_GPU_SHADERSTAGE_FRAGMENT;si.num_samplers=1;si.num_storage_buffers=2;si.num_uniform_buffers=1;
    shader_=SDL_CreateGPUShader(device_,&si);if(!shader_)return false;
    SDL_GPUBufferCreateInfo bi{};bi.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;bi.size=capacity;
    scene_=SDL_CreateGPUBuffer(device_,&bi);textures_=SDL_CreateGPUBuffer(device_,&bi);
    if(!scene_ || !textures_)return false;
    SDL_GPUBuffer* buffers[]={scene_,textures_};SDL_GPURenderStateCreateInfo ri{};
    ri.fragment_shader=shader_;ri.num_storage_buffers=2;ri.storage_buffers=buffers;
    state_=SDL_CreateGPURenderState(renderer,&ri);
    white_=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,1,1);
    if(!state_ || !white_)return false;
    const uint32_t white=0xFFFFFFFF;SDL_UpdateTexture(white_,nullptr,&white,4);SDL_SetTextureBlendMode(white_,SDL_BLENDMODE_NONE);
    return true;
}
SDL_Texture* GpuRenderer::render(const Frame& f,int scale) {
    if(!ready() || (scale!=1 && scale!=2 && scale!=4 && scale!=5) || f.w<1 || f.h<1 || f.w>1024 || f.h>512 || f.px.size()!=size_t(f.w)*f.h)return nullptr;
    const int width=f.w*scale,height=f.h*scale;
    if(width!=width_ || height!=height_) {
        last_scene_.clear();last_textures_.clear();
        if(output_)SDL_DestroyTexture(output_);
        if(filtered_)SDL_DestroyTexture(filtered_);filtered_=nullptr;filtered_valid_=false;
        output_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width,height);
        if(!output_)return nullptr;
        width_=width;height_=height;SDL_SetTextureBlendMode(output_,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(output_,SDL_SCALEMODE_LINEAR);
    }
    const bool geometry=f.hd && f.hd->composition.size()==f.px.size();
    const size_t count=geometry?f.hd->quads.size():0;
    const uint32_t columns=(f.w+tile_size-1)/tile_size,rows=(f.h+tile_size-1)/tile_size,pixels=uint32_t(f.px.size());
    const uint32_t quad_at=12+pixels*10,bin_at=quad_at+uint32_t(count)*40,index_at=bin_at+columns*rows*2;
    std::vector<uint32_t> data(index_at),tex(1,0);
    std::vector<std::vector<uint32_t>> bins(columns*rows);
    data[0]=f.w;data[1]=f.h;data[2]=scale;data[3]=uint32_t(count);data[4]=12;data[5]=quad_at;
    data[6]=bin_at;data[7]=index_at;data[8]=columns;
    data[9]=f.origin_x;data[10]=tile_size;data[11]=uint32_t(f.native_w>0?f.native_w:f.w);
    for(uint32_t i=0;i<pixels;++i) {
        const auto at=12+i*10;data[at]=f.px[i];data[at+3]=0x100;
        if(geometry) {
            const auto& e=f.hd->composition[i];data[at+1]=e.absent;data[at+2]=e.constant;data[at+3]=e.mode;data[at+4]=e.ratio;
            for(int c=0;c<3;++c)data[at+5+c]=uint32_t(e.offset[c]);data[at+8]=e.owner;
        }
    }
    // End codes terminate a source row. Resolve the two directions once per
    // texture/mode, rather than scanning that row for every output fragment.
    std::unordered_map<const HdTexture*,std::unordered_map<unsigned,uint32_t>> textures;
    for(uint32_t i=0;i<count;++i) {
        const auto& q=f.hd->quads[i];const auto at=quad_at+i*40;
        data[at+25]=0xFFFFFFFF;
        if(!q.gpu_eligible || !q.texture || q.texture->raw.size()!=size_t(q.texture->w)*q.texture->h)continue;
        unsigned mode=(q.hflip?1u:0u)|(q.gpu_flags&0x80?2u:0u);
        auto [it,added]=textures[q.texture.get()].emplace(mode,uint32_t(tex.size()));
        if(added) {
            const auto base=tex.size();tex.insert(tex.end(),q.texture->raw.begin(),q.texture->raw.end());
            if(!(mode&2))for(int y=0;y<q.texture->h;++y) {
                unsigned ends=0;
                for(int x=0;x<q.texture->w;++x) {
                    const int column=q.hflip?q.texture->w-1-x:x;auto& value=tex[base+y*q.texture->w+column];
                    if(value==0x7FFF)++ends;
                    if(value==0x7FFF || ends>=2)value=0x2000000;
                }
            }
        }
        float xy[8];std::copy(q.xy,q.xy+8,xy);
        // Dot steps along the character's columns and rows, as VDP1 counts a
        // line: the longer axis of each edge (manual, distorted sprite drawing).
        auto steps=[&](int a,int b){return std::max(std::abs(int(xy[2*b])-int(xy[2*a])),std::abs(int(xy[2*b+1])-int(xy[2*a+1])));};
        data[at+33]=uint32_t(std::max(steps(0,1),steps(3,2)));data[at+34]=uint32_t(std::max(steps(0,3),steps(1,2)));
        if((q.gpu_flags>>16&15)<2) {
            const float dx=xy[2]>=xy[0]?0.5f:-0.5f,dy=xy[7]>=xy[1]?0.5f:-0.5f;
            for(int v=0;v<4;++v){xy[v*2]+=(v==1 || v==2)?dx:-dx;xy[v*2+1]+=(v>=2)?dy:-dy;}
        } else {
            // Distorted vertices are dot centres and VDP1 fills the dots its
            // edges pass through, so the drawn area reaches half a dot beyond
            // them. Without this, tiles that abut at x and x+1 leave a
            // one-dot seam of background at every upscale.
            const float cx=(xy[0]+xy[2]+xy[4]+xy[6])/4,cy=(xy[1]+xy[3]+xy[5]+xy[7])/4;
            for(int v=0;v<4;++v) {
                xy[v*2]+=xy[v*2]>cx?0.5f:xy[v*2]<cx?-0.5f:0.f;
                xy[v*2+1]+=xy[v*2+1]>cy?0.5f:xy[v*2+1]<cy?-0.5f:0.f;
            }
        }
        std::memcpy(data.data()+at,xy,32);std::memcpy(data.data()+at+8,q.shade,48);
        data[at+20]=q.hflip;data[at+21]=q.vflip;data[at+22]=it->second;data[at+23]=q.texture->w;data[at+24]=q.texture->h;data[at+25]=q.gpu_flags;
        for(int k=0;k<6;++k)data[at+26+k]=uint32_t(q.clip[k]);
        if(f.origin_x) {
            if(q.clip[0]>=f.native_w-1) {data[at+32]=uint32_t(-f.origin_x);data[at+26]=f.w-f.origin_x-1;}
            if(q.clip[2]==0 && q.clip[4]>=f.native_w-1) {data[at+28]=uint32_t(-f.origin_x);data[at+30]=f.w-f.origin_x-1;}
        }
        float minx=xy[0],maxx=xy[0],miny=xy[1],maxy=xy[1];
        for(int k=1;k<4;++k){minx=std::min(minx,xy[2*k]);maxx=std::max(maxx,xy[2*k]);miny=std::min(miny,xy[2*k+1]);maxy=std::max(maxy,xy[2*k+1]);}
        data[at+35]=uint32_t(std::clamp((maxx-minx)*(maxy-miny),1.0f,1000000.0f));   // area for ray shadows (33/34 are the dot steps)
        minx+=f.origin_x;maxx+=f.origin_x;
        if(maxx<-.5f || maxy<-.5f || minx>f.w || miny>f.h)continue;
        const int x0=std::clamp(int(std::floor((minx-1)/tile_size)),0,int(columns)-1),x1=std::clamp(int(std::floor((maxx+1)/tile_size)),0,int(columns)-1);
        const int y0=std::clamp(int(std::floor((miny-1)/tile_size)),0,int(rows)-1),y1=std::clamp(int(std::floor((maxy+1)/tile_size)),0,int(rows)-1);
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)bins[y*columns+x].push_back(i);
    }
    for(uint32_t i=0;i<bins.size();++i) {
        data[bin_at+2*i]=uint32_t(data.size());data[bin_at+2*i+1]=uint32_t(bins[i].size());
        data.insert(data.end(),bins[i].begin(),bins[i].end());
    }
    if(data.size()*4>capacity || tex.size()*4>capacity){SDL_SetError("GPU geometry scene exceeds buffer capacity");return nullptr;}
    // Exact packed inputs include pixels, clipping, effects, geometry, source
    // texels and dimensions. Repeated fields can reuse the completed picture.
    if(data==last_scene_ && tex==last_textures_) {++cache_hits_;return filtered_valid_?filtered_:output_;}
    if(!SDL_FlushRenderer(renderer_) || !upload(scene_,data) || !upload(textures_,tex))return nullptr;
    auto* previous=SDL_GetRenderTarget(renderer_);
    if(!SDL_SetRenderTarget(renderer_,output_))return nullptr;
    const uint32_t normal_pass[4]={0,0,0,0},effects_pass[4]={uint32_t(aa_?1:0)|uint32_t(ray_shadows_?2:0),0,0,0};
    if(!SDL_SetGPURenderStateFragmentUniforms(state_,0,normal_pass,sizeof normal_pass)) {
        SDL_SetRenderTarget(renderer_,previous);return nullptr;
    }
    SDL_SetGPURenderState(renderer_,state_);
    bool ok=SDL_RenderTexture(renderer_,white_,nullptr,nullptr);
    SDL_SetGPURenderState(renderer_,nullptr);SDL_SetRenderTarget(renderer_,previous);
    filtered_valid_=false;
    if(ok && (aa_ || ray_shadows_)) {
        if(!filtered_) {
            filtered_=SDL_CreateTexture(renderer_,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,width,height);
            if(filtered_){SDL_SetTextureBlendMode(filtered_,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(filtered_,SDL_SCALEMODE_LINEAR);}
        }
        if(filtered_ && SDL_SetRenderTarget(renderer_,filtered_) &&
           SDL_SetGPURenderStateFragmentUniforms(state_,0,effects_pass,sizeof effects_pass)) {
            SDL_SetGPURenderState(renderer_,state_);
            filtered_valid_=SDL_RenderTexture(renderer_,output_,nullptr,nullptr);
        }
        SDL_SetGPURenderState(renderer_,nullptr);SDL_SetRenderTarget(renderer_,previous);
        if(!filtered_valid_){SDL_Log("GPU presentation effects unavailable: %s; original picture retained",SDL_GetError());aa_=false;ray_shadows_=false;}
    }
    if(ok) {last_scene_=std::move(data);last_textures_=std::move(tex);++submissions_;}
    return ok?(filtered_valid_?filtered_:output_):nullptr;
}
bool GpuRenderer::capture(Frame& f) {
    if(!output_ || !SDL_FlushRenderer(renderer_))return false;
    auto* previous=SDL_GetRenderTarget(renderer_);if(!SDL_SetRenderTarget(renderer_,filtered_valid_?filtered_:output_))return false;
    auto* surface=SDL_RenderReadPixels(renderer_,nullptr);SDL_SetRenderTarget(renderer_,previous);if(!surface)return false;
    auto* rgb=SDL_ConvertSurface(surface,SDL_PIXELFORMAT_XRGB8888);SDL_DestroySurface(surface);if(!rgb)return false;
    f.w=rgb->w;f.h=rgb->h;f.hd.reset();f.px.resize(size_t(f.w)*f.h);
    for(int y=0;y<f.h;++y)std::memcpy(f.px.data()+y*f.w,(uint8_t*)rgb->pixels+y*rgb->pitch,f.w*4);
    for(auto& p:f.px)p&=0xFFFFFF;SDL_DestroySurface(rgb);return true;
}
bool GpuRenderer::wait(){return device_ && SDL_WaitForGPUIdle(device_);}
void GpuRenderer::release(){if(renderer_)SDL_FlushRenderer(renderer_);if(state_)SDL_DestroyGPURenderState(state_);if(white_)SDL_DestroyTexture(white_);if(output_)SDL_DestroyTexture(output_);if(filtered_)SDL_DestroyTexture(filtered_);if(device_){if(scene_)SDL_ReleaseGPUBuffer(device_,scene_);if(textures_)SDL_ReleaseGPUBuffer(device_,textures_);if(shader_)SDL_ReleaseGPUShader(device_,shader_);}state_=nullptr;white_=output_=filtered_=nullptr;filtered_valid_=false;scene_=textures_=nullptr;shader_=nullptr;width_=height_=0;last_scene_.clear();last_textures_.clear();submissions_=cache_hits_=0;}
void GpuRenderer::destroy_device(){if(device_)SDL_DestroyGPUDevice(device_);device_=nullptr;}
}
