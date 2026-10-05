# State at the first guarded Im2D draw

Actual muted build178/boot137 reaches82408CC0 after a real112-byte upload into
slot0/native buffer00D00001. A bounded external reader inspected this owned
native process while its existing failure handler held the window. It changed
no process memory, page protections, bindings or original files, and terminated
its own held child afterward. This deliberately interrupted cleanup; it does
not qualify normal shutdown. See `build/im2d-upload/actual137-first-state.json`.

The original current camera isE2CA7010, rasterE1A52960,1280x720 with zero raster
offset. Scratch owns index00C00001 and declaration00B00001. The original cached
declaration, explicit vertex shader and explicit pixel shader are all zero.
Stage0's raster is null and its applied color/alpha operations are3, with
argument2 selecting diffuse. Original UI global82D090F0 remains zero. No bundle
completion, UI construction or menu completion is inferred from this draw.

Five original scalar requests are pending, in exact queue order:

| Original ID | Pending value | Previous applied | Meaning |
| --- | ---: | ---: | --- |
| 03C | 1 | 0 | Blend enabled |
| 060 | 1 | 0 | Alpha test enabled |
| 02C | 7 | 6 | Depth comparison always |
| 028 | 0 | 1 | Depth test disabled |
| 030 | 0 | 1 | Depth writes disabled |

Retained applied values include cull2, source blend6, destination blend7, alpha
reference0 and alpha comparison4 (GREATER). These are original offset-style
engine IDs and values, not desktop D3D enum values. Several other original
applied slots remainFFFFFFFF. Such sentinels cannot establish native effective
state; native defaults and previous owner transactions still matter.

Original82408CC0 binds the scratch declaration, clears explicit shaders when
present, queues high CPU scalars19E/19F=0, selects the flat/textured stage0
schedule, binds stream0/offset0/stride28 and commits the pending state. The
next implementation must preserve that sequence and the exact existing commit
owner. Requiring pre-commit depth/blend state to equal its post-commit values
would incorrectly reject this genuine path. Returning success without applying
the pending changes would also be incorrect.

The original draw wrapper823F4B60 then obtains fixed-function shaders through
8240F028 and82410A68 when explicit shaders are absent, converts primitive count
to vertex count, draws and performs its original cleanup. Native equivalents
still need shader/binding lifetimes, the cull/pixel-center policy, alpha test,
blend equations, target precision and any required depth/stencil behavior.
The independently qualified Im2D shaders and packed vertex decoder provide
inputs for that implementation; they do not authorize skipping these effects.

The inspector identifies the owned4GB guest arena by exact original function
bytes and allocation extent. Physical guest addresses use the existing
Runtime::pointer alias mapping. It records all425 scalar value/queued/applied
entries and checks queue membership. Runs134/135 exposed reader implementation
errors; run136 used an incorrect pending-record stride. Their logs and an
explicit erratum are preserved. Use run137 for the corrected state observation.

The unadjusted final renderer readback is
`build/captures/native-loading-137/native-frame-0026.png`: original dim loading
artwork on black after26 original loading draws/32 presentations. This is a
renderer readback, not desktop scanout or a console gamma/precision reference.
