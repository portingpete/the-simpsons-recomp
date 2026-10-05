#include "skin_dualtextured_uv_shader.hlsl"
#include "shadow_mesh.hlsl"
struct SkinDualUVAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
SkinDualUVAlphaDrawOutput PSSkinDualUVAlphaDraw(SkinDualUVAlphaOutput input) {
    SkinDualUVAlphaDrawOutput result; result.color=PSSkinDualUVAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
