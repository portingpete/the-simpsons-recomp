// Only the two original opaque programs consume D24 world/character depth.
#define REMAINING_NATIVE_DEPTH_RRRR
#include "rigid_remaining_shader.hlsl"
#include "shadow_mesh.hlsl"
struct RemainingDrawOutput {float4 color:SV_Target0;float depth:SV_Depth;};
RemainingDrawOutput PSChocolateOpaqueDraw(ChocolateOpaqueOutput input) {
    RemainingDrawOutput o;o.color=PSChocolateOpaque(input);o.depth=PSShadowMeshDepth(input.position);return o;
}
RemainingDrawOutput PSProjtexDraw(ProjtexOutput input) {
    RemainingDrawOutput o;o.color=PSProjtex(input);o.depth=PSShadowMeshDepth(input.position);return o;
}
