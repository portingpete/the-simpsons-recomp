#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_alpha_shader.hlsl"
#include "shadow_mesh.hlsl"
#include "skin_draw_input.hlsl"

struct SkinAlphaDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
SkinAlphaDrawOutput PSSkinAlphaDraw(SkinDrawVarying input) {
    SkinAlphaDrawOutput result;
    result.color=PSSkinAlpha(skinDrawInput(input));
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
