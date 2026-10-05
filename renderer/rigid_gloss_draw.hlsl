#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "rigid_gloss_shader.hlsl"
#include "shadow_mesh.hlsl"

struct RigidGlossDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
RigidGlossDrawOutput PSRigidGlossDraw(RigidGlossOutput input) {
    RigidGlossDrawOutput result;
    result.color=PSRigidGloss(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
