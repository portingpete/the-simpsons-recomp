#include "temporal_motion_common.hlsl"
#include "shadow_mesh.hlsl"
struct TemporalMotionOutput {
    float2 motion : SV_Target0;
    float biasCurrent : SV_Target1;
    float depth : SV_Depth;
};
TemporalMotionOutput PSTemporalMotion(TemporalVarying input) {
    TemporalMotionOutput result;
    // Previous clip can cross the camera plane during disocclusion. Reject its
    // temporal history at that pixel, instead of publishing infinities.
    const bool valid=historyValid!=0&&input.currentClip.w>0.0&&input.previousClip.w>0.0;
    if(valid) {
        precise float2 current=input.currentClip.xy/input.currentClip.w;
        precise float2 previous=input.previousClip.xy/input.previousClip.w;
        result.motion=(previous-current)*float2(0.5,-0.5)*renderSize;
    } else result.motion=0.0;
    result.biasCurrent=valid&&conservativeCurrent==0?0.0:1.0;
    result.depth=PSShadowMeshDepth(input.position);
    return result;
}
