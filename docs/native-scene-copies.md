# Original global scene color copies

Boot191 reaches the opening movie's frame/plane allocation, then stops at the
shared texture factory82440578, caller823C75C8. The request is1280x720,
one level, one layer, usage0, format182801B6, no auxiliary pointer, type3.
This is a separate owner from the three typed shadows textures, despite the
previous fallback diagnostic naming that service.

The evidence is the pinned original `analysis/simpsons.pe`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Read-only disassembly of823C7500..779C shows:

- Original mode byte82CD1430 controls this optional path.
-823C7590..759C queries the real video mode and writes dimensions tosp+50/+54.
-823C75C4 and823C75F0 call82440578 with the identical profile. Their original
  continuations publish the returned IDs at82D09894 and82D6C7F0.
-823C760C copies the active camera's color to the first texture using826B08B0;
  the later823C7680 call uses the second. Both pass zero depth destination.
-823C76C4 passes the two IDs onward to an effect. Other original stores at
 823C8484 and823C94xx publish borrowed shader-parameter values. This adapter
  does not qualify those effects, shader bindings or texture sampling.
- Original823C70C0 calls82441708 at823C70F4 and823C7110, then clears each
  owning global itself. No additional retain is introduced by the native code.

`EngineSceneCopies` pairs those exact requests with independent real RGB10A2
render targets and monotonic, non-addressable native identities. Creation
checks original caller, parent registers, dimensions, mode, active camera,
pair order and previous publications. It leaves original publication stores
in the AOT body. Pixels remain unspecified until the original copy executes.

The existing full-camera copy helper admits these two destinations only at
their authored caller addresses and with the matching original camera ABI.
It still runs original8269D388 for the rectangle, verifies active attachment
ownership and full1280x720 extent, and preflights before native GPU work.
The already-tested native full RGB10A2 copy preserves pixel codes and pipeline
state; GPU events retain both resources through completion. No clear, draw,
rescale or presentation is substituted. Existing shared color/depth roles and
their negative checks are unchanged.

Release verifies original cleanup caller/global register, publication and
absence from attachment caches. The original body clears the global. Native
queued-copy leases remain independent of that original reference. General
sampling remains unavailable; driver color lookup does not expose these IDs.

`OriginalSceneCopies` exercises the original parent, exact copy pixels from
independently cleared camera colors, both destinations, rejected caller/role/
publication/binding mutations, original cleanup, recreation without ID reuse,
and retirement after completed GPU work. Build: `build/scene-copy-build.log`.
The fixture does not prove movie or world rendering, audio, or gameplay.

The regenerated build passed all104 tests in199.92 seconds. OriginalSceneCopies
passed in4.62 seconds, including normal return from the actual original parent
and its paired cleanup; no test-only observation/continuation hook was needed.
