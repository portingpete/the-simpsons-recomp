// Genuine SMAA 1x HIGH, canonical iryoku/SMAA algorithm and lookup textures.
// See vendor/smaa/PROVENANCE.json and LICENSE.txt. No temporal jitter/history.
cbuffer SMAAMetrics : register(b0) { float4 smaaMetrics; };
SamplerState smaaLinear : register(s0);
SamplerState smaaPoint : register(s1);
Texture2D smaaInput : register(t0);
Texture2D smaaAreaOrWeights : register(t1);
Texture2D smaaSearch : register(t2);
#define SMAA_RT_METRICS smaaMetrics
#define SMAA_PRESET_HIGH
// The upstream HLSL4 sampling definitions, with explicit native register slots.
#define SMAA_CUSTOM_SL
#define SMAATexture2D(tex) Texture2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(smaaLinear, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(smaaPoint, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(smaaLinear, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(smaaLinear, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(smaaPoint, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(smaaLinear, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#include "vendor/smaa/SMAA.hlsl"
struct EdgeVarying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 offset[3] : TEXCOORD1; };
struct WeightVarying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float2 pixel : TEXCOORD1; float4 offset[3] : TEXCOORD2; };
struct NeighborhoodVarying { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float4 offset : TEXCOORD1; };
void fullscreen(uint id,out float4 position,out float2 uv) {
    uv=float2((id<<1)&2,id&2);position=float4(uv*float2(2,-2)+float2(-1,1),0,1);
}
#if SMAA_INCLUDE_VS
EdgeVarying VSSMAAEdge(uint id : SV_VertexID) {
    EdgeVarying output;fullscreen(id,output.position,output.uv);SMAAEdgeDetectionVS(output.uv,output.offset);return output;
}
WeightVarying VSSMAAWeight(uint id : SV_VertexID) {
    WeightVarying output;fullscreen(id,output.position,output.uv);
    SMAABlendingWeightCalculationVS(output.uv,output.pixel,output.offset);return output;
}
NeighborhoodVarying VSSMAANeighborhood(uint id : SV_VertexID) {
    NeighborhoodVarying output;fullscreen(id,output.position,output.uv);SMAANeighborhoodBlendingVS(output.uv,output.offset);return output;
}
#endif
#if SMAA_INCLUDE_PS
float2 PSSMAAEdge(EdgeVarying input) : SV_TARGET {
    return SMAAColorEdgeDetectionPS(input.uv,input.offset,smaaInput);
}
float4 PSSMAAWeight(WeightVarying input) : SV_TARGET {
    return SMAABlendingWeightCalculationPS(input.uv,input.pixel,input.offset,smaaInput,smaaAreaOrWeights,smaaSearch,float4(0,0,0,0));
}
float4 PSSMAANeighborhood(NeighborhoodVarying input) : SV_TARGET {
    return SMAANeighborhoodBlendingPS(input.uv,input.offset,smaaInput,smaaAreaOrWeights);
}
#endif
