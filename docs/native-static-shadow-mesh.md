# Original static shadow mesh

Verified on 2026-09-13: `OriginalShadowCameraPass` passed **1/1 in 1.31 s** in
[the focused log](K:/SimpsonsNativeCopy/build/reach-game-static-camera-tests.log).
The subsequent decoder run replaced LastTest: `NativeStaticShadowMeshDecode`
passed **135 checks, zero failing groups, 0.02 s** in
[LastTest](K:/SimpsonsNativeCopy/build/native/Testing/Temporary/LastTest.log).
That check count belongs to the decoder, not the camera integration fixture.

Original callback dispatch is `82706F48 → 82707700 → 82707678(T,object,metadata)`.
The complete `82707678` body sets world through `827055E0`, sets receiver=true
through `82705AA0`, and selects the unskinned branch when metadata `+0x24` is zero.
Its activation return LR is `827076DC`, commit LR `827076E4`, and original
`82706378` mesh-entry LR `827076F8`. The qualified technique is `0007FFFC`
(`RenderShadowDepth`, VS `820C2FA0`, no PS). Native guards require the original
0x80-byte caller frame, `r28=0`, zero bones, committed `c40.x=0`, and mesh `r6=0`.
Live009 captured this caller and the following **36-byte stream-zero** profile:

| Byte offset | Format word | Semantic |
| --- | --- | --- |
| 0 | `002A23B9` | float3 position0 |
| 12 | `002A2187` | packed normal0 |
| 16 | `00182886` | packed color0 |
| 20 | `002C23A5` | float2 UV0 |
| 28 | `002C23A5` | float2 UV1 |
| terminator | `00FF0000 FFFFFFFF 00000000` | sixth declaration row |

[The decoder](K:/SimpsonsNativeCopy/runtime/static_shadow_mesh.cpp) copies finite
big-endian position/UV0 values into owned native vertices. It validates the unused
normal/color/UV1 formats and initializes absent blend lanes to positive zero;
it supplies no fabricated bone or unit weight. Those lanes are inactive only
under the qualified unskinned shader branch. The declaration fixture matches
[the captured 72 bytes](K:/SimpsonsNativeCopy/build/automatic-startup/reach-game-009/captures/static-shadow-elements.bin).

The original zero-bone submesh path at `82706478..B4` resolves material as
`BE32[82D6D814] + BE32[BE32[BE32[object+18]+24] + 4*BE32[submesh]]`.
When material flags `BE16[material] & 4` are zero, it draws immediately.
Otherwise it calls original `827060B8`: append a 16-byte entry containing
`{submesh, metadata, object}` as three BE pointers and `U8[material+2]` at byte12.
Bytes13..15 remain untouched; queue count at `T+AC` increments. `T+A8` owns the
queue pointer and `T+B0` its capacity; the original growth path remains separate.

[The integration fixture](K:/SimpsonsNativeCopy/tests/test_shadow_camera_pass.cpp:78)
invokes whole `82707678` with a constructor-owned root frame and synthetic quad.
Flags0 produces exactly one native indexed draw, translated depth probes,
receiver/world updates and an actual unskinned constant commit. Flags4 produces
exactly one original queue entry, preserving padding/unused capacity and making
no additional draw. Nonempty native alpha activation still rejects without
consuming that queue or changing pixels/state. The fixture restores the original
material-pool global, queue header and frame; it does not supply textured-caster
resources or qualify nonempty alpha rendering.
