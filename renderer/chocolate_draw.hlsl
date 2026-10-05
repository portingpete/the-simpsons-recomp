#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "chocolate_shader.hlsl"
#include "shadow_mesh.hlsl"

struct ChocolateDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
ChocolateDrawOutput PSChocolateDraw(ChocOutput input) {
    ChocolateDrawOutput result;
    result.color=PSChocolate(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
