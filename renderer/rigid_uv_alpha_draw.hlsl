#include "rigid_uv_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RigidSingleUVAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
RigidSingleUVAlphaDrawOutput PSRigidUVAlphaDraw(RigidSingleUVAlphaOutput input) {
    RigidSingleUVAlphaDrawOutput result; result.color=PSRigidUVAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
