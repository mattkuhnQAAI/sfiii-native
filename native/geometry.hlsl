// Native texture sampling, geometry coverage and VDP2 composition on the GPU.
ByteAddressBuffer scene:register(t1,space2);
ByteAddressBuffer textures:register(t2,space2);
Texture2D<float4> picture:register(t0,space2);
SamplerState picture_sampler:register(s0,space2);
cbuffer Pass:register(b0,space3){uint aa_pass;uint3 pass_padding;};
uint S(uint at){return scene.Load(at*4);}
uint T(uint at){return textures.Load(at*4);}
struct Input {float4 color:TEXCOORD0;float2 uv:TEXCOORD1;float4 position:SV_Position;};
float2 vertex_at(uint q,uint at){return asfloat(uint2(S(q+at),S(q+at+1)));}
bool sample_quad(uint q,float2 p,int2 cell,out uint color) {
    color=0;uint flags=S(q+25);if(flags==0xFFFFFFFF)return false;
    if(cell.x<int(S(q+32)) || cell.y<0 || cell.x>int(S(q+26)) || cell.y>int(S(q+27)))return false;
    if(flags&0x400) {
        bool inside=cell.x>=int(S(q+28)) && cell.y>=int(S(q+29)) && cell.x<=int(S(q+30)) && cell.y<=int(S(q+31));
        if(inside==((flags&0x200)!=0))return false;
    }
    float2 a=vertex_at(q,0),b=vertex_at(q,2)-a,d=vertex_at(q,6)-a,c=a-vertex_at(q,2)+vertex_at(q,4)-vertex_at(q,6);
    // Inverse bilinear iteration alone can converge to a spurious solution
    // outside a strongly skewed quad. Tile bins are only an optimization;
    // changing their origin must never change coverage.
    float2 corners[4]={a,a+b,vertex_at(q,4),a+d};float orientation=0;
    [unroll]for(int edge=0;edge<4;++edge) {
        float2 v=corners[(edge+1)&3]-corners[edge],to=p-corners[edge];
        float side=v.x*to.y-v.y*to.x;
        if(abs(side)>0.00001) {if(orientation*side<0)return false;orientation=side;}
    }
    float2 uv=.5;
    [unroll]for(int i=0;i<6;++i) {
        float2 f=a+b*uv.x+d*uv.y+c*uv.x*uv.y-p,du=b+c*uv.y,dv=d+c*uv.x;
        float determinant=du.x*dv.y-du.y*dv.x;if(abs(determinant)<0.00001)return false;
        uv-=float2(f.x*dv.y-f.y*dv.x,du.x*f.y-du.y*f.x)/determinant;
    }
    if(any(isnan(uv)) || any(isinf(uv)) || any(uv<0) || any(uv>1))return false;
    if(any(abs(a+b*uv.x+d*uv.y+c*uv.x*uv.y-p)>0.001))return false;
    uint w=S(q+23),h=S(q+24),base=S(q+22);
    uint u=min(uint(uv.x*w),w-1),v=min(uint(uv.y*h),h-1);
    if(S(q+20))u=w-1-u;if(S(q+21))v=h-1-v;
    uint raw=T(base+v*w+u);
    if(raw&0x2000000)return false;
    if(raw==0 && !(flags&0x40))return false;
    if(raw==0){color=0x1000000;return true;} // SPD clears the previous sprite dot.
    int3 rgb=int3(raw&31,(raw>>5)&31,(raw>>10)&31);
    if(flags&4) {
        float3 ga=float3(asfloat(S(q+8)),asfloat(S(q+9)),asfloat(S(q+10)))/8+16;
        float3 gb=float3(asfloat(S(q+11)),asfloat(S(q+12)),asfloat(S(q+13)))/8+16;
        float3 gc=float3(asfloat(S(q+14)),asfloat(S(q+15)),asfloat(S(q+16)))/8+16;
        float3 gd=float3(asfloat(S(q+17)),asfloat(S(q+18)),asfloat(S(q+19)))/8+16;
        int3 top=int3(ga)+int3((gb-ga)*uv.x),bottom=int3(gd)+int3((gc-gd)*uv.x);
        rgb=clamp(rgb+top+int3((bottom-top)*uv.y)-16,0,31);
    }
    uint3 bytes=(uint3(rgb)<<3)|(uint3(rgb)>>2);
    color=bytes.r<<16|bytes.g<<8|bytes.b;return true;
}
float4 output_color(uint rgb){return float4(float3((rgb>>16)&255,(rgb>>8)&255,rgb&255)/255,1);}
// Original, conservative spatial edge filter. No history, depth or vendor SDK.
// Mask by native ownership: ordinary sprite/text and unsupported VDP2 paths
// retain their exact samples. This intentionally leaves unclassified edges alone.
float4 edge_aa(Input input) {
    float4 center=picture.Load(int3(uint2(input.position.xy),0));
    uint2 cell=min(uint2(input.position.xy)/S(2),uint2(S(0)-1,S(1)-1));
    uint p=S(4)+(cell.y*S(0)+cell.x)*10,owner=S(p+8);
    if(S(p+3)==0 || (S(p+3)&0x100) || owner==0 || owner>S(3))return float4(center.rgb,1);
    uint q=S(5)+(owner-1)*40;
    if(S(q+25)==0xFFFFFFFF || ((S(q+25)>>16)&15)!=2)return float4(center.rgb,1);
    float2 texel=1.0/float2(S(0)*S(2),S(1)*S(2));
    float3 n=picture.SampleLevel(picture_sampler,input.uv-float2(0,texel.y),0).rgb;
    float3 s=picture.SampleLevel(picture_sampler,input.uv+float2(0,texel.y),0).rgb;
    float3 e=picture.SampleLevel(picture_sampler,input.uv+float2(texel.x,0),0).rgb;
    float3 w=picture.SampleLevel(picture_sampler,input.uv-float2(texel.x,0),0).rgb;
    const float3 luminance=float3(.299,.587,.114);
    float c=dot(center.rgb,luminance),ln=dot(n,luminance),ls=dot(s,luminance),le=dot(e,luminance),lw=dot(w,luminance);
    float lo=min(c,min(min(ln,ls),min(le,lw))),hi=max(c,max(max(ln,ls),max(le,lw)));
    float contrast=hi-lo,threshold=max(.04,hi*.125);
    float2 gradient=float2(le-lw,ls-ln),magnitude=abs(gradient);
    // Avoid softening axis-aligned pixel-art edges and isolated texels.
    if(contrast<threshold || min(magnitude.x,magnitude.y)<max(magnitude.x,magnitude.y)*.1 || dot(gradient,gradient)<.0001)
        return float4(center.rgb,1);
    float2 offset=normalize(gradient)*texel*.5;
    float3 smooth=.5*(picture.SampleLevel(picture_sampler,input.uv-offset,0).rgb+
                         picture.SampleLevel(picture_sampler,input.uv+offset,0).rgb);
    float strength=.65*saturate((contrast-threshold)/max(contrast,.0001));
    return float4(lerp(center.rgb,smooth,strength),1);
}
// A short ray through the visible Saturn command owners approximates a
// directional contact shadow. It has no world-space depth or hardware RT
// acceleration; only eligible geometry receives the optional presentation tint.
float4 ray_shadow(Input input,float4 base) {
    uint w=S(0),h=S(1),scale=S(2);
    int2 cell=int2(min(uint2(input.position.xy)/scale,uint2(w-1,h-1)));
    uint p=S(4)+(uint(cell.y)*w+uint(cell.x))*10,owner=S(p+8),mode=S(p+3);
    if(mode==0 || (mode&0x100) || owner==0 || owner>S(3))return base;
    uint q=S(5)+(owner-1)*40,flags=S(q+25),area=S(q+33);
    if(flags==0xFFFFFFFF || ((flags>>16)&15)!=2 || area<120)return base;
    float strength=0;
    [loop]for(int step=1;step<=8;++step) {
        int2 sample_cell=cell-int2(step,(step+1)/2);
        if(any(sample_cell<0))break;
        uint sample_p=S(4)+(uint(sample_cell.y)*w+uint(sample_cell.x))*10;
        uint blocker=S(sample_p+8);
        if(blocker<=owner || blocker>S(3))continue;
        uint block_q=S(5)+(blocker-1)*40;
        if(S(block_q+25)==0xFFFFFFFF)continue;
        uint block_area=S(block_q+33);
        if(block_area>0 && block_area*3<area*2) {
            strength=.24*(1.0-float(step)/10.0);
            break;
        }
    }
    return float4(base.rgb*(1.0-strength),1);
}
uint rotation_byte(uint base,uint address) {address&=0x7FFFF;return (T(base+address/4)>>((address&3)*8))&255;}
uint rotation_word(uint base,uint address) {address&=0x7FFFE;return rotation_byte(base,address)*256+rotation_byte(base,address+1);}
bool rotation_sample(uint info,float2 position,out uint color) {
    color=0;uint d=S(info),rows=S(info+1),memory=S(info+2),palette=S(info+3),height=S(info+4);
    float y=clamp(position.y,0.0,float(height));uint row=min(uint(y),height-1);
    float4 a=asfloat(uint4(S(rows+row*4),S(rows+row*4+1),S(rows+row*4+2),S(rows+row*4+3)));
    float4 b=asfloat(uint4(S(rows+(row+1)*4),S(rows+(row+1)*4+1),S(rows+(row+1)*4+2),S(rows+(row+1)*4+3)));
    float2 uv=lerp(a.xy+a.zw*position.x,b.xy+b.zw*position.x,y-row);
    if(any(isnan(uv)) || any(isinf(uv)) || any(abs(uv)>4194304))return false;
    uint2 map_point=uint2(int2(floor(uv)));uint mw=S(d+11),mh=S(d+12),over=S(d+13),flags=S(d);
    bool outside=over==3?((map_point.x|map_point.y)&~511u)!=0:over!=0 && ((map_point.x&~(mw-1))!=0 || (map_point.y&~(mh-1))!=0);
    if(outside && (over&2))return false;
    map_point&=uint2(mw-1,mh-1);
    uint colors=S(d+1),base=0,pal=0,index=0;
    if(flags&1) {base=S(d+8);pal=S(d+9);index=map_point.y*S(d+6)+map_point.x;}
    else {
        uint pw=S(d+3)*512,ph=S(d+4)*512,cs=(flags&2)?16:8;
        uint2 local=map_point%uint2(pw,ph);uint page=local.y/512*S(d+3)+local.x/512;
        uint tile=(local.y%512)/cs*(512/cs)+(local.x%512)/cs;
        uint address=S(d+16+(map_point.y/ph)*4+map_point.x/pw)+page*S(d+5)+tile*((flags&4)?2:4);
        uint chr=0,supp=S(d+2);bool hf=false,vf=false;
        if((flags&4) || outside) {
            uint w=outside?S(d+14):rotation_word(memory,address);
            pal=colors==0?((w>>12)&15)|((supp>>1)&0x70):(w>>8)&0x70;
            if(!(flags&8)) {
                hf=(w&0x400)!=0;vf=(w&0x800)!=0;
                chr=(flags&2)?((w&0x3FF)<<2)|(supp&3)|((supp&0x1C)<<10):(w&0x3FF)|((supp&0x1F)<<10);
            } else chr=(flags&2)?((w&0xFFF)<<2)|(supp&3)|((supp&0x10)<<10):(w&0xFFF)|((supp&0x1C)<<10);
        } else {
            uint w=rotation_word(memory,address);hf=(w&0x4000)!=0;vf=(w&0x8000)!=0;
            pal=w&0x7F;chr=rotation_word(memory,address+2)&0x7FFF;
        }
        uint2 pixel=local%cs;if(hf)pixel.x=cs-1-pixel.x;if(vf)pixel.y=cs-1-pixel.y;
        base=chr*32;
        if(flags&2)base+=(pixel.y/8*2+pixel.x/8)*(colors==0?32:colors==1?64:128);
        index=(pixel.y&7)*8+(pixel.x&7);
    }
    if(colors==3) {
        uint raw=rotation_word(memory,base+index*2);
        color=(raw&31)*8<<16|((raw>>5)&31)*8<<8|((raw>>10)&31)*8;return true;
    }
    uint value=colors==0?rotation_byte(memory,base+index/2):colors==1?rotation_byte(memory,base+index):rotation_word(memory,base+index*2)&0x7FF;
    if(colors==0)value=(index&1)?value&15:value>>4;
    uint entry=colors==0?pal*16+value:colors==1?(pal&0x70)*16+value:value;
    color=T(palette+((entry+S(d+10))&2047));return true;
}
uint rotation_offset(uint info,uint color) {
    uint result=0;
    [unroll]for(int channel=0;channel<3;++channel) {
        uint shift=16-channel*8;result|=uint(clamp(int((color>>shift)&255)+int(S(info+5+channel)),0,255))<<shift;
    }
    return result;
}
float4 main(Input input):SV_Target {
    if(aa_pass!=0) {
        float4 base=(aa_pass&1)!=0?edge_aa(input):picture.Load(int3(uint2(input.position.xy),0));
        return (aa_pass&2)!=0?ray_shadow(input,base):base;
    }
    uint w=S(0),h=S(1),scale=S(2);uint2 pixel=uint2(input.position.xy),cell=min(pixel/scale,uint2(w-1,h-1));
    uint p=S(4)+(cell.y*w+cell.x)*10,mode=S(p+3);
    if(mode&0x100)return output_color(S(p));
    uint native_color=S(p),absent=S(p+1),fixed=S(p+2),rotation=S(p+9),info=S(11);
    if(info!=0 && rotation!=0 && scale>1) {
        uint original=0,enhanced=0;
        float2 center=float2(int2(cell)-int2(int(S(9)),0));
        float2 sample_position=(float2(pixel)+.5)/scale-.5-float2(int(S(9)),0);
        // Retain native pixels when format/precision cannot reproduce the CPU sample.
        if(rotation_sample(info,center,original) && original==(rotation&0xFFFFFF) && rotation_sample(info,sample_position,enhanced)) {
            uint roles=rotation>>24,offset_color=rotation_offset(info,enhanced);
            if(roles&1)native_color=offset_color;
            if(roles&2)absent=offset_color;
            if(roles&4)fixed=(roles&8)?offset_color:enhanced;
        }
    }
    uint tile=S(10);
    uint bin=S(6)+(cell.y/tile*S(8)+cell.x/tile)*2,start=S(bin),count=S(bin+1),color=0;bool found=false;
    int origin=int(S(9));int2 native_cell=int2(cell)-int2(origin,0);
    float2 position=(float2(int2(pixel)-int2(origin*int(scale),0))+.5)/scale-.5;
    [loop]for(uint i=count;i>0;--i) {
        uint id=S(start+i-1),q=S(5)+id*40;
        if(sample_quad(q,position,native_cell,color)){found=true;break;}
    }
    if(!found)return output_color(S(p+8)!=0?absent:native_color);
    if(color&0x1000000)return output_color(absent);
    if(mode==0)return output_color(fixed);
    uint ratio=S(p+4),result=0;
    [unroll]for(int channel=0;channel<3;++channel) {
        int sh=16-channel*8;uint a=(color>>sh)&255,b=(fixed>>sh)&255,c=a;
        if(mode==2)c=(a*(31-ratio)+b*(ratio+1))>>5;
        if(mode==3 || mode==5)c=min(a+b,255);
        if(mode==4)c=(b*(31-ratio)+a*(ratio+1))>>5;
        c=uint(clamp(int(c)+int(S(p+5+channel)),0,255));result|=c<<sh;
    }
    return output_color(result);
}
