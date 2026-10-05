// Test transport only; arithmetic stays in the original instruction transcription.
#include "flipbook_shader.hlsl"
cbuffer FlipbookProbeConstants:register(b2) { float4 flipbookProbe[4]; };
FlipbookOutput VSFlipbookPixelProbe(uint id:SV_VertexID) {
    FlipbookOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=flipbookProbe[0].xy; o.t1=flipbookProbe[1].xyz;
    o.t2=flipbookProbe[2]; return o;
}
FlipbookAlphaOutput VSFlipbookAlphaPixelProbe(uint id:SV_VertexID) {
    FlipbookAlphaOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=flipbookProbe[0].xy; o.t1=flipbookProbe[1];
    o.t2=flipbookProbe[2].xyz; o.t3=flipbookProbe[3]; return o;
}
[maxvertexcount(1)]
void GSFlipbookProbe(point FlipbookOutput input[1],inout PointStream<FlipbookOutput> output) {
    output.Append(input[0]);
}
[maxvertexcount(1)]
void GSFlipbookAlphaProbe(point FlipbookAlphaOutput input[1],inout PointStream<FlipbookAlphaOutput> output) {
    output.Append(input[0]);
}
