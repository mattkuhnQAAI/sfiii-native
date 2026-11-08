#include "video.h"
#include <filesystem>
#include <cstdio>
#include <cstdarg>
#include <utility>
#include <initializer_list>
void sat_note(const char* text,...) {
    va_list args;va_start(args,text);std::vfprintf(stderr,text,args);va_end(args);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    namespace fs=std::filesystem;fs::create_directories(argv[1]);
    for(auto [w,h]:{std::pair{1,1},{320,240},{427,240},{3843,2160}}) {
        Frame image;image.w=w;image.h=h;image.px.resize(size_t(w)*h);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)
            image.px[size_t(y)*w+x]=((x*17+y*13)&255)<<16|((x*7+y*19)&255)<<8|((x*23+y*3)&255);
        const auto path=fs::path(argv[1])/(std::to_string(w)+"x"+std::to_string(h)+".png");
        write_png(path.string(),image);if(!fs::exists(path))return 3;
    }
    return 0;
}
