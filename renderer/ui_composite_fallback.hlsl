// Preserve paired single-sample scene values through integer nearest-center mapping.
Texture2D<float> sourceDepth : register(t0);
Texture2D<uint2> sourceStencil : register(t1);
cbuffer UiComposite : register(b0) {
    uint2 sourceExtent;
    uint2 outputExtent;
    float2 sourceJitter; // Captured current frame, in source render pixels.
    float2 transferPadding;
};
cbuffer StencilBit : register(b1) {uint stencilBit;uint3 bitPadding;};

uint2 uiCompositeSourcePixel(float4 position) {
    if(all(sourceJitter==0.0f)) {
        // Preserve byte-exact legacy/default integer nearest-center mapping.
        const uint2 outputPixel=uint2(position.xy);
        return min(((2u*outputPixel+1u)*sourceExtent)/(2u*outputExtent),sourceExtent-1u);
    }
    precise float2 sourcePosition=position.xy*float2(sourceExtent)/float2(outputExtent)+sourceJitter;
    const int2 selected=int2(floor(sourcePosition));
    // Signed clamp precedes conversion: negative offsets cannot wrap to UINT.
    return uint2(clamp(selected,int2(0,0),int2(sourceExtent)-1));
}

float PSUiCompositeDepthOnly(float4 position : SV_Position) : SV_Depth {
    return sourceDepth.Load(int3(uiCompositeSourcePixel(position),0));
}

void PSUiCompositeStencilBit(float4 position : SV_Position) {
    if((sourceStencil.Load(int3(uiCompositeSourcePixel(position),0)).y&stencilBit)==0u)discard;
}
