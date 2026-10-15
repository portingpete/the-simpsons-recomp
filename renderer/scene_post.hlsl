// Native world-image passes applied after the original post effects and
// before the HUD: colour looks. Scene alpha carries packed flags and is kept.
cbuffer ScenePostConstants : register(b0) {
    // x: exposure gain, y: saturation scale, z: vibrance, w: contrast scale.
    float4 look;
};
Texture2D<float4> sceneColor : register(t0);
struct Varying {float4 position : SV_POSITION;float2 uv : TEXCOORD0;};
Varying VSScenePost(uint id : SV_VertexID) {
    Varying result;
    result.uv=float2((id<<1)&2,id&2);
    result.position=float4(result.uv*float2(2,-2)+float2(-1,1),0,1);
    return result;
}
float4 PSWorldLook(Varying input) : SV_TARGET {
    const float4 color=sceneColor.Load(int3(int2(input.position.xy),0));
    float3 rgb=color.rgb*look.x;
    const float luma=dot(rgb,float3(0.2126,0.7152,0.0722));
    rgb=lerp(luma.xxx,rgb,look.y);
    // Vibrance raises muted colours more than ones that are already vivid.
    const float chroma=max(rgb.r,max(rgb.g,rgb.b))-min(rgb.r,min(rgb.g,rgb.b));
    rgb=lerp(luma.xxx,rgb,1+look.z*(1-saturate(chroma)));
    rgb=(rgb-0.5)*look.w+0.5;
    return float4(saturate(rgb),color.a);
}
