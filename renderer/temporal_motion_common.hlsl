// Unjittered previous-current vectors in render pixels (+X right,+Y down).
cbuffer TemporalFrameConstants : register(b11) {
    float2 jitterPixels;
    float2 renderSize;
    uint historyValid;
    uint conservativeCurrent;
    uint2 temporalReserved;
};
struct TemporalVarying {
    float4 position : SV_Position;
    float4 currentClip : TEXCOORD14;
    float4 previousClip : TEXCOORD15;
};
