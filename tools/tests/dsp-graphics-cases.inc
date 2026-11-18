// Synthetic DSP programs and RGB555 tiles. No game instructions or assets.
// Product reference is independent of the runtime's sext48 helper.
static int64_t reference_product(int32_t x,int32_t y) {
    const uint64_t bits=uint64_t(int64_t(x)*y)&0xFFFFFFFFFFFFull;
    return bits&0x800000000000ull ? int64_t(bits)-0x1000000000000ll : int64_t(bits);
}

static void multiplier_path_cases() {
    const int32_t values[]={INT32_MIN,INT32_MIN+1,-65537,-32768,-1,0,1,32767,65537,INT32_MAX-1,INT32_MAX};
    unsigned cases=0;
    // Exercise instruction decoding and sequencing, not direct register setters.
    for(int bank=0;bank<4;++bank) for(bool increment : {false,true})
    for(int path=0;path<3;++path) for(int32_t x : values) for(int32_t y : values) {
        g={};g.rx=19;g.ry=y;g.mul=int64_t(19)*y;g.ram[bank][63]=x;g.ct[bank]=63;
        const unsigned source=bank+(increment ? 4 : 0);
        if(path==0) g.prog[0]=0x02000000|(source<<20); // X-bus RAM -> RX.
        if(path==1) g.prog[0]=0x00003400|source;      // D1-bus RAM -> RX.
        if(path==2) {
            g.rx=x;g.ry=19;g.mul=int64_t(x)*19;
            g.ram[bank][63]=y;
            g.prog[0]=0x00080000|(source<<14);       // Y-bus RAM -> RY.
        }
        g.prog[1]=0x01000000;g.prog[2]=0;g.prog[3]=0x01000000;
        g.prog[4]=0xF0000000;g.ex=true;run();
        check(g.p==reference_product(x,y),"RAM multiplier path must provide signed 48-bit product and retain it across NOP");
        check(g.ct[bank]==unsigned(increment ? 0 : 63),"Multiplier RAM source must increment once and wrap at 64");
        ++cases;
    }
    for(int32_t x : values) for(int32_t y : values) for(bool upper : {false,true}) {
        g={};g.a=upper ? int64_t(x)*65536 : int64_t(x);g.ry=y;g.mul=123;
        g.prog[0]=upper ? 0x0000340A : 0x00003409; // D1 ALH / ALL -> RX.
        g.prog[1]=0x01000000;g.prog[2]=0xF0000000;g.ex=true;run();
        check(g.p==reference_product(x,y),"Both accumulator halves must feed the RX multiplier path");
        ++cases;
    }
    const int32_t immediates[]={-16777216,-128,-1,0,1,127,16777215};
    for(int32_t x : immediates) for(int32_t y : values) {
        g={};g.ry=y;g.mul=123;
        g.prog[0]=0x90000000|(uint32_t(x)&0x1FFFFFF); // Signed MVI RX.
        g.prog[1]=0x01000000;g.prog[2]=0xF0000000;g.ex=true;run();
        check(g.p==reference_product(x,y),"Signed immediate RX path must feed multiplier");++cases;
        if(x>=-128 && x<=127) {
            g={};g.ry=y;g.mul=123;
            g.prog[0]=0x00001400|(uint32_t(x)&255); // Signed D1 immediate RX.
            g.prog[1]=0x01000000;g.prog[2]=0xF0000000;g.ex=true;run();
            check(g.p==reference_product(x,y),"Signed D1 immediate RX path must feed multiplier");++cases;
        }
    }
    // Conditional writes must update the product only on the selected path.
    for(bool z : {false,true}) for(bool positive_condition : {false,true})
    for(int32_t x : {-262144,-1,0,262143}) for(int32_t y : values) {
        g={};g.rx=13;g.ry=y;g.mul=int64_t(13)*y;g.z=z;
        const uint32_t condition=positive_condition ? 0x61 : 0x41;
        g.prog[0]=0x92000000|(condition<<19)|(uint32_t(x)&0x7FFFF);
        g.prog[1]=0x01000000;g.prog[2]=0xF0000000;g.ex=true;run();
        const bool taken=positive_condition ? z : !z;
        check(g.rx==(taken ? x : 13) && g.p==reference_product(taken ? x : 13,y),
              "Untaken conditional RX write must preserve the input and product");++cases;
    }
    // A simultaneous product read sees the preceding inputs; the next sees
    // both newly written inputs. Every RAM bank and counter mode is covered.
    for(int bank=0;bank<4;++bank) for(bool increment : {false,true})
    for(int32_t x : values) for(int32_t y : values) {
        g={};g.rx=2;g.ry=3;g.mul=6;g.a=x;g.ram[bank][63]=y;g.ct[bank]=63;
        const unsigned source=bank+(increment ? 4 : 0);
        g.prog[0]=0x01083409|(source<<14); // MOV MUL,P; RAM -> Y; ALL -> RX.
        // Use the ALU to preserve the preceding product in M1.
        g.prog[1]=0x00020000; // CLR A, preserving P.
        g.prog[2]=0x10003109; // ADD; ALL -> M1.
        g.prog[3]=0x01000000;g.prog[4]=0xF0000000;g.ex=true;run();
        const unsigned saved=(bank==1 && !increment) ? 63 : 0;
        // M1's CT can belong to the Y input. Read its actual destination slot.
        check(g.ram[1][saved]==6,"Parallel product read must consume previous multiplier inputs");
        check(g.p==reference_product(x,y),"Parallel Y/D1 writes must feed the following product");++cases;

        g={};g.rx=13;g.ry=y;g.mul=int64_t(13)*y;g.ram[bank][63]=x;g.ct[bank]=63;
        g.prog[0]=0x03000000|(source<<20); // RAM -> X and MOV MUL,P.
        g.prog[1]=0x00020000;g.prog[2]=0x10003109;
        g.prog[3]=0x01000000;g.prog[4]=0xF0000000;g.ex=true;run();
        check(g.ram[1][saved]==uint32_t(int64_t(13)*y),"Parallel X-bus update must not change the product read in the same instruction");
        check(g.p==reference_product(x,y),"Parallel X-bus write must feed the following product");++cases;
    }
    std::printf("PASS: %u multiplier input-path programs (all RAM banks, counter wrap, signed limits, immediates, conditions, parallel moves)\n",cases);
}

static void texture_processing_cases() {
    // Scale the channels of an invented 32x32 RGB555 tile using complete
    // DSP programs: input DMA -> D1 RX -> multiply -> shifts -> output DMA.
    // Every result and its neighboring bytes are checked against host math.
    constexpr uint32_t source=0x06001000,dest=0x05C10000;
    constexpr unsigned pixels=32*32,channels=pixels*3,batch_size=48;
    unsigned programs=0;
    for(unsigned gain : {0u,1u,64u,127u,128u,192u,255u,256u}) {
        fixture_video.assign(0x80000,0xCD);
        std::vector<uint16_t> actual(pixels,0x8000);
        for(unsigned start=0;start<channels;start+=batch_size) {
            const unsigned count=std::min(batch_size,channels-start);
            for(unsigned j=0;j<count;++j) {
                const unsigned sample=start+j,pixel=sample/3,x=pixel%32,y=pixel/32;
                const unsigned value=sample%3==0 ? x : sample%3==1 ? y : x^y;
                st32(source+4*j,value);
            }
            g={};g.ry=gain;g.mul=-12345;
            g.prog[0]=0x98000000|(source/4);
            g.prog[1]=0xC0010200|count; // Input DMA into M2.
            g.prog[2]=0;g.prog[3]=0xD3400003;g.prog[4]=0;
            g.prog[5]=0xA8000000|(count-1);g.prog[6]=0x00001B07;
            g.prog[7]=0x00003406; // MOV MC2,RX (the faulty path).
            g.prog[8]=0x01020000;g.prog[9]=0x10040000; // Product -> AC.
            for(unsigned j=10;j<18;++j) g.prog[j]=0x20040000; // Divide by 256.
            g.prog[18]=0x00003009; // Channel -> M0.
            g.prog[19]=0xE0000000;g.prog[20]=0;
            g.prog[21]=0x00001C00; // Rewind M0 after producing the batch.
            g.prog[22]=0x9C000000|((dest+4*start)/4);
            g.prog[23]=0xC0011000|count; // Output DMA from M0 into VRAM.
            g.prog[24]=0;g.prog[25]=0xD3400019;g.prog[26]=0;
            g.prog[27]=0xF0000000;g.ex=true;run();++programs;
            for(unsigned j=0;j<count;++j) {
                const unsigned sample=start+j,pixel=sample/3,x=pixel%32,y=pixel/32;
                const unsigned value=sample%3==0 ? x : sample%3==1 ? y : x^y;
                const uint32_t result=ld32(dest+4*sample);
                if(result!=value*gain/256) throw std::runtime_error("Texture channel mismatch: gain="+std::to_string(gain)+
                    " sample="+std::to_string(sample)+" actual="+std::to_string(result)+" expected="+std::to_string(value*gain/256));
                actual[pixel]|=result<<(5*(sample%3));
            }
        }
        for(unsigned pixel=0;pixel<pixels;++pixel) {
            const unsigned x=pixel%32,y=pixel/32;
            const uint16_t expected=0x8000|(x*gain/256)|((y*gain/256)<<5)|(((x^y)*gain/256)<<10);
            check(actual[pixel]==expected,"RGB555 tile must retain channel ordering and brightness");
        }
        for(unsigned j=0;j<fixture_video.size();++j)
            if(j<dest-0x05C00000 || j>=dest-0x05C00000+channels*4)
                check(fixture_video[j]==0xCD,"Texture DMA must leave all unrelated VRAM unchanged");
    }
    fixture_video.clear();
    std::printf("PASS: %u texture-processing programs, 8192 RGB555 pixels across eight brightness levels\n",programs);
}
