#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "rigid_normalmap_shader.hlsl"
#include "shadow_mesh.hlsl"

struct NormalmapDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
NormalmapDrawOutput PSRigidNormalmapDraw(NormalmapOutput input) {
    NormalmapDrawOutput result;
    result.color=PSRigidNormalmap(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
