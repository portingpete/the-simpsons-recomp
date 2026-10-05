// Native fixed-function depth adaptation for original VS820C2FA0, which has
// no original pixel stage. Compile PSShadowMeshDepth ps_5_0 /Ges /Gis /O3.
// The original VS clip exports remain unchanged. Native viewport depth is 0..1.
cbuffer ShadowMeshDepthConstants : register(b1) {
    uint reverseDepth;
    uint constantBiasBits;
    uint slopeBiasBits;
    uint reserved;
};

// Same 20e4 RNE arithmetic as im2d_draw.hlsl:68..90, with mapping separated
// so that polygon offset and clamping precede the ONE depth conversion.
float shadow20e4(float mapped) {
    uint bits = asuint(mapped);
    if (!(mapped > 0.0)) return 0.0;
    uint packed;
    if (bits >= 0x3ffffff8u) packed = 0xffffffu;
    else {
        if (bits < 0x38800000u) {
            uint shift = min(113u - (bits >> 23), 24u);
            bits = (0x800000u | (bits & 0x7fffffu)) >> shift;
        } else bits += 0xc8000000u;
        bits += 3u + ((bits >> 3) & 1u);
        packed = (bits >> 3) & 0xffffffu;
    }
    if (packed == 0) return 0.0;
    uint mantissa = packed & 0xfffffu;
    uint exponent = packed >> 20;
    if (exponent == 0) {
        uint shift = 20u - uint(firstbithigh(mantissa));
        exponent = 1u - shift;
        mantissa = (mantissa << shift) & 0xfffffu;
    }
    return asfloat(((exponent + 112u) << 23) | (mantissa << 3));
}

float PSShadowMeshDepth(float4 position : SV_Position) : SV_Depth {
    precise float z = reverseDepth != 0 ? 1.0 - position.z : position.z;
    // Original8243AAA0 (SDK CC) multiplies slope by16 into2A50/2A58.
    // Original8243AB68 (SDK D0) stores constant offset in2A54/2A5C.
    // Upload8244C1B0 maps these to GPU2380 SCALE,2381 OFFSET for both faces.
    // Retain the original multiplication before converting subpixel slope
    // units back to adjacent-pixel derivatives (reference xenos.h).
    precise float constantBias = asfloat(constantBiasBits);
    precise float subpixelSlope = asfloat(slopeBiasBits) * 16.0;
    precise float slopeScale = subpixelSlope * 0.0625;
    // Evaluate derivatives before clamping/quantization, using the unbiased
    // depth plane. No UV/clip varying or extra divide by W is appropriate.
    precise float slope = max(abs(ddx_fine(z)), abs(ddy_fine(z)));
    precise float slopeBias = slopeScale * slope;
    precise float bias = slopeBias + constantBias;
    precise float biased = z + bias;
    return shadow20e4(saturate(biased));
}
