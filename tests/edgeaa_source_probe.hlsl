// Test-only inspection of native texture conversion, separate from edgeAA.
Texture2D<float4> baseSource : register(t3);
SamplerState baseSampler : register(s3);
float4 PSEdgeAASourceProbe(float4 position : SV_POSITION,float2 uv : TEXCOORD0) : SV_TARGET {
    return baseSource.Sample(baseSampler,uv);
}
