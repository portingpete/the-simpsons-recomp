// Test transport only: the original production shader body stays unchanged.
#include "rigid_dualtextured_uv_shader.hlsl"
cbuffer UVProbeConstants:register(b2) { float4 uvProbe[5]; };
RigidUVOutput VSRigidDualTexturedUVPixelProbe(uint id:SV_VertexID) {
    RigidUVOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=uvProbe[0]; o.t1=uvProbe[1].xy; o.t2=uvProbe[2];
    o.t3=uvProbe[3].xyz; o.t4=uvProbe[4]; return o;
}
RigidUVAlphaOutput VSRigidDualTexturedUVAlphaPixelProbe(uint id:SV_VertexID) {
    RigidUVAlphaOutput o; o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=uvProbe[0]; o.t1=uvProbe[1]; return o;
}
[maxvertexcount(1)]
void GSRigidDualTexturedUVProbe(point RigidUVOutput input[1],inout PointStream<RigidUVOutput> output) {
    output.Append(input[0]);
}
[maxvertexcount(1)]
void GSRigidDualTexturedUVAlphaProbe(point RigidUVAlphaOutput input[1],inout PointStream<RigidUVAlphaOutput> output) {
    output.Append(input[0]);
}
