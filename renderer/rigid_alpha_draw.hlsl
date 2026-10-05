#include "rigid_alpha_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RigidAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
RigidAlphaDrawOutput PSRigidAlphaDraw(RigidAlphaOutput input) {
    RigidAlphaDrawOutput result;
    result.color=PSRigidAlpha(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
