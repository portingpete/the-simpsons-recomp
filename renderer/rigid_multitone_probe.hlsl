// Test-only pass-through observer; no arithmetic or expected values here.
#include "rigid_multitone_shader.hlsl"
[maxvertexcount(1)]
void GSRigidMultitoneProbe(point MultitoneOutput input[1], inout PointStream<MultitoneOutput> output) {
    output.Append(input[0]);
}
cbuffer MultitoneProbeInputs : register(b2) { float4 multitoneProbe[6]; };
MultitoneOutput VSRigidMultitonePixelProbe(uint id:SV_VertexID) {
    MultitoneOutput o;
    o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1);
    o.uv=multitoneProbe[0]; o.characterShadow=multitoneProbe[1];
    o.worldShadow=multitoneProbe[2]; o.normal=multitoneProbe[3].xyz;
    o.noiseUV=multitoneProbe[4].xy; o.color=multitoneProbe[5];
    return o;
}
