#include "chocolate_shader.hlsl"

// Test-only stream-output observer. Reuse the production Chocolate output
// signature so D3D11 checks the actual VS-to-GS register linkage.
[maxvertexcount(1)]
void GSChocolateUVProbe(point ChocOutput input[1],
    inout PointStream<ChocOutput> stream) {
    stream.Append(input[0]);
}
