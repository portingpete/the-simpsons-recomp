#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_shader.hlsl"
#include "shadow_mesh.hlsl"

struct SkinDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
SkinDrawOutput PSSkinDraw(SkinOutput input) {
    SkinDrawOutput result;
    result.color=PSSkin(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
