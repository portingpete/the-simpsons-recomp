// The opaque UV record has exactly one character depth sampler at t0.
#define RIGID_UV_CHARACTER_DEPTH_RRRR
#include "rigid_dualtextured_uv_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RigidUVDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
RigidUVDrawOutput PSRigidDualTexturedUVDraw(RigidUVOutput input) {
    RigidUVDrawOutput result; result.color=PSRigidDualTexturedUV(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
