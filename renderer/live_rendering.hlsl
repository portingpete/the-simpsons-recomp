// Live Video menu changes re-back scaled engine targets at a new extent or
// coverage. Carry their current contents across: filtered color, nearest
// depth, and stencil one bit per pass (no shader stencil export required).
// The vertex stage is VSAntialiasing (fullscreen triangle with 0..1 UVs).
Texture2D<float4> sourceColor : register(t0);
Texture2D<float> sourceDepth : register(t1);
Texture2D<uint2> sourceStencil : register(t2);
SamplerState linearClamp : register(s0);
cbuffer Stretch : register(b0) {
    // Source pixels per destination pixel, and the stencil bit of this pass.
    float2 sourceScale;
    uint stencilBit;
    uint padding;
};
struct Varying {float4 position : SV_POSITION;float2 uv : TEXCOORD0;};
float4 PSLiveStretchColor(Varying input) : SV_TARGET {
    return sourceColor.SampleLevel(linearClamp,input.uv,0);
}
int3 sourcePixel(float4 position,uint w,uint h) {
    return int3(min(int2(position.xy*sourceScale),int2(w,h)-1),0);
}
float PSLiveStretchDepth(Varying input) : SV_Depth {
    uint w,h;sourceDepth.GetDimensions(w,h);
    return sourceDepth.Load(sourcePixel(input.position,w,h));
}
void PSLiveStretchStencil(Varying input) {
    uint w,h;sourceStencil.GetDimensions(w,h);
    if(!(sourceStencil.Load(sourcePixel(input.position,w,h)).y&stencilBit))discard;
}
