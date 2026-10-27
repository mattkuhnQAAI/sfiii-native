// Original, synthetic GPU-versus-CPU parity test. Every fixture is generated
// here from VDP1 command lists and VDP2 registers written by this file; no
// disc, BIOS or captured video data is read. A local video dump may be passed
// with --dump for out-of-tree diagnosis, but CI only uses the synthetic scenes.
//
// For each scene the accurate CPU path (no GPU, render scale 1) composes the
// native picture. The same video state is then composed in GPU mode and
// rendered through sf3::GpuRenderer at 1x, 2x and 4x. Reported per scale:
//   exact  - output samples equal to the native CPU dot they upscale
//   miss   - native dots whose block contains no sample of the CPU colour
//            (a wrong colour, missing sprite or garbage, not edge refinement)
//   alien  - samples matching no CPU colour within one native dot
//            (colours the accurate path never produces nearby)
//   lost   - native dots whose colour appears nowhere within one dot
// The test fails when 1x is not identical to the CPU picture or when miss or
// lost exceed the limits below. Diff PNGs (grey: equal, yellow: near, magenta:
// alien, red: miss, cyan: lost) are written for every scale that differs.
#include "gpu-renderer.h"
#include "saturn.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <vector>
namespace fs=std::filesystem;
static void check(bool value,const std::string& why){if(!value)throw std::runtime_error(why);}

// ---- fixture writers ------------------------------------------------------------------
static void w16(uint8_t* m,uint32_t a,uint16_t v){m[a]=v>>8;m[a+1]=uint8_t(v);}
static void v1(uint32_t a,uint16_t v){w16(g_vdp1_vram,a,v);}
static void v2(uint32_t a,uint16_t v){w16(g_vdp2_vram,a,v);}
static void r2(uint32_t a,uint16_t v){w16(g_vdp2_regs,a,v);}
static void cram(uint32_t i,uint16_t v){w16(g_vdp2_cram,i*2,v);}
static uint16_t rgb(int r,int g,int b){return uint16_t(0x8000|(b&31)<<10|(g&31)<<5|(r&31));}

struct Lcg{uint32_t s;explicit Lcg(uint32_t seed):s(seed){} uint32_t next(){s=s*1664525u+1013904223u;return s>>8;} int in(int a,int b){return a+int(next()%uint32_t(b-a+1));}};

// One VDP1 command table, written from address 0 (manual ST-013 section 6).
struct Cmds {
    uint32_t at=0;
    struct C{uint16_t ctrl=0,pmod=0,colr=0,srca=0,size=0,grda=0;int16_t xy[8]{};};
    void put(const C& c){
        v1(at,c.ctrl);v1(at+2,0);v1(at+4,c.pmod);v1(at+6,c.colr);v1(at+8,c.srca);v1(at+10,c.size);
        for(int i=0;i<8;++i)v1(at+12+2*i,uint16_t(c.xy[i]));v1(at+28,c.grda);v1(at+30,0);at+=32;
    }
    void sysclip(int x,int y){C c;c.ctrl=9;c.xy[4]=int16_t(x);c.xy[5]=int16_t(y);put(c);}
    void userclip(int x0,int y0,int x1,int y1){C c;c.ctrl=8;c.xy[0]=int16_t(x0);c.xy[1]=int16_t(y0);c.xy[4]=int16_t(x1);c.xy[5]=int16_t(y1);put(c);}
    void local(int x,int y){C c;c.ctrl=10;c.xy[0]=int16_t(x);c.xy[1]=int16_t(y);put(c);}
    void end(){C c;c.ctrl=0x8000;put(c);}
};
struct Tex{uint16_t srca=0,size=0;int w=0,h=0;};
// Texture area from 0x10000; Gouraud tables from 0x7F000; lookup tables from 0x7E000.
static uint32_t g_tex_at=0x10000,g_grd_at=0x7F000,g_lut_at=0x7E000;
static Tex rgb_texture(int w,int h,int kind){
    Tex t;t.w=w;t.h=h;t.srca=uint16_t(g_tex_at/8);t.size=uint16_t((w/8)<<8|h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        uint16_t p;
        switch(kind){
        case 0:p=rgb(x*31/(w-1),y*31/(h-1),((x/2)^(y/2))&1?28:6);break;           // gradient checker
        case 1:p=((x+y)%5==0)?0:rgb(31-x*31/(w-1),12,y*31/(h-1));break;              // transparent diagonals
        case 2:p=(x==w-3&&y%3==0)?0x7FFF:rgb((x*7)&31,(y*5)&31,((x+y)*3)&31);break;  // end codes inside rows
        default:p=(x==0||y==0||x==w-1||y==h-1)?rgb(31,31,31):rgb(4,((x*y)>>2)&31,20);break; // outlined
        }
        v1(g_tex_at+2*uint32_t(y*w+x),p);
    }
    g_tex_at+=uint32_t(w*h*2+7)&~7u;return t;
}
static Tex lut_texture(int w,int h){
    Tex t;t.w=w;t.h=h;t.srca=uint16_t(g_tex_at/8);t.size=uint16_t((w/8)<<8|h);
    for(int y=0;y<h;++y)for(int x=0;x<w;x+=4){
        uint16_t v=0;for(int i=0;i<4;++i)v=uint16_t(v<<4|(((x+i)/2+y/2)&15));
        v1(g_tex_at+uint32_t((y*w+x)/2),v);
    }
    g_tex_at+=uint32_t(w*h/2+7)&~7u;return t;
}
static uint16_t gouraud(uint16_t a,uint16_t b,uint16_t c,uint16_t d){
    uint32_t at=g_grd_at;g_grd_at+=8;v1(at,a);v1(at+2,b);v1(at+4,c);v1(at+6,d);return uint16_t(at/8);
}
static Cmds::C sprite(const Tex& t,int comm,std::initializer_list<int> xy,uint16_t pmod=0x00A8,int dir=0){
    Cmds::C c;c.ctrl=uint16_t(comm|dir<<4);c.pmod=pmod;c.srca=t.srca;c.size=t.size;int i=0;for(int v:xy)c.xy[i++]=int16_t(v);return c;
}
static Cmds::C poly(int comm,uint16_t colour,std::initializer_list<int> xy,uint16_t pmod=0x00C0){
    Cmds::C c;c.ctrl=uint16_t(comm);c.pmod=pmod;c.colr=colour;int i=0;for(int v:xy)c.xy[i++]=int16_t(v);return c;
}

// Common VDP2 state: 320x224, mixed RGB/palette sprites, back screen, NBG0 bitmap.
static void vdp2_base(bool nbg0,int nbg_prio,bool cc){
    std::memset(g_vdp2_regs,0,sizeof g_vdp2_regs);std::memset(g_vdp2_vram,0,sizeof g_vdp2_vram);std::memset(g_vdp2_cram,0,sizeof g_vdp2_cram);
    r2(0x00,0x8000);                       // TVMD: display on, 320 dots, 224 lines
    r2(0xAC,0);r2(0xAE,0x7000);v2(0xE000,rgb(2,4,12)); // back screen colour at 0xE000
    r2(0xE0,0x0020|(cc?0x0300:0));         // SPCTL: mixed, type 0, colour calc when priority <= 3
    r2(0xF0,0x0306);r2(0xF2,0x0702);r2(0xF4,0x0504);r2(0xF6,0x0101); // sprite priorities by register
    r2(0x100,0x0810);r2(0x102,0x1804);r2(0x104,0x0C14);r2(0x106,0x1C00); // sprite colour-calc ratios
    for(int i=0;i<2048;++i)cram(uint32_t(i),uint16_t(((i*3)&31)|((i*7)&31)<<5|((i>>3)&31)<<10));
    if(nbg0){
        r2(0x20,0x0001);r2(0x28,0x0032);r2(0x3C,0x0002);r2(0xF8,uint16_t(nbg_prio));r2(0x108,0x000C);
        for(int y=0;y<256;++y)for(int x=0;x<512;++x){
            const bool hole=((x/24)+(y/24))%3==0;
            v2(0x40000+2*uint32_t(y*512+x),hole?0:rgb((x/8)&31,(y/6)&31,10));
        }
    }
    if(cc)r2(0xEC,0x0040|(nbg0?0x0001:0));  // CCCTL: sprite (and NBG0) colour calculation
}

// ---- scenes ---------------------------------------------------------------------------
static void scene_sprites(){
    Cmds l;l.sysclip(319,223);l.local(0,0);
    Tex a=rgb_texture(32,24,0),b=rgb_texture(16,16,1),c=rgb_texture(24,12,2),d=rgb_texture(40,32,3);
    for(int dir=0;dir<4;++dir)l.put(sprite(a,0,{8+dir*40,8},0x00A8,dir));            // normal, four flips
    l.put(sprite(b,0,{170,8},0x0028));                                                // SPD off: no transparent dots
    l.put(sprite(b,0,{190,8},0x00E8));                                                // SPD on
    l.put(sprite(c,0,{212,8},0x00A8));l.put(sprite(c,0,{240,8},0x0028));          // end codes on/off
    for(int dir=0;dir<4;++dir)l.put(sprite(a,1,{8+dir*76,44,0,0,8+dir*76+63,44+47},0x00A8,dir));   // 2x zoom
    l.put(sprite(d,1,{8,100,0,0,8+19,100+15}));                                       // 0.5x shrink
    l.put(sprite(d,1,{36,100,0,0,36+28,100+20}));                                     // 0.7x shrink
    l.put(sprite(d,1,{72,100,0,0,72+51,100+41}));                                     // 1.3x stretch
    l.put(sprite(d,1,{132,100,0,0,132+39,100+5}));                                    // squashed
    {auto s=sprite(d,1,{196,100,0,0,196+17,100+30},0x10A8);l.put(s);}                 // HSS shrink
    l.put(sprite(a,2,{230,100,290,110,280,150,226,138}));                             // distorted, rotated
    l.put(sprite(a,2,{8,160,70,160,50,210,28,210},0x00A8,1));                         // trapezoid, hflip
    l.put(sprite(d,2,{90,158,130,170,118,215,80,200},0x00A8,3));                      // both flips
    l.put(sprite(b,2,{150,170,190,165,182,205,148,210}));
    l.put(sprite(a,2,{300,170,250,172,256,220,306,214}));                             // mirrored winding
    l.put(sprite(c,2,{200,160,205,160,240,200,230,205}));                             // thin sliver
    l.end();
}
static void scene_effects(){
    Cmds l;l.sysclip(319,223);l.local(0,0);
    Tex a=rgb_texture(32,32,0),b=rgb_texture(24,24,1),lut=lut_texture(16,16);
    // Lookup table: RGB entries and palette codes.
    for(int i=0;i<16;++i)v1(g_lut_at+2*uint32_t(i),i==0?0:(i&1)?rgb(i*2,31-i*2,16):uint16_t(0x0040+i));
    l.put(poly(4,rgb(20,4,4),{10,10,150,10,150,100,10,100}));                          // base polygon
    l.put(poly(4,rgb(4,20,4),{160,10,310,10,310,100,160,100}));
    l.put(sprite(a,0,{20,20},0x00AB));                                                // half-transparent sprite
    l.put(sprite(a,1,{60,20,0,0,60+63,20+63},0x00A9));                                // shadow
    l.put(sprite(b,0,{170,20},0x00AA));                                               // half-luminance
    l.put(sprite(a,0,{200,20},0x01A8));                                               // mesh
    l.put(sprite(a,1,{240,20,0,0,240+59,20+59},0x01AB));                              // mesh + half-trans
    {auto s=sprite(a,0,{20,120},0x00AC);s.grda=gouraud(rgb(31,0,0),rgb(0,31,0),rgb(0,0,31),rgb(31,31,31));l.put(s);}  // Gouraud
    {auto s=sprite(a,1,{60,120,0,0,60+63,120+63},0x00AC);s.grda=gouraud(rgb(31,31,0),rgb(8,8,8),rgb(31,0,31),rgb(16,16,16));l.put(s);}
    {auto s=sprite(a,2,{140,115,200,125,190,180,135,170},0x00AC);s.grda=gouraud(rgb(0,16,31),rgb(31,16,0),rgb(16,16,16),rgb(31,31,31));l.put(s);}
    {auto s=poly(4,rgb(16,16,16),{210,120,300,120,300,200,210,200},0x00C4);s.grda=gouraud(rgb(31,0,0),rgb(0,31,0),rgb(0,0,31),rgb(31,31,0));l.put(s);}
    {auto s=sprite(a,0,{230,140},0x00AF);s.grda=gouraud(rgb(31,8,8),rgb(8,31,8),rgb(8,8,31),rgb(31,31,31));l.put(s);}  // Gouraud + half-trans
    {auto s=sprite(lut,0,{20,190},0x0088);s.colr=uint16_t(g_lut_at/8);l.put(s);}      // lookup-table sprite
    {auto s=sprite(lut,1,{44,190,0,0,44+31,190+31},0x0088);s.colr=uint16_t(g_lut_at/8);l.put(s);}
    {auto s=sprite(lut,0,{90,190},0x0008);s.colr=0x0100;l.put(s);}                    // 16-colour bank
    l.put(poly(5,rgb(31,31,0),{100,190,180,195,170,220,110,215}));                    // polyline
    l.put(poly(6,rgb(0,31,31),{190,190,300,222}));                                    // line
    l.put(sprite(a,0,{120,105},0x80A8));                                              // MSB on
    l.end();
}
static void scene_clipping(){
    Cmds l;l.sysclip(299,203);l.local(12,6);
    Tex a=rgb_texture(48,40,0),b=rgb_texture(32,32,3);
    l.put(sprite(a,0,{-20,-10}));                                                     // crosses origin
    l.put(sprite(a,1,{260,170,0,0,260+79,170+59}));                                   // crosses system clip
    l.userclip(60,40,200,140);
    l.put(sprite(a,1,{40,30,0,0,40+95,30+79},0x04A8));                                // clipped inside
    l.put(sprite(b,2,{150,100,240,90,230,170,160,180},0x04A8,2));                     // distorted, clipped inside
    l.put(sprite(b,1,{180,20,0,0,180+63,20+63},0x06A8));                              // drawn outside the user clip
    l.put(sprite(a,2,{0,150,90,140,100,200,10,210},0x06A8,1));
    l.userclip(100,150,140,190);
    l.put(poly(4,rgb(31,16,0),{80,140,160,140,160,200,80,200},0x04C0));
    l.put(sprite(b,0,{-40,180}));l.put(sprite(b,0,{300,-20}));
    l.end();
}
static void scene_priority(){
    Cmds l;l.sysclip(319,223);l.local(0,0);
    Tex a=rgb_texture(40,40,0),b=rgb_texture(32,32,1),lut=lut_texture(32,32);
    // Palette sprites encode priority (bits 15-14) and colour-calc (13-11) per dot in type 0.
    for(int i=0;i<16;++i)v1(g_lut_at+2*uint32_t(i),uint16_t((i&3)<<14|((i>>2)&3)<<11|(0x20+i)));
    for(int i=0;i<4;++i)l.put(sprite(a,1,{10+i*78,10,0,0,10+i*78+69,10+59},0x00A8,i)); // RGB sprites, priority register 0
    {auto s=sprite(lut,1,{10,90,0,0,10+95,90+63},0x0088);s.colr=uint16_t(g_lut_at/8);l.put(s);}
    {auto s=sprite(lut,2,{120,90,200,95,190,160,115,150},0x0088);s.colr=uint16_t(g_lut_at/8);l.put(s);}
    l.put(sprite(b,2,{210,90,300,100,290,170,215,160}));
    l.put(poly(4,rgb(31,31,31),{20,170,120,170,120,215,20,215}));
    l.put(sprite(a,0,{60,160},0x00AB));
    l.put(sprite(b,1,{150,165,0,0,150+127,165+47}));
    l.end();
}
// Abutting distorted tiles, as a perspective floor or world map is built:
// each tile's vertices are dots and the next tile starts on the next dot.
static void scene_tiles(){
    Cmds l;l.sysclip(319,223);l.local(0,0);
    Tex a=rgb_texture(16,16,0),b=rgb_texture(16,16,3);
    for(int row=0;row<9;++row)for(int col=0;col<12;++col){
        auto at=[&](int c,int r,int k){
            const double y=8+r*23.0,spread=0.55+0.05*r,x=160+(c*26-156)*spread+(r%2)*0.3;
            return k?int(y)+(r>0?0:0):int(std::floor(x));};
        const int x0=at(col,row,0),x1=at(col+1,row,0)-1,x2=at(col+1,row+1,0)-1,x3=at(col,row+1,0);
        const int y0=at(col,row,1),y1=at(col,row+1,1)-1;
        l.put(sprite((row+col)&1?a:b,2,{x0,y0,x1,y0,x2,y1,x3,y1}));
    }
    l.end();
}
// A deterministic busy scene shaped like a battle frame: many overlapping
// sprites of every kind, sizes and flips, with all the effects mixed in.
static void scene_replay(uint32_t seed){
    Cmds l;l.sysclip(319,223);l.local(0,0);Lcg r(seed);
    std::vector<Tex> tex;for(int i=0;i<6;++i)tex.push_back(rgb_texture(8*r.in(1,6),r.in(8,48),i&3));
    Tex lut=lut_texture(24,24);
    for(int i=0;i<16;++i)v1(g_lut_at+2*uint32_t(i),i==0?0:uint16_t((i&1)?rgb(i*2,i,31-i):(0x4000|(0x30+i))));
    l.put(poly(4,rgb(3,6,9),{0,150,319,150,319,223,0,223}));
    for(int n=0;n<140;++n){
        const Tex& t=tex[size_t(r.in(0,5))];const int kind=r.in(0,99),dir=r.in(0,3);
        uint16_t pmod=0x00A8;
        const int fx=r.in(0,99);
        if(fx<10)pmod|=3;else if(fx<14)pmod|=1;else if(fx<17)pmod|=2;else if(fx<21)pmod|=0x100;
        bool shade=r.in(0,99)<15;if(shade && !(pmod&3))pmod|=4;
        const int x=r.in(-24,320),y=r.in(-24,224);
        Cmds::C c;
        if(kind<45)c=sprite(t,0,{x,y},pmod,dir);
        else if(kind<75){const int w=r.in(t.w/3+1,t.w*3),h=r.in(t.h/3+1,t.h*3);c=sprite(t,1,{x,y,0,0,x+w-1,y+h-1},pmod,dir);}
        else if(kind<88){const int s=r.in(10,60),dx=r.in(-s/2,s/2),dy=r.in(-s/2,s/2);c=sprite(t,2,{x,y,x+s,y+dy,x+s+dx,y+s+dy,x+dx,y+s},pmod,dir);}
        else if(kind<94){c=sprite(lut,r.in(0,1),{x,y,0,0,x+r.in(12,48),y+r.in(12,48)},uint16_t(0x0088|(pmod&0x103)),dir);c.colr=uint16_t(g_lut_at/8);}
        else{const int s=r.in(8,50);c=poly(4,rgb(r.in(0,31),r.in(0,31),r.in(0,31)),{x,y,x+s,y,x+s,y+s,x,y+s},uint16_t(0x00C0|(pmod&0x107)));}
        if(pmod&4)c.grda=gouraud(rgb(r.in(8,31),r.in(8,31),r.in(8,31)),rgb(r.in(8,31),r.in(8,31),r.in(8,31)),rgb(r.in(8,31),r.in(8,31),r.in(8,31)),rgb(r.in(8,31),r.in(8,31),r.in(8,31)));
        l.put(c);
    }
    l.end();
}

// ---- running a scene ------------------------------------------------------------------
static Frame rendered(sf3::GpuRenderer& gpu,const Frame& f,int scale){
    check(gpu.render(f,scale)!=nullptr,"GPU render failed");Frame out;check(gpu.capture(out),"GPU readback failed");return out;
}
static int channel_gap(uint32_t a,uint32_t b){int g=0;for(int s=0;s<24;s+=8){int d=int(a>>s&255)-int(b>>s&255);if(d<0)d=-d;if(d>g)g=d;}return g;}
struct Result{double exact=0;unsigned miss=0,alien=0,lost=0,native=0;};
static Result compare(const Frame& cpu,const Frame& out,int scale,const std::string& png){
    check(out.w==cpu.w*scale && out.h==cpu.h*scale,"GPU output dimensions");
    Result r;r.native=unsigned(cpu.w*cpu.h);size_t same=0;Frame diff;diff.w=out.w;diff.h=out.h;diff.px.resize(out.px.size());
    for(int y=0;y<cpu.h;++y)for(int x=0;x<cpu.w;++x){
        const uint32_t want=cpu.px[size_t(y)*cpu.w+x];bool found=false;
        for(int sy=0;sy<scale;++sy)for(int sx=0;sx<scale;++sx){
            const size_t at=size_t(y*scale+sy)*out.w+x*scale+sx;const uint32_t got=out.px[at];
            if(got==want){++same;found=true;}
            bool near=false;
            for(int ny=y-1;ny<=y+1 && !near;++ny)for(int nx=x-1;nx<=x+1 && !near;++nx)
                if(nx>=0 && ny>=0 && nx<cpu.w && ny<cpu.h && channel_gap(cpu.px[size_t(ny)*cpu.w+nx],got)<=8)near=true;
            if(!near)++r.alien;
            const uint32_t grey=(want>>2&0x3F3F3F);
            diff.px[at]=got==want?grey:near?0xFFD000:0xFF00FF;
        }
        if(!found){
            // Lost: the colour appears nowhere within one native dot of here
            // (texel boundaries may move by a dot when sprites are resampled).
            bool nearby=false;
            for(int oy=(y-1)*scale;oy<(y+2)*scale && !nearby;++oy)for(int ox=(x-1)*scale;ox<(x+2)*scale && !nearby;++ox)
                if(ox>=0 && oy>=0 && ox<out.w && oy<out.h && channel_gap(out.px[size_t(oy)*out.w+ox],want)<=8)nearby=true;
            if(!nearby){++r.lost;for(int sy=0;sy<scale;++sy)for(int sx=0;sx<scale;++sx)diff.px[size_t(y*scale+sy)*out.w+x*scale+sx]=0x00FFFF;continue;}
            ++r.miss;for(int sy=0;sy<scale;++sy)for(int sx=0;sx<scale;++sx)if(out.px[size_t(y*scale+sy)*out.w+x*scale+sx]!=want)diff.px[size_t(y*scale+sy)*out.w+x*scale+sx]=0xFF0000;}
    }
    r.exact=100.0*double(same)/double(out.px.size());
    if(r.miss || r.alien || same!=out.px.size())write_png(png,diff);
    return r;
}

int main(int argc,char** argv){
    if(argc<3){std::fprintf(stderr,"usage: gpu_parity OUTDIR SHADERDIR [--dump FILE]... [--report-only]\n");return 2;}
    const fs::path out=argv[1];fs::create_directories(out);bool report_only=false;std::vector<std::string> dumps;
    for(int i=3;i<argc;++i){std::string a=argv[i];if(a=="--report-only")report_only=true;else if(a=="--dump" && i+1<argc)dumps.push_back(argv[++i]);else return 2;}
    // 1x must be exact. Upscales resample distorted and shrunk characters on
    // subpixels, so some dots legitimately differ; these limits (Lavapipe
    // results plus 30% and 20 dots) catch regressions such as wrong colour
    // expansion, tile seams or lost sprites, which cost tens of thousands.
    struct Limit{unsigned miss2,lost2,miss4,lost4;};
    const std::map<std::string,Limit> limits={
        {"sprites",{1569,115,648,24}},
        {"tiles",{1033,20,197,20}},
        {"effects",{945,20,310,20}},
        {"clipping",{615,20,301,20}},
        {"priority",{297,20,91,20}},
        {"replay-a",{2203,54,1118,23}},
        {"replay-b",{2788,279,1447,36}},
        {"replay-c",{1291,24,612,22}},
    };
    sf3::GpuRenderer gpu;SDL_Renderer* renderer=nullptr;
    try{
        check(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization");renderer=gpu.create_renderer(nullptr);
        check(renderer!=nullptr,"GPU renderer initialization");check(gpu.initialize(renderer,argv[2]),"Geometry shader initialization");
        auto* scratch=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,16,16);
        check(scratch && SDL_SetRenderTarget(renderer,scratch),"Offscreen render target");
        std::printf("GPU %s: %s\n",gpu.backend(),gpu.adapter());
        struct Scene{std::string name;std::function<void()> vdp1,vdp2;};
        std::vector<Scene> scenes={
            {"sprites",scene_sprites,[]{vdp2_base(false,0,false);}},
            {"tiles",scene_tiles,[]{vdp2_base(false,0,false);}},
            {"effects",scene_effects,[]{vdp2_base(true,3,false);}},
            {"clipping",scene_clipping,[]{vdp2_base(true,2,false);}},
            {"priority",scene_priority,[]{vdp2_base(true,4,true);}},
            {"replay-a",[]{scene_replay(0x5F3A11u);},[]{vdp2_base(true,4,true);}},
            {"replay-b",[]{scene_replay(0x00C0FFEEu);},[]{vdp2_base(false,0,false);}},
            {"replay-c",[]{scene_replay(0x13572468u);},[]{vdp2_base(true,6,true);}},
        };
        std::vector<uint8_t> dump_bytes;
        for(const auto& path:dumps){
            std::string name="dump-"+fs::path(path).stem().string();
            scenes.push_back({name,[path]{
                std::ifstream in(path,std::ios::binary);std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),{});
                check(b.size()==0x141220,"Local video dump size");
                std::memcpy(g_vdp1_vram,b.data(),0x80000);
                std::memcpy(g_vdp2_vram,b.data()+0xC0020,0x80000);std::memcpy(g_vdp2_cram,b.data()+0xC0020+0x80000,0x1000);
                std::memcpy(g_vdp2_regs,b.data()+0xC0020+0x81000,0x200);
                vdp1_reg_write(0,(b[0xC0000]<<8)|b[0xC0001],2);},nullptr});
        }
        bool failed=false;std::FILE* summary=std::fopen((out/"summary.txt").string().c_str(),"w");
        for(const auto& s:scenes){
            auto run_vdp1=[&](bool g,int scale){
                vdp1_init();vdp1_gpu_available(g);vdp1_render_scale(scale);vdp1_hd_collect(g);
                if(s.vdp2){std::memset(g_vdp1_vram,0,sizeof g_vdp1_vram);g_tex_at=0x10000;g_grd_at=0x7F000;s.vdp2();s.vdp1();vdp1_reg_write(0,0,2);}
                else s.vdp1();
                vdp1_reg_write(4,1,2);vdp1_vblank_out();
            };
            run_vdp1(false,1);Frame cpu;vdp2_compose(cpu);
            check(cpu.w>0 && cpu.h>0,s.name+": CPU composition empty");
            write_png((out/(s.name+"-cpu.png")).string(),cpu);
            run_vdp1(true,2);Frame source;vdp2_compose(source);
            check(source.px==cpu.px,s.name+": GPU-mode native composition differs from the CPU path");
            const size_t quads=source.hd?source.hd->quads.size():0;size_t eligible=0;
            if(source.hd)for(const auto& q:source.hd->quads)eligible+=q.gpu_eligible;
            std::printf("%s: %dx%d, %zu GPU quads (%zu eligible), composition %s\n",s.name.c_str(),cpu.w,cpu.h,quads,eligible,
                        source.hd && !source.hd->composition.empty()?"on":"off (native fallback)");
            for(int scale:{1,2,4}){
                Frame g=rendered(gpu,source,scale);
                write_png((out/(s.name+"-gpu-"+std::to_string(scale)+"x.png")).string(),g);
                Result r=compare(cpu,g,scale,(out/(s.name+"-diff-"+std::to_string(scale)+"x.png")).string());
                const auto limit=limits.find(s.name);bool bad=scale==1?(r.exact<100.0):false;
                if(scale>1 && limit!=limits.end()) {
                    const unsigned miss=r.miss+r.lost,lost=r.lost;
                    bad=scale==2?(miss>limit->second.miss2 || lost>limit->second.lost2):(miss>limit->second.miss4 || lost>limit->second.lost4);
                }
                std::printf("  %dx: exact %.3f%%  miss %u/%u  lost %u  alien %u%s\n",scale,r.exact,r.miss+r.lost,r.native,r.lost,r.alien,bad?"  MISMATCH":"");
                if(summary)std::fprintf(summary,"%s %dx exact=%.3f miss=%u lost=%u alien=%u native=%u\n",s.name.c_str(),scale,r.exact,r.miss+r.lost,r.lost,r.alien,r.native);
                if(bad && s.vdp2)failed=true;
            }
            // The CPU scaled companion (no GPU) at the same scales, for reference.
            for(int scale:{2,4}){
                run_vdp1(false,scale);Frame c;vdp2_compose(c);
                if(c.w!=cpu.w*scale){std::printf("  cpu %dx companion: %dx%d (native)\n",scale,c.w,c.h);continue;}
                Result r=compare(cpu,c,scale,(out/(s.name+"-cpu-diff-"+std::to_string(scale)+"x.png")).string());
                std::printf("  cpu %dx companion: exact %.3f%%  miss %u  lost %u  alien %u\n",scale,r.exact,r.miss+r.lost,r.lost,r.alien);
                if(summary)std::fprintf(summary,"%s cpu-%dx exact=%.3f miss=%u alien=%u\n",s.name.c_str(),scale,r.exact,r.miss,r.alien);
            }
        }
        if(summary)std::fclose(summary);
        vdp1_gpu_available(false);vdp1_render_scale(1);
        gpu.release();SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(scratch);SDL_DestroyRenderer(renderer);gpu.destroy_device();SDL_Quit();
        if(failed && !report_only){std::fprintf(stderr,"FAIL: GPU output differs from the CPU path (see diff PNGs)\n");return 1;}
        std::printf(failed?"REPORT: mismatches recorded\n":"PASS: GPU within the CPU-parity limits on every synthetic scene; 1x exact\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s; SDL: %s\n",e.what(),SDL_GetError());return 1;}
}
