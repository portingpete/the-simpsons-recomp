#include "skin_dualalpha_shader.hlsl"
#include "shadow_mesh.hlsl"
struct SkinDualAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
SkinDualAlphaDrawOutput PSSkinDualAlphaDraw(SkinOutput input) {
    SkinDualAlphaDrawOutput result;
    result.color=PSSkinDualAlpha(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
