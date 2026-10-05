// Offline test only. Consumes the SRVs installed by bindRigidShadowDepth;
// no sampler, constants, depth adapter, or rigid material code participates.
Texture2D<float> recordingDepth0 : register(t0);
Texture2D<float> recordingDepth1 : register(t1);

float4 PSRecordingDepthBinding(float4 position : SV_Position) : SV_Target0 {
    int3 texel = int3(int2(position.xy), 0);
    return float4(recordingDepth0.Load(texel), recordingDepth1.Load(texel), 0.5, 1.0);
}
