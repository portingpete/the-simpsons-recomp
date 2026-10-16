#include "skin_rim_control.hlsl"

// Keep the floor-based rim enable control out of perspective division
// (skin_rim_control.hlsl); the original smooth color, normals and positions
// retain perspective sampling.
struct SkinDrawVarying {
    float4 position : SV_Position;
    float2 t0 : TEXCOORD0;
    float3 t1 : TEXCOORD1;
    float4 t2 : TEXCOORD2;
    float4 t3 : TEXCOORD3;
    noperspective float rimControl : TEXCOORD4;
};
SkinOutput skinDrawInput(SkinDrawVarying input) {
    SkinOutput result;
    result.position=input.position; result.t0=input.t0; result.t1=input.t1;
    result.t2=input.t2; result.t3=input.t3;
    result.t3.x=skinRimControl(input.rimControl);
    return result;
}
