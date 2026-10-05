# Trail-effect color resolve crash — September 23, 2026

The level-play run in `build/input-recordings/20260923-215000Z-05fef1ea`
stopped after 8,445 presents and viewport depth-copy scene 8,414. Its last
failure was `Unimplemented native engine graphics boundary 0x82455570,
caller 0x827724E0`. The `trail_h` RGBA8 texture was uploaded immediately
before this call. The earlier `20260923-214940Z-782d9332` run ended when its
window was closed; it is a separate event.

Original function `82772468`, reached in phase one of `82751778`, calls the
SDK resolve at `827724DC`. Its arguments select the owned 1280×720 viewport
color texture at `82DFE360`, with selector `0x100` and a zero float4 clear
value. The original `82455570` code tests that bit and clears the source color
attachment after resolving it. The previous native bridge handled other
unflagged post-filter resolves but rejected this call site.

The native bridge now accepts this exact caller, frame, destination, selector,
clear value, original viewport ownership, and active full-size camera. It
submits a packed color copy to the separately owned viewport texture, then
clears the actual source render target to transparent black. Other SDK resolve
callers remain guarded. The original `82772468` and phase dispatcher continue
to run their own CPU logic.

`OriginalViewportColorTextures` checks the guard through the graphics boundary
and verifies the exact copied bytes, zeroed source, unchanged depth, camera
binding, effective state, and unrelated counters. Malformed selector,
destination, and clear-vector calls leave both targets unchanged. The focused
viewport camera, edge/AA, and copy tests also pass. Both `SimpsonsNative.exe`
and `SimpsonsInputRecorder.exe` were rebuilt after AOT regeneration with zero
semantic diagnostics.

The original input recording ended by user request at scene 390, about 216
seconds before the crash. A separate reconstruction of 814 logged keyboard
changes reached scene 9,000 but diverged in game state and never activated the
trail resolve. The exact manual route has therefore not been replayed through
the new bridge. A fresh run through the trail area is the remaining gameplay
check. If another failure occurs, start recording before entering that area
and press F9 only after leaving it.
