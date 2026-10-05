#include "chocolate_shader.hlsl"
cbuffer RemainingChocolateProbe:register(b2) {float4 remainingChocolateInputs[7];};
ChocOutput VSChocolateAlphaPixelProbe(uint id:SV_VertexID) {
    ChocOutput o;o.position=float4(id==1?3:-1,id==2?-3:1,.5,1);
    o.t0=remainingChocolateInputs[0];o.t1=remainingChocolateInputs[1];o.t2=remainingChocolateInputs[2].xyz;
    o.t3=remainingChocolateInputs[3];o.t4=remainingChocolateInputs[4];o.t5=remainingChocolateInputs[5];return o;
}
[maxvertexcount(1)]
void GSChocolateAlphaFullProbe(point ChocOutput input[1],inout PointStream<ChocOutput> output) {output.Append(input[0]);}
