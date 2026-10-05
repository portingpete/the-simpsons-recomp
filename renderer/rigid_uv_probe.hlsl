// Test transport only; the original instruction transcription stays unchanged.
#include "rigid_uv_shader.hlsl"
cbuffer RigidSingleUVProbeConstants:register(b2) { float4 singleUvProbe[5]; };
RigidSingleUVOutput VSRigidUVPixelProbe(uint id:SV_VertexID) {
    RigidSingleUVOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=singleUvProbe[0].xy; o.t1=singleUvProbe[1]; o.t2=singleUvProbe[2];
    o.t3=singleUvProbe[3].xyz; o.t4=singleUvProbe[4]; return o;
}
RigidSingleUVAlphaOutput VSRigidUVAlphaPixelProbe(uint id:SV_VertexID) {
    RigidSingleUVAlphaOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=singleUvProbe[0].xy; o.t1=singleUvProbe[1];
    o.t2=singleUvProbe[2].xyz; o.t3=singleUvProbe[3]; return o;
}
[maxvertexcount(1)]
void GSRigidUVProbe(point RigidSingleUVOutput input[1],inout PointStream<RigidSingleUVOutput> output) {
    output.Append(input[0]);
}
[maxvertexcount(1)]
void GSRigidUVAlphaProbe(point RigidSingleUVAlphaOutput input[1],inout PointStream<RigidSingleUVAlphaOutput> output) {
    output.Append(input[0]);
}
