# Reach-game305 — cached sky drawing and inherited sampler state

Goal remains active. Scene output is malformed and character control is not
verified. This checkpoint continues [301](reach-game-301.md).

## Sky cache ownership

The native renderer now implements the original recorded sky traversal:
82701448 mesh entry,826B5618 material entry,827015C0 draw and82701628 closure.
The existing immediate path remains separate. Original row classification,
parameter callbacks, dirty filtering, cache accounting and context restoration
execute in their original order; no forced immediate fallback was added.

`renderer/sky_mesh.cpp` records native D3D11 command lists retaining geometry,
four texture owners, shader objects, targets and fixed state. Each draw owns
distinct DEFAULT constant buffers. Each cached payload owns separate live sky
constants, initially unready. Original replay's two56-register uploads supply
the live values; the reflected first-pass mask chooses inherited groups at
execution. Material cloud-velocity rows46/47 stay in each draw's snapshot.
Preparation rejects unready, released, foreign or nonfinite owners and does not
invoke original CPU code. Recording neither renders early nor changes immediate
bindings.

The runtime keeps sky payloads in the existing retained rigid-family cache,
with a distinct64-row VS owner rather than widening the47-row rigid bank.
Sky material accumulation and texture references survive the context swap.
Shared dirty filtering now follows the original pool eligibility mask, as the
rigid implementation already does.

Run302 reached and sealed payload006000FA,2492 indices,1880 owned constant
bytes, then hit the older replay source allowlist. Run303 admitted the sky's
new owner and executed this same native payload six times. The original
packet, effect, metadata, camera and cache receipt checks remain enforced.

## Next reached sampler state

Run303 subsequently stopped at `Rigid inherited sampler state is unqualified`
in an immediate168F8 alpha draw. Run304 captured the exact mismatch:

- Source820168F8,VS8201739C,PS82017E4C,typedE1AABB30.
- Packet82D6DCBC,geometryE9866E58,materialE98671D0.
- Stage1 words: `1 1 2 0 0 0 2 0 0 1 1 1 0 13 0 0 0 0 0 1`.
- Prior admitted stage1: `2 2 2 0 1 1 2 0 0 1 1 1 0 13 0 0 0 0 0 1`.

The alpha PS only samples stage0. Stage1 inherits preceding state and cannot
be required to equal one earlier draw's state. Both observed profiles now
retain their actual native descriptor: point/mirror U,V/clamp W, or
linear/clamp XYZ, with base-level sampling. This also corrects the former
stage1 mapping, which had mistakenly treated address fields as filter fields.
The SDK field identities are pinned in `renderer/engine_state.*`; original
8243C1A0 inserts address bits and8243BD84 inserts mip bits unchanged. Read-only
reference `K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h` identifies
address1 as mirrored repeat and mip2 as base map. No reference code was edited
or introduced as a runtime decoder.

## Validation and limits

- Regenerations302..305:311 AOT files, zero semantic diagnostics. Generated C++
  was not edited.
- `build/reach-game-305-build.log`: game,sky and rigid tests build.
- `build/reach-game-305-tests.log`: six focused tests pass: sky WARP/hardware,
  rigid WARP/hardware,sky vertex decode and exact original sky inventory.
- Sky tests use SHA-qualified original shaders and the actual original
  reflected first-pass mask. They check pixel parity with immediate rendering,
  independent live owners, differing cloud velocities, per-replay ticker
  changes, resource retention, state preservation and invalid lifetimes.
- A filtered midpoint differed by one RGB10 code between WARP and hardware;
  the test permits that one-code midpoint tolerance while keeping endpoint
  colors and immediate/recorded parity exact. Initial304 hardware failure and
  the successful305 result are retained in their test logs.
- Rigid tests execute both newly observed native sampler profiles against
  owned copied depth, checking pixels and immediate-state preservation.
- The complete suite was not rerun; earlier broader failures still apply.

Run303's automated check reported six frames over1.15 seconds, then the game
failed at the sampler boundary. That is not sustained gameplay. The startup
observer now requires at least30 captured scene frames over10 seconds before
reporting sustained rendering; character control remains a separate criterion.

Reviewed frame: `build/automatic-startup/reach-game-303/preview/native-frame-1884944.png`
(presentation1245). HUD placement is intact but world materials are flat,
large areas are black and the pink sky occupies the lower portion. Rendering
orientation/depth/material correctness still needs investigation. In
particular the current sky PS does not consume the bound depth adapter
constants; these tests do not claim a verified reversed sky-depth policy.

Run305 passes the sampler boundary and stops at a new music source:
`Unqualified EXm0 streamed format/start/seek or source ownership` at823424D8,
return823424D0. Header0314BB804002C442 is loc_mus stream0,idB52A088E,
181314 frames,36 blocks. Its first2913-byte reader-normalized block hash is
`b7aaa9c0a0928089137d1d5d520890b1aecc3aaea5d422afc52ab8f5b616d967`.
This exactly matches the original asset; no existing loc music certificate
was present. The source is six-channel48kHz,nonlooping,with no seek offset.
`qualifiedEaXmaFrames` returns zero until the new bank is independently
qualified. The bank SHA256 is
`b350a5b1ce17ff5b53a2b0a6c035d9c36eafbf213252dad07b2c387f6c0743fa`.
The strengthened startup observer correctly reports no gameplay success.
