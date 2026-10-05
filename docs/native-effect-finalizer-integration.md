# Original effect finalizers and native shadow sampler values

Build157 retains the same62 passing suites (86.16 seconds) and adds an explicit
entry stop at8273C2B8. Actual muted boot099 reaches it at caller826FF198 after
all47 common reflections. This establishes the zero-flag, N=16 cubemap path.
No constructor mutation or SDK device read occurs at this stop. The new bounded
cubemap evidence records20 spans/1,472 words and23 instruction mutations in
`build/reflection-cubemap/`. It finds the paired destructor8273C000; no direct
caller was found, so normal global teardown remains unresolved.

Build156 passes62/62 suites in86.11 seconds. The complete finalizer integration
passes17,605 checks; the isolated common gateway passes80,995 checks. Actual
muted boot098 executes all47 common reflections and the shadow sampler tail,
then leaves the original manager successfully. It reaches device AddRef82451ED0,
caller8273C2FC, in the next reflection-cubemap constructor8273C2B8. Reading the
native context00900001 as an SDK device fails explicitly at0090003D. This new
constructor's resource/retain lifecycle is not yet implemented.

The original manager826B7218 is enabled. Its original linked-list walk and
virtual dispatch execute all48 typed effects from the two registration tables;
particles has no typed callback. The final four genuine CPU pool lookups still
publish manager+21C/220/224/228. General effect application and shader binding
remain unqualified. This change establishes CPU setup, not original game pixels.

The independent fixture generator reads the exact original image, follows
constant query names and result-to-store instructions, and records196 handle
publications across48 profiles and17 derived finalizer bodies. Some second-table
finalizers publish globals instead of typed-object fields; those destinations
and actual manager iteration order are checked too. Together with the manager
and three shadow helpers,21 complete function spans are recorded. The unchanged
first25 source proof checks73 spans/4,889 words and16 instruction mutations.

The original quad finalizer826B7650 contains17 technique and8 parameter queries;
it does not set sampler values. All other derived finalizers also retain their
original lookup/publication code. Common reflection is the prior native gateway.

## Shadow sampler boundary

The only new runtime hook starts at82706BD8 and resumes at82706CDC. The260-byte
extent (SHA256ca273f71fe02892ffd38e6fe6eb5b6d78088ad97ea10a7a2e9c8a9c5b2960804)
contains two inline SDK FX sampler stores. The hook updates native private
default slots277 and278 for handles0148009E(kShadowBackDepthSampler) and
014C00A0(kFirstDepthSampler), using the actual typed owner's F0 and F8 texture
identities. Both descriptors are exact leaf word0000000C. Only the first word
of each16-byte storage slot changes; other default words remain intact.

Original modified bits use MSB-first leaf ordinals79/80 in F+0..7F. A separate
native128-byte mask owns these bits, initially copied from the source prefix.
They must not be confused with common reflection's F/P+80..FF masks or immutable
pass usage. Repeated stores OR the existing modified bits.

Preflight checks the live FX/wrapper/manager/table identity, original published
handles, all three native shadow resources, borrowed driver depth role, real pool
provenance and bit LUT. Invalid handles or a stale borrowed depth fail before
native value/mask changes. The call check uses last-entered query823C7CA0 and its
original LR82706BC8; lastFunction is a diagnostic last-callee marker, not a stack.

The skipped block's r27 value is dead until ABI restoration. Original code
overwrites r30 at82706CE4 and r28 at82706D08 before reading either. The hook keeps
r29 (82061428 LUT) and r31 (typed owner). Getter827225B0 ignores its inputs and
loads the genuine global pool. All subsequent pool queries, three sampler
stores and three vector/scalar helpers execute as original AOT code.

The retained pool stores update slots15/16/17 with F0/F4/FC. Helper827058B0 and
827059B0 write slot18 as the four-word vector [T+CC,T+C8,T+D0,0], preserving the
original VMX packing. Helper82705AA0 with zero writes zero to T+D4 and slot19.
Original P+0..7F modification bits6..10 are ORed. Complete pool/default buffers
are checked with distinct finite vector components; unrelated words survive.

## Lifetime and verification scope

The original inline stores do not AddRef the texture pointers. Native values
likewise borrow identities owned by the typed shadows resources/driver. No
synthetic SDK texture object is allocated. Later shader binding must resolve
these identities through native resource ownership and establish any additional
GPU lifetime. It is not enabled by CPU sampler publication.

The integration fixture executes each derived finalizer twice, then the full
original manager walk, in each of two cleanup cycles:192 direct plus96 manager
dispatches. It checks all publication destinations, exact common call counts,
private defaults/modification bits, shared values/masks, manager fields, ABI and
both cleanup orders. The prior isolated common-gateway fixture still checks
188 calls and entire poisoned output buffers. The manager guard assertion was
removed from that isolated test because the full closure now has its own test.

Original normal cleanup retains the previously documented four raster
associations per cycle and unproven camera state-extension ownership. Shared
sampler words also remain after original typed cleanup, as in the original CPU
code; later rendering must not dereference retired native identities. Repeated
registration/finalization replaces them with the new owners. This is not a
claim that application/global teardown or later draws are complete.

Reproduction: `python -B tools/generate_effect_finalizer_fixture.py`,
`python -B build/effect-finalizers/verify.py --test`, `.\tools\build.ps1 -Jobs 8`.
Evidence and test-only fixtures live in `build/effect-finalizer-integration/`.
