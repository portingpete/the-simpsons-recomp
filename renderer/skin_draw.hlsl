#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_shader.hlsl"
#include "shadow_mesh.hlsl"
#include "skin_draw_input.hlsl"

SkinDrawVarying VSSkinDraw(SkinInput input) {
    SkinOutput original=VSSkin(input);
    SkinDrawVarying result;
    result.position=original.position; result.t0=original.t0; result.t1=original.t1;
    result.t2=original.t2; result.t3=original.t3; result.rimControl=original.t3.x;
    return result;
}

struct SkinDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
SkinDrawOutput PSSkinDraw(SkinDrawVarying input) {
    SkinDrawOutput result;
    result.color=PSSkin(skinDrawInput(input));
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
