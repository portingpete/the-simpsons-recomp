#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_alpha_shader.hlsl"
#include "shadow_mesh.hlsl"

struct SkinAlphaDrawOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};
SkinAlphaDrawOutput PSSkinAlphaDraw(SkinOutput input) {
    SkinAlphaDrawOutput result;
    result.color=PSSkinAlpha(input);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
