#include "particle_dual_projected_shader.hlsl"
#include "shadow_mesh.hlsl"
// Quantized RGB10A2 codes roundtrip through typed UNORM without changing codes.
// D3D11 functional spec 3.2.3.5/.6: the conservative error is <.001 code,
// leaving every adjacent integer >.999 away from the permitted .6-code error.
// https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
uint4 particleCodesFromUNorm(float4 value) {
    precise float4 scaled=value*float4(1023,1023,1023,3);
    return uint4(round(scaled));
}
float4 particleCodesToUNorm(uint4 codes) {
    precise float4 value=float4(codes)/float4(1023,1023,1023,3);
    return value;
}
#if defined(PARTICLE_UNORM_PROBE)
struct ParticleUNormProbeOutput { float4 position:SV_Position; };
ParticleOutput VSParticleUNormProbe(uint id:SV_VertexID) {
    float2 xy=float2((id<<1)&2,id&2);
    // Match the real snapshot PS input signature, including unused varyings.
    ParticleOutput output=(ParticleOutput)0;
    output.position=float4(xy*float2(2,-2)+float2(-1,1),0,1);
    return output;
}
Texture2D<uint4> particleProbeCodes:register(t1);
float4 PSParticleUNormRoundTripDraw(ParticleUNormProbeOutput input):SV_Target0 {
    return particleCodesToUNorm(particleProbeCodes.Load(int3(int2(input.position.xy),0)));
}
#elif defined(PARTICLE_SNAPSHOT_UNORM)
Texture2D<float4> particleSnapshotTarget:register(t1);
// Exact current-target codes over the original quad coverage, without depth,
// alpha rejection, blending, or color-mask rejection.
uint4 PSParticleSnapshotDraw(ParticleOutput input):SV_Target0 {
    return particleCodesFromUNorm(particleSnapshotTarget.Load(int3(int2(input.position.xy),0)));
}
#else
cbuffer ParticleDrawState:register(b2) { uint particleBlend; float particleAlphaReference; uint particleBlendEnable; uint particlePadding; };
Texture2D<uint4> particleDestination:register(t1);
struct ParticlePixelOutput { uint4 color:SV_Target0; float depth:SV_Depth; };
ParticlePixelOutput particlePacked(ParticleOutput input,precise float4 source) {
    if(!(source.a>particleAlphaReference))discard;
    uint4 codes=particleDestination.Load(int3(int2(input.position.xy),0));
    precise float4 destination=float4(codes)/float4(1023,1023,1023,3);
    precise float3 product=source.rgb*source.a;
    precise float3 rgb;
    if(!particleBlendEnable)rgb=source.rgb;
    else if(particleBlend==0x10106u)rgb=product+destination.rgb;
    else if(particleBlend==0x10186u)rgb=destination.rgb-product;
    else if(particleBlend==0x10706u) {
        precise float inverseAlpha=1.0-source.a;
        precise float3 retained=destination.rgb*inverseAlpha;
        rgb=product+retained;
    } else {
        rgb=float3(1.0,0.0,1.0);
    }
    ParticlePixelOutput result;
    result.color=uint4(round(saturate(float4(rgb,source.a))*float4(1023,1023,1023,3)));
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
ParticlePixelOutput PSParticleDraw(ParticleOutput input) { return particlePacked(input,PSParticle(input)); }
ParticlePixelOutput PSParticleProjectedDraw(ParticleOutput input) { return particlePacked(input,PSParticleProjected(input)); }
ParticlePixelOutput PSParticleDualDraw(ParticleOutput input) { return particlePacked(input,PSParticleDual(input)); }
ParticlePixelOutput PSParticleDualProjectedDraw(ParticleOutput input) { return particlePacked(input,PSParticleDualProjected(input)); }
float4 PSParticleUNormDraw(ParticleOutput input,out float depth:SV_Depth):SV_Target0 {
    ParticlePixelOutput packed=particlePacked(input,PSParticle(input));
    depth=packed.depth;
    return particleCodesToUNorm(packed.color);
}
float4 PSParticleProjectedUNormDraw(ParticleOutput input,out float depth:SV_Depth):SV_Target0 {
    ParticlePixelOutput packed=particlePacked(input,PSParticleProjected(input));
    depth=packed.depth;
    return particleCodesToUNorm(packed.color);
}
float4 PSParticleDualUNormDraw(ParticleOutput input,out float depth:SV_Depth):SV_Target0 {
    ParticlePixelOutput packed=particlePacked(input,PSParticleDual(input));
    depth=packed.depth;
    return particleCodesToUNorm(packed.color);
}
float4 PSParticleDualProjectedUNormDraw(ParticleOutput input,out float depth:SV_Depth):SV_Target0 {
    ParticlePixelOutput packed=particlePacked(input,PSParticleDualProjected(input));
    depth=packed.depth;
    return particleCodesToUNorm(packed.color);
}
#endif
