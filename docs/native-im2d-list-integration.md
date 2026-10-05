# Original Im2D triangle-list bridge

Build188 full validation passes all96 suites in169.05 seconds. The original
driver fixture has123,265 checks, including157 original Im2D draw checks.
Native Im2D passes449,810 checks on each of WARP and hardware; engine state
passes9,888 checks across10 groups. Actual161 is the latest game run.

Actual159 reached82409308 with primitive3,498 vertices at0203A7C0, caller
826C0A5C, and the copied font rasterE1AC6548 bound. The active camera, pass,
buffer capability and zero subraster fields all match the existing strip path.
The previous guard rejected its primitive before upload or text submission.
The font's first six captured vertices form two independent triangles; the
next glyph begins at vertex6. Their source Z is3EFEF9DB and ARGB is02000000.
These values are retained, not replaced by demonstration geometry.

`build/im2d-upload/list188-evidence.json` reuses the already-verified154-word
upload function span and checks19 literal pins plus all7 topology-table words.
Original826C0A1C loads the unsigned16-bit vertex count from the font batch;
826C0A2C loads the vertex pointer and826C0A48 selects primitive3. The original
engine call at826C0A58 has the observed return address826C0A5C.

Original82409380/384 computes vertexCount/3 for primitive3. The existing strip
branch computes vertexCount-2. The table82062E08 maps these engine primitives
to4 and6 respectively. Original82409538 forwards that mapped type and computed
primitive count; the epilogue advances its cursor by the full vertex count.
The bridge admits complete lists only,3..9360 vertices divisible by3. Trailing
incomplete triplets remain an explicit unsupported profile even though the
original integer division would omit their incomplete primitive.

The typed upload scope now retains its primitive alongside source, count,
stack, buffer, slot and offset. Unlock and draw validate the same original
primitive; draw checks both the mapped topology and original primitive count.
The original allocation, copy, setup, state commit and epilogue remain AOT.
The native packet receives the retained engine primitive. Font ownership,
shader selection, UV/color/Z input, depth mapping and sampler contracts remain
independent checks. Backend list ordering is described in
`native-im2d-triangle-list.md`.

Actual160 passed the primitive entry/upload and reached the original font's
sampler. The old shared screen gate rejected its non-repeat U/V addressing.
`EngineState::requireOriginalIm2DScreenState` now checks original U/V modes0
(repeat) or2 (clamp-to-edge), preserves both in its returned snapshot, and
applies the existing screen gate to every remaining policy field. It validates
a copy; effective state, original requests and the ordinary screen gate do not
change. The bridge maps each axis separately to D3D11 WRAP/CLAMP. The existing
one-level owner requirement, linear min/mag, zero bias, minimum mip0,
anisotropy1 and retained maximum-mip13 policy remain checked. The Texture2D
fetch keeps the prior inactive-W/separate-Z treatment and requires a supported
native W mode. No new point-filter, mirror, border or mip-chain profile is
admitted by this bridge.

The scalar/ownership tests and native sampling fixtures pass after this change:
three focused suites in6.44 seconds and449,810 Im2D checks on WARP/hardware.
State fixtures check independent U/V modes, preserved requests and rejection
of mirror/border modes without mutation. Actual sampled pixels outside both
axes distinguish wrap from clamp; the added V check uses linear filtering.
Actual161 submitted nine498-vertex batches with the original font raster and
completed front copy583 after those draws. The following frame reached a
different original pixel-program expression and rejected before that draw.
The prior capture schedule missed the first textured frame; the diagnostic
build now records that first frame and the rejected generated pixel source.
Actual162 remains pending: the user requested a session wrap-up before that
launch. Readable text is not inferred from the passing fixtures or submission
counts. The final diagnostic build passed the full96-suite regression.

The added original-code fixture submits12 vertices forming two separated
opaque rectangles. It checks every color/depth/stencil pixel, source words,
ABI, cursor advance, override reset, queue commit, upload/draw counts and
temporary ownership. The empty region between the rectangles must survive,
which catches accidental strip connections. This is a synthetic semantic
fixture, not evidence of rendered game text. Full regression is complete;
actual162 and visual verification remain pending. Resume instructions and
artifact paths are in `session-handoff-188.md`.
