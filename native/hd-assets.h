// Local replacement-art pack and projection. Does not change guest textures.
#pragma once
#include "video.h"
#include "hd-pack-index.h"
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <future>
#include <thread>
#include <deque>

namespace sf3 {
class HdAssets {
    friend class HdGpu;
    struct Asset { int w,h; float uv[4]{0,0,1,1}; std::vector<uint32_t> original;
        mutable Frame replacement; std::filesystem::path replacement_path; bool character=false;
        mutable uint64_t last_used=0; };
    std::vector<Asset> assets_;
    std::unordered_map<uint64_t,std::vector<size_t>> buckets_;
    mutable size_t last_pixels_=0;
    mutable Frame projected_,overlay_;
    mutable bool cache_valid_=false;
    mutable uint64_t cache_geometry_=0;
    mutable uint64_t cache_materials_=0;
    mutable int cache_w_=0,cache_h_=0,cache_origin_=0;
    mutable uint64_t replacement_epoch_=0;
    mutable size_t replacement_bytes_=0;
    mutable std::deque<size_t> loaded_;
    size_t replacement_budget_;
    bool indexed_=false;
    static uint32_t blend_rgba(uint32_t a,uint32_t b,unsigned w) {
        unsigned t=256-w;
        auto pair=[&](uint32_t x,uint32_t y){return (((x&0xFF00FF)*t+(y&0xFF00FF)*w)>>8)&0xFF00FF;};
        return pair(a,b)|(pair(a>>8,b>>8)<<8);
    }
    static uint64_t key(int w,int h,const std::vector<uint32_t>& pixels) {
        // Use the native decoder's existing texture fingerprint to avoid
        // rehashing every source pixel on each presented field. This is only a
        // bucket hint: dimensions and every original pixel are still compared.
        uint64_t value=0;
        hd_fingerprint(value,uint64_t(unsigned(w))<<32|unsigned(h));
        for(auto p:pixels)hd_fingerprint(value,p);
        return value;
    }
    static bool read_rgba(const std::filesystem::path& path,int w,int h,std::vector<uint32_t>& pixels) {
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file || file.tellg()!=std::streamoff(size_t(w)*h*4)) return false;
        file.seekg(0);pixels.resize(size_t(w)*h);
        std::vector<uint8_t> bytes(size_t(w)*h*4);
        if(!file.read((char*)bytes.data(),bytes.size())) return false;
        for(size_t i=0;i<pixels.size();++i) pixels[i]=(uint32_t(bytes[i*4+3])<<24)
            |(uint32_t(bytes[i*4])<<16)|(uint32_t(bytes[i*4+1])<<8)|bytes[i*4+2];
        return true;
    }
    const Asset* match(const HdTexture& t,unsigned command=2) const {
        auto bucket=buckets_.find(t.fingerprint?t.fingerprint:key(t.w,t.h,t.rgba));
        if(bucket!=buckets_.end()) for(auto i:bucket->second) {
            const auto& a=assets_[i];
            if(a.w!=t.w || a.h!=t.h || a.original!=t.rgba
               || (a.character ? command>2 : command!=2)) continue;
            a.last_used=replacement_epoch_;
            if(a.replacement.px.empty()) {
                size_t bytes=size_t(a.replacement.w)*a.replacement.h*4;
                // Keep every material matched in this frame pinned until upload/composition.
                // A pathological frame may exceed the soft budget; later frames evict it.
                size_t attempts=loaded_.size();
                while(replacement_bytes_+bytes>replacement_budget_ && attempts-- && !loaded_.empty()) {
                    auto old=loaded_.front();loaded_.pop_front();const auto& previous=assets_[old];
                    if(previous.last_used==replacement_epoch_) {loaded_.push_back(old);continue;}
                    replacement_bytes_-=previous.replacement.px.size()*4;
                    std::vector<uint32_t>().swap(previous.replacement.px);
                }
                if(!read_rgba(a.replacement_path,a.replacement.w,a.replacement.h,a.replacement.px)) {
                    std::vector<uint32_t>().swap(a.replacement.px);return nullptr;
                }
                replacement_bytes_+=bytes;loaded_.push_back(i);
            }
            return &a;
        }
        return nullptr; // Full original bytes are compared after hashing.
    }
    static bool uv(const HdQuad& q,float x,float y,float& u,float& v) {
        float bx=q.xy[2]-q.xy[0],by=q.xy[3]-q.xy[1];
        float dx=q.xy[6]-q.xy[0],dy=q.xy[7]-q.xy[1];
        float qx=q.xy[0]-q.xy[2]+q.xy[4]-q.xy[6];
        float qy=q.xy[1]-q.xy[3]+q.xy[5]-q.xy[7];
        u=v=0.5f;
        for(int i=0;i<5;++i) {
            float fx=q.xy[0]+bx*u+dx*v+qx*u*v-x,fy=q.xy[1]+by*u+dy*v+qy*u*v-y;
            float ax=bx+qx*v,ay=by+qy*v,cx=dx+qx*u,cy=dy+qy*u;
            float det=ax*cy-ay*cx;if(std::abs(det)<0.00001f) return false;
            u-=(fx*cy-fy*cx)/det;v-=(ax*fy-ay*fx)/det;
        }
        if(!std::isfinite(u)||!std::isfinite(v)) return false;
        u=std::clamp(u,0.0f,1.0f);v=std::clamp(v,0.0f,1.0f);
        if(q.hflip) u=1-u;if(q.vflip) v=1-v;
        return true;
    }
public:
    explicit HdAssets(size_t budget=256*1024*1024):replacement_budget_(budget) {}
    // Each artwork style has an independent, demand-filled CPU cache. Keep a
    // conservative fallback when host memory is unavailable or limited.
    static size_t recommended_cache_bytes(int system_ram_mb) {
        if(system_ram_mb>=24*1024)return size_t(1024)*1024*1024;
        if(system_ram_mb>=16*1024)return size_t(512)*1024*1024;
        return size_t(256)*1024*1024;
    }
    bool configure_cache_budget(size_t bytes) {
        if(replacement_bytes_ || !bytes)return false;
        replacement_budget_=bytes;return true;
    }
    size_t cache_budget() const {return replacement_budget_;}
    size_t load(const std::string& root) {
        assets_.clear();buckets_.clear();cache_valid_=false;std::error_code error;
        loaded_.clear();replacement_bytes_=0;replacement_epoch_=0;
        indexed_=false;
        std::vector<pack_index::Entry> indexed;
        if(pack_index::read(root,indexed)) {
            assets_.reserve(indexed.size());
            for(auto& entry:indexed) {
                Asset a;a.w=entry.w;a.h=entry.h;a.replacement.w=entry.hw;a.replacement.h=entry.hh;
                std::copy(entry.uv.begin(),entry.uv.end(),a.uv);a.character=entry.character;
                a.original=std::move(entry.original);
                a.replacement_path=std::filesystem::path(root)/entry.replacement_path;
                buckets_[key(a.w,a.h,a.original)].push_back(assets_.size());assets_.push_back(std::move(a));
            }
            indexed_=true;return assets_.size();
        }
        for(std::filesystem::directory_iterator i(root,error),end;i!=end && !error;i.increment(error)) {
            if(!i->is_directory()) continue;
            std::ifstream descriptor(i->path()/"asset.txt");std::string magic;
            int w=0,h=0,hw=0,hh=0;descriptor>>magic>>w>>h>>hw>>hh;
            if(magic!="SF3HDASSET1" || w<1 || h<1 || w>1024 || h>1024 || hw<w || hh<h || hw>4096 || hh>4096) continue;
            Asset a;a.w=w;a.h=h;a.replacement.w=hw;a.replacement.h=hh;
            descriptor>>std::ws;
            if(descriptor.peek()!=std::char_traits<char>::eof()) {
                if(!(descriptor>>a.uv[0]>>a.uv[1]>>a.uv[2]>>a.uv[3])) continue;
                bool valid=true;for(float v:a.uv) valid&=std::isfinite(v)&&v>=0&&v<=1;
                if(!valid || a.uv[2]<=a.uv[0] || a.uv[3]<=a.uv[1]) continue;
                std::string kind;
                if(descriptor>>kind) {if(kind!="character" && kind!="world")continue;a.character=kind=="character";}
            }
            a.replacement_path=i->path()/"replacement.rgba";
            std::error_code size_error;
            auto replacement_size=std::filesystem::file_size(a.replacement_path,size_error);
            if(size_error || replacement_size!=size_t(hw)*hh*4
               || !read_rgba(i->path()/"original.rgba",w,h,a.original)) continue;
            buckets_[key(w,h,a.original)].push_back(assets_.size());assets_.push_back(std::move(a));
        }
        return assets_.size();
    }
    size_t count() const { return assets_.size(); }
    bool loaded_from_index() const {return indexed_;}
    void begin_frame() const {++replacement_epoch_;}
    size_t cached_replacement_bytes() const {return replacement_bytes_;}
    size_t last_pixels() const { return last_pixels_; }
    const Frame& overlay(const Frame& source) const {
        overlay_.w=1440;overlay_.h=1080;overlay_.px.assign(1440*1080,0);
        apply(source,overlay_,true);return overlay_;
    }
    void apply(const Frame& source,Frame& output,bool overlay_mode=false) const {
        last_pixels_=0;
        if(!source.hd || source.hd->visible.size()!=source.px.size() || assets_.empty()) return;
        begin_frame();
        unsigned hardware=std::thread::hardware_concurrency();
        int workers=hardware>=8 ? 4 : hardware>=4 ? 2 : 1;
        uint64_t material_key=14695981039346656037ull;
        for(const auto& q:source.hd->quads) {
            material_key=(material_key^q.gpu_flags)*1099511628211ull;
            material_key=(material_key^unsigned(q.gpu_eligible))*1099511628211ull;
        }
        if(!cache_valid_ || cache_geometry_!=source.hd->geometry || cache_materials_!=material_key || projected_.w!=output.w
            || projected_.h!=output.h || cache_w_!=source.w || cache_h_!=source.h || cache_origin_!=source.origin_x) {
        cache_valid_=false; // A no-match rebuild must not leave the previous cache identity valid.
        projected_.w=output.w;projected_.h=output.h;projected_.px.assign(output.w*output.h,0);
        std::vector<const Asset*> materials(source.hd->quads.size()+1,nullptr);
        std::vector<uint8_t> visible(materials.size(),0);
        for(auto id:source.hd->visible) if(id<visible.size())visible[id]=1;
        bool any=false;
        std::unordered_map<const HdTexture*,const Asset*> matches[3];
        for(size_t i=0;i<source.hd->quads.size();++i) {
            const auto& quad=source.hd->quads[i];
            auto t=quad.texture;
            unsigned command=(quad.gpu_flags>>16)&15;
            if(!visible[i+1] || !quad.gpu_eligible || command>2 || !t) continue;
            auto& bucket=matches[command];auto found=bucket.find(t.get());
            const Asset* asset=found==bucket.end() ? match(*t,command) : found->second;
            bucket[t.get()]=asset;materials[i+1]=asset;
            any|=asset!=nullptr;
        }
        if(!any) return;
        struct Projection {float u=0,v=0,ux=0,uy=0,vx=0,vy=0;bool valid=false;};
        std::vector<Projection> projections(source.px.size());
        for(int y=0;y<source.h;++y) for(int x=0;x<source.w;++x) {
            size_t at=y*source.w+x;unsigned id=source.hd->visible[at];
            if(id>=materials.size() || !materials[id]) continue;
            const auto& q=source.hd->quads[id-1];auto& p=projections[at];
            if(!uv(q,float(x-source.origin_x),float(y),p.u,p.v)) continue;
            float gu=q.hflip?1-p.u:p.u,gv=q.vflip?1-p.v:p.v;
            float qx=q.xy[0]-q.xy[2]+q.xy[4]-q.xy[6],qy=q.xy[1]-q.xy[3]+q.xy[5]-q.xy[7];
            float ax=q.xy[2]-q.xy[0]+qx*gv,ay=q.xy[3]-q.xy[1]+qy*gv;
            float cx=q.xy[6]-q.xy[0]+qx*gu,cy=q.xy[7]-q.xy[1]+qy*gu;
            float det=ax*cy-ay*cx;if(std::abs(det)<0.00001f) continue;
            float us=q.hflip?-1.0f:1.0f,vs=q.vflip?-1.0f:1.0f;
            p.ux=us*cy/det;p.uy=-us*cx/det;p.vx=-vs*ay/det;p.vy=vs*ax/det;p.valid=true;
        }
        struct Column {int at;float delta;};std::vector<Column> columns(output.w);
        for(int x=0;x<output.w;++x) {float sx=(x+0.5f)*source.w/output.w-0.5f;int at=std::clamp(int(std::floor(sx+0.5f)),0,source.w-1);columns[x]={at,sx-at};}
        auto rows=[&](int begin,int end) {
          size_t replaced=0;
          for(int y=begin;y<end;++y) {
          float sy=(y+0.5f)*source.h/output.h-0.5f;int ny=std::clamp(int(std::floor(sy+0.5f)),0,source.h-1);float dy=sy-ny;
          for(int x=0;x<output.w;++x) {
            int nx=columns[x].at;size_t at=ny*source.w+nx;const auto& p=projections[at];
            if(!p.valid) continue;
            unsigned id=source.hd->visible[at];const auto& a=*materials[id];float dx=columns[x].delta;
            float u=std::clamp(p.u+p.ux*dx+p.uy*dy,0.0f,1.0f),v=std::clamp(p.v+p.vx*dx+p.vy*dy,0.0f,1.0f);
            float tx=(a.uv[0]+u*(a.uv[2]-a.uv[0]))*(a.replacement.w-1);
            float ty=(a.uv[1]+v*(a.uv[3]-a.uv[1]))*(a.replacement.h-1);
            int ix=int(tx),iy=int(ty),jx=std::min(ix+1,a.replacement.w-1),jy=std::min(iy+1,a.replacement.h-1);
            unsigned fx=unsigned((tx-ix)*256),fy=unsigned((ty-iy)*256);
            const auto* top=a.replacement.px.data()+iy*a.replacement.w;const auto* bottom=a.replacement.px.data()+jy*a.replacement.w;
            uint32_t rgba=blend_rgba(blend_rgba(top[ix],top[jx],fx),blend_rgba(bottom[ix],bottom[jx],fx),fy);
            const auto& quad=source.hd->quads[id-1];float gu=quad.hflip?1-u:u,gv=quad.vflip?1-v:v;
            uint32_t lit=0;
            for(int ch=0,sh=16;ch<3;++ch,sh-=8) {
                float upper=quad.shade[ch]*(1-gu)+quad.shade[3+ch]*gu;
                float lower=quad.shade[9+ch]*(1-gu)+quad.shade[6+ch]*gu;
                int value=std::clamp(int(((rgba>>sh)&255)+upper*(1-gv)+lower*gv+0.5f),0,255);
                lit|=uint32_t(value)<<sh;
            }
            rgba=(rgba&0xFF000000)|lit;
            projected_.px[y*output.w+x]=rgba;++replaced;
          }
          }
          return replaced;
        };
        std::vector<std::future<size_t>> pending;
        for(int i=1;i<workers;++i)
            pending.push_back(std::async(std::launch::async,rows,i*output.h/workers,(i+1)*output.h/workers));
        last_pixels_=rows(0,output.h/workers);
        for(auto& result:pending) last_pixels_+=result.get();
        cache_geometry_=source.hd->geometry;cache_materials_=material_key;cache_w_=source.w;cache_h_=source.h;cache_origin_=source.origin_x;cache_valid_=true;
        }
        std::vector<int> columns(output.w);
        for(int x=0;x<output.w;++x) columns[x]=std::min((int64_t(2*x+1)*source.w)/(2*output.w),int64_t(source.w-1));
        auto compose=[&](int first,int last) {
          size_t changed=0;
          for(int y=first;y<last;++y) {
            int ny=std::min((int64_t(2*y+1)*source.h)/(2*output.h),int64_t(source.h-1));
            for(int x=0;x<output.w;++x) {
              uint32_t rgba=projected_.px[y*output.w+x];unsigned alpha=rgba>>24;
              if(!alpha) continue;
              uint32_t color=rgba&0xFFFFFF;
              if(source.hd->effects.size()==source.px.size()) {
                const auto& e=source.hd->effects[ny*source.w+columns[x]];
                if(e.flags&1) {
                  if(e.flags&2) {
                    uint32_t result=0;for(int sh=0;sh<=16;sh+=8) result|=std::min(((color>>sh)&255)+((e.second>>sh)&255),255u)<<sh;
                    color=result;
                  } else {
                    unsigned a=31-e.ratio,b=e.ratio+1;
                    color=((((color&0xFF00FF)*a+(e.second&0xFF00FF)*b)>>5)&0xFF00FF)
                         |((((color&0x00FF00)*a+(e.second&0x00FF00)*b)>>5)&0x00FF00);
                  }
                }
                if(e.offset[0] || e.offset[1] || e.offset[2]) {
                  uint32_t result=0;for(int sh=16,ch=0;sh>=0;sh-=8,++ch) result|=uint32_t(std::clamp(int((color>>sh)&255)+e.offset[ch],0,255))<<sh;
                  color=result;
                }
                if(e.flags&4) color=(color>>1)&0x7F7F7F;
              }
              auto& dest=output.px[y*output.w+x];
              if(overlay_mode) dest=color|(alpha<<24);
              else if(alpha==255) dest=color;
              else {uint32_t result=0;for(int sh=0;sh<=16;sh+=8) result|=((((color>>sh)&255)*alpha+((dest>>sh)&255)*(255-alpha)+127)/255)<<sh;dest=result;}
              ++changed;
            }
          }
          return changed;
        };
        std::vector<std::future<size_t>> pending;
        for(int i=1;i<workers;++i) pending.push_back(std::async(std::launch::async,compose,i*output.h/workers,(i+1)*output.h/workers));
        last_pixels_=compose(0,output.h/workers);
        for(auto& result:pending) last_pixels_+=result.get();
    }
};
}
