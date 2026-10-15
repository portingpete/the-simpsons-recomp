Texture2D<float4> reconstructed : register(t0);
Texture2D<float4> originalWorld : register(t1);
// Captured original world frame, in render pixels (+X right,+Y down).
cbuffer TemporalOutputJitter : register(b1) {float2 alphaSourceJitter;float2 alphaJitterPadding;};
struct OutputVertex {float4 position:SV_Position;};
OutputVertex VSTemporalOutput(uint id:SV_VertexID) {
    OutputVertex output;float2 uv=float2((id<<1)&2,id&2);
    output.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return output;
}
float4 PSTemporalOutput(OutputVertex input):SV_Target0 {
    float4 result=reconstructed.Load(int3(int2(input.position.xy),0));
    // Packed engine alpha is a separate retained post-processing channel.
    // Do not assume a particular NGX preset reconstructs alpha. Preserve the
    // actual two-bit source code with the same nearest output depth mapping.
    uint sourceWidth,sourceHeight,outputWidth,outputHeight;
    originalWorld.GetDimensions(sourceWidth,sourceHeight);reconstructed.GetDimensions(outputWidth,outputHeight);
    uint2 source;
    if(all(alphaSourceJitter==0.0f))
        source=((2*uint2(input.position.xy)+1)*uint2(sourceWidth,sourceHeight))/(2*uint2(outputWidth,outputHeight));
    else {
        precise float2 sourcePosition=input.position.xy*float2(sourceWidth,sourceHeight)/float2(outputWidth,outputHeight)+alphaSourceJitter;
        source=uint2(clamp(int2(floor(sourcePosition)),int2(0,0),int2(sourceWidth,sourceHeight)-1));
    }
    result.a=originalWorld.Load(int3(source,0)).a;return result;
}
// UI-only title/loading/pause backgrounds have no temporal world geometry.
// This native-output spatial copy does not call NGX or advance its history.
SamplerState uiBackgroundSampler : register(s0);
cbuffer UiBackgroundExtent : register(b0) {uint2 uiOutputExtent;uint2 uiExtentReserved;};
struct UiBackgroundVertex {float4 position:SV_Position;float2 uv:TEXCOORD0;};
UiBackgroundVertex VSTemporalUiBackground(uint id:SV_VertexID) {
    UiBackgroundVertex output;output.uv=float2((id<<1)&2,id&2);
    output.position=float4(output.uv*float2(2,-2)+float2(-1,1),0,1);return output;
}
float4 PSTemporalUiBackground(UiBackgroundVertex input):SV_Target0 {
    uint width,height;reconstructed.GetDimensions(width,height);
    float4 result;uint2 source;
    if(all(alphaSourceJitter==0.0f)) {
        // Retain the existing exact zero-grid sampling and integer nearest rule.
        result=reconstructed.SampleLevel(uiBackgroundSampler,input.uv,0);
        source=((2*uint2(input.position.xy)+1)*uint2(width,height))/(2*uiOutputExtent);
    } else {
        // Source jitter is measured in render pixels, not output pixels or UVs.
        // Decoded RGB is bilinear; alpha uses exactly the paired depth grid.
        result=reconstructed.SampleLevel(uiBackgroundSampler,input.uv+alphaSourceJitter/float2(width,height),0);
        precise float2 sourcePosition=input.position.xy*float2(width,height)/float2(uiOutputExtent)+alphaSourceJitter;
        source=uint2(clamp(int2(floor(sourcePosition)),int2(0,0),int2(width,height)-1));
    }
    result.a=reconstructed.Load(int3(source,0)).a;return result;
}
