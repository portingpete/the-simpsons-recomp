// Compile ahead of time: VSIm2DDraw vs_5_0; PSIm2DDrawFlat and
// PSIm2DDrawTextured ps_5_0; /Ges /Gis /O3. No runtime shader compiler.
// Reuse the qualified source expressions without changing im2d.hlsl.
#include "im2d.hlsl"

cbuffer Im2DDrawConstants : register(b1) {
    uint draw_reverse_depth;
    uint draw_reserved;
    float draw_alpha_reference;
    uint draw_alpha_test;
    uint draw_alpha_compare;
    uint draw_blend_word;
    uint2 draw_padding;
};
Texture2D<uint4> im2d_destination : register(t1);

Im2DOutput VSIm2DDraw(float4 position : POSITION, float4 color : COLOR0, float2 uv : TEXCOORD0) {
    // Center conversion belongs to the viewport, after clipping. In particular
    // do not cancel the original subtraction or alter shader rounding order.
    return VSIm2D(position,color,uv);
}
void im2d_alpha_gate(float alpha) {
    if (draw_alpha_test == 0) return;
    bool accepted = false;
    if (draw_alpha_compare == 1) accepted = alpha < draw_alpha_reference;
    else if (draw_alpha_compare == 2) accepted = alpha == draw_alpha_reference;
    else if (draw_alpha_compare == 3) accepted = alpha <= draw_alpha_reference;
    else if (draw_alpha_compare == 4) accepted = alpha > draw_alpha_reference;
    else if (draw_alpha_compare == 5) accepted = alpha != draw_alpha_reference;
    else if (draw_alpha_compare == 6) accepted = alpha >= draw_alpha_reference;
    else if (draw_alpha_compare == 7) accepted = true;
    if (!accepted) discard;
}
uint4 im2d_finish(float4 source, float4 position) {
    im2d_alpha_gate(source.a);
    precise float4 result = source;
    if (draw_blend_word != 0x00010001) {
        uint4 codes = im2d_destination.Load(int3(int2(position.xy),0));
        // ZERO/ADD/ONE retains every destination code without re-quantizing.
        // Alpha rejection has already run; the real primitive still executes
        // coverage, depth comparison and the pixel entry point's depth output.
        if (draw_blend_word == 0x01000100) return codes;
        precise float4 destination = float4(codes) / float4(1023.0,1023.0,1023.0,3.0);
        precise float4 product = source * source.a;
        if (draw_blend_word == 0x07060706) {
            precise float inverse_alpha = 1.0 - source.a;
            precise float4 retained = destination * inverse_alpha;
            result = product + retained; // Nonseparate alpha, including source.a^2.
        } else {
            precise float3 rgb;
            if (draw_blend_word == 0x00010706) {
                precise float inverse_alpha = 1.0 - source.a;
                precise float3 retained = destination.rgb * inverse_alpha;
                rgb = product.rgb + retained;
            } else if (draw_blend_word == 0x00010106) rgb = product.rgb + destination.rgb;
            else rgb = destination.rgb - product.rgb; // Validated 0x00010186 only.
            result = float4(rgb,source.a); // These words explicitly replace alpha.
        }
    }
    // One explicit quantization. UINT output avoids a second UNORM conversion.
    // Mirrors the existing screen arithmetic policy; Xenos precision unproven.
    return uint4(round(saturate(result) * float4(1023.0,1023.0,1023.0,3.0)));
}
// The original D24FS8 stores positive 20e4, not IEEE float32. Keep its
// representable values in the native float depth surface. Conversion follows
// the independently audited CFloat24/reference arithmetic in im2d-depth.
// Quantize after viewport mapping and interpolation, before depth comparison.
float im2d_depth(float z) {
    precise float mapped = draw_reverse_depth != 0 ? 1.0 - z : z;
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
struct Im2DPixelOutput {
    uint4 color : SV_Target0;
    float depth : SV_Depth;
};
Im2DPixelOutput PSIm2DDrawFlat(Im2DOutput input) {
    Im2DPixelOutput result;
    result.color = im2d_finish(PSIm2DFlat(input),input.position);
    result.depth = im2d_depth(input.position.z);
    return result;
}
Im2DPixelOutput PSIm2DDrawTextured(Im2DOutput input) {
    Im2DPixelOutput result;
    result.color = im2d_finish(PSIm2DTextured(input),input.position);
    result.depth = im2d_depth(input.position.z);
    return result;
}
// ZERO/ADD/ONE (or a zero color mask) leaves the color attachment untouched.
// Keep real alpha rejection and quantized depth output, without a color UAV,
// scratch copy or sampled destination. Draw ordering remains unchanged.
float PSIm2DDepthFlat(Im2DOutput input) : SV_Depth {
    im2d_alpha_gate(PSIm2DFlat(input).a);
    return im2d_depth(input.position.z);
}
float PSIm2DDepthTextured(Im2DOutput input) : SV_Depth {
    im2d_alpha_gate(PSIm2DTextured(input).a);
    return im2d_depth(input.position.z);
}
