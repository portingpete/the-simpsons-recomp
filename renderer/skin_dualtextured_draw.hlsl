#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_dualtextured_shader.hlsl"
#include "shadow_mesh.hlsl"

struct SkinDualDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
SkinDualDrawOutput PSSkinDualDraw(SkinDualOutput input) {
    SkinDualDrawOutput result;
    result.color=PSSkinDual(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
