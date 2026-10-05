#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_textured_shader.hlsl"
#include "shadow_mesh.hlsl"
struct SkinTexturedDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
SkinTexturedDrawOutput PSSkinTexturedDraw(SkinTexturedOutput input) {
    SkinTexturedDrawOutput o; o.color=PSSkinTextured(input); o.depth=PSShadowMeshDepth(input.position); return o;
}
SkinTexturedDrawOutput PSSkinTexturedAlphaDraw(SkinTexturedAlphaOutput input) {
    SkinTexturedDrawOutput o; o.color=PSSkinTexturedAlpha(input); o.depth=PSShadowMeshDepth(input.position); return o;
}
