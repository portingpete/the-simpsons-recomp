// Test-only transport; the production shader arithmetic is included unchanged.
#include "rigid_alpha_shader.hlsl"
cbuffer RigidAlphaProbeConstants : register(b1) { float4 probeRows[4]; };
RigidAlphaOutput VSRigidAlphaPixelProbe(uint id:SV_VertexID) {
    RigidAlphaOutput result;
    result.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);
    result.t0=probeRows[0]; result.t1=probeRows[1];
    result.t2=probeRows[2]; result.t3=probeRows[3];
    return result;
}
[maxvertexcount(1)]
void GSRigidAlphaProbe(point RigidAlphaOutput input[1],inout PointStream<RigidAlphaOutput> output) {
    output.Append(input[0]);
}
