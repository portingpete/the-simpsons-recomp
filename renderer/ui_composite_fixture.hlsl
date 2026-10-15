// A source-space pattern with exact depth fractions and coordinate stencil codes.
float4 VSUiCompositeFixture(uint vertex : SV_VertexID) : SV_Position {
    const float2 clipCorners[3]={float2(-1,1),float2(3,1),float2(-1,-3)};
    return float4(clipCorners[vertex],0,1);
}

struct UiCompositeFixtureValue {float depth : SV_Depth;uint stencil : SV_StencilRef;};
UiCompositeFixtureValue PSUiCompositeFixture(float4 position : SV_Position) {
    const uint2 pixel=uint2(position.xy);
    UiCompositeFixtureValue value;
    value.depth=float(((pixel.x+3u*pixel.y)&15u)+1u)/32.0;
    value.stencil=(pixel.x*17u+pixel.y*29u+0x55u)&255u;
    return value;
}
