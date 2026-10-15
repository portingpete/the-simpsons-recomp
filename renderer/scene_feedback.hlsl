// Finite background transport, not a discrete inverse of reconstruction.
Texture2D<float4> sourceColor : register(t0);
Texture2D<float> sourceDepth : register(t1);
Texture2D<uint2> sourceStencil : register(t2);
SamplerState linearClamp : register(s0);
cbuffer SceneFeedback : register(b0) {
    uint2 sourceExtent;
    uint2 destinationExtent;
    float2 destinationJitter; // Actual next render frame, in Scene pixels.
    float2 transferPadding;
};
cbuffer StencilBit : register(b1) {uint stencilBit;uint3 bitPadding;};
float4 VSSceneFeedback(uint vertex : SV_VertexID) : SV_Position {
    float2 clipCoordinate=float2((vertex<<1)&2,vertex&2);
    return float4(clipCoordinate*float2(2,-2)+float2(-1,1),0,1);
}
uint2 feedbackSourcePixel(float4 position) {
    if(all(destinationJitter==0.0f)) {
        const uint2 pixel=uint2(position.xy);
        return min(((2u*pixel+1u)*sourceExtent)/(2u*destinationExtent),sourceExtent-1u);
    }
    // Each precise binary32 step is part of the declared transport contract.
    precise float2 translated=position.xy-destinationJitter;
    precise float2 scaled=translated*float2(sourceExtent);
    precise float2 sourcePosition=scaled/float2(destinationExtent);
    return uint2(clamp(int2(floor(sourcePosition)),int2(0,0),int2(sourceExtent)-1));
}
float4 PSSceneFeedbackColor(float4 position : SV_Position) : SV_Target {
    precise float2 translated=position.xy-destinationJitter;
    precise float2 uv=translated/float2(destinationExtent);
    float3 rgb=sourceColor.SampleLevel(linearClamp,uv,0).rgb;
    // The original two-bit alpha shares exactly the D/S source texel.
    return float4(rgb,sourceColor.Load(int3(feedbackSourcePixel(position),0)).a);
}
struct FeedbackValue {float depth : SV_Depth;uint stencil : SV_StencilRef;};
FeedbackValue PSSceneFeedbackDepth(float4 position : SV_Position) {
    const uint2 pixel=feedbackSourcePixel(position);
    FeedbackValue value;
    value.depth=sourceDepth.Load(int3(pixel,0));
    value.stencil=sourceStencil.Load(int3(pixel,0)).y;
    return value;
}
float PSSceneFeedbackDepthOnly(float4 position : SV_Position) : SV_Depth {
    return sourceDepth.Load(int3(feedbackSourcePixel(position),0));
}
void PSSceneFeedbackStencilBit(float4 position : SV_Position) {
    if((sourceStencil.Load(int3(feedbackSourcePixel(position),0)).y&stencilBit)==0u)discard;
}
