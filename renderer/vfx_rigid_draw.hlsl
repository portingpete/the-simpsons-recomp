#include "vfx_rigid_shader.hlsl"
#include "shadow_mesh.hlsl"
struct VfxRigidDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
VfxRigidDrawOutput PSVfxRigidDraw(VfxRigidOutput input) {
    VfxRigidDrawOutput result;
    result.color=PSVfxRigid(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
