// Invented DSP programs. Optional local crash fixture stays outside source.
#include SCUDSP_SOURCE
#include <stdexcept>
#include <cstdarg>
#include <fstream>
#include <iterator>

extern const SH2Module* const g_sh2_modules[] = {nullptr};
extern const int g_sh2_nmodules=0;
void sh2_program_start(SH2Context&,uint32_t) {throw std::runtime_error("unexpected SH2 start");}
void sh2_call_unknown(SH2Context&,uint32_t) {throw std::runtime_error("unexpected SH2 call");}
static std::vector<uint8_t> fixture_video;
uint32_t sh2_mmio_read(uint32_t address,int size) {
    if(address>=0x05C00000 && uint64_t(address)+size<=0x05C80000 && !fixture_video.empty()) {
        uint32_t value=0;for(int i=0;i<size;++i) value=value<<8|fixture_video[address-0x05C00000+i];return value;
    }
    throw std::runtime_error("unmapped DSP read "+std::to_string(address));
}
void sh2_mmio_write(uint32_t address,uint32_t value,int size) {
    if(address>=0x05C00000 && uint64_t(address)+size<=0x05C80000 && !fixture_video.empty()) {
        for(int i=size-1;i>=0;--i) {fixture_video[address-0x05C00000+i]=uint8_t(value);value>>=8;}return;
    }
    throw std::runtime_error("unmapped DSP write "+std::to_string(address));
}
void scu_raise(int) {}
void sat_note(const char*,...) {}
[[noreturn]] void sat_fatal(const char* fmt,...) {
    char text[1024];va_list args;va_start(args,fmt);std::vsnprintf(text,sizeof text,fmt,args);va_end(args);
    throw std::runtime_error(text);
}
[[noreturn]] void state_fail(const char* text) {throw std::runtime_error(text);}
static void check(bool yes,const char* reason) {if(!yes) throw std::runtime_error(reason);}
#include "dsp-graphics-cases.inc"
static void proactive_cases() {
    // Sega ST-097 condition truth table, exercised through complete programs
    // rather than only testing the decoder/condition helper in isolation.
    const uint32_t codes[]={0x41,0x42,0x43,0x44,0x48,0x61,0x62,0x63,0x64,0x68};
    unsigned cases=0;
    for(unsigned flags=0;flags<8;++flags) {
        const bool z=flags&1,s=flags&2,c=flags&4;
        const bool expected[]={!z,!s,!(z||s),!c,true,z,s,z||s,c,false};
        for(unsigned k=0;k<10;++k) {
            for(bool jump : {false,true}) {
                g={};g.z=z;g.s=s;g.c=c;
                g.prog[0]=(jump ? 0xD0000000u : 0xB0000000u)|(codes[k]<<19)|4;
                g.prog[1]=0x00001007; // Marker in the delay/sequential word.
                g.prog[2]=0xF0000000;
                g.prog[4]=0x00001109; // Marker reached only on a taken transfer.
                g.prog[5]=0xF0000000;g.ex=true;run();
                check(g.ct[0]==1 && g.ram[0][0]==7,"Conditional PC transfer must execute the following word once on taken and untaken paths");
                check(g.ct[1]==unsigned(expected[k]),"Conditional PC transfer must honor the flags");
                if(!jump && expected[k]) check(g.top==1,"Taken MVI PC must retain the return address in TOP");
                ++cases;
            }
            g={};g.z=z;g.s=s;g.c=c;
            g.prog[0]=0x88000000u|(codes[k]<<19)|0x7FFFF; // Conditional signed MVI -1,MC2.
            g.prog[1]=0xF0000000;g.ex=true;run();
            check(g.ct[2]==unsigned(expected[k]),"Conditional data transfer must honor all conditions");
            if(expected[k]) check(g.ram[2][0]==0xFFFFFFFF,"Conditional immediate must sign-extend 19 bits");
            ++cases;
        }
    }
    for(int bank=0;bank<4;++bank) for(bool hold : {false,true}) for(unsigned count : {1u,2u,3u,8u,63u,64u}) {
        for(unsigned i=0;i<count;++i) st32(0x06000400+4*i,0x12340000+i);
        g={};g.ra0=0x06000400/4;g.ct[bank]=62;
        g.prog[0]=0xC0010000u|(hold ? 0x4000 : 0)|(bank<<8)|count;
        g.prog[1]=0xD2400001;g.prog[2]=0xD3400002;g.prog[3]=0;g.prog[4]=0xF0000000;
        g.ex=true;run();
        for(unsigned i=0;i<count;++i) check(g.ram[bank][(62+i)&63]==0x12340000+i,"DMA must copy each word across bank wrap");
        check(g.ra0==0x06000400/4+(hold ? 0 : count),"DMA hold mode must preserve the external address");
        ++cases;
    }
    std::printf("PASS: %u proactive DSP programs (condition paths, signed immediates, DMA banks/counts/wrap/hold)\n",cases);
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--graphics-only") {
            multiplier_path_cases();texture_processing_cases();return 0;
        }
        if(argc==2 && std::string(argv[1])=="--texture-only") {
            texture_processing_cases();return 0;
        }
        if(argc!=1 && argc!=3 && argc!=4 && argc!=5)
            throw std::runtime_error("usage: dsp_regression [--graphics-only | --texture-only | START WRAM [VIDEO [OUTPUT]]]");
        if(argc==3 || argc==4 || argc==5) {
            std::ifstream snapshot(argv[1],std::ios::binary);
            check(bool(snapshot.read((char*)&g,sizeof g)),"DSP snapshot size");
            std::ifstream file(argv[2],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
            check(bytes.size()==0x100000 && sh2_mem_write(0x06000000,bytes.data(),bytes.size()),"WRAM fixture");
            if(argc>=4) {
                std::ifstream video(argv[3],std::ios::binary);
                fixture_video.assign(std::istreambuf_iterator<char>(video),{});
                check(fixture_video.size()>=0x80000,"video fixture size");
            }
            std::printf("Local DSP fixture: start=%02X top=%02X lop=%03X\n",g.pc,g.top,g.lop);
            run();check(!g.ex,"local DSP fixture did not finish");
            if(argc==5) {
                std::ofstream output(argv[4],std::ios::binary);
                check(bool(output.write((const char*)fixture_video.data(),fixture_video.size())),"video fixture output");
            }
            std::puts("PASS: local DSP fixture completed");return 0;
        }
        // RX has two write paths in addition to X-bus moves. Each must feed
        // the multiplier before the next instruction can transfer MUL to P.
        g={};g.rx=2;g.ry=7;g.mul=14;g.a=11;
        op_alu(0x00003409); // NOP; MOV ALL,RX (D1 bus).
        op_alu(0x01000000); // MOV MUL,P.
        check(g.p==77,"D1 RX writes must refresh the multiplier");
        g={};g.ry=-9;g.prog[0]=0x90000005; // MVI #5,RX.
        g.prog[1]=0x01000000;g.prog[2]=0xF0000000;g.ex=true;run();
        check(g.p==-45,"Immediate RX writes must refresh the signed multiplier");
        const int32_t products[]={INT32_MIN,-65537,-1,0,1,65537,INT32_MAX};
        for(int32_t x : products) for(int32_t y : products) {
            g={};g.a=x;g.ry=y;g.mul=123;
            op_alu(0x00003409);op_alu(0x01000000);
            check(g.p==sext48(int64_t(x)*y),"D1 RX signed product boundary");
        }
        g={};g.rx=2;g.ry=3;g.mul=6;g.a=5;g.ram[0][0]=7;
        op_alu(0x01083409); // MOV MUL,P; MOV M0,Y; MOV ALL,RX.
        check(g.p==6 && g.rx==5 && g.ry==7,"Parallel moves must retain the old product for MOV MUL,P");
        op_alu(0x01000000);
        check(g.p==35,"Simultaneous Y-bus and D1 writes must produce the next product");
        g={};g.a=0x123456789ABCll;g.alu=0x111122223333ull;g.z=true;g.c=true;
        op_alu(0x00003409); // NOP ALU; MOV ALL,RX.
        check(uint32_t(g.rx)==0x56789ABC,"NOP must pass AC through to the D1 bus");
        check(g.alu==0x123456789ABCull,"NOP must pass all 48 accumulator bits");
        check(g.z && g.c,"NOP must preserve flags");
        g={};g.a=sext48(0xFEDC00000003ll);g.p=4;g.alu=0x111100000000ull;
        op_alu(0x10000000); // ADD, retaining accumulator's upper bits.
        check(g.alu==0xFEDC00000007ull,"32-bit ALU must retain accumulator upper bits");
        g={};g.prog[0]=0xA8000002;g.prog[1]=0xE8000000;g.prog[2]=0x00003404;g.prog[3]=0xF0000000;
        g.ram[0][0]=10;g.ram[0][1]=20;g.ram[0][2]=30;g.ex=true;run();
        check(g.ct[0]==3 && g.rx==30,"LPS must execute the following word LOP+1 times");
        g={};g.prog[0]=0xB0000004;g.prog[1]=0x00001007;g.prog[2]=0x40000000;g.prog[4]=0xF0000000;
        g.ex=true;run();check(g.ct[0]==1 && g.ram[0][0]==7,"PC load must execute its prefetched delay word once");
        g={};g.pc=0xFD;g.prog[0]=0x40000000;
        // Constructed immediate-address loader, crossing the program RAM boundary.
        g.prog[0xFD]=0x98000000|(0x06000400/4); // MVI source,RA0.
        g.prog[0xFE]=0xC0010402;              // DMA two immediate-count words to PRG.
        g.prog[0xFF]=0xD0000000;              // Prefetched JMP 0.
        g.ram[1][0]=10;g.ram[1][1]=20;
        const uint8_t program[]={0x00,0x00,0x34,0x05,0xF0,0,0,0};
        check(sh2_mem_write(0x06000400,program,sizeof program),"program DMA source RAM");
        g.ex=true;run();
        check(g.prog[0]==0x00003405 && g.prog[1]==0xF0000000,"program DMA must begin two slots after its instruction and wrap at 256");
        check(g.ct[1]==1 && g.rx==10,"program reload must flush stale fetch and execute first uploaded word once");
        // A caller can wait for the start edge, then for the end edge. Keeping
        // T0 permanently clear deadlocks the first loop despite completed data.
        g={};g.ra0=0x06000400/4;
        g.prog[0]=0xC0010104; // DMA four words to bank 1.
        g.prog[1]=0xD2400001; // JMP NT0,1 (wait for start).
        g.prog[2]=0xD3400002; // JMP T0,2 (wait for end).
        g.prog[3]=0;g.prog[4]=0xF0000000;g.ex=true;run();
        check(!g.ex && g.ram[1][0]==0x00003405,"DMA start/end wait pair must finish with transferred data");
        check(cond(0x68,true) && !cond(0x68,false) && !cond(0x48,true) && cond(0x48,false),"T0 and NT0 must test opposite DMA states");
        g={};g.ra0=0x06000400/4;
        g.prog[0]=0xC0010104; // DMA four words to bank 1.
        g.prog[1]=0x83400007; // MVI 7,MC0,T0 while busy.
        g.prog[2]=0xD3400002; // Wait until DMA ends.
        g.prog[3]=0;
        g.prog[4]=0x82400009; // MVI 9,MC0,NT0 while idle.
        g.prog[5]=0xF0000000;g.ex=true;run();
        check(g.ct[0]==2 && g.ram[0][0]==7 && g.ram[0][1]==9,"Conditional MVI must observe busy and idle DMA states");
        multiplier_path_cases();
        texture_processing_cases();
        proactive_cases();
        std::puts("PASS: DSP accumulator bypass, upper bits, flags, repetition, branch delay, program DMA placement, pipeline flush and T0 start/end polling");return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
