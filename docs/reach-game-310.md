# Reach-game310 — rigidalpha runtime integration

Goal remains active. Source8200CCB8 now executes its original alpha pass with
native shaders and its base texture. Runs310c and310d complete29 scene
presentations, six alpha fallback packets, and then stop at nonempty cached
record cleanup827374B0. The new scene readback remains visibly malformed.
Sustained rendering, opening-movie completion and character control remain
unverified. This follows [309](reach-game-309.md).

## Integrated and exercised

- The material compiler admits the exact original VS8200D734/PS8200E1BC
  records using309's offline transcription. It rejects mixed pairs. Native
  compiled population is50, with194 remaining unsupported original records.
- CCB8 keeps both opaque and alpha techniques. The actual begin request
  selects rigid0003FFFC or rigidalpha0007FFFC. Active selection checks,
  texture transfer and end/restore now follow the selected shader pair;
  CCB8 was not globally changed to an alpha-only source. A change between
  passes ends the old original cache selection before beginning the next.
- Live entry validates typed+A8=0003FFFC and typed+AC=0007FFFC. The alpha
  fallback arguments are r4=1,r5=1, with the existing original r6 branches
  preserved. The first packet is82D6EBEC, typedE1AA88F0, identity0050001B,
  wrapperE1AA8710, context00900001. Original CPU activation, mesh loop,
  per-object staging, material callbacks and cleanup continue to execute.
- Alpha context2910 selects the six original stage0 sampler rows. A new
  pass-specific metadata query complements typed reflection, which still
  uses the original front-pass classification. The selected alpha pass has
  zero usage for all three shadow rows. Their original lookup/loop still
  executes; no shadow resource is bound for those unused rows.
- Private g_BaseSampler handle00500020, leaf16, word88 resolves through the
  retained ITXD header. Its stage0 binding is checked against the selected
  metadata. Live runs upload its16x16 BC3 image and complete the draw.
  The new renderer samples RGBA base data; it applies no depth-RRRR adapter
  to that texture. Stage0 sampler state is explicitly linear/wrap.
- Alpha draw metadata explicitly marks shadow sampling as unused. The
  backend rejects missing base textures, unexpected shadow owners, incorrect
  policy and invalid sampler state. The original packed blend07060706 maps
  to source-alpha/inverse-source-alpha for color and alpha. Existing native
  depth quantization, viewport, depth/stencil checks and state restoration
  remain in force. Immediate and recorded backend paths support the pair.
- Runtime recorded-alpha constant replay remains guarded until its original
  replay mask/path is qualified. The observed game path is immediate fallback.

## Camera constant correction

The first310 run reached material commit and rejected the new assumption
that the shared world-eye parameter must be material-eligible.310b shows
handle00080003, leaf1, usage2, PS register4/count1, eligibility00 and dirty00.
The completed original PS staging bank already contained nonzero camera
coordinates (C0F25ED9,40A95FAE,C13C70F2,00000000).

Shared reflection exclusions accumulate across materials. When world-eye
is excluded, the native commit now retains the original staged c4 rather
than issuing another material update. It verifies the consumed XYZ bits
against the original source, the active effect manager+230. Original
WORLD_EYE_POS callback8270C290 reads this address before tail-calling
8270A370. The unused fourth staged lane is preserved. A material-eligible
case still uses the filtered dirty bit and owned pool storage; uninitialized
or nonfinite camera data rejects.310c/d pass these checks and complete all
six observed alpha packets.

Other consumed constants retain their existing original sources:
view-projection c0..3 and object c12..15 in VS, object-ID c40.w from original
staging, and private custom-line c49.xy. The shader's independent arithmetic
qualification remains documented in309.

## Verification

Latest game build310d succeeds after AOT regeneration:311 files,zero semantic
diagnostics. Logs: `build/reach-game-310d-regenerate.log` and310d-build.log.
Generated C++ was not edited. Original assets/reference trees remain unchanged.

Seven focused tests pass in `build/reach-game-310c-tests.log`:

- Native rigid-alpha shader WARP/hardware and original shader inventory.
- Native rigid mesh WARP/hardware, including the new real alpha mesh tests.
- Native original material artifacts and original engine resource bridge.

The mesh tests check original pass-specific shadow/base/eye mappings and
reject an unknown technique. GPU cases check slot0 base color, both silhouette
and translucent outputs, color/alpha blending, depth/stencil, unchanged sibling
targets, immediate/recorded output parity, state restoration and resource/state
rejections. Earlier shader tests independently cover transforms, view-angle
arithmetic and zero directions. The first310 mesh-test attempt stopped on an
incorrect expected diagnostic substring; correcting the expectation from
"sampler" to the actual "linear/wrap" message leaves behavior unchanged.

310d only improves texture-transfer trace accuracy and adds read-only cleanup
diagnostics; the renderer and tested metadata behavior are unchanged from310c.
The full suite was not rerun. Existing unrelated defects remain open.

Run310d:29 scene presentations, last1238,1165 cumulative scene draws and21
new draws. Run310c has the same scene counts, last presentation1290. Both
stop at827374B0 called by823FB3DC. No game process remains running.

An extra request after the first completed alpha packet captured310c's
presentation1285 at1060 cumulative scene draws:
`build/automatic-startup/reach-game-310c/captures/native-frame-1966416.rgb10a2`.
Preview: `build/reach-game-310-preview/native-frame-1966416.png`.
The visible HUD, chocolate mountains and particle effects do not establish
playability: the main world still has broad pink surfaces and malformed black
geometry. No visual-correctness claim is made for the complete scene.

## Next nonempty cached-record destruction

310d's preflight captures the original state without unlinking or releasing:

- ObjectE1BE89C0, plugin offsetB0, flags10, SP0203EFD0, LR823FB3DC.
- Four heads:0,E2ED1E0C,0,0. Slot1 corresponds to object+B4=E1BE8A74.
- NodeE2ED1E0C words0..30:
  E2ED1994,E2ED0C94,E1BE8A74,0,0,648,0,E89940A0,0,0,00600106,2,0.
- Payload00600106 is a completed CCB8 opaque record, typedE1AA88F0,
  metadataE89940A0. It has13 successful native replays/executed draws before
  destruction. Its CPU scratch pointer+30 is0, so this instance does not
  request the separate CPU allocation free at82737424.

Current827374B0 hook only accepts empty plugin lists. It must be extended to
validate and retire known records. Original827374B0 walks each of four heads
and calls82737400 until the corresponding list is empty.82737400 currently
has a separate explicit unimplemented guard and contains these operations:

1. Free optional CPU allocation+30 with8269BF10 and clear+30/+20.
2. Release nonzero resource+28 at82441708; for this record it is native
   payload00600106, not a guest COM pointer. Set the released flag and clear+28.
3. Update the object-list backlink and next link; clear node+8/+C.
4. If a payload was released, call826F4BE8 to update manager LRU/accounting.
5. Return the CPU record to its pool with826F39B0; finish at827374A4.

Preserve those original list/accounting/pool operations while introducing
native payload retirement at the resource boundary. Both ownership registries
need coordination: EngineRecordingOwners::State::payloads/nodes/lru/freeOrder
and EngineEffects::State::rigidPayloads (or skinPayloads for that family).
Native replay-constant aliases must not remain valid after retirement.
Current graph validation also tracks allocation-order node links, manager
history/bytes and the free-list order; it cannot simply ignore the deletion.
Do not zero the plugin head or bypass its destructor. Capture additional
original deletion inputs as needed, then regenerate before building/running.
