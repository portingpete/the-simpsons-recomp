# Ball Homer distortion sprite

The original `82771A18` effect reaches a console immediate quad submission after
the texture bind at `82771B40`. Accepting that copied ITXD texture alone leaves
the subsequent device shader calls and vertex lock active. In the reported
freeze, `82771CD0`, `82771CE8`, and `82771D04` receive a null console device, then
`82771D18` enters the console quad allocator. This is a separate missing draw
bridge, after successful texture resolution.

`tools/analyze_ball_effect_shaders.py --verify --self-test` checks the original
image, shader records, all scheduled instructions, declaration, constant-bank
helpers and complete CPU effect. It rejects each mutated executable byte.

The source shader associations are `82CF25B4` / `82156548`
(`Distort_Xenon_VSSprite`, 0x220 bytes) and `82CF2584` / `82155D60`
(`Distort_Xenon_PSSprite`, 0x1C4 bytes). These are distinct from Screen_Xenon.
Their hashes and instruction fields are emitted by the evidence tool.

The vertex declaration at `82151724` contains POSITION float2 at offset zero
and TEXCOORD float2 at offset eight, stride sixteen. The original CPU writes
four XYUV vertices in order `(-s,-s)`, `(s,-s)`, `(s,s)`, `(-s,s)` and selects
primitive 13 (QUADLIST). UV origin and cell width/height come from the original
animated frame calculations; the native backend preserves those values.

VS c0..3 are the world-to-view matrix at `82DFEA60`; c4..7 are view-to-screen at
`82DFEAA0`. VS c8 is the world particle XYZ and alpha at the original frame's
stack+0x60. `82444D10` computes a VS destination as
`device + ((constantIndex + 0x78) << 4)`, so device+0x800 is VS c8. The pixel
constant helper `82444DF8` instead adds 0x178 before scaling.

The vertex shader transforms the particle center into view space, adds each
vertex's XY offset in the camera plane, then projects. Its original ordered
operations (r0.zw holds vertex XY, r0.xy holds UV) are:

```
r1.xyz = c8.zzz * c2.xzy + c3.xzy
r1.xyz = c8.yyy * c1.zxy + r1.yxz
r1.xyz = c8.xxx * c0.xzy + r1.yxz
r0.zw += float2(r1.x, r1.z)
r1 = r1.yyyy * c6 + c7
r1 = r0.wwww * c5 + r1
position = r0.zzzz * c4 + r1
interpolator1 = float4(1, 1, 1, c8.w)
interpolator0.xy = r0.xy
```

The pixel shader fetches the texture's green and alpha channels with normalized
UVs and computed LOD, using the selected sampler. Scalar MULs swizzle 0x61
selects X and Y of that fetched pair. It computes, in order,
`saturate(((green * textureAlpha) * particleAlpha) * 1.5)` and replicates that
value into RGBA. It does not output the original RGB texture color. Slot 12 of
the VS overlaps vector/scalar export masks for RGB; that encodes literal one,
with particle alpha only in W.

The native transcription preserves the finite arithmetic ordering and original
shader dataflow. Console fused arithmetic, filtering precision and
rasterization equivalence are not established by this evidence.

The complete downstream filter, emboss, scene restore and displacement phase is
documented in [ball-distortion-phase.md](ball-distortion-phase.md).
