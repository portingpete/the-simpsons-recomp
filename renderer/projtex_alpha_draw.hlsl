// The alpha program's bank0 is an RGBA material texture.
#include "rigid_remaining_shader.hlsl"
#include "shadow_mesh.hlsl"
struct ProjtexAlphaDrawOutput {float4 color:SV_Target0;float depth:SV_Depth;};
ProjtexAlphaDrawOutput PSProjtexAlphaDraw(ProjtexAlphaOutput input) {
    ProjtexAlphaDrawOutput o;o.color=PSProjtexAlpha(input);o.depth=PSShadowMeshDepth(input.position);return o;
}
