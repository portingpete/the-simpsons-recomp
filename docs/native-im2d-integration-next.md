# Remaining Im2D integration details

Build178's shader/vertex qualification does not remove the82408CC0 guard.
The actual pending state and native identities are recorded in
`native-im2d-first-draw-state.md`; the next implementation should start there.

The existing Screen_Xenon pipeline cannot simply be reused for this draw.
Its four packed blend modes use replacement alpha. Original scalar blend
enable8243A010 rebuilds all four effective targets from separate RGB/alpha
shadows. With ADD, source6, destination7 and separate-alpha disabled, the
original rotate/mask sequence8243A028..8243A044 produces07060706. The resulting
alpha is sourceAlpha squared plus destinationAlpha times one-minus-sourceAlpha.
The existing screen selector1 word00010706 has the same RGB factors but replaces
alpha. Preserve the actual native effective state after the pending commit;
the stated07060706 result is conditional on those complete retained settings.
Do not infer unobserved native settings fromFFFFFFFF original cache sentinels.

Original cull setter82439F00..82439F18 preserves all but the low three bits of
device+2948 and writes those three request bits. Original half-pixel setter
8243B718..8243B738 preserves all but bit0 of device+29C0. Their interpretation
is corroborated by Xenia's research
[register definitions](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/registers.h):
the cull control contains front/back rejection and front-face winding; pixel
center is separate from vertex rounding and quantization. Its
[pixel-center enum](https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/xenos.h)
distinguishes integer and half-integer centers. This is supporting research,
not proof of this game's complete native raster policy. Establish viewport Y
orientation, strip winding, subpixel rounding and coverage using the original
producer and a suitable reference. Do not remove the verified shader's0.5
subtraction merely because a desktop rasterizer uses half-integer samples.

The native scratch declaration00B00001 and dynamic buffer IDs are owner
identities. They must never enter the original SDK setters as guest pointers.
Prepare real native shader/input/buffer state with explicit ownership and
failure handling, preserve the original stage schedule and commit, then call
the real native engine draw implementation. Stage0 null raster selects the
qualified flat expression; other texture/lod/sampler/resource cases still need
their own complete validation. Shader selections outside the two qualified
unlit branches must remain explicit failures.

The current Im2D upload stores original big-endian bytes in its native buffer.
The new decoder returns an owned40-byte native vertex representation. A draw
owner must retain the necessary source/decoded range for the required lifetime;
it cannot retain the physical staging pointer after unlock frees it. Retaining
an owned CPU source range alongside the upload may avoid a GPU readback when
the draw consumes it, but range replacement/wrap and other buffer users must
remain coherent. No such additional owner is implemented in build178.

Finally, preserve original823F4B60 cleanup823F4970 and82409308's final start-
vertex update/return ABI once a draw really succeeds. Current fixtures stop
before those effects and explicitly restore their interrupted caller frames;
they do not qualify successful full-draw return or cleanup semantics.
