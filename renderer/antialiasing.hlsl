// Native presentation filters. FXAA uses local contrast adaptive directional
// edge blur (Timothy Lottes, FXAA Console, SIGGRAPH 2011 course notes):
// https://www.iryoku.com/aacourse/downloads/Filtering-Approaches-for-Real-Time-Anti-Aliasing.pdf
Texture2D<float4> source : register(t0);
SamplerState linearClamp : register(s0);
struct Varying {float4 position : SV_POSITION;float2 uv : TEXCOORD0;};
Varying VSAntialiasing(uint id : SV_VertexID) {
    Varying result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
float luma(float3 rgb) {return dot(rgb,float3(0.299,0.587,0.114));}
float4 PSFXAA(Varying input) : SV_TARGET {
    uint w,h;source.GetDimensions(w,h);
    int2 pixel=int2(input.position.xy);
    int2 last=int2(w,h)-1;
    float4 center=source.Load(int3(pixel,0));
    float nw=luma(source.Load(int3(clamp(pixel+int2(-1,-1),0,last),0)).rgb);
    float ne=luma(source.Load(int3(clamp(pixel+int2(1,-1),0,last),0)).rgb);
    float sw=luma(source.Load(int3(clamp(pixel+int2(-1,1),0,last),0)).rgb);
    float se=luma(source.Load(int3(clamp(pixel+int2(1,1),0,last),0)).rgb);
    float middle=luma(center.rgb);
    float low=min(middle,min(min(nw,ne),min(sw,se)));
    float high=max(middle,max(max(nw,ne),max(sw,se)));
    // Leave low-contrast/flat regions untouched, including their packed values.
    if(high-low<max(1.0/32.0,high/8.0))return center;
    float2 direction=float2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
    float reduce=max((nw+ne+sw+se)*(0.25/8.0),1.0/128.0);
    direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduce),-8.0,8.0)/float2(w,h);
    float3 two=0.5*(source.SampleLevel(linearClamp,input.uv-direction/6.0,0).rgb+
                    source.SampleLevel(linearClamp,input.uv+direction/6.0,0).rgb);
    float3 four=two*0.5+0.25*(source.SampleLevel(linearClamp,input.uv-direction*0.5,0).rgb+
                              source.SampleLevel(linearClamp,input.uv+direction*0.5,0).rgb);
    float filtered=luma(four);
    return float4(filtered<low||filtered>high?two:four,center.a);
}
float4 PSSSAA4x(Varying input) : SV_TARGET {
    int2 pixel=int2(input.position.xy)*2;
    // Four independent scene samples per output pixel; no display-size shortcut.
    return (source.Load(int3(pixel,0))+source.Load(int3(pixel+int2(1,0),0))+
            source.Load(int3(pixel+int2(0,1),0))+source.Load(int3(pixel+int2(1,1),0)))*0.25;
}
