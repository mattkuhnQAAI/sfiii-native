// Final presentation filter. No changes to Saturn pixels or geometry.
#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sf3 {
enum class ImageFilter { Nearest, Bilinear, IntegerSmooth };
class ImageScaler {
    SDL_Texture* intermediate_=nullptr;
    int width_=0,height_=0;
    static bool layers(SDL_Renderer* renderer,SDL_Texture* base,SDL_Texture* overlay,
                       const SDL_FRect& destination,SDL_ScaleMode mode) {
        if(!SDL_SetTextureScaleMode(base,mode) || !SDL_RenderTexture(renderer,base,nullptr,&destination))return false;
        return !overlay || (SDL_SetTextureScaleMode(overlay,mode) && SDL_RenderTexture(renderer,overlay,nullptr,&destination));
    }
public:
    void release() {if(intermediate_)SDL_DestroyTexture(intermediate_);intermediate_=nullptr;width_=height_=0;}
    int intermediate_width() const {return width_;}
    int intermediate_height() const {return height_;}
    bool draw(SDL_Renderer* renderer,SDL_Texture* base,SDL_Texture* overlay,
              const SDL_FRect& destination,ImageFilter filter) {
        if(!base || !std::isfinite(destination.w) || !std::isfinite(destination.h) || destination.w<=0 || destination.h<=0)
            return SDL_SetError("Invalid presentation dimensions");
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
