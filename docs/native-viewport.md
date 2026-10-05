# Native viewport contract

Status: complete, tested and frozen. This is the host D3D11 single
viewport service only; original camera begin and reversed depth remain gated.

`NativeBackend::setViewport(const D3D11_VIEWPORT&) -> D3D11_VIEWPORT` validates
all six finite float fields before RSSetViewports, verifies actual retained state
with RSGetViewports and returns that state. `viewport() const` returns an optional
value from the actual context: empty when unbound, an explicit failure for an
unsupported multiple-viewport state. There is no cached state or guest pointer.
Both enforce the existing backend owner thread and device-availability checks.

Feature-level-11 bounds are enforced: X/Y in [-32768,32767], width/height >= 0,
and right/bottom edges <= 32767. Edge addition uses double to reject float sums
that would otherwise round down across the limit. Fractional/negative origins,
zero dimensions and equal depth endpoints are valid native state. Depth endpoints
must lie in [0,1] with MinDepth <= MaxDepth. Reversed input throws an actionable
error before any context mutation; endpoints are never sorted or clamped.

The API sets only the native viewport. Target-dependent guest clamping, scissor,
depth/stencil policy, matrices, vertex transforms, drawing, presentation and
engine lifecycle are separate. Existing EngineState and screen pipeline semantics
are unchanged; drawScreen still selects its own full-target 0..1 viewport.

## Original byte evidence

Source: derived `analysis/simpsons.pe`, VA minus 82000000; SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Read-only SimpsonsDisasm was used for four bounded original functions:
823EE6C8/124, 823F00C0/1B8, 8243D0F8/7C, 8243CE80/264 (hex address/size).
The existing render-boundary report was crosschecked; raw IDA pseudocode is not
ABI authority and omits viewport stack stores, as documented in ida-analysis.md.

Camera begin 823F00C0 takes camera in r4, moves it to r31 at 823F00DC, retains
it at 82E3DD60, performs original matrix work, then passes r31 in r3 at 823F021C
to 823EE6C8 at 823F0224. This is not authorization to skip that matrix work.

823EE6C8 chooses the raster/parent and targets from camera +60/+64, raster +0,
plugin offset [82E3DC94] and the original type check. At SP+50 it constructs
six 32-bit big-endian words:

- +00/+04: raster X/Y offsets from signed halfwords +1C/+1E (LHA at
  823EE7A0/B0, stores 823EE7AC/B4).
- +08/+0C: dimensions from raster +0C/+10 (LWZ, stores 823EE7BC/C4).
- +10/+14: float endpoint start=1 (`82000BB0=3F800000`) and end=0
  (`821DD0D8=00000000`), stores 823EE7D4/DC.
- 823EE7E0 calls 8243D0F8 with r3=[82D0CAF8], r4=&SP+50.

8243D0F8 is a conversion wrapper, not six host floats: its four LWZ loads at
8243D104/10C/114/11C zero-extend the first four words, then STD/LFD/FCFID/FRSP
convert those unsigned integer values to float. It loads the last two words as
float into f5/f6 at 8243D118/110 and calls 8243CE80 at 8243D160 with
f1=X, f2=Y, f3=width, f4=height, f5=start, f6=end. Thus negative raster offsets
stored by LHA are reinterpreted as unsigned by this wrapper; directly mapping
them to a negative host X/Y would not reproduce the original instruction path.
The new host API deliberately performs no guest six-word conversion.

8243CE80 uses bound color slot 0, otherwise depth, and device mode/dimension
state to bound the effective width/height; it returns without setting state if
neither target exists. Its integer edge/clipping path is 8243CF8C..D008 and it
stores effective float state at device+3160..3174. It calls 8243C430 at D058
for additional related state; that callee is outside this bounded service.
The host API does not reproduce these SDK side effects or silently clamp against
the current target. A future camera bridge must provide the proven effective
viewport and handle/reject unsupported original mode paths.

Depth proof: 8243CEA4/A8 retain f5/f6 in f31/f26; 8243D060 (`ED9AF828`)
computes `f6-f5`, D064 (`D3FF291C`) stores start at device+291C, and D068
(`D19F2918`) stores scale at +2918. Therefore the original 1,0 request is
scale=-1, offset=1. X/Y use width/2 and -height/2, plus X+width/2 and
Y+height/2 (D06C..D090; constant 82000FB8=3F000000).

## Depth ordering and native limitation

The original writes a genuine negative depth scale, not an unordered pair of
bounds. Read-only reference `K:/Simpsons/RexGlueCurrent/include/rex/graphics/`
`register_table.inc:526` names Xenos ZSCALE/ZOFFSET; `src/graphics/util/draw.cpp`
around 488 corroborates that reverse-range handling also flips the vertex depth
transform. No renderer/reference implementation is copied or linked.

Microsoft's [rasterizer documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-getting-started)
requires ordered D3D11 endpoints and defines depth as MinDepth + z*(MaxDepth-MinDepth).
The original 1,0 mapping gives 1-z; replacing it with 0,1 alone gives z instead.
For a proven D3D-style clip-depth convention, a compensated clip transform
z'=w-z with native endpoints 0,1 is the algebraic candidate. Its clipping,
shader depth-export, float rounding and original 20e4 depth-storage behavior
are not implemented/proven by this service, so reversed inputs explicitly fail.
No inherited screen or scene shader is altered.

The [D3D11_VIEWPORT contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_viewport)
supplies dimension/bounds rules, while [RSGetViewports](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rsgetviewports)
supplies the actual bound count/state. Returning an empty query after ClearState
is intentional, rather than inventing a default viewport.

## Integration and tests

Parent adds renderer/native_viewport.cpp to SimpsonsGraphics. Test source is
tests/test_native_viewport.cpp, linked against SimpsonsGraphics and bcrypt.
Command: `NativeViewportTests analysis/simpsons.pe [--hardware]`; default is
real WARP, optional hardware is explicit and never falls back silently.

Focused ClangCL C++20 `/EHsc /MD /W4 /WX` compilation passed. Actual WARP and
hardware each passed **150 checks**, both at feature level 11.1. Tests pin the
entire original-derived image SHA256 and 27 instruction/constant words, then
exercise real native set/query state, fractional/negative origins, full coordinate
bounds, zero dimensions, equal depth endpoints, non-finite values in all six
fields, bad dimensions/edges/depth, reversed original 1,0 rejection before state
mutation, independent value snapshots, two-device isolation, foreign-thread
rejection, target binding without viewport clamping, and ClearState/rebinding.
Device removal and externally installed multiple-viewports were not fault-injected;
the implementation uses existing device-availability checks and rejects a queried
count other than zero/one. No drawing or presentation occurred.

Suggested parent integration (parent owns CMake):

```cmake
target_sources(SimpsonsGraphics PRIVATE renderer/native_viewport.cpp)
add_executable(NativeViewportTests tests/test_native_viewport.cpp)
target_link_libraries(NativeViewportTests PRIVATE SimpsonsGraphics bcrypt)
add_test(NAME NativeViewport COMMAND NativeViewportTests
  "${CMAKE_SOURCE_DIR}/analysis/simpsons.pe")
```

For an isolated direct build, sources are tests/test_native_viewport.cpp plus
renderer/native_viewport.cpp, native_backend.cpp, target_bindings.cpp,
screen_pipeline.cpp, depth_resources.cpp and material_resources.cpp. Include the
project root and existing build/shaders; define NOMINMAX/WIN32_LEAN_AND_MEAN;
link d3d11.lib, dxgi.lib, user32.lib and bcrypt.lib. The focused build ran entirely
under TEMP and did not rebuild/regenerate the parent project.

Written files: renderer/native_viewport.cpp, tests/test_native_viewport.cpp,
this document, and only `<optional>` plus two method declarations/comments in
renderer/native_backend.h, announced before editing. No CMake, parent runtime,
EngineState, screen implementation, generated AOT, original asset or reference
project edits are part of this component. This is not permission for an original
camera to advance through the still-unimplemented reversed-depth path.
