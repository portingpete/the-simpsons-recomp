# Bounded original loading camera pass

Status: complete and frozen for this bounded evidence task; implementation
limits below remain explicit.

The next useful implementation is **root-camera target/viewport selection at
`823EE6C8`, retaining original begin/end and their CPU helpers**. A black
selector-3 screen pass has a small, separable contract. Animated selector-0
quads still require unproved expanded-blend arithmetic; complete presentation
also has outstanding display/completion requirements. This report does not
authorize an original frame or world drawing.

Only `tools/analyze_camera_pass.py`, this document, and
`analysis/native-camera-pass.json` belong to this task. No runtime, renderer,
generated code, previous evidence files, assets, or references were changed.

## Authority and current execution limit

Original flat image: `analysis/simpsons.pe`, base `82000000`, 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The analyzer validates this identity, independently validates the flat PE and
BE `.pdata`, and pins 40 complete function/leaf byte ranges. It checks critical
branch targets/LRs, store operands, clear-table entries and constants. Original
instructions are authoritative; report annotations are reviewed interpretations,
not automatically inferred symbols. Disassembly contains evidence only, with
no CPU/GPU execution or command-stream implementation.

Boot035 reached both root camera/depth creations and stopped at loading texture
creation. Read-only `build/boot-037.log` now shows both original 256x256 BC3
textures attached, followed by an unmapped read at `0090007D`, LR `82723D88`.
No camera pass or original game pixel has executed in either observation.
The parent's subsequent entry guard at application scalar dispatcher
`82723D80` is earlier than this pass. Its SDK-offset-table access cannot use the
opaque native context. Application state updates must reach the host state
owner or reject before inherited-state claims become valid. This report does
not recover or implement that separate dispatcher.

The immediate relation was independently checked at `82723DC4..DE8`:
load context from `82D6D890`, load an offset from `82150580+4*selector`, then
load the indirect setter from `context+offset+40` at `82723DE0` and call it
with `r4=value`. This is a callback lookup, not a successful state setter.
The existing frozen poststart report pins the encompassing dispatcher;
the camera analyzer does not broaden its function set to recover that service.

## Entry ABI and CPU ownership

Let `E = BE32[82D0CA68]`, `C = camera`, `R = camera color raster`,
`P = BE32[R+00]`, and `X = P + BE32[82E3DC94]`. All addresses/offsets below
are hexadecimal; guest words are BE unless specified otherwise.

- Begin: `823F00C0(r3=0,r4=C,r5=0)`, called through `E+4C` at `823F18B0`,
  LR `823F18B4`. It returns an existing nonzero `CB1C`, or sets/returns 1.
- End: `823EE7F0(r3=0,r4=C,r5=0)`, through `E+70` at `823F1834`,
  LR `823F1838`; returns 1. Its body ignores the camera argument.
- Target/viewport: `823EE6C8(r3=C)`, called at begin `823F0224`,
  LR `823F0228`, and clear `823EE9B4`, LR `823EE9B8`.
  Both callers ignore its return; do not invent a Boolean failure protocol.
- Clear: `823EE940(r3=C,r4=RGBA-byte pointer,r5=selector)`, through `E+9C`
  at `823F1BA4`, LR `823F1BA8`; returns 1.
- Present: `823EE820`, through `E+98` at `82408068`, LR `8240806C`.
  The loading caller supplies `r3=R,r4=0,r5=1` through `823F1BD0 → 82408030`;
  the platform body ignores those inputs and returns 1.

Keep public dispatchers `823F1A18` / `823F1A08`: they tail-call camera
`+18` / `+1C`. Keep wrappers `823F1870` / `823F1800`:

- Begin stores `E+00=C` **before** calling `8240D5C0` at `823F1894`.
  This synchronizes dirty frame lists and invokes original attached-object
  callbacks at `8240D624/65C/68C`; removing it loses CPU behavior. Preserve
  checked AOT dispatch and existing guards; this is not a proof that arbitrary
  future callbacks are GPU-free. On platform success it calls `823F8D60`
  at `823F18C0`; that original leaf is exactly `blr`.
- End clears `E+00` only after a nonzero platform result, then returns C;
  otherwise it returns zero. Begin failure does not roll back `E+00` itself.
  Any stronger native preflight/transaction policy must account for this
  publication order, not silently assume the original wrapper is atomic.

The platform begin body has these CPU effects, in order:

1. `823F00E4`: `82E3DD60=C`.
2. Call `823F2540(C+4 frame)` at `00EC`, LR `00F0`, then
   `823F3DC8(r3=82D0CA70,r4=returned matrix)` at `0100`, LR `0104`.
   These retain frame hierarchy/matrix work and engine tolerance selection.
3. At view matrix `82D0CA70`, negate floats `+00/+10/+20/+30`, set
   `+0C/+1C/+2C=0`, `+3C=1`. Call `823EF028(1,view)` at `015C`, LR `0160`.
4. Update projection `82CD1AB0`: `+00=C[70]`, `+14=C[74]`,
   `+20=C[78]*C[70]`, `+24=C[7C]*C[74]`, `+30=-C[78]*C[70]`,
   `+34=-C[7C]*C[74]`. For BE32 `C[14]==2`, set
   `s=1/(C[84]-C[80])`, `+2C=0,+3C=1`; otherwise
   `s=C[84]/(C[84]-C[80])`, `+2C=1,+3C=0`.
   Set `+28=s,+38=-(s*C[80])`. Other projection bytes are untouched by
   this body. These are original f32 operations; keep AOT operation order.
   Call `823EF028(2,projection)` at `0210`, LR `0214`.
5. `823F0220`: `82D0CB34=0`; call target/viewport service.
6. Queue scalar IDs `199` and `19A` through original `82400170` at
   `823F0230/023C`, with **raw words** `C[88]` and `C[84]` respectively.
   These CPU pending-state updates still matter when screen shaders ignore
   projection. They are not depth-clear values or float-to-integer conversions.
7. Set `82D0CB1C=1` only when it was zero.

`823EF028` is practical to retain: it compares all sixteen matrix words,
allocates a 64-byte pool entry from `82D0CB3C` through `E+138` when absent
(LR `823EF0BC`, hint `10411`), and copies through `82A3CD80`.
Entries live at `82D0CB40+4*index`. Its apparent device matrix helper
`823F47E0` is **`li r3,0; blr`**, independently byte-pinned; it never
dereferences CAF8. Original zero is interpreted as success. Keep real pool
allocation, copies, original cleanup and failure behavior. This is not a
replacement stub or permission to omit the matrix caches.

End is entirely CPU: clear nonzero `82D0CB1C`, clear `82E3DD60`, return 1.
It neither resolves nor unbinds targets, clears pixels, restores state, nor
waits for GPU completion. A host association can end alongside this leaf,
but original target caches remain selected.

## Target and viewport contract

`823EE6C8` follows **one parent pointer**, not a recursive search. For a
nonnull color parent whose byte `P+20 != 5`, it selects global default color
`82D0CB00` and depth `82D0CAFC`. This is the observed type-2 branch.
It does **not** obtain the color target from type-2 extension `X+00`, which
remains zero. It selects default depth even if `C+64` is zero.

Type 5 selects color `X+00` and the depth parent's extension `+00` instead.
The null-color path can dereference the zero extension pointer before its later
viewport fallback. Do not infer support for depth-only/null-color cameras.
Keep type 5, children, private depth and mismatched dimensions outside the
first native pass.

Color helper `823EDB68(slot=0,target)` compares the CPU cache at
`82D0CF5C+4*slot`, stores a changed target, then calls SDK `8243DED0`
at `823EDBA0`. Depth selection similarly compares/stores `82D0CF58`,
then calls `8243D598` at `823EE77C`. These mixed helpers cannot run
unchanged against a native identity. A native service should validate real
driver roles/owner generation first, perform actual host binding, and preserve
the changed/unchanged cache semantics. Retain inherited expanded-mode requests
across binding; binding a target does not clear them.

Viewport stack record at `823EE7AC..DC` is exactly six 32-bit words:
signed-halfword raster offsets `P+1C/+1E` sign-extended into words,
BE32 width/height `P+0C/+10`, float bits `3F800000/00000000`.
`8243D0F8` loads the first four with LWZ and converts their **unsigned** values
to float. Therefore negative offsets are not ordinary negative host origins.

SDK `8243CE80` bounds width/height using bound target dimensions or special
mode dimensions. It also calls `8243C430`, which retains requested scissor
`device+317C..3188` and intersects it with the effective viewport when
`device+2E48 != 0`. The original logical float viewport resides at
`device+3160..3174`; those are SDK fields, **not fields to fabricate in CAF8**.
For the observed root camera, require zero offsets and exact 1280x720 target
extent. Then no invented clipping is necessary. Reject unsupported offsets,
special modes, child rectangles and unknown scissor changes. A disabled
scissor or a proved full-target effective rectangle is a separate draw
precondition; the existing EngineState screen snapshot does not prove it.

At `8243D060..68`, the SDK stores `Zscale=end-start=-1`, `Zoffset=start=1`.
X scale is width/2, Y scale is -height/2; translations are X+width/2 and
Y+height/2. Original reverse depth is thus `1-z`, not interchangeable ordered
bounds. Startup depth comparison is GREATER_EQUAL, and the engine clear depth
is zero. With the begin projection, the usual near/far mapping is reversed
after viewport; this algebra is not a complete floating-depth precision proof.

For the four proven screen shaders, VS emits Z=0,W=1. Original viewport depth
would be 1; a host 0..1 viewport gives 0. The values differ. Their screen color
effects are equivalent only with depth testing and effective depth writes
disabled, stencil disabled, no shader depth export/read and no other consumer
of that Z value. Preserve logical guest endpoint bits and inherited depth
write request (startup is 1); effective host writes must be inert while depth
testing is off. Revalidate before every draw, and after quad cleanup re-enables
the depth request. Store the prior host viewport so normalization is reversible
when leaving the bounded native operation. Do not sort/clamp the original
endpoints, modify its matrices, apply a world-shader Z fix, or allow a later
depth-enabled draw through this exception.

No resource generation counter is incremented by these five engine bodies.
`CB1C` is a Boolean-like begin/end flag, `CB34` a reset field with broader
meaning not established here, and `DD60/E+00` are current-camera pointers.
Native owner generation, target generation and pass/state revision checks
are legitimate host lifetime protections, **not recovered guest fields**.

## Clear and actual loading selectors

Engine clear `823EE940` reads table `82062AA0` using unbounded `4*r5`:
selectors 0..7 give `0,F,10,1F,20,2F,30,3F`. Native input must reject outside
0..7. Bit 0 of the selector enables reading four bytes R,G,B,A, packed into
SDK `AARRGGBB`; otherwise the pointer is not read and packed color is zero.
Missing `C+64` removes bits `30` from the result. The clear then selects
targets, sets the same reverse viewport again, and calls `82453C30` at
`823EEA50`, LR `823EEA54`:

```text
r3=CAF8, r4=0 rectangle count, r5=0 rectangle pointer,
r6=table mask (possibly stripped), r7=packed ARGB,
f1=0 depth, r9=BE32[82D0CB14] stencil, r10=0.
```

`823EE8D8/8E8` are CPU setter/getter leaves for CB14. Clear does not overwrite
that value or the current-camera pointers. SDK zero-rectangle handling
`82453B08` uses target/mode extent; inner `82453478` intersects it with
viewport and enabled scissor and returns early for an empty rectangle.
Thus native full-resource clears are only equivalent under a proved
full-target region. Exact general RGBA conversion into 10:10:10:2 and 20e4
depth quantization are outside this proof; black and depth zero are exact
representable candidates, not permission for unvalidated general clears.

Actual drawing branch: `82862D50` calls `828625A0` at `82862E40` with
`r4=1` (background) and `r5=1` (animation). The helper begins the camera,
selects viewport scale using `82752090 → 82751510`, and then:

- At `8286266C`, requests flat full-raster black RGBA `(0,0,0,1)`, no texture,
  selector **3**. This branch tests the low byte of the background flag.
- At `82862880`, conditional animated elements use selector **0**, white RGB
  with computed alpha, one of the two loading textures and UV 0..1. There are
  five candidate elements; time/alpha conditions can suppress draws.
- At `82862894`, ends the camera. Caller presents through `823F1BD0` at
  `82862E58` if the selected camera is nonnull.

This drawing branch does **not** call the camera clear callback. The separate
zero-animation-state branch at `82862E78..A0` directly calls SDK clear with
flags F, packed color 0 and f1=1, then presents. F selects color only, so this
is not evidence for a depth-one clear. That bypass needs its own explicit
guard/boundary; implementing only `823EE940` cannot safely cover it.

Screen helper `82756480` sets depth request off, cull off, alpha test on,
GREATER and reference integer 1 (=float32 1/255). Packed target-0 equations:

- Selector 0: `00010106`, `S.rgb*S.a + D.rgb`.
- Selector 1: `00010706`, `S.rgb*S.a + D.rgb*(1-S.a)`.
- Selector 2: `00010186`, `D.rgb-S.rgb*S.a`.
- Selector 3: `00010001`, replacement RGB.

All four replace alpha with S.a. This loading caller selects 3 and 0, not 1/2.
Selectors 0..2 set expanded mode on before drawing and off afterward;
selector 3 leaves inherited expansion unchanged. Cleanup broadcasts scalar
blend disable/replacement, disables alpha test and enables depth request;
it does not restore prior state. Keep original viewport-scale CPU selection:
`82751510` copies a selected 100-byte record, including reciprocal scales into
`82DFEB3C/40`. Do not substitute arbitrary reciprocal target dimensions if
the original selected record disagrees.

For a first black-background permit, require selector3, no texture, exact
original flat material/declaration/constants/geometry, inherited expansion=0,
the proven EngineState screen snapshot, one default color role, full viewport/
effective scissor, single sample, established coverage/sample state and valid
resource generations. Every relevant state change must be observed or reject.
The state owner alone is insufficient for these target/rasterization checks.
Stop at the first unsupported animated request; silently omit it and present
the black result only as a test artifact, never as a completed original frame.

## Present: copy, sampling, CPU lists and completion

Platform present `823EE820` does the following, independently of camera end:

1. Swap `82D0CF90` and `82D0CF8C` at `823EE874/878`. Old CF90 is now CF8C.
2. At `823EE87C`, resolve via `82455570`: r4/r5/r7/r8/r9/r10=0,
   r6=**old CF90**, f1=0, stack words caller SP+5C/+64=0.
3. Store old `82D0CF94` at `82D0CF98`; call `82457E30` at `823EE88C`
   and store its result at CF94. That SDK helper captures `device+2A9C`,
   writes `+2AB0`, submits through `82457CC8`, and returns the captured value.
   These are submission-history tokens, not camera/resource generations or
   a proved engine frame counter. Returning an arbitrary increment is not a
   completion proof.
4. Call `824544E8` at `823EE898`. It tail-calls `82454048` with r4=0,
   entering real SDK submission synchronization; it is not a no-op.
5. Call `824544F0(CAF8,CF8C,0)` at `823EE8A8`, with the same resolved texture.
6. Call original CPU leaf `823FC5B8` at `823EE8AC`, LR `823EE8B0`:
   zero `82D0D0E0/E4/E8/EC`, increment `82D0D0DC`, reset it to zero if the
   increment is >=4. Retain this exactly; no allocation/free call occurs in
   the leaf. Its associated resource reuse must respect native completion.

The outer raster wrapper has a further CPU obligation **before** platform
present: `82408054 → 823FA978`, LR `82408058`. Let
`Q=E+BE32[82E3DC88]`. The leaf concatenates/splices the intrusive lists
referenced by Q+20 and Q+24, empties the old Q+20 head, swaps the two head
pointers and clears Q+08. It has no SDK calls. Preserve the wrapper and this
list operation; replacing `823F1BD0` wholesale with host presentation would
lose it. A failed platform present does not undo that preceding list work.

Resolve facts established directly in `82455570`:

- r4 low three bits select source attachment; zero selects `device+3090`,
  color slot 0. For the original single-sample source, omitted sample bits
  become `10` at `824555E4`. Null source rectangle becomes `(0,0,width,height)`
  from destination texture dimensions at `82455714..28`.
- Null destination point selects the original zero pair at `8206AD58`.
  Destination level/slice arguments are zero. Destination surface format 54
  is remapped to 7 at `824557BC`.
- The normalized `10` path does not select the post-copy color/depth clear
  branches at `82455910/2C`. Do not clear the source just because it was
  resolved. Keep distinct color surface and front-texture roles/backing.

Startup color stores 10:10:10:2 UNORM (`182801B6`); front textures use
`28280136`. They share packed component widths; the latter has endian0 and
**ZYX1 sampling**, the former endian2 and ZYXW. Native logical-RGBA resources
must preserve stored component codes on the compatible same-size/single-sample
copy. Alpha storage and sample alpha are separate: sampling a front texture
must return alpha **1**, regardless of its stored two alpha bits. Do not
overwrite all alpha bits just to imitate that view, and do not add a second
red/blue swap if native producers already use logical RGBA. These source
format definitions are corroborated by pinned local `xenos.h`; their builder
derivation remains in frozen `native-driver-state.md`.

`824544F0` reaches `VdSwap` at `8245473C → 82CC2CA4`, import ordinal `025B`
(both original import marker words and local export definition checked).
It manipulates SDK display descriptors/modes and may enter capture branches
through `8246D3A0`. **No engine quad or expanded-mode setter is called by
`823EE820` itself.** An extra native front-copy draw, if required by host
resource/view constraints, is an implementation choice; it is not a recovered
second expanded-blend pass. Keep transfer unblended with the documented alpha
view policy when such a copy is eventually justified.

Unresolved: complete SDK/display scaling, gamma, scanout timing and optional
capture mode closure; numeric output conversion if host display storage
differs; complete CPU consumers of CF94/98 and every asynchronous reuse path.
The direct call graph does not prove absence of indirect consumers. Do not
claim that `CopyResource + Present` alone matches the original display or
that an opaque native token is safe for any unreviewed SDK wait.

A viable native design is a real same-size packed color transfer into the
existing alternating front owners, paired with native submission/completion
tracking and a separately specified display service. Validate before guest
publication; preserve native ownership through GPU use. There is no console
ring or mirrored SDK device. This is a design proposal, not an implemented
or numerically certified present service.

## Bounded next integration plan

1. Close the earlier application-state dispatcher guard independently.
2. Preflight root camera/raster identities, live driver roles/generations,
   dimensions, offsets, matrix pool/frame graph and callback ownership.
   Retain original begin CPU work; replace `823EE6C8` with real target binding
   plus logical viewport tracking. Keep the other SDK target/viewport entries
   guarded. A host viewport alone never grants draw permission.
3. Retain original CPU end/wrappers. Associate a native pass lifetime without
   treating CB1C or SDK submission history as a host generation counter.
4. Permit only the independently validated selector-3 black screen draw;
   retain all CPU preparation/state effects, reject expanded animation.
   Preserve the distinction between a bounded diagnostic output and original
   loading-frame completion.
5. Keep camera clear and the direct loading SDK-clear bypass guarded until
   their full-region/color/depth subset is implemented. Handle presentation
   separately with real packed transfer, front-view alpha policy, original
   CPU list/index work and explicit completion/display limitations.

## Reproduction and validation

```powershell
python -B tools/analyze_camera_pass.py --self-test --image analysis/simpsons.pe
python -B tools/analyze_camera_pass.py --image analysis/simpsons.pe --report analysis/native-camera-pass.json
```

Without `--report`, JSON goes to stdout. `--disassembler` and `--reference-root`
can select relocated copies of the same inputs; reference hashes must match.
The report contains no timestamps or machine-specific paths. Only the owned
report path is accepted. **13 self-tests passed**, covering 40 pinned extents,
3,570 instruction words, 35 critical calls and 20 named store assertions.
They mutate bytes in memory and cover
image/code/table/pdata corruption, disassembly mismatch, all eight clear masks,
asymmetric RGBA packing, six-word viewport framing, unsupported negative/
nonzero origins, malformed/reversed endpoints, distinct front-role rotation,
submission-history separation, store shapes and output-path rejection.

An independent CLI run reproduced the report byte-for-byte after normalizing
Windows stdout line endings. Invalid output path and wrong input image both
failed before writing; the existing report and original-image hash remained
unchanged. Final JSON SHA256:
`88e45e211bcf316721727f842af12d9bd21286253c7ac1da70ee1d7d15128c19`.

The viewport fixture's positive dimensions <=16384 are a deliberately bounded
native proposal, not an inferred original API maximum. Actual startup remains
1280x720. Fixtures do not render, evaluate hardware expanded blending, certify
display behavior, or run the game.
