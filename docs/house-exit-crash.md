# First house exit crash repair

The user reproduced a crash by leaving the Simpsons' house after the Land of
Chocolate completion outro and recap. The direct completion shortcut remains
`Play First Mission - Completion.lnk`, targeting
`build/native/SimpsonsLauncher.exe --first-mission-completion` without a startup
window. Live retests use fresh private profile, content and video copies under
`build/house-exit-fix/runs`.

## Observed failures

The first manual run rejected `827016D8` skin bone arguments because native
code treated `r28` as a submesh index. Retail `8274010C = 7CDC3378` retains the
incoming Boolean `r6` in `r28`; the first bone call does not redefine it. The
repair accepts both Boolean values while retaining the exact call frame,
effect owner, typed packet, composition source and selected bone bounds.

After that repair, run `20261001-210858Z-caaec4dd` reached the house and the
user left through the front door. Root also supplied movement in that run;
the logs do not identify which author supplied each keyboard state. The
original `82755FD0` black fade submitted a real screen draw, then native effect
retirement failed with `Actual native rigid shader bindings differ from their
owners`.

The original screen call intentionally publishes its screen declaration and
VS/PS in `82CD1A68/6C/70`. Restoring the previous scene shaders would change
retail behavior. The backend now issues an opaque receipt only after one
completed original flat or textured screen draw, with current device, shader
pair, draw counter and epoch validation. Effect retirement checks both that
physical screen binding and the retained original logical manager, wrapper,
technique, cache and typed owner. It then runs the original cache end helper
`826B37B8` and retires the receipt. Arbitrary shader corruption still fails.

Repeated selection of the same wrapper and technique is a genuine original
no-op: `826B5FD8..826B5FEC` can branch directly to `826B605C` without ending or
rebinding the effect. That selection must retain the validated screen receipt;
actual scene commits and draws still require their own scene shader pair.

The next live candidate `20261001-220040Z-20cbd97a` reached the house. Its
initial timed replay stopped after two delivered inputs because acknowledgements
were buffered. The user subsequently supplied keyboard and mouse input and
left the house; repeated fades and outside asset uploads reached a further
`Original screen replacement shader/declaration cache changed` failure.
This is retained as a failed retest, not an exterior success. Opted-in input
receipts now flush immediately, and replay scheduling does not wait for log
acknowledgements before issuing the next timed action.

The same review identified original direct sprites and batch ends as additional
screen-state publishers. The unready `8276AF78` branch changes vertex input
without drawing, so it needs a distinct zero-draw input-binding proof. Ready
ordinary and query sprites require actual completed-draw receipts. Original
batch ends clear the declaration cache and input layout while retaining the
shader pair; that exact publication is tracked separately from arbitrary
cache changes. Original CPU and GPU regressions cover these publications.
The unready sprite deliberately retains expanded blending through batch and
manager cleanup; the bounded fixtures preserve and verify that state before
establishing the next scene baseline. Sky alpha cleanup also deliberately
restores depth writing for metadata bit two. Tests verify write-zero at the
actual mesh call and the correct state after the original parent returns.

Run `20261001-222700Z-9eeb3f19` completed the 25-command replay with all ordered
acknowledgements and no command outside its source timing bound. Its completed
capture shows Bart in the entry hall, still inside the house. A subsequent
two-second forward movement and interaction reproduced another cache failure:
`826B4628` called from `826D54A4` saw declaration `00B00001` and null VS/PS,
where the completed fade receipt still expected its screen shaders. This run
is a failed exit retest. Original entry `826D5CB0` pushes graphics mode zero before
its auxiliary UI traversal; changing to that mode invokes `823EFDA0`, clears
the native bindings and preserves the selected logical FX manager. The
traversal returns through camera restoration and manager end at `826D54A4`
without rebinding the old scene shaders. The native reset transaction must
therefore track the successful full clear and its retained logical owner.
Arbitrary null shader caches are not accepted. The intervening Im2D declaration
is accepted only through its guarded original setter and resource owner.

Run `20261001-231708Z-72a8bf6e` retained the new reset repair but failed after
the user's manual house exit. Keyboard/mouse packets, including the final
interaction, preceded outside geometry and three completed billboard draws.
The next rigid shader ownership check failed. This is another failed live
retest, requiring a separate original immediate/billboard lifetime review.

Diagnostic run `20261001-232843Z-d22ec92b` also received a manual house exit
before the timed route sent its movement. After the three billboard draws,
`NATIVE CORONA QUERY` completed three entries. The original black-fade caller
(`lr=82755FD8`) then entered `requireRigidSelection` with guest declaration
`00F0000D` and VS/PS `E3E968F0/E3E9AA70`, no screen/reset receipt, and a
different nonnull physical shader pair from rigid `8200D734/8200E1BC`.
This identifies the query-to-overlay handoff, rather than the billboard draw,
as the next boundary needing qualification. Failure diagnostics now preserve
the expected/actual physical shader pair and the exact CPU function/LR/frame.
Original `8276AAA0..8276AAE4` publishes query VS/PS/declaration from
`82CF2340/82CF2334/82DFEB34`, mapping to `82152880/82152708`. Cleanup
`8276AC04/AC08` clears the query producer fields without ending the selected
FX or restoring its scene shaders. Overlay preflight occurs before its own
shader setters, so this real completed query needs a retained-manager handoff
proof just as completed screen draws do.

The native repair issues a separate completed-query counter and epoch proof
after the real corona draw and final textured binding restoration. It does
not increment the ordinary screen-draw count. Original manager, camera,
declaration and shader-cache association remain pinned across the original
query vertex loop. Genuine selected rigid, skin and sky queries now precede
the original overlay in all nine shared effect profiles; empty queries remain
zero work. Hardware/WARP receipt negatives cover stale, foreign and changed
bindings, intervening draws and original batch declaration cleanup.

The remaining mono, Z prepass, shadow and edge families are closed before the
stock query stage. Stage-12 pre-callback `82753470` ends the selected manager
at `82753490`, even when the stage has no scene objects; stage-13 mono parent
`8276E0D8` unconditionally enters `82773D40`, ending its manager at `82773D74`.
Successful full shadow parent `82706F48` ends its wrapper at `82707044` before
camera end. Full edge/AA parents `823CA568/823CA188` also end their wrapper.
The registered renderer executes stages in ascending order through
`8269EAB8/8269E980`. These are stock whole-pipeline reachability proofs;
arbitrary custom or partial-range rendering has not been qualified.

Run `20261001-233949Z-0b45f4aa` completed the 25-command route variant with
overlapping game-window input. This variant replaced the observed leading idle
with 12 seconds after fresh clock calibration and retained the remaining
relative timings; it is not a complete timing reproduction. Its viewed capture
still shows the kitchen. The query handoff survived, then the outside scene
dispatcher reached unported `simpsons_uv` (`82042F58`), alpha technique
`0007FFFC`, and rejected `82740680` called from `8273B4E0`. No exterior success
is claimed for this run.

`simpsons_uv` now has its own original opaque VS/PS `8204364C/82044068` and
alpha VS/PS `82043BC0/82044850`. The 132-word original material bank retains
animation VS rows 47/46/45, custom PS row 49, opaque shadow/rim rows 44/43,
and alpha-test row 48. The original ticker callback remains responsible for
private time updates. Base texture leaf 23/word 116 binds stage two for opaque
and stage zero for alpha; only opaque samples both original depth banks.
The geometry declaration uses UV0. This is distinct from dual-textured UV.

The independent Python oracle passes seven tests and generates 160 original
instruction cases, including 12 strict alpha discards and independent depth
adaptation expectations. GPU tests additionally exercise 96 production draw
adapters. Original full dispatcher, recording/replay, partial material updates,
ABI/dirty banks and screen/query/reset transitions have a separate fixture.
The native CPU and hardware/WARP checks pass. At that milestone, live exterior
validation remained pending; subsequent run outcomes are recorded below.

The UV build subsequently passed all 100 selected native tests, including
hardware/WARP numerical transport, original full dispatch, query/reset and
recording tests. Run `20261002-001043Z-23f58073` advanced through the fade,
then exposed unported `simpsons_flipbook` (`82039208`, identity `00500028`)
in alpha technique `0007FFFC`. Its full scene dispatcher rejected
`82740680` from `8273B4E0`; this run is also a failed live retest.
Opaque and alpha flipbook now use their distinct original shader pairs
`820398BC/8203A24C` and `82039D70/8203A644`. Both consume only the original
stage-zero base texture and UV0 geometry. The original callback supplies time
at VS row 22; material animation parameters remain at row 47. Opaque rim
lighting remains at PS row 46. Neither pass consumes alpha-test or shadow
inputs, and neither shader discards pixels.

The independent oracle passes six tests and generates 160 original instruction
cases, plus 96 production depth-adapter comparisons. Both hardware and WARP
checks pass. The first 104-test native run passed 103 tests; the genuine
original flipbook fixture then required source-correct mask and material-upload
assertions. Unused mask bits remain set, and material upload does not rewrite
the CPU matrix staging bank. The pixel-sensitive atlas case uses FPS 4 at
time 0.25: the original shader translates UV by `(0.5,0)`. FPS 8 instead
translates by `(1,0.5)`, which is invisible on this wrapping palette because
its vertical halves are identical. The independent original-word proof is
`build/house-exit-fix/flipbook-atlas-sensitivity.json`. A source review also
corrected opaque sampler and no-shadow admission routing.

Run `20261002-003758Z-d3382697` passed the recap and house loading transition.
After the timed PAD route, root personally viewed the living room, sent a
capture-directed move into the entry hall, and viewed that location. A later
completed frame at presentation `1710152` visibly shows Bart outside in
Springfield. Game-window keyboard/camera input overlapped the PAD route;
the input state log does not identify its authors. This is verified exterior
evidence, not a fully automated door-exit reproduction.

The same run subsequently failed while loading more scene resources:
`Unqualified original audio reader group profile`. Original caller `8233062C`
requested identifier `50544851`, one manager, ring bytes `4BC80`, four entries,
root `E4627DE0`, and zero allocator/auxiliary overrides. The existing native
qualification hard-coded ring lengths `55280` and `47E00`, so it rejected
this new original profile. The saved repair validates the actual producer
`82330540`: its full frame, new stream owner, current identifier, signed bitrate,
retained duration, two single-precision products, `fctidz`, stored conversion
and 16-byte ring alignment. The startup profile remains distinct. All existing
allocation, claim, reset and retirement checks remain. The full-producer fixture
now passes the observed ring and five other source-derived profiles. This run
is not a completed crash-free live test.

The completed reset proof retains its actual COM device, private serial and
shader epoch, and requires actual null VS/PS. It rejects foreign devices,
stale resets, an actual later screen bind, pending UI work, and a destroyed
backend recreated at the same address with coincident counters. Pending UI
is submitted before reset and before logical end validation. A new screen
binding invalidates the preceding reset proof before drawing; completed
original screen calls then issue their own fresh proof.

Reset preflight validates the selected family's original logical and physical
owners before any cache mutation. Completion checks the same incoming CPU
frame and the bounded nested callback backchain. Later end restores the real
original state cache using the completed reset proof. Rigid, skin, sky, mono,
depth-prepass, edge and character-shadow end paths are covered; actual scene
commit/draw paths continue requiring their own shader pair. Character-shadow
completion is observed at original epilogue `82706468`, including deferred
static alpha rows and empty lists. Reset-specific shadow cases cover camera
selector zero; existing parent and depth-copy tests cover both selectors.

The original emitter's zero-texture branch also skips the direct sprite call
and still reaches batch cleanup. Its separate `LR=8276B59C` path is accepted
only with the original frame, selected camera, zero texture, zero retained
register and matching branch condition. The ordinary `LR=8276B6D8` sprite-return
path keeps its existing checks.

## Broader repairs

- A proactive source audit found base rigid `8200CCB8` unnecessarily rejected
  opaque entry flags `(0,1)`. Original dispatcher metadata produces both
  Boolean values, and fallback `82740120` overwrites incoming `r5` before its
  first use. Qualification now accepts all four Boolean pairs for this exact
  source. Existing pass, source, material, caller, frame and draw checks remain;
  values above one still reject. The genuine base-family dispatcher fixture
  passes all four pairs and compares same-pass pixels with full original ABI
  and owner checks. This was a proactively found guard gap, not a witnessed
  live crash. Evidence: `build/house-exit-fix/base-rigid-boolean-proof.json`.
- Colored skin, mono-mask skin and opaque character-shadow paths support
  composed arrays of 1–255 entries with original submesh palette selection.
  Each GPU palette remains bounded to 64 entries. Tests use genuine joint
  owners and original bone maps, composition and group-copy helpers. The
  complete packet and range bytes are pinned before composition, rechecked
  after selection and at draw. Trailing empty ranges and empty shadow rows
  follow original zero-work behavior without dereferencing null rows.
- The grouped mono endpoint preserves original composition, Boolean selection,
  material commit and draw loops. Its native replacement is restricted to the
  hardware bone-array upload at `82700440`.
- Both ordinary and type-5 direct particle vertices now combine with ordinary,
  dual, projected and dual-projected pixels: all eight original selections.
  If the original projector is absent, the original ordinary/dual pixel
  fallback is retained. Independent instruction execution proves the inherited
  projection constants affect only the unconsumed `TEX1` export in that case.
  Texture wrappers, sampler state, caller, matrix ownership and actual packed
  output remain validated.
- Sky now preserves the original opaque/alpha selection and distinct shader
  identities. The authored line texture is separate from the palette. The
  original line callback reads `82D6C7F0`; a missing authored line follows the
  real built-in white fallback. Alpha blending is restricted to the original
  alpha shader pair. Cleanup checks `r30 == run.alpha`, matching the original
  Boolean retained at `82740108` and truncated at `827401C8`.
- Textured rigid alpha shader `820168F8` consumes stage zero only. An unused
  stage-one sampler no longer rejects that selected pass; all consumed sampler
  and texture ownership checks remain strict.

## Sound and resource sweep

`build/stream-catalog-decode/full/report.json` records actual native decoding of
all 9,256 unique complete streamed chains, plus verification of aliases. This
covers all 9,470 streams, 377,643 blocks and 7,430 source files, with zero
decode or framing failures. Source/block hashes, chain order, codec inputs,
native output and current codec DLL hashes are checked by the integrated
audio audit. No sound device or synthetic EOF is used by the census.

The resident SBK report also passes all 9,024 unique encoded blocks and retains current catalog,
probe, codec source and DLL hashes. The AMX census was refreshed against the
current catalog: 254 cues across eight original payloads pass, including 32
bitwise-equal loop reset/redecode checks. Its previous report is preserved as
`build/amx-catalog-decode/report-before-current-catalog.json`. The combined
`build/house-exit-fix/audio-runtime-coverage.json` records zero format/storage
violations for these sound classes.

Default native playback now uses Windows' virtual audio client (`NULL`
device, flags zero), so the system can move playback when an output device
disappears. Explicit endpoint IDs remain pinned. First-error HRESULT and
callback/API origin are published atomically and retained in Dac diagnostics;
unknown errors remain failures. The real default-routing oracle, real explicit
endpoint PCM completion, concurrent error publication, raw `88880001` callback
delivery and Dac propagation regressions pass in both native and Release.
See `build/house-exit-fix/audio-device-loss-audit.md` for the Windows event
evidence, Microsoft API references and the limits of injected-error testing.

The census proves one complete decoding pass. It does not establish live ring
scheduling, seeking, restart/replay or cancellation behavior. The registered
effect audit also keeps missing shader rows separate from observed crashes;
direct particle selections are separate from registered particle FX. Skinned
Z prepass, nonempty character-shadow alpha and opaque VFX emitter fields still
have explicit coverage limits. This repair does not establish all-level crash
freedom or a gameplay performance improvement.

## Regression and live evidence

CPU fixtures execute genuine original dispatch, composition, material callbacks,
manager retirement and subsequent effect draws. GPU fixtures run both WARP and
hardware, compare packed output and test recorded resource lifetimes. Screen
regressions cover original fade and flat/textured screen calls between actual
rigid, skin and sky draws, changed guest caches, stale/foreign receipts and
physical shader corruption.

`drive-exit-replay.py` reconstructs the observed route from 41 keyboard state
changes bounded by adjacent presentation timestamps. It preserves the observed
idle and confirms each of 25 native PAD command acknowledgements. This is a
wall-clock replay with documented timing bounds and release polls, not an
exact controller-poll recording. Timed DPad input has explicit 123 ms hold,
release and original input-manager regression coverage.

The combined native regression run passes 109 renderer/resource tests. The new
full-producer audio fixture then passes 271 checks after initializing the real
original voice-factory allocator, asset manager and clock. All four selected
original audio tests pass together. The fixture executes six genuine
`82330540` producers, including the observed `4BC80` ring, real claims/releases,
deferred retirement, zero-duration behavior and 30 strict negative cases.

The next private run, `20261002-011157Z-d072259c`, completed the recap and
rendered the house but did not execute movement: the route helper rejected
the detailed timing CSV before queuing any PAD actions. That schema handling
has been corrected. While stationary, the game later failed at presentation
`11327` with native Dac stage 2 error `88880001`. Windows audio events record
the Galaxy Buds FE render endpoint becoming unplugged at
`2026-10-02T01:16:04.907075Z`, coincident with this failure. The backend had
explicitly disabled default-device migration. The bounded repair and regressions
are complete, as recorded in the next milestone; this failed run does not verify
the exterior transition.

The default-device output now uses Windows' virtual client routing, preserving
explicit endpoint selection and all callback/buffer ownership checks. Native
and Release each pass the 114-test regression milestone, including the real
audio producer, default/explicit output, Dac error propagation, original rigid
base Boolean dispatch and differential matrix-compose fixtures.

The subsequent private run `20261002-014606Z-8dc29dc2` accepted the actual
`4BC80` audio ring and progressed beyond the house. Completed renderer captures
show Bart's slingshot tutorial and Bartman on a platform in the next mission.
At presentation `43394`, the run failed in the original `826FF388` auxiliary
skin stream branch, with the last original helper at `8270D1C0`. The old native
sampler guard hard-coded a 48-byte stream. The original instructions instead
load the active geometry's stride at `+4` and descriptor at `+38`; dual-UV
geometry has a 56-byte stride.

The repaired guard now requires the active geometry, its exact descriptor,
the original fixed arguments, and a matching qualified 48- or 56-byte stride.
Whole original textured, base and dual skin fixtures execute the auxiliary
lookup and both selected passes. All three pass; seven malformed stream-call
cases retain strict rejection and unchanged private banks. Their source pins
include the complete `826FF388..3B4` instruction sequence.

This failed run proves progression beyond the house, not a crash-free final
build. The proactive material sweep has since added nine exact original passes:
opaque/alpha skin gloss, skin flipbook and skin dual-UV; opaque/alpha rigid
projected texture; and opaque Chocolate. Their private constant banks,
selected samplers, input declarations and actual shader identities are retained.
The native compiler now contains 108 original material artifacts.

Whole original dispatcher fixtures exercise actual auxiliary streams, composed
bone palettes and all Boolean pass flags. Numerical GPU checks execute the
original instruction-derived cases on WARP and hardware. They exposed and
repaired unused alpha inputs in DXBC signatures, the base-skin mixed VS/PS
leaf map, legacy zero multiplication in Chocolate, and projected texture's
four-input declaration. Explicit generated-header dependencies prevent stale
test consumers after shader regeneration.

Skin flipbook's discrete atlas calculation also exposed a one-ULP hardware
reciprocal difference at frame 33. The three vertex reciprocal sites now use
an exact integer rounding correction, with no optional FP64 requirement. The
576 numerical cases and comparison tolerances remain unchanged; both GPU
backends pass. This adds 605 static instruction slots to each flipbook vertex
shader, without changing pixel programs. Static instruction count does not
establish frame-time cost. Details are in
`build/house-exit-fix/skin-variants-stage.md`.

The effect audit covers 49 effects, 110 passes and 256 immutable materials:
47 passes have artifacts and 44 have explicit runtime selections. The two
remaining game-specific table-1 rows are AArow and AAcol. Their original
constructors and setup are known, but a stock queued selection is not proven.
Legacy table-0 gaps and opaque VFX emitter semantics remain explicit limits;
catalog coverage alone does not verify every level.

Final native and Release builds each pass all 138 selected tests, in
175.96 and 118.59 seconds. Both AOT gates verify 311 files with zero semantic
diagnostics. The original profile, 253 content files and video settings match
their before copies. The existing completion shortcut still targets
`build/native/SimpsonsLauncher.exe --first-mission-completion` and creates no
startup UI.

The resulting live run `20261002-032625Z-adf0fc66` completes the outro,
recap and loading screen and reaches Springfield outside the house. The
completed `kitchen-return/native-frame-2106336.png` capture was personally
viewed. The run includes the complete 25-command PAD replay, later game-window
input whose authorship is not recorded, and acknowledged exterior jump,
slingshot and attack commands. No unexpected failure occurs over 371.24 seconds
and 17,959 presentations. Only its verified window is closed with WM_CLOSE.
`completion-exterior-verification.json` preserves this build's receipt.

The user's separate direct Bartman Begins launcher is now integrated. The new
`Play Bartman Begins.lnk` passes `--bartman-begins` to the GUI helper, with its
own private stores and logs. The original `-stream brt brt.str` route bypasses
LOC completion and skips bootstrap movies until a strict original BRT map-ready
receipt; normal later cutscene controls then resume. Both updated builds again
pass all 138 selected tests (117.58 / 112.54 seconds), with AOT 311 files and
zero semantic diagnostics.

Actual GUI dispatch reaches the authored map-ready boundary. A separate current
native run `20261002-035032Z-020063be` is personally captured in the opening cave
and completes eight acknowledged movement/jump/weapon/interaction/character
inputs without an unexpected failure over 443.21 seconds and 12,602
presentations. Its owned window is closed normally. All 254 original store
files and video preferences remain unchanged. This exercises the opening area,
not a complete mission or every level. See `bartman-begins-launcher.md` and
`build/house-exit-fix/final-live-verification.json`.

The complete streamed-audio decode census now covers 9,470 catalog streams,
9,256 unique chains and 377,643 blocks from 7,430 source files, with zero
decode failures. Duplicate chains and blocks are reverified. This proves one
complete introduction/loop-body decode through the actual runtime parser and
codecs; live seek, restart and cancellation scheduling remain separate limits.

## Performance evidence

The mixed moving-gameplay segment in `20261002-003758Z-d3382697` contains 4,557
consecutive accepted presentations with positive scene geometry. At the user's
retained 3440x1440, AA-zero settings it averages 31.71 FPS, with median frame
time 27.034 ms and p95 49.621 ms. This is not a controlled comparison and does
not identify the present CPU or GPU bottleneck.

The recording graph now reuses the checked pointer returned by its full-pool
`region` validation, removing a second identical `runtime.pointer` call.
Every owner, address, permission, header and live-byte check still executes.
This localized change has no measured FPS claim. The subsequent private run
enabled detailed timing buckets and bounded native CPU sampling. Sampled frames
carry diagnostic overhead and must be excluded from FPS comparisons.

The next primary-thread sample found checked guest-pointer helpers accounting
for 25.6% of exclusive samples. The hot matrix-compose entry `823F2D98` now
uses the existing checked inlining mechanism, retaining all 75 loads, 12 stores,
volatile access, byte order, floating-point operations and fault ordering.
Differential tests execute both the exact original body and compiled entry,
comparing complete registers, memory, aliases, FP modes and partial failures;
they pass in native and Release.

The new live run's stationary 35–50 second house phase qualifies against the
earlier run with identical source-store hashes, video settings, camera eye and
personally checked view. Complete accepted intervals average 41.1224 FPS,
versus 34.6861 FPS in the reference (+6.4363 FPS, +18.56%). Capture occurs before
the measured phase; neither phase includes recorded movement or sampling.
This is one observational comparison of the combined build, not an isolated
matrix benchmark or a first-level FPS result. Earlier unexplained time-dependent
drift remains a limit. The immutable snapshot report is
`runs/20261002-032625Z-adf0fc66/stationary-house-timing.json`.

The final direct Bartman run still shows uneven performance: two unprofiled
windows average 31.41 and 26.16 FPS. Its bounded primary-thread sample places
10.74% of samples in checked guest-memory access, with graphics allocation and
file-system calls also prominent. These sampled positions include blocked
calls and do not establish a CPU/GPU bottleneck or a controlled improvement.
Exact receipts and timing exclusions are recorded in
`build/house-exit-fix/bartman-performance-assessment.md`.
