The qualified `RenderShadowDepthAlpha` path executes the complete original
`82706130` CPU function for an empty deferred-caster queue and binds its real
compiled shader pair. Nonempty queues fail with
`Character shadow alpha queue is not yet qualified` before effect mutation.
Live `reach-game-007` and completed gameplay remain separate validation work.
All addresses, offsets and handles below are hexadecimal.

Let `T` be the typed shadow owner, `W=*(T+18)` its wrapper, `M=*(T+10)` its
manager, and `P` the attached shared pool. The original empty closure is:

1. `82706148` calls `826B6078(W, *(T+5E0))`, which reaches `826B5FC0` with
   LR `8270614C` and technique `0003FFFC`. An opaque-to-alpha transition ends
   the previous technique through `826B4628`, then selects manager fields
   `M+4=W`, `M+8=identity`, `M+C=0003FFFC`. Original `826B35D8` saves/applies
   technique-index-zero state and selects `W+2C=cache`. Scalar SDK offsets
   `28,30,60` receive `1,1,1`; stage-zero sampler offsets `0,4,8,10,14,18`
   receive `0,0,0,0,0,2`. The native pair is VS `820C1E6C`, PS `820CA530`.
2. `82706158` calls `823C8EB0(W, *(T+678), 1)`, LR `8270615C`, with
   `r31=T`. Handle `00340016` identifies `kIsAlphaTested`, descriptor
   `00200008/00010011`. Low-byte Boolean conversion writes float `1` to the
   first lane of private vector slot `11`, preserving neighboring lanes;
   its leaf dirty bit is private byte `1`, mask `10`.
3. `82706164` calls `82705AA0(T, 1)`, LR `82706168`. Its arguments are
   `r3=T,r4=Boolean`, not wrapper/handle/value. Retained helper `827225B0`
   supplies the shared pool. The helper writes float `1` at `T+D4` and,
   through `*(T+680)=002C0015`, to `kIsShadowReceiver`, shared vector slot
   `13` at `*(P+108)+130`; it sets `P[1] |= 20`. Other lanes remain intact.
4. The loop reads queue storage/count at `T+A8/AC`. Count zero reaches the
   original epilogue without shader-constant commit, texture/mesh binding,
   draw, queue reset or deallocation. Both Boolean writes and dirty masks
   remain pending. The parent calls original wrapper end `826B4B18(W)` at
   `82707044`, LR `82707048`: `826B37B8(W+14)` restores application state,
   clears `W+2C`, `M+4` and `M+C`, and retains `M+8`, parameter values and
   dirty bits. Camera end follows at `8270704C`.
5. Outer parent `82707220` selects destination `*(T+F0)` or `*(T+F4)`
   using `r26=0/1`, then calls the complete `82704BE8` helper at `827073B8`,
   LR `827073BC`. The helper allocates `80` bytes below the parent's `E0`
   frame. At the mid-BL hook `82704C2C`, LR is still `82704C00`, saved
   caller LR is at `SP+78`, and saved owner is at `SP+74`; `r24` retains
   the selected camera. Arguments are `r3=context`, `r4=4`,
   `r6=r31=destination`, `r5/r7..r10=0`, `f1=+0`, and stack `+5C/+64=0`.
   Flags `4` copy the entire 1024-by-1024 depth/stencil resource without
   clearing the source. Both original roles have format `1A220197`.
   The native backend performs a same-format resource copy, retaining
   source/destination ownership through its GPU event. Submission publishes
   destination phase `Uploaded`, increments `copyCount`, and advances LR
   to `82704C30`; the original helper epilogue restores the caller ABI.

The copy helper first calls `823EE8F8`, whose retained `823EDD38` clears
the five CPU attachment-cache words `82D0CF58..82D0CF68` at
`823EDD60..80`. Actual native attachments and effective render-state values
remain unchanged. Copy validation requires these cache words to stay zero
and verifies the owned camera attachments with `requireSelectedTargets`.
It also requires no active manager effect and the matching destination
owner/field. Original camera end precedes the copy, but qualification uses
the retained actual binding rather than requiring global camera fields to
be zero. A later copy rejection still retains the earlier CPU cache reset.

On this shader-context transition, original `826B1FB0` clears the private
128-byte dirty line; live `FX+120=2` causes one 16-byte `FF` block, leaving
112 zero bytes. Shared initialization at `826B1FF0..204C` runs when
`*(82D00F80)==0 || *(liveFX+2B8)==0`: clear 128 bytes and fill
`16 * ceil(*(liveFX+124)/2)` bytes with `FF`. The metadata body's zero
`+2B8` is not the live attachment state: writer `82C1D3BC` stores `r28=P`
there, while `82C1D334..338` copies `P+114` into live `FX+124`. This owned
shadow pool is nonzero; metadata `+124` and pool `+114` are both `1`.
Therefore only global zero reseeds shared dirtiness here, with 16 `FF` bytes
and a zero tail. Global nonzero preserves the line. The receiver helper
subsequently sets its bit in either case.

State restoration saves application values, not a native-state snapshot.
`826B36BC/36D4` stores the application sampler getter in row `+C`.
End `826B3850` calls `826B79E8` with `r6=0`, forwarded as **force=false**.
`82723CC4..CD4` skips the SDK setter only if the cached request already
equals the saved value; otherwise `82723D04` calls it immediately. The
native `applicationSampler(..., apply=false)` is preflight; the actual
SDK callsite publishes with `apply=true`. Begin also applies direct SDK
literals, so these six rows finish at their saved application values.
The passing fixture saved `2,2,2,1,1,2`, with prior native values
`0,0,2,1,1,2`: five restore misses publish saved values, while mip's hit
retains `2`. Neither unconditional prior-native restoration nor
unconditional retention of all alpha literals describes the original.

Static shader evidence pins a 544-byte PS record with 64 literal bytes and
a 96-byte code region including its 12-byte trailer (84 executable bytes).
The PS returns `(1,1,1,1)` when `c41.x` is zero; otherwise it samples
texture/sampler zero at normalized UV and replicates sampled alpha to all
four lanes. It contains no discard, saturation or depth adapter. Alpha VS
and opaque VS `820C2FA0` have identical 696 executable bytes and 64 literal
bytes; only the first unexecuted trailer word differs.

The six focused tests passed September 13, 2026, at 15:14 Eastern.
After copy integration, the expanded camera test passed again at 15:24:
**1,490 checks in 1.47 seconds**, recorded in
[LastTest.log](K:/SimpsonsNativeCopy/build/native/Testing/Temporary/LastTest.log)
and the [focused run log](K:/SimpsonsNativeCopy/build/reach-game-shadow-copy-tests.log).
The results below distinguish that expanded test from the earlier shader
and mesh runs; full-suite and live-game completion are not established here.

- `NativeShadowAlphaShaderWARP` and `NativeShadowAlphaShaderHardware` each
  submitted **77 actual draws**, checking **1,232 exact RGBA lanes**
  (**1,234 total checks**). Hardware was an NVIDIA GeForce RTX 3080 Ti.
  Offline compiled PS tests cover zero/nonzero predicates, unbound zero
  branch, wrap/clamp, point/linear filtering and unsaturated alpha. Their
  probe VS isolates PS output; these are not deferred game-caster draws.
- `OriginalShadowAlphaShaderEvidence`: **10,122** mutation/extent checks.
- `OriginalShadowCameraPass`: **1,490 checks** (expanded from 1,212), including the preserved
  two-draw manual mesh fixture, original empty-alpha transition and wrapper
  end, Boolean neighbors, exact dirty tails, unchanged queue/pixels, zero
  additional draws, ABI, manager/cache ownership and pre-mutation nonempty
  rejection with pending dirtiness untouched. Four actual depth copies
  cover both selectors through synthesized caller frames plus both complete
  original empty parents, including repeated copies into `Uploaded` owners.
  Readback compares every depth bit and stencil byte, including actual
  nonzero mesh depth; the unused X24 bits are excluded. Source, sibling,
  border, color, effective state and binding remain intact. Rejections
  cover an active alpha effect, wrong destination/camera/selector, caller
  LR and backchain. Seeded stale attachment-cache words must become and
  remain zero on both success and rejection.
- `NativeShadowMeshWARP` and `NativeShadowMeshHardware`: each **125,770
  checks and 229 native draws** for the existing opaque mesh path.
