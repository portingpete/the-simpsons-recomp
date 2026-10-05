# Native screen raster policy

Implementation update: [the original screen bridge](native-original-screen.md)
uses the recovered mode1 coordinate convention and native rasterizer precision.
The subpixel differences described below remain explicitly unverified; the
implementation does not claim arbitrary fractional rectangles are pixel-exact.

Bounded evidence for `82756480` and the four verified Screen_Xenon shaders.
No runtime, renderer, test, config, CMake or reference files are changed.
The original image is `analysis/simpsons.pe`, base `82000000`, size 15,466,496,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Addresses and words below are hexadecimal; pixel sizes and bit numbers are decimal.

## Actionable result after the pipeline wrapper

**The observed screen path uses HALFPIXELOFFSET=1 and guard X/Y=1.0.**
The complete effective sequence matters: app initialization sets 1,
`823F46A0 -> 823F4618` temporarily sets 0, then wrapper `826B09A0` sets 1
at `826B09D4` before returning. Wrapper exit `826B09F0` also sets 1 at
`826B0A0C`, after popping the pipeline. Parent's boot050 correction prompted
an independent byte check of both complete wrappers and both calls.

Independently checked original reset instructions show a
67-row loop over eight-byte `{SDK ID, value}` pairs at `82CD1B70`. Its final
pair, at `82CD1D80`, is `{00000144,00000000}`. Neither `158` nor `15C` occurs
in that table, so the application's guard values survive this reset. Preserve
effective state at draw time; neither initialization nor the intermediate
reset alone is the final draw snapshot.

For **the actual half-integer-center mode 1**, the bounded native mapping is:

```text
host viewport = { TopLeftX=0, TopLeftY=0,
                  Width=original W, Height=original H,
                  MinDepth=0, MaxDepth=1 }
original clip-space XY and UV are unchanged
effective pixel rectangle remains [0,W) x [0,H)
```

**No position or UV half-shift is required or allowed for mode 1.** The
different integer-center mode 0 requires host origin `(0.5,0.5)` with W/H
unchanged; its derivation below is a separate policy, not the observed draw.
Apply any screen-specific viewport temporarily, restore the prior native
viewport, and leave the original logical camera's `(depth start,end)=(1,0)`
descriptor intact. The existing depth-off exception in `native-camera-pass.md`
remains required.

This mode-1 representation uses the existing unshifted viewport and D3D11's
normal triangle rasterizer. Keep solid/no-cull,
zero biases, depth clipping enabled, one sample, and the existing depth,
stencil, alpha, sampler, blend and resource gates. D3D11's literal null/default
rasterizer has back-face culling; the existing screen pipeline deliberately
uses `CULL_NONE` and should continue doing so.

For guard 1, conservatively require all four **actual emitted** XY pairs to
be finite and inside `[-1,+1]` inclusive, the original rectangular TL/TR/BL/BR
strip, and the verified shader output `Z=0,W=1`. Require a positive root
viewport matching the whole target, zero guest offsets, viewport enabled,
no user clip planes and disabled/full-target scissor. Both triangles then lie
inside the canonical clip volume; host guard-band implementation differences
cannot change their clipping. Reject crossing/oversized geometry rather than
clamp it, change UVs or silently choose a different guard value.

**A separate precision gate is necessary for arbitrary fractional quads.**
Original setup selects 1/16-pixel vertex quantization; D3D11 uses 1/256.
The black full-target rectangle with exact clip corners `(-1,+1)` through
`(+1,-1)` and the observed integral 1280x720 viewport is a concrete safe first
case. General fractional textured quads are not certified merely by changing
the half-pixel/guard state checks. See the numerical limits below.

## Original instruction evidence

- `826B09B8 = 4BD43CE9` calls pipeline push `823F46A0`; `826B09C0 = 38800001`
  reloads r4=1 after that call; `826B09D4 = 4BD8AD45` calls `8243B718`,
  LR `826B09D8`. Exit `826B09FC = 4BD43CFD` calls pop `823F46F8`, then
  `826B0A04 = 38800001` and `826B0A0C = 4BD8AD0D` set the same value,
  LR `826B0A10`. Both writes are unconditional on these returning paths.
  Hooking only the reset's indirect calls loses this final effective state.
- `823F4630 = 3BCB1B70` selects the reset table; `4634` indexes by eight;
  `4640/4644` load the pair; `464C/4654` load/invoke the SDK callback;
  `465C = 2B1F0043` bounds the loop at 67. `823F46E8 = 4BFFFF30` tail-calls
  this reset on the changed, nonzero pipeline path. Table SHA256 is
  `82fd8b9fe750b17d294f1b0eb8dff5555e197637ad1bc02dcb29521d6d1b3591`.
- `8243B718..738` loads `D+29C0`, preserves its bits 1..31, inserts raw bit 0,
  stores it at `B728 = 908329C0`, and marks dirty bit 35 at `D+20`.
  There is **no inversion** between SDK state 144 and this register bit.
- `824659DC = 39200004` and `82465A00 = 913F29C0` initialize the complete
  vertex-control word to 4. Half state 0 leaves it 4; state 1 makes it 5.
  Read-only `RexGlueCurrent/include/rex/graphics/registers.h:492` identifies
  bit 0 as `PA_SU_VTX_CNTL.pix_center`, bits 1..2 as `round_mode`, bits 3..5
  as `quant_mode`. `xenos.h:850..875` identifies center 0 as integer, center 1
  as half-integer, round mode 2 as nearest-even, quant mode 0 as 1/16 pixel.
  The original fixed-state emitter also pairs register index `2302` and
  value 4 at `82444854/858 -> 824448A0/A4`. These are evidence of original
  state identity, not an instruction to build or interpret command packets.
- `8243B810..830` writes guard X float bits to `D+29CC` at `B820`;
  `8243B840..860` writes guard Y to `D+29C4` at `B850`. Local register names
  are horizontal/vertical `PA_CL_GB_*_CLIP_ADJ` (`2305` / `2303`);
  the neighboring DISC_ADJ states are distinct. The application sets exact
  `3F800000`; the reset does not touch either. No numerical proof for arbitrary
  guard factors, discard adjustments or out-of-volume intersections follows
  from these names. The proposed in-volume restriction avoids those cases.
- `8243B260..29C` with viewport enabled selects `D+294C=43F` (six scale/offset
  enables, ordinary clip-space XY) and clears clip-disable in `D+2944`.
  Original setup `82465A0C/1C` sets the D3D depth-volume bit. The screen
  output `Z=0,W=1` satisfies both canonical XY and depth clip inequalities.
- `82756518..550` adds signed raster offsets, multiplies by the original
  scale floats at `82DFEB3C/40`, then forms `x=2*xScaled-1` and
  `y=1-2*yScaled`. Constants `821DD0E4=40000000`, `82000BB0=3F800000`.
  Preserve the actual original f32 operation order and values, including
  `fmsubs/fnmsubs`. There is no half-pixel add/subtract in this construction.
  Both flat/textured branches use these coordinates; original UV endpoints
  are independent inputs. Native VS passes XY and emits `Z=0,W=1` as proved
  separately in `screen-shaders.md`.
- `8243D070..090` gives viewport scale `(W/2,-H/2)` and translation
  `(X+W/2,Y+H/2)`. `8243C430` uses the viewport rectangle when the scissor
  request is zero; enabled scissor intersects the saved rectangle. Disabled
  scissor does not mean the original can render outside its viewport.
- As an independent original-code corroboration, helper `824529EC..2A10`
  chooses `0.5` when `D+29C0 bit0=0`, else zero; `82452AE0/AE8/AF4/AFC`
  **subtract** that amount when building its own pixel-coordinate vertices.
  This is a different SDK helper, not a call in `82756480`. Copying its
  subtraction into the screen bridge would double-apply a policy belonging
  to that helper's vertex construction.

The original image and instruction bytes are primary evidence for program
behavior. Local Rex headers and `src/graphics/util/draw.cpp:325..328` provide
read-only hardware-enum/translation corroboration; that latter code adds
`+0.5` to host XY only for integer-center mode. None of that backend is reused.

## Pixel and clipping arithmetic

For the root viewport, write `p_x=(x+1)*W/2`, `p_y=(1-y)*H/2` for the
unshifted post-viewport position. Original mode 0 samples at `(i,j)`;
original mode 1 samples at `(i+0.5,j+0.5)`. D3D11 samples at the latter.
For mode 0, translating the geometry by `(+0.5,+0.5)` makes every edge/sample
difference and barycentric weight the same in exact arithmetic:
`(i+0.5)-(p_x+0.5) = i-p_x`. For mode 1 the required translation is zero.
This fixes **both coverage and interpolation**; shifting UV alone cannot.
Microsoft's [coordinate-system documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-coordinates)
confirms the two pixel-center conventions.

For example, a textured screen interval from x=0 to x=4 with u=0 to u=1
has original mode-0 u at pixel 0 equal to 0. Unshifted native geometry gives
u=1/8. The shifted viewport restores u=0. At a fractional left edge x=1/4,
original integer-center coverage begins at pixel 1; unshifted D3D11 includes
pixel 0. Full-target flat black alone cannot expose either error.

The viewport implementation is preferable here to adding `(1/W,-1/H)` to
clip XY: it preserves the original clip-volume inputs and avoids creating
right/bottom clipping intersections solely for the correction. Microsoft
defines fractional viewports for this compatibility use; its implicit
viewport scissor floors each **edge**, giving `[0,W) x [0,H)` for origin
`(0.5,0.5)` and integral W/H. The potentially nondeterministic strip below the
fractional left/top boundary contains no single-sample center here; all
accepted vertices also lie inside the canonical volume. This does not extend
to arbitrary fractional origins or MSAA patterns. See the
[D3D11 specification, sections 15.6.1 and 15.7](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#15.6.1%20Viewport%20Range).

The native clip tests are `w>0`, `-w<=x,y<=w`, `0<=z<=w`; hardware may use
wider XY guard bands. Guard 1 need not be numerically installed in D3D11
for the accepted in-volume case. Default depth clipping remains enabled.
The explicit descriptor has no programmable guard-band-size field.
See [D3D11_RASTERIZER_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_rasterizer_desc).

## Subpixel precision and remaining scope

Let `Q4(p)=roundEven(16*p)/16`. The recovered original register requests this
position grid. D3D11's grid has eight fractional bits. The translation alone
does not imply `Q8(p+0.5) = Q4(p)+0.5` for general p. At p=1/32, the
ideal nearest-even original conversion yields 0, while native p+0.5 is
17/32 exactly. For a mode-0 left edge this changes whether pixel 0 lies on
the edge or outside. **Mode 1 also has a counterexample:** p=17/32 snaps to
1/2 on the original grid but remains 17/32 on D3D11's grid. The sample at
1/2 is then included only by the original left edge. At 3/32, the original
tie rounds up to 1/8. These are concrete reasons not to label every finite
in-volume quad exact.

A sufficient bounded numerical subset has post-viewport vertex positions
on the common 1/16 grid, with the same snapped positions and retained UVs on
both implementations. Exact integral full-target corners meet it. A stricter
initial implementation may accept only those corners for the flat black draw.
For broader rectangles, validate the **actual f32 clip-coordinate snapshot**
and float viewport arithmetic; nominal input pixel integers do not prove the
emitted floats map back exactly. Do not use an arbitrary epsilon to accept
coordinates near a sample/quantization boundary. D3D11's normative conversion
also has a stated tolerance; fixtures near ties are diagnostics, not a hardware
oracle. Its interpolation setup uses snapped positions. See
[D3D11 sections 3.2.4.1 and 15.16](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#15.16%20Rasterizer%20Precision).

Pre-snapping arbitrary quads to the Xenos grid may be a future implementation,
but this report does not prove all original viewport rounding/interpolator
details or certify a new geometry rewrite. No generic world, clipped quad,
MSAA, degenerate/inverted rectangle, depth-enabled or expanded-blend claim is
made. In particular selector-0 loading animation still has the independently
unresolved expanded RGB10 blend arithmetic. The reverse-depth exception and
all sampler/texture/output gates in the existing reports remain independent.

## Integration checks for the parent

1. At draw snapshot time assert wrapper-effective `144=1`,
   `158=15C=3F800000` for the observed path. Regression-test the sequence
   `1 -> reset 0 -> wrapper 1`, including wrapper exit. A mode-0 caller must
   choose its separate policy or reject; old unshifted half-0 fixtures did
   not prove original raster equivalence.
2. Query the actual RS viewport after setup: `(0,0,1280,720,0,1)` for
   this mode-1 screen draw. Check restoration after success and failure.
3. Flat full-target selector-3 black: first/last rows and columns written,
   no shared-strip diagonal gap or double coverage; depth/stencil unchanged.
4. Diagnostic fractional rectangle with left/top 1/4 pixel: pixel 0 excluded
   in mode 0, included in mode 1. Use a separate proved constant-color mode
   so alpha/expanded blending cannot conceal the raster result.
5. Textured four-pixel interval: compare sampled u=0 versus the incorrect
   1/8 at pixel 0. Use a known linear diagnostic texture before treating BC3
   decode/filter variability as raster error. Never bias the original UVs.
6. Reject NaN/infinite/out-of-volume vertices, unsupported target samples,
   non-root/fractional guest viewport and enabled unsupported clip planes.
   Quantization-boundary inputs must reject or enter a separately validated
   conversion path; the state owner alone cannot inspect geometry.

These are proposed native validation fixtures. This evidence task does not
run the parent build, create GPU resources or claim a newly rendered frame.

## Frozen evidence and reproduction

The owned analyzer uses only Python's standard library, the original derived
image and the existing offline `SimpsonsDisasm.exe`. It hash-pins 15 ranges
(14 code ranges and the reset table), checks 504 disassembled instruction
words against original bytes, checks four direct call targets/LRs and 19
critical words, and verifies hashes of the four read-only Rex reference files.
Range interpretations are manually reviewed evidence, not a general PPC or
GPU interpreter. Shader arithmetic stays in the existing screen shader proof.

```powershell
python -B tools/analyze_screen_raster_policy.py --self-test
python -B tools/analyze_screen_raster_policy.py --report analysis/native-screen-raster-policy.json
python -B tools/analyze_screen_raster_policy.py --verify-report
```

Ten tests pass: reset/wrapper effective sequence, image mutation rejection,
pixel-center coverage and interpolation, the incorrect unshifted mode-0
counterexample, quantization ties/differences, shared grid, canonical bounds,
output-path restrictions and disassembler mismatches. Numerical fixtures use
exact rational arithmetic; they neither measure original hardware nor test
the live D3D11 implementation. Report regeneration is deterministic. Only the
owned JSON path is permitted as output; all original/reference files are read
only. Final scope is frozen to this doc, analyzer and JSON.
