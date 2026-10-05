#include "flipbook_shader.hlsl"
#include "shadow_mesh.hlsl"
struct FlipbookDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
FlipbookDrawOutput PSFlipbookDraw(FlipbookOutput input) {
    FlipbookDrawOutput result; result.color=PSFlipbook(input);
    result.depth=PSShadowMeshDepth(input.position); return result;
}
