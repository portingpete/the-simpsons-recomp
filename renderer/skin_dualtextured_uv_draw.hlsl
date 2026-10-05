#include "skin_dualtextured_uv_shader.hlsl"
#include "shadow_mesh.hlsl"
struct SkinDualUVDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
SkinDualUVDrawOutput PSSkinDualUVDraw(SkinDualUVOutput input) {
    SkinDualUVDrawOutput result; result.color=PSSkinDualUV(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
