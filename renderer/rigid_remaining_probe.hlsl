#include "rigid_remaining_shader.hlsl"
cbuffer RemainingProbe:register(b2) {float4 remainingInputs[7];};
ChocolateOpaqueOutput VSChocolateOpaquePixelProbe(uint id:SV_VertexID) {
    ChocolateOpaqueOutput o;o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=remainingInputs[0];o.t1=remainingInputs[1];o.t2=remainingInputs[2];o.t3=remainingInputs[3].xyz;
    o.t4=remainingInputs[4];o.t5=remainingInputs[5];o.t6=remainingInputs[6];return o;
}
ProjtexOutput VSProjtexPixelProbe(uint id:SV_VertexID) {
    ProjtexOutput o;o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=remainingInputs[0].xy;o.t1=remainingInputs[1];o.t2=remainingInputs[2];o.t3=remainingInputs[3].xyz;
    o.t4=remainingInputs[4].xy;o.t5=remainingInputs[5];return o;
}
ProjtexAlphaOutput VSProjtexAlphaPixelProbe(uint id:SV_VertexID) {
    ProjtexAlphaOutput o;o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=remainingInputs[0].xy;o.t1=remainingInputs[1];o.t2=remainingInputs[2].xyz;o.t3=remainingInputs[3];return o;
}
[maxvertexcount(1)]
void GSChocolateOpaqueProbe(point ChocolateOpaqueOutput input[1],inout PointStream<ChocolateOpaqueOutput> output) {output.Append(input[0]);}
[maxvertexcount(1)]
void GSProjtexProbe(point ProjtexOutput input[1],inout PointStream<ProjtexOutput> output) {output.Append(input[0]);}
[maxvertexcount(1)]
void GSProjtexAlphaProbe(point ProjtexAlphaOutput input[1],inout PointStream<ProjtexAlphaOutput> output) {output.Append(input[0]);}
