// Runs the real presentation hook against external, locally generated cases.
// Game instruction bytes and case data are never part of this source package.
#include "vertex-precision.h"
#include "saturn.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <stdexcept>
#include <vector>

uint8_t g_wram_h[0x100000]{},g_wram_l[0x100000]{};
uint32_t g_sh2_watch_lo=1,g_sh2_watch_len=0;
static SH2Module boot{};
extern const SH2Module* const g_sh2_modules[]={&boot};
extern const int g_sh2_nmodules=1;
uint32_t sh2_io_read(uint32_t,int){throw std::runtime_error("unexpected device read");}
void sh2_io_write(uint32_t,uint32_t,int){throw std::runtime_error("unexpected device write");}
void sh2_watch_store(uint32_t,uint32_t,int){}
uint64_t sat_vblanks(){return 100;}
static bool gpu=true;static int scale=5;
bool vdp1_gpu_active(){return gpu;}
int vdp1_render_scale(){return scale;}
void sf3_vertex_precision_call(SH2Context&,uint32_t,bool);
bool sf3_vertex_precision_quad(const uint8_t*,float*);
static void require(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
static uint32_t crc32(const std::vector<uint8_t>& bytes) {
    uint32_t crc=~0u;for(auto byte:bytes){crc^=byte;for(int b=0;b<8;++b)crc=(crc>>1)^(0xEDB88320u&-(crc&1));}return ~crc;
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    try {
        std::ifstream image(argv[1],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(image)),{});
        require(image.eof() || image.good(),"cannot read external BOOT");
        boot.name="BOOT";boot.base=0x06004000;boot.size=uint32_t(bytes.size());boot.crc=crc32(bytes);
        const auto* profile=sf3::vertex_producer_profile(boot.base,boot.size,boot.crc);
        require(profile && bytes.size()<0x80000,"unsupported external BOOT profile");
        std::copy(bytes.begin(),bytes.end(),g_wram_h+0x4000);
        std::ifstream cases(argv[2]);std::ofstream output(argv[3]);
        require(cases.good() && output.good(),"cannot open external cases/output");
        output<<std::setprecision(12);
        char kind;unsigned id;
        while(cases>>kind>>id) {
            if(kind=='P') {
                sf3::ProjectedPoint point;uint32_t x,y,z;int projection,near,ox,oy;
                require(bool(cases>>point.packed>>x>>y>>z>>projection>>near>>ox>>oy),"bad projection row");
                point.x=int32_t(x);point.y=int32_t(y);point.z=int32_t(z);
                std::array<float,2> precise{};
                bool ok=sf3::recover_projected_point(point,projection,near,precise);
                output<<"P "<<id<<' '<<ok<<' '<<precise[0]<<' '<<precise[1]<<'\n';
            } else if(kind=='Q') {
                unsigned entry,projection,near;std::string command;
                require(bool(cases>>entry>>projection>>near),"bad quad header");
                SH2Context cpu{};cpu.gbr=0x06080000;cpu.r[11]=0x06090000;
                cpu.pr=profile->queue;cpu.pc=profile->queue;
                st32(cpu.gbr+108,projection);st8(cpu.gbr+173,near);st16(cpu.gbr+148,0);st16(cpu.gbr+150,0);
                for(int i=0;i<4;++i) {
                    cpu.r[i+1]=0x06091000+i*16;
                    for(int field=0;field<4;++field){uint32_t value;require(bool(cases>>value),"bad quad point");st32(cpu.r[i+1]+field*4,value);}
                }
                require(bool(cases>>command) && command.size()==64,"bad quad command");
                uint8_t cmd[32];float native[8];
                for(int i=0;i<32;++i)cmd[i]=uint8_t(std::stoul(command.substr(i*2,2),nullptr,16));
                for(int i=0;i<8;++i)native[i]=float(int16_t(uint16_t(cmd[12+i*2])*256+cmd[13+i*2]))+11;
                auto submit=[&](bool returned) {
                    std::vector<uint8_t> retained(g_wram_h,g_wram_h+sizeof g_wram_h);
                    sf3_vertex_precision_call(cpu,profile->formatter+entry,false);
                    require(std::equal(retained.begin(),retained.end(),g_wram_h),"entry observer wrote guest memory");
                    std::copy(cmd,cmd+32,g_wram_h+0x90000);
                    retained.assign(g_wram_h,g_wram_h+sizeof g_wram_h);
                    sf3_vertex_precision_call(cpu,returned?profile->formatter+entry:profile->queue,returned);
                    require(std::equal(retained.begin(),retained.end(),g_wram_h),"queue observer wrote guest memory");
                };
                sf3::vertex_precision_configure(true);submit(entry==20);
                const std::vector<uint8_t> before(g_wram_h,g_wram_h+sizeof g_wram_h);
                float precise[8];std::copy(native,native+8,precise);
                bool applied=sf3_vertex_precision_quad(cmd,precise);
                require(std::equal(before.begin(),before.end(),g_wram_h),"presentation hook wrote guest memory");
                for(int i=0;i<8;++i)require(std::floor(precise[i]-11)==native[i]-11,"refinement changed the native integer cell");
                uint8_t viewport[8];std::copy(g_wram_h+(profile->projection&0xfffff)-8,g_wram_h+(profile->projection&0xfffff),viewport);
                for(int i=0;i<8;++i)g_wram_h[(profile->projection&0xfffff)-8+i]^=0x5a;
                sf3::vertex_precision_configure(true);submit(entry==20);std::copy(native,native+8,precise);
                require(sf3_vertex_precision_quad(cmd,precise)==applied,"adjacent writable viewport data disabled verified code");
                std::copy(viewport,viewport+8,g_wram_h+(profile->projection&0xfffff)-8);
                // Native resolution, unavailable GPU, reset, wrong caller and
                // changed code must all leave the same command untouched.
                scale=1;require(!sf3_vertex_precision_quad(cmd,precise),"1x must retain native vertices");scale=5;
                gpu=false;require(!sf3_vertex_precision_quad(cmd,precise),"GPU-off must retain native vertices");gpu=true;
                sf3::vertex_precision_configure(true);cpu.pr^=2;submit(entry==20);
                require(!sf3_vertex_precision_quad(cmd,precise),"wrong return site accepted");cpu.pr^=2;
                for(uint32_t address:{profile->formatter,profile->projection,profile->queue}) {
                    sf3::vertex_precision_configure(true);g_wram_h[address&0xfffff]^=1;submit(entry==20);
                    require(!sf3_vertex_precision_quad(cmd,precise),"changed producer code accepted");g_wram_h[address&0xfffff]^=1;
                }
                sf3::vertex_precision_configure(false);submit(entry==20);
                require(!sf3_vertex_precision_quad(cmd,precise),"disabled hook applied");
                output<<"Q "<<id<<' '<<applied<<'\n';
            } else throw std::runtime_error("unknown external case kind");
        }
        require(cases.eof(),"incomplete case stream");return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
