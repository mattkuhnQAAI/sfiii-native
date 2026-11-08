Texture2D<float4> picture:register(t0,space2);
SamplerState picture_sampler:register(s0,space2);
cbuffer Pass:register(b0,space3){float4 sizes;float4 axis_filter;};
struct Input {float4 color:TEXCOORD0;float2 uv:TEXCOORD1;float4 position:SV_Position;};
float weight(float x) {
    x=abs(x);
    if(axis_filter.z<.5) { // Catmull-Rom, support [-2,2].
        if(x<1)return (1.5*x-2.5)*x*x+1;
        if(x<2)return ((-.5*x+2.5)*x-4)*x+2;
        return 0;
    }
    if(x<.00001)return 1;
    if(x>=3)return 0;
    const float pi=3.141592653589793;
    return sin(pi*x)*sin(pi*x/3)/(pi*pi*x*x/3);
}
float4 main(Input input):SV_Target {
    float2 source=sizes.xy,destination=sizes.zw;
    float2 center=input.uv*source-.5;
    bool horizontal=axis_filter.x>.5;
    float coordinate=horizontal?center.x:center.y;
    float stretch=max(1.0,horizontal?source.x/destination.x:source.y/destination.y);
    float radius=(axis_filter.z<.5?2.0:3.0)*stretch;
    int first=int(ceil(coordinate-radius)),last=int(floor(coordinate+radius));
    float4 sum=0;float total=0;
    [loop]for(int i=first;i<=last;++i) {
        float w=weight((coordinate-i)/stretch);
        int2 at=int2(floor(center+.5));if(horizontal)at.x=i;else at.y=i;
        at=clamp(at,int2(0,0),int2(source)-1);
        sum+=picture.Load(int3(at,0))*w;total+=w;
    }
    return saturate(sum/max(total,.00001))*input.color;
}
