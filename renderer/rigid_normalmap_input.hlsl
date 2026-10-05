// Tangent-bearing rigid input interface for simpsons_rigid_normalmap82057E08.
//
// Grounding (no invented attributes; every field is a decoded declaration slot):
// - Field order and byte offsets match Graphics::RigidVertex and the declaration
//   rows accepted by decodeRigidVertices in runtime/rigid_vertices.cpp:
//   position usage0/index0 type002A23B9, normal usage3/index0 type002A2187,
//   color usage10/index0 type00182886, uv usage5/index0 type002C23A5,
//   uv1 usage5/index1 type002C23A5, tangent usage6/index0 type002A2187.
// - The opaque normalmap VS8205855C issues six vertex fetches (slots5..10) while
//   every other rigid VS issues four. The two additional fetches are slot6 to
//   destination r5 (the only fetch in any rigid VS targeting r5) and slot10, the
//   second fetch targeting r4. Remaining destinations match the established
//   pattern: r1 position (slot5, xyz + literal 1.0), r2 packed XYZ + 1.0, r3 four
//   components, r4.xy then r4.zw (slots9..10, the two-UV split).
// - r5 is therefore the tangent feed: same 002A2187 packed 10-bit signed XYZ
//   encoding as normals (high two bits dead under the XYZ fetch mask), consumed
//   only when the declaration carries usage6/index0. Slots22..24 transform r5 by
//   the world matrix exactly as slots15..17 transform r2, confirming r5 is a
//   direction vector input rather than padding.
// - The existing 5-element dualtextured layout (TEXCOORD0..4) is unchanged and
//   remains the binding for every non-normalmap draw. This 6-element interface
//   appends tangent as TEXCOORD5 at byte offset56, so the first 56 bytes stay
//   byte-identical and every existing consumer is preserved. It is bound only
//   through the explicit normalmap declaration path, never by default.
//
// This file provides the input structure plus a bit-exact passthrough VS/GS
// pair used to qualify the layout and interface on GPU. It performs no lighting
// or shading arithmetic and replaces no original shader.
struct RigidNormalInput {
    float3 position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 color : TEXCOORD2;
    float2 uv : TEXCOORD3;
    float2 uv1 : TEXCOORD4;
    float3 tangent : TEXCOORD5;
};
struct RigidNormalPassthrough {
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float4 color : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float2 uv1 : TEXCOORD3;
    float3 tangent : TEXCOORD4;
};
RigidNormalPassthrough VSRigidNormalTangent(RigidNormalInput input) {
    RigidNormalPassthrough result;
    result.position = float4(input.position, 1.0);
    result.normal = input.normal;
    result.color = input.color;
    result.uv = input.uv;
    result.uv1 = input.uv1;
    result.tangent = input.tangent;
    return result;
}
[maxvertexcount(1)]
void GSRigidNormalTangentProbe(point RigidNormalPassthrough input[1],
    inout PointStream<RigidNormalPassthrough> stream) {
    stream.Append(input[0]);
}
