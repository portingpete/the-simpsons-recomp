// Only the two original opaque programs consume D24 world/character depth.
#define REMAINING_NATIVE_DEPTH_RRRR
#include "rigid_remaining_shader.hlsl"
#include "shadow_mesh.hlsl"
#include "palette_coordinate.hlsl"
struct RemainingDrawOutput {float4 color:SV_Target0;float depth:SV_Depth;};
RemainingDrawOutput PSChocolateOpaqueDraw(ChocolateOpaqueOutput input) {
    // uv1 is the palette coordinate, interpolated as t4.w (u) and t5.w (v) and
    // read only through frac(); snap it before the original PS runs.
    const float2 palette=rigidPaletteCoordinate(float2(input.t4.w,input.t5.w));
    input.t4.w=palette.x;input.t5.w=palette.y;
    RemainingDrawOutput o;o.color=PSChocolateOpaque(input);o.depth=PSShadowMeshDepth(input.position);return o;
}
RemainingDrawOutput PSProjtexDraw(ProjtexOutput input) {
    RemainingDrawOutput o;o.color=PSProjtex(input);o.depth=PSShadowMeshDepth(input.position);return o;
}
