// Test-only transport and stream-output observers.
#include "rigid_family_alpha_shader.hlsl"
[maxvertexcount(1)] void GSRigidFamilyAlphaProbe(point RigidFamilyAlphaOutput input[1],
    inout PointStream<RigidFamilyAlphaOutput> stream) { stream.Append(input[0]); }
cbuffer RigidFamilyAlphaProbeInputs : register(b2) { float4 probeInputs[4]; };
RigidFamilyAlphaOutput VSRigidFamilyAlphaPixelProbe(uint id:SV_VertexID) {
    RigidFamilyAlphaOutput result;
    result.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);
    result.t0=probeInputs[0]; result.t1=probeInputs[1]; result.t2=probeInputs[2]; result.t3=probeInputs[3];
    return result;
}
