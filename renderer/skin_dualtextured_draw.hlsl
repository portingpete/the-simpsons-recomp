#define RIGID_NATIVE_D24FS8_DEPTH_RRRR
#include "skin_dualtextured_shader.hlsl"
#include "shadow_mesh.hlsl"
#include "skin_rim_control.hlsl"

// The original export plus its rim flag (color.x) as a noperspective copy.
// PSSkinDual reads color.x only through that floor (slot36); the shadow
// block later reuses r5.x/y for samples.
struct SkinDualDrawVarying {
    float4 position:SV_Position; float2 uv:TEXCOORD0; float2 uv1:TEXCOORD1;
    float3 normal:TEXCOORD2; float4 world:TEXCOORD3; float4 shadow:TEXCOORD4;
    float4 color:TEXCOORD5; noperspective float rimControl:TEXCOORD6;
};
SkinDualDrawVarying VSSkinDualDraw(SkinDualInput input) {
    SkinDualOutput original=VSSkinDual(input);
    SkinDualDrawVarying result;
    result.position=original.position; result.uv=original.uv; result.uv1=original.uv1;
    result.normal=original.normal; result.world=original.world; result.shadow=original.shadow;
    result.color=original.color; result.rimControl=original.color.x;
    return result;
}

struct SkinDualDrawOutput { float4 color:SV_Target0; float depth:SV_Depth; };
SkinDualDrawOutput PSSkinDualDraw(SkinDualDrawVarying input) {
    SkinDualOutput original;
    original.position=input.position; original.uv=input.uv; original.uv1=input.uv1;
    original.normal=input.normal; original.world=input.world; original.shadow=input.shadow;
    original.color=float4(skinRimControl(input.rimControl),input.color.yzw);
    SkinDualDrawOutput result;
    result.color=PSSkinDual(original);
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
