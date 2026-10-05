# Native startup camera rasters

Status: bounded evidence complete and frozen. Only this document, its analyzer and its
JSON are owned. Original instructions are authority; no runtime or renderer edits.

**Implement the observed positive-size root camera type 2, then first shared
depth type 1. Keep type 5, private depth and subraster creation unsupported.**
The original CPU wrappers `82408130/82407DC0` remain intact. The relevant
platform entries are `823F7070` create and `823F62A0` destroy.

## Observed path and ABI

`build/boot-033.log` records raster `E1A52960`, flags `00000002`, width/height
1280/720 decimal, input depth field 0, extension offset `34`, at create entry
with LR `824081C0`. It stops before executing that callback. The following
type-1 request is **statically proved, not yet observed executing**.

`82867A48 -> 827142D8` creates the camera. `8271431C` calls
`82408130(width,height,0,2)` on this branch and stores the result at camera `+60`.
`82714338` calls the same wrapper with flags 1 and the incoming depth argument,
then stores its result at camera `+64`. Nonzero low byte of the constructor's
r6 would select type 5 instead of 2; that branch is outside the bounded port.

Create ABI: **r3=0, r4=R raster, r5=flags; r3 return Boolean**. Destroy ABI:
**r3=0, r4=R, r5=0; r3 return Boolean**. These are engine callback slots E+58/+5C,
not SDK methods. E is `BE32[82D0CA68]`. Hexadecimal is used for addresses,
offsets, masks and field values below; dimensions above are decimal.

Let **X = R + BE32[82E3DC94]**. The plugin `040C` is registered with size `20`
by `823F52C8`; the current `34` offset is observed, not a universal constant.
Its ctor/dtor `823F52B8/823F52C0` are bare `blr` instructions. They do not zero X.

Verified live-registry preflight: registry `82CD1E28` has `+00` total allocation
size, `+10` head, `+14` tail. Each readable `3C`-byte record has `+00` object
offset, `+04` requested size, `+08` ID, `+20` ctor, `+24` dtor, `+28` copy,
`+30` next, `+34` previous, `+38` registry owner. `82407EC8` forwards registration
to `823FB098`; its actual stores at `823FB24C..320` establish this layout.
Constructor traversal is head/next (`823FB33C/364`); destruction is tail/previous
(`823FB3BC/3DC`). Require one matching `040C` record, size `20`, exact original
ctor/dtor, owner `82CD1E28`, offset equal to `BE32[82E3DC94]`, and a bounded,
acyclic consistent chain. Check this live record and allocation extent, not the
offset global alone. Existing raster plugins must still run in original order.

## Preserve wrapper allocation and plugin lifecycle

`82408130` allocates `BE32[82CD1E28]` bytes through E+120 with r4=`10`.
It writes R+00=self; R+0C/10/14=input width/height/depth; R+04/08=0;
R+1C/1E=zero halfwords; R+21/22=zero bytes. It invokes E+58 at `824081BC`.
On false it returns storage through E+124 `(R,registrySize,10)` and returns zero.
On success it runs `823FB328(82CD1E28,R)` for **all registered raster plugins**.
That helper's return is not checked by this wrapper. Do not replace the wrapper
with a fixed-size allocation or skip the other plugin constructors.

`82407DC0` runs `823FB3A8(82CD1E28,R)` first, then E+5C at `82407E00`, then
E+124 `(R,registrySize,10)`. **It ignores the platform result and returns 1.**
Returning false from native destruction cannot safely reject an unsupported
case; the wrapper would still free R. A checked failure must stop continuation.
Plugin destructors have already run at that point, so validate lifecycle before
requesting a destroy that might fail.

## Exact positive-size create writes

`823F7070` common prefix (`707C..70D0`), before CPU format normalization:

- BE32 R+04=0, R+08=0; U8 R+20=`flags & 7`, R+21=`flags & F8`.
- BE32 X+00=0, X+04=0, X+0C=0, X+18=0.
- U8 X+08=0, X+09=0, X+0A=0, X+0B=FF.
- **X+10, X+14, X+1C are untouched.** Do not invent zero writes for all 32 bytes.

Retain **`823F6E68(R,flags)`** for this prevalidated scope. Types 2 and 1 do
CPU metadata work only; other types can call SDK capability paths and are not
automatically safe. The helper also rewrites R+20/21 to the same masks above.

For flags **2**:

- `823F6EEC -> 823F5048(BE32[82E3DCE8])` obtains the presentation format's
  CPU metadata. This leaf clears/writes four scratch bytes at `82D0D000` and
  returns their address. It is not pure/read-only, but has no SDK call.
- With presentation scalar `182801B6`, the original branch at `823F50D0`
  produces scratch `{01,20,0B,00}`. Normalization writes R+14 from
  `BE32[82E3DF94]` and U8 R+23=`0B`. The presentation scalar is established by
  original driver startup and the parent's native initialization; check it live.
- Type-2 body `823F7190..71BC` then writes BE32 R+18=0, R+04=0,
  R+28=width, R+2C=height, R+14=`20`; U8 R+21=`80`;
  BE32 X+18=`BE32[82E3DCE8]`.
- **X+00 stays zero.** The raster does not own/create a color surface here.
  Its host record may borrow the live driver's default-target role separately.
- `823F7268 -> 823F5DA8(R)` adds the real original CPU list node, then returns 1.

For flags **1**, positive dimensions, and **live `BE32[82CD1D88] != 0`**:

- Normalization sets BE32 R+14=`20` and U8 R+23=`09`, regardless of the
  incoming depth field. It does not invoke `823F5048` on this type.
- `823F71E4` writes `82CD1D88=0`; `71F0` writes X+18=`1A220197`;
  `71F8` copies `BE32[82D0CAFC]` into X+00. No AddRef or allocation occurs.
- `823F7268 -> 823F5DA8(R)` adds its own CPU list node. R+21 remains zero.
- **R+18, R+24, R+28, R+2C, R+30 are not initialized by this positive-size
  depth branch.** Preserve those bytes rather than invent a whole-raster clear.

If the shared flag is zero, `823F7208 -> 823F61F8` computes an allocation base
with `823ED930` and creates a private depth surface through `82440698`.
Reject that branch for now. Image data initializes `82CD1D88` to 1; destruction
does not restore it. No reset site was established by the bounded direct-immediate
search. This is not proof against all indirect writes and is not restart support.

Type 5 calls SDK surface creation at `823F717C`; zero dimensions and flag `80`
have different no-storage behavior. Neither is included in the startup contract.

## CPU list ownership and safe helpers

`823F5DA8(R)` calls E+138 `(BE32[82D0D020],30411)`, obtaining an **8-byte node**.
It writes node+0=R, publishes the new head at `82D0D01C`, then writes
node+4=the previous head. It does not allocate a surface or increment a raster refcount.
The helper does not check allocation failure before dereferencing the result.
Use a checked ABI frame, a live original pool and failure handling; do not replace
the node with host metadata or claim the list insertion cannot fail.

**Add-node return is the node pointer in r3**, unchanged since E+138 returned;
it is not Boolean 1. Check `result == BE32[82D0D01C]`, node+0=R, and node+4=old
head. Original outer call LR is **`823F726C`**, internal allocation LR
**`823F5DE8`**. Normalization `823F6E68(R,flags)` uses LR **`823F70D8`** and does
return a Boolean. Its flags-2 format lookup has LR `823F6EF0`.

`823F5E10(R)` finds the first matching node, relinks predecessor+4 or list head,
then tail-calls E+13C `(pool,node)`. If absent, it returns without freeing.
This is original CPU-only list removal; validate list bounds/cycles/ownership
before using it in a native transaction. Preserve unrelated list entries.
Removal has **no Boolean return contract**: an early absent return retains R
in r3; successful removal tail-calls E+13C and inherits that callback's return.
Validate the list postcondition instead. Original caller LR is **`823F6430`**
for type 2, **`823F636C`** for shared type 1. A newly inserted-node rollback may
reuse the checked helper ABI, but is native failure policy, not an original call.
`823F69E0` creates this pool; `823F6A20` drains nodes and frees the pool at stop.
Its drain frees **nodes**, not raster objects or their platform surfaces.

## Exact scoped destroy and parent distinction

`823F62A0` first queries stages 0..7 through CPU-only `82401AC8(stage)`.
The getter reads `BE32[82D0E3F8 + stage*18]`. Each exact R match calls
`82401940(0,stage)`, which changes CPU binding/pending-state fields and eventually
calls **SDK `824408E0`**. Thus the setter is **not** a safe CPU helper wholesale.
For the first bounded implementation, require R absent from all eight stages;
otherwise explicitly reject pending a native unbind contract.
Here stride `18` is **24 bytes decimal**, not 18 decimal. The leaf itself has
no bounds check; native preflight must restrict stage to 0..7 (last address
`82D0E4A0`). Its original caller LR is **`823F62CC`**; the unsafe unbind's LR is
`823F62E0`. Raw BE reads of those eight words preserve this getter's behavior.

After that loop:

- Root (`BE32[R]==R`) type 2 calls `823F5E10(R)`; it does not release X+00.
- Root type 1 with flags bit `80` clear calls `823F5E10(R)`, then compares
  X+00 against current `82D0CAFC`. Equal (or zero) skips release; another
  nonzero value calls SDK `82441708`. Native shared-depth validation must require
  the exact live driver's default-depth identity, not merely a nonzero token.
- No shared-depth flag is re-armed, and X+00 is not cleared by these branches.
  The wrapper subsequently frees the raster storage.
- If R's parent differs from R, type 2 still removes the CPU list node; other
  types skip resource release and list removal. This establishes a parent/root
  ownership distinction, **not** a complete subraster creation/refcount contract.
  Reject child rasters in the initial native owner.

`82714220` destroys camera+60 via `82407DC0`, zeroes that camera field, then
destroys/zeroes camera+64, then destroys the camera. The driver owns default
targets; these raster associations do not acquire/release SDK references.
Track native driver generation and borrowed roles, require the owner to outlive
the associations, and reject premature driver teardown. Do not create a second
native surface for either of these initial rasters.

## Target use and implementation checks

`823EE6C8` resolves camera raster parents. A non-type-5 color parent selects
global `82D0CB00` and `82D0CAFC`; it does **not** read type-2 X+00 for its target.
The original function then performs SDK target/viewport work and is not a safe
CPU helper to enable. Its viewport uses parent offsets/dimensions and endpoints
1 then 0. Creation of the associations does not authorize camera begin/draw.

Before a native create, check exact callback ABI/flags, positive dimensions,
live driver/submission owner and matching target dimensions, root parent, live
dynamic plugin registration and allocation bounds (`offset + 20 <= registrySize`),
original allocator/pool services, and no duplicate host association/list entry.
Keep R mapped and owned through all callbacks. For depth, additionally check the
live shared flag, format/role and default-depth identity before any mutation.
Do not assume observed offset `34`, input depth 0 or image flag 1 are permanent.

Preflight every failure-prone host check before original CPU mutations. Keep
the precise write set, call `823F6E68` and `823F5DA8` under the checked original
ABI, and record success only when the original node and metadata are valid.
If a later step fails, remove a newly inserted node through `823F5E10` and restore
only this transaction's metadata/flag changes. This is native rollback policy,
not an original OOM guarantee. Preserve scratch-helper writes as CPU effects.

Destroy preflight must distinguish tracked generation, root/type/flags, unchanged
borrowed identity, eight unbound stages and valid list membership. Keep original
plugin destruction and final object free in `82407DC0`. Do not call the original
platform destroy on unvalidated types/private resources merely because a handle
is nonzero. All other raster branches remain explicitly unsupported.

## Reproduction and evidence limits

```powershell
python -B tools/analyze_camera_rasters.py --self-test
python -B tools/analyze_camera_rasters.py --report analysis/native-camera-rasters.json
```

The standard-library analyzer reads the pinned flat `analysis/simpsons.pe`
(base `82000000`, 15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`), checks PE/
`.pdata` using the frozen, hash-pinned post-start analyzer, and byte-checks the
existing offline disassembler output. It writes only its owned JSON, or stdout.
No runtime, generator, renderer, original or reference source is changed.

The report contains **23 extents, 1,303 checked words, and 54 store-shape
assertions**, plus exact call targets and dynamic-offset/observation metadata.
Eight self-tests cover changed store opcode/offset, finite extension bounds,
malformed/changed/missing/duplicate boot observations and reserved write ranges.
Repeated analysis is byte-identical; a source-overwrite report destination is
rejected, and the original image hash remains unchanged.

Boot033 SHA256:
`8d1075c08494d46ea1fea0ce9f722786b1e4772f508b1992493c69284039b80a`.
This proves the observed **entry only**. Shared-depth availability, successful
metadata/list mutation, full camera setup and destruction are not claimed as
executed by this task. Tests of a future runtime bridge must compare original
CPU writes/list changes and exercise rollback, stale owners, bound-stage rejection,
untouched bytes and ordered camera cleanup independently.
