// Only the qualified native D24FS8-equivalent depth views use RRRR expansion.
// PSRigid compiled directly from rigid_shader.hlsl retains arbitrary RGBA.
#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "rigid_shader.hlsl"
#include "shadow_mesh.hlsl"

struct RigidDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
RigidDrawOutput PSRigidDraw(RigidOutput input) {
    RigidDrawOutput result;
    result.color=PSRigid(input);
    // Reuse the audited mapping, both original bias scales and ONE 20e4 RNE
    // conversion. Neither the original VS clip export nor PS color is replaced.
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
