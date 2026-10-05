#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "rigid_multitone_shader.hlsl"
#include "shadow_mesh.hlsl"

struct MultitoneDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
MultitoneDrawOutput PSRigidMultitoneDraw(MultitoneOutput input) {
    MultitoneDrawOutput result;
    result.color=PSRigidMultitone(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
