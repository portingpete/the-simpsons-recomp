// Independent source population and a real late-world depth-test draw.
// Reuse this VS for every fixture PS; no optional shader stencil reference.
cbuffer JitterFixture : register(b0) {
    uint2 sourceExtent;
    float2 sourceJitter;
};
cbuffer StencilBit : register(b1) {uint stencilBit;uint3 bitPadding;};

float4 VSUiCompositeJitterFixture(uint vertex : SV_VertexID) : SV_Position {
    const float2 clipCorners[3]={float2(-1,1),float2(3,1),float2(-1,-3)};
    return float4(clipCorners[vertex],0,1);
}

bool jitterSilhouette(float2 position) {
    const float2 world=position-sourceJitter;
    return all(world>=0.5)&&all(world<float2(sourceExtent)-0.5)&&
           world.x+world.y<float(sourceExtent.x+sourceExtent.y)*0.5;
}

float PSUiCompositeJitterFixtureDepthOnly(float4 position : SV_Position) : SV_Depth {
    if(!jitterSilhouette(position.xy))discard;
    const uint2 pixel=uint2(position.xy);
    return float(((pixel.x+3u*pixel.y)&15u)+1u)/32.0;
}

void PSUiCompositeJitterFixtureStencilBit(float4 position : SV_Position) {
    if(!jitterSilhouette(position.xy))discard;
    const uint2 pixel=uint2(position.xy);
    const uint stencil=(pixel.x*17u+pixel.y*29u+0x55u)&255u;
    if((stencil&stencilBit)==0u)discard;
}

struct JitterLateWorldValue {float4 color : SV_Target;float depth : SV_Depth;};
JitterLateWorldValue PSUiCompositeJitterLateWorld(float4 position : SV_Position) {
    const uint2 pixel=uint2(position.xy);
    JitterLateWorldValue value;
    value.color=float4(0,1,0,1);
    value.depth=(pixel.y%7u)==6u?0.75:0.25;
    return value;
}
