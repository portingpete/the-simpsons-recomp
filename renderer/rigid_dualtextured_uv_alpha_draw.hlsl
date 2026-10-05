#include "rigid_dualtextured_uv_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RigidUVAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
RigidUVAlphaDrawOutput PSRigidDualTexturedUVAlphaDraw(RigidUVAlphaOutput input) {
    RigidUVAlphaDrawOutput result; result.color=PSRigidDualTexturedUVAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
