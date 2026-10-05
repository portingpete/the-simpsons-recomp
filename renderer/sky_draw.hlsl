#include "sky_shader.hlsl"
#include "shadow_mesh.hlsl"

struct SkyDrawOutput { float4 color : SV_Target0; float depth : SV_Depth; };
SkyDrawOutput PSSkyDraw(SkyOutput input) {
    SkyDrawOutput result;
    result.color=PSSky(input);
    // The original sky VS exports clip z==w. Apply the same viewport depth
    // mapping as the world: far is zero for its reversed 1..0 viewport.
    // Keep original color/geometry and the original depth/write/compare state.
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
SkyDrawOutput PSSkyOpaqueDraw(SkyOutput input) {
    SkyDrawOutput result;
    result.color=PSSkyOpaque(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
