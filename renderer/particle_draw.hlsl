#include "particle_dual_projected_shader.hlsl"
#include "shadow_mesh.hlsl"
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
