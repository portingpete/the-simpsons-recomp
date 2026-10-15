// Preserve the selected single-sample scene depth/stencil pair at the UI extent.
Texture2D<float> sourceDepth : register(t0);
Texture2D<uint2> sourceStencil : register(t1);
cbuffer UiComposite : register(b0) {
    uint2 sourceExtent;
    uint2 outputExtent;
    float2 sourceJitter; // Captured current frame, in source render pixels.
    float2 transferPadding;
};

float4 VSUiComposite(uint vertex : SV_VertexID) : SV_Position {
    float2 clipCoordinate=float2((vertex<<1)&2,vertex&2);
    return float4(clipCoordinate*float2(2,-2)+float2(-1,1),0,1);
}

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

struct UiCompositeValue {float depth : SV_Depth;uint stencil : SV_StencilRef;};
UiCompositeValue PSUiCompositeDepth(float4 position : SV_Position) {
    const uint2 sourcePixel=uiCompositeSourcePixel(position);
    UiCompositeValue value;
    value.depth=sourceDepth.Load(int3(sourcePixel,0));
    value.stencil=sourceStencil.Load(int3(sourcePixel,0)).y;
    return value;
}
