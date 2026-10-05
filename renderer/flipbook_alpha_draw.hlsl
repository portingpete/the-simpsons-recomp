#include "flipbook_shader.hlsl"
#include "shadow_mesh.hlsl"
struct FlipbookAlphaDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
FlipbookAlphaDrawOutput PSFlipbookAlphaDraw(FlipbookAlphaOutput input) {
    FlipbookAlphaDrawOutput result; result.color=PSFlipbookAlpha(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
