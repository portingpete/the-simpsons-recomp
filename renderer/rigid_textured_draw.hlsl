#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "rigid_textured_shader.hlsl"
#include "shadow_mesh.hlsl"

struct RigidTexturedDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
RigidTexturedDrawOutput PSRigidTexturedDraw(RigidOutput input) {
    RigidTexturedDrawOutput result;
    result.color=PSRigidTextured(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
