# Native first-effect ownership

The first original registration row, `fourtapblend` at `82CEFD20`, now owns a
native effect resource. The implementation retains the original CPU wrapper,
name, manager registrations, typed constructor/finalizer and paired destructors.
It does not create an SDK effect layout or execute serialized GPU contexts.
The other 24 registration rows and all effect application remain unqualified.

## Original and native responsibilities

`827019E8` still allocates the real 30-byte wrapper, copies its name, invokes
the typed callback, and publishes both registrations. Only its SDK creation
call at `826B4BAC` is replaced. The original body resumes at `826B4BB0` and
calls the native reflection boundary at `826B4828`. Production still visits
all 25 rows; no count or conditional branch is shortened.

`runtime/engine_effects.cpp` owns a complete immutable copy of the exact
3,244-byte blob at `820B8AA0`, with its full SHA-256 checked before publication.
It parses the original technique name, two private parameter names/handles,
20 default vector words, eight scalar and five sampler states, and the two
shader associations reached through the original pass context. Unknown
profiles fail before a native identity or resource is published.

Each native record owns its own `MaterialRegistry`, two separately owned
original shader records, and two real D3D11 shader artifacts made from offline
FXC output. Construction failure destroys any partially acquired native
resources. The registry now recognizes 22 exact original shader identities:
six have qualified native shader bodies; sixteen remain explicitly unsupported
at preparation. Shader creation is not declaration, binding or draw readiness.
The arithmetic and sampling limits are recorded in `fourtap-shaders.md`.

Reflection allocates the real original C8-byte CPU cache through `8269BE40`.
It reads the live scalar/sampler mapping tables and retains the original
24-byte technique row, eight 12-byte scalar rows and five 16-byte sampler rows.
Saved previous-value fields remain unwritten. The original wrapper stores and
active-row default are retained. This supports fresh reflection only; it does
not qualify rebuilding an active cache or applying its inherited render state.

The original typed finalizer `8273B3B8` still looks up the wrapper and obtains
its native identity through the original virtual getter. Two callsite hooks
replace SDK-layout name queries. Original code stores its four final outputs:
wrapper, native identity, technique `3FFFC` and sampler parameter `180008`.
The native names also resolve `g_Weights` to `40000`; missing names return zero.
No SDK reader is permitted to dereference the opaque identity.

The wrapper destructor's call at `826B393C` retires the owned shader artifacts,
records, defaults and native identity. Original following instructions clear
the wrapper reference and free the CPU cache, name and object. Original paired
registration cleanup deletes the borrowing typed object before the wrapper.
Native identities are never reused, even when the original heap reuses storage.

## Actual original shared pool

The original static constructor creates the genuine aligned CPU pool before
the native driver exists. A continuing observation at `82C1CEDC` records its
root/thread provenance after successful construction. Native creation verifies
that provenance, the original allocation backpointer, manager/global aliases,
initialized empty metadata and root reference count1. Uninitialized pool padding
is neither zeroed nor required to be zero.

Each native effect retains a host lifetime association with that root. It does
not pretend to be an SDK effect or increment the SDK effect-reference count.
Original pool retirement through `82CC1820`, `82722568`, `82C1CF00`, and its
tail-call alias `82C1D0A0` rejects before mutation while native leases exist.
Afterward the entire original release body continues. Driver stop likewise
requires no native FX owners. This empty-pool policy does not qualify shared
parameter merging, SDK clone/get-pool operations or complete CRT shutdown.

An empty FX owner must also permit rollback of a partially constructed driver.
Build144's first restored-audio run exposed an overly strict cleanup check
that required the driver to have been published. The check now verifies only
runtime/thread and empty ownership; normal effect operations still require
the live native context. OriginalDriverLifecycle exercises the failing-start
rollback as well as normal start/stop.

## Executed evidence and limits

Build146 passes all 51 CTest suites in 75.73 seconds, including the corrected
partial-start rollback. Executable SHA-256:
`3cfbf5765773542e533e990f48504ef645b705fcf5825c3655100e8baba70876`.
Actual muted boot089 repeats the first-effect/second-effect boundary below.
Logs: `build/native-first-effect-146.log`, `build/boot-089.log`.

Build144's isolated original lifecycle passed 1,827 checks over two real
manager/registration/finalizer/destruction cycles. Build145 adds actual shader
ownership and stale shader-owner checks: 1,831 checks pass. Both cycles reuse
the original wrapper/typed/cache addresses while receiving distinct native IDs.
One cycle changes valid mappings through the original table setters, proving
reflection uses current state. All four premature pool-retirement routes fail
without changing the live pool, manager, wrapper, cache, table or entry stack.
There is no aggregate heap-leak or unwritten-cache-byte-pattern claim.

Actual muted boot088 creates wrapper `E1A9C9F8`, native effect `00500001`,
cache `E1A9CA30` and both native shader objects. It then rejects the second
registration, `littextured` blob `820D5730`, wrapper `E1A9CBF8`, before native
publication. Its caller remains original `82701A94` in `826B4B88`. Terminal
shutdown releases host backing but is explicitly not original full cleanup.

The fixture uses count1 only as a bounded test of the actual registration API.
The game remains on its original 25-row loop. Begin, commit and end boundaries
remain guarded. No original effect draw, screen, menu, world or gameplay has
been demonstrated. Next work is the remaining metadata/shared-pool contracts
and verified caller, declaration, texture, constants and inherited-state closure.

Reproduction:

```powershell
.\tools\build.ps1 -Jobs 8
python -B tools/analyze_fourtap_shaders.py --verify --self-test
python -B build/first-effect-contract/verify.py
python -B tools/run_native.py --timeout 20 --log build/boot-next.log
```

The full build includes the original effect lifecycle and standalone WARP and
hardware shader tests. Automated runs remain muted. Missing audio endpoints
are reported as actual XAudio2 errors, never replaced by a successful fake graph.
