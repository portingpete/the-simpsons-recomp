// Test-only pass-through observer; no arithmetic or expected values here.
#include "rigid_normalmap_vs.hlsl"
[maxvertexcount(1)]
void GSRigidNormalmapProbe(point NormalmapVSOutput input[1], inout PointStream<NormalmapVSOutput> output) {
    output.Append(input[0]);
}
