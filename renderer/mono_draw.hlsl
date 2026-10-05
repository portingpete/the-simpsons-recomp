// Original mono white export plus the audited fixed-function depth adapter.
#include "mono_shader.hlsl"
#include "shadow_mesh.hlsl"
struct MonoDrawOutput { float4 color:SV_Target; float depth:SV_Depth; };
MonoDrawOutput PSMonoDraw(MonoOutput input) {
    MonoDrawOutput output;
    output.color=PSMono(input);
    output.depth=PSShadowMeshDepth(input.position);
    return output;
}
