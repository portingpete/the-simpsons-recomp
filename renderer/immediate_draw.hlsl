#include "radial_shader.hlsl"
#include "projected_immediate_shader.hlsl"
#include "shadow_mesh.hlsl"
cbuffer ImmediateDrawState:register(b2) {uint immediateBlend;float immediateAlphaReference;uint immediateBlendEnable;uint immediateAlphaTest;};
Texture2D<uint4> immediateDestination:register(t3);
struct ImmediatePixelOutput {uint4 color:SV_Target0;float depth:SV_Depth;};
ImmediatePixelOutput packImmediate(float4 source,float4 position) {
    if((immediateAlphaTest&1u) && !(source.a>immediateAlphaReference))discard;
    uint4 codes=immediateDestination.Load(int3(int2(position.xy),0));
    precise float4 destination=float4(codes)/float4(1023,1023,1023,3);
    precise float3 product=source.rgb*source.a;
    precise float3 rgb;
    if(!immediateBlendEnable||immediateBlend==0x10001u)rgb=source.rgb;
    else if(immediateBlend==0x10106u)rgb=product+destination.rgb;
    else if(immediateBlend==0x10186u)rgb=destination.rgb-product;
    else if(immediateBlend==0x10706u) {
        precise float inverseAlpha=1.0-source.a;
        precise float3 retained=destination.rgb*inverseAlpha;
        rgb=product+retained;
    } else {
        rgb=float3(1.0,0.0,1.0);
    }
    ImmediatePixelOutput result;
    result.color=uint4(round(saturate(float4(rgb,source.a))*float4(1023,1023,1023,3)));
    result.depth=PSShadowMeshDepth(position);return result;
}
ImmediatePixelOutput PSImmediateDraw(ImmediateOutput input) {return packImmediate(PSImmediate(input),input.position);}
ImmediatePixelOutput PSImmediateDualDraw(ImmediateOutput input) {return packImmediate(PSImmediateDual(input),input.position);}
ImmediatePixelOutput PSImmediateProjectedDraw(ProjectedImmediateOutput input) {return packImmediate(PSImmediateProjected(input),input.position);}
ImmediatePixelOutput PSImmediateProjectedDualDraw(ProjectedImmediateOutput input) {return packImmediate(PSImmediateProjectedDual(input),input.position);}
ImmediatePixelOutput PSRadialDraw(ImmediateOutput input) {
    precise float4 source=(immediateAlphaTest&2u)?PSRadialQuery(input):PSRadial(input);
    return packImmediate(source,input.position);
}
