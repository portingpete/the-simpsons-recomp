// Fixture shaders for tests/test_vulkan_device.cpp (Vulkan D3D11 device
// conformance). Compiled by FXC; the build also checks their SPIR-V twins.
cbuffer Fixture : register(b0) {
    float4 color;
    float depth;
    float pixelDepth;
    float2 padding;
};
struct Output {float4 position : SV_Position;float2 uv : TEXCOORD0;};

// Full-target triangle from SV_VertexID at the constant depth.
Output VSVkFixtureFullscreen(uint vertex : SV_VertexID) {
    Output o;
    const float2 corner=float2((vertex<<1)&2,vertex&2);
    o.position=float4(corner*float2(2,-2)+float2(-1,1),depth,1);
    o.uv=corner;
    return o;
}
// Indexed geometry: POSITION xy in clip space, constant depth.
Output VSVkFixturePosition(float2 position : POSITION,float2 uv : TEXCOORD0) {
    Output o;o.position=float4(position,depth,1);o.uv=uv;return o;
}
float4 PSVkFixtureColor(Output input) : SV_Target0 {return color;}
float4 PSVkFixtureUv(Output input) : SV_Target0 {return float4(input.uv,0,1);}
float PSVkFixtureDepth(Output input) : SV_Depth {return pixelDepth;}
// Reads SV_Position.z/w the way the shadow and Z-prepass adapters do.
float PSVkFixturePositionDepth(float4 position : SV_Position) : SV_Depth {return 1.0-position.z;}
Texture2D<float4> source : register(t0);
SamplerState sourceSampler : register(s0);
float4 PSVkFixtureSample(Output input) : SV_Target0 {return source.SampleLevel(sourceSampler,input.uv,0);}
// Regression: one push-descriptor set serves both stages, so a pixel-stage
// constant buffer (b1) must not disturb the vertex stage's b0 constants.
cbuffer Reverse : register(b1) {uint reverseDepth;uint3 reverseUnused;};
float PSVkFixtureReverse(float4 position : SV_Position) : SV_Depth {
    precise float z = reverseDepth != 0 ? 1.0 - position.z : position.z;
    return z;
}
