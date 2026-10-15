// Use the existing UI fixture fullscreen VS; derive gold from actual source pixels.
cbuffer StencilBit : register(b1) {uint stencilBit;uint3 bitPadding;};

float PSUiCompositeFixtureDepthOnly(float4 position : SV_Position) : SV_Depth {
    const uint2 pixel=uint2(position.xy);
    return float(((pixel.x+3u*pixel.y)&15u)+1u)/32.0;
}

void PSUiCompositeFixtureStencilBit(float4 position : SV_Position) {
    const uint2 pixel=uint2(position.xy);
    const uint stencil=(pixel.x*17u+pixel.y*29u+0x55u)&255u;
    if((stencil&stencilBit)==0u)discard;
}
