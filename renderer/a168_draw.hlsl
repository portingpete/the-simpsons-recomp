#include "a168_shader.hlsl"
#include "shadow_mesh.hlsl"

struct A168DrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
A168DrawOutput PS168F8Draw(A168Output input) {
    A168DrawOutput result;
    result.color=PS168F8(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
