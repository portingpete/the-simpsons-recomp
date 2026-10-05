# Bounded native screen bridge

Implementation update: [the original screen bridge](native-original-screen.md)
now implements this boundary with a documented native arithmetic model. The
original prefix/epilogue and CPU effects below remain the contract. Exact console
pixel precision is still an open fidelity requirement.

The proposed **before-instruction hook at `82756554`, resuming at `8275684C`,
is a usable engine boundary** for `82756480`. It must replace the draw, its
effective state transitions, resource bindings, and six engine cache-word
writes. Merely jumping over the SDK calls loses CPU-visible state. Keep the
original prefix and epilogue, and reject unsupported draws before publication.

This report changes only this document. Original bytes are authoritative;
generated AOT was inspected for the register handoff, not treated as additional
proof of PPC numerical equivalence. No SDK object, packet parser, command
processor, runtime hook, or renderer is implemented here.

## Source and precise handoff

Original `analysis/simpsons.pe`: base `82000000`, size 15,466,496, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Addresses/offsets below are hexadecimal. Guest words and floats are BE.
The existing `.pdata` validator gives `[82756480,8275685C)` and
`[823EEBD8,823EEC78)` as complete function extents.

Entry arguments: r3=camera, f1/f2=left/top, f3/f4=right/bottom,
f5/f6=u0/v0, f7/f8=u1/v1, r8=RGBA float32 pointer, r9=blend selector,
r10=texture object or zero. At **82756554 before its BL**:

- r30 retains the color pointer; r27 retains selector; r28 retains texture.
- r29=`82DFEA20`; r31 low32=`82D10000`.
- f31/f30/f29/f28 are the original computed left/right/top/bottom clip XY.
- f27/f26/f25/f24 retain u0/v0/u1/v1, respectively.
- r3 is now `BE32[82D0CAF8]`, not the camera; r4=0. Do not recover the camera
  from r3 here, and do not require native opaque identity in CAF8. CAF8 can
  remain zero. Retain/validate the entry camera separately if the bridge needs
  to compare it with its current native camera binding.
- r1 is the original entry SP minus D0. The original prologue has saved
  nonvolatile GPR27..31, FPR24..31 and the caller LR. Resume **at 8275684C**,
  whose instructions restore SP, FPRs and GPR/LR through the original helpers.
  Do not manually pop this frame, return from the whole function at the hook,
  or jump to the final GPR restore while omitting the FPR restore.

Use the existing full-PPCContext/base mid-instruction hook ABI. The replaced
word `82756554=4BCE4175` is a BL to `8243A6C8` (its normal LR would be
`82756558`). The resume word is `8275684C=382100D0`. The skipped half-open
range is `[82756554,8275684C)`, 2F8 bytes. No normal path in that range returns
an application result; the function has no explicit Boolean result contract.
Its volatile register residue is not a success code to manufacture.

The prefix is semantically useful. `827564B4..C0` skips directly to the same
epilogue when color alpha is less than float word **3C010204** at `821DD350`
(approximately 0.00787401572). That is separate from the later 1/255 fragment
alpha test. The skip has no shader/stream/global cache writes. It occurs before
the camera raster read. Do not require otherwise-unused material/texture
resources for this original early-out. Unordered/nonfinite inputs are not
certified by the comparison; reject them on a draw path.

`827564C8..82756550` reads camera+60 raster, signed halfword raster offsets
+1C/+1E, and scales S+11C/+120 (S=`82DFEA20`). It computes the four values
with original fadds/fmuls/fmsubs/fnmsubs, constants 2 and 1. Consuming these
results avoids a second native coordinate transform and preserves the original
operation order in AOT. This does not independently certify all exceptional or
fused PPC rounding behavior. Do not replace the scales with rectangle-derived
reciprocals. UV stores in the skipped body are stfs: supply the corresponding
float32 values, not an invented UV adjustment.

## Stream zero: exact CPU contract of 823EEBD8

General entry is `r3=stream index, r4=buffer identity, r5=offset, r6=stride`.
There is no original bounds check. The bounded screen service should accept
only `(0,0,0,0)` until another use is independently supported.

Let `B=82D0CAB0+16*index`. The function compares `BE32[B+0]` to r4,
`BE32[B+4]` to r5 and `BE32[B+8]` to r6, at `823EEBF8..823EEC20`.
If all match it returns without writing or calling the SDK. Otherwise:

1. `823EEC28=7C8B512E` writes buffer to B+0.
2. `823EEC5C=7D2BF12E` writes offset to B+4.
3. `823EEC60=7CEBE92E` writes stride to B+8.
4. `823EEC6C=4804D955` calls `8243C5C0`, LR=`823EEC70`.

The **B+0C word is neither read nor written**. No allocation, AddRef, Release,
engine callback, list mutation or original ownership transfer occurs here.
The caches are borrowed binding values, not owners. Return r3 is incidental:
unchanged stream index on the cache-hit path, SDK residue on the other path.

Screen calls are `82756688` (textured, LR `8275668C`) and `82756788`
(flat, LR `8275678C`), both with all four input words zero. For index zero
the SDK call arguments are exactly:

    r3=BE32[82D0CAF8], r4=0, r5=0, r6=0, r7=0, r8=1

The mask is computed by the original multiply/shift sequence; it is 1 for this
specific index. Do not mistake incoming r3=0 for a device pointer at the engine
entry. One alternative to reproducing the three stores in the screen bridge is
to retain this entire CPU helper and replace only its SDK call at `823EEC6C`,
with an entry preflight restricting the supported arguments. This preserves
its cache-hit behavior automatically. The broad screen skip instead has to
perform the same cache comparison/publication itself; it cannot call the
unmodified helper when CAF8=0.

Reset `823EDD38` invalidates four stream records: B+0=FFFFFFFF, B+4=B+8=0,
starting at CAB0 with stride 10 (`823EDDA0..DDC0`). It leaves their +0C words
alone. Thus an initial all-zero unbind is **not necessarily a cache hit**.
FFFFFFFF is a cache-invalid sentinel, not a resource to release or dereference.
Changing stream 0 must not clear the other three records.

## What the SDK unbind does, and what replaces it

Complete `8243C5C0` contains no call to AddRef `82441690` or Release
`82441708` and never updates an object+4 count. With new buffer r5=0 it skips
the new buffer's descriptor/address reads (`8243C5DC..C630`). It still:

- Reads the old binding from `D+30A4+4*index` at `8243C638`.
- If old binding is nonzero and `D+2A9C` is nonzero, stores that submission
  token at old-object+8 (`8243C650`). Otherwise, when `D+2AA0 & old-object[0]`
  is nonzero, records deferred usage through the SDK's `D+34BC/+34C0` area;
  its slow path calls `82459810` at `8243C67C`, LR `8243C680`.
- Stores new binding zero to D+30A4 at `8243C6AC` and stride/4 byte zero to
  D+30E8 at `8243C6B4`. Zero bypasses the following stride-change dirty branch.

These are console binding and outstanding-GPU-use bookkeeping, **not a guest
reference decrement and not permission to free a still-used native buffer**.
The native equivalent is real stream-0 unbinding with native resource lifetime
retention appropriate to already submitted work. Preserve logical creation/
release ownership; never send a native token or FFFFFFFF sentinel to the SDK.
The real D3D11 binding/upload owners can carry host references separately from
guest logical refcounts. No SDK descriptor, token stamp, dirty mask or submission
ring needs to be recreated. Unknown live buffer identities or unsupported
pending work must reject rather than be discarded as a successful no-op.

## Shader, declaration, texture and constant effects inside the skip

The six CPU words to publish on a successful draw path are:

- VS cache `82CD1A6C`: flat `BE32[82CF231C]`, textured `BE32[82CF2340]`.
  Stores are `82756758` / `827565DC`; SDK binds `82445578` at `8275675C`
  / `827565E0`, LRs `82756760` / `827565E4`.
- PS cache `82CD1A70`: flat `BE32[82CF2310]`, textured `BE32[82CF2334]`.
  Stores are `82756770` / `827565F4`; binds `82445278` at `82756774`
  / `827565F8`, LRs `82756778` / `827565FC`.
- Stream words CAB0, CAB4, CAB8 = 0, as above.
- Declaration cache `82CD1A68`: flat `BE32[S+110]`, textured `BE32[S+114]`.
  Stores are `82756798` / `82756698`; binds `82445798` at `8275679C`
  / `8275669C`, LRs `827567A0` / `827566A0`.

Validate each identity through its actual native owner, stage and pinned original
record/declaration. A nonzero global alone does not prove a valid live object.
These original cache stores are unconditional on shader/declaration equality;
the null stream helper alone has the three-word equality fast path. They remain
selected after the draw; the epilogue does not restore prior bindings.

The shader binders have old-object usage tracking like the stream binder. They
store PS at D+318C / VS at D+3190 and set SDK dirty/patch flags. Their optional
metadata patch loops are conditional on **copied original header+14**:
PS reads object+28+14 at `82445320`, VS object+368+14 at `82445630`.
All four pinned screen records have this word zero. Original creation copies
the headers to those locations at `82448210` / `8244839C`; VS initializer
`82445480` clears only the preceding 368-byte SDK area. Thus these exact screen
binds do not execute their optional header-driven constant/sampler patch loops.
Their payload-prefix lengths are also zero, independently verified from each
record's code-metadata section. This conclusion does not extend to arbitrary
startup shaders or changed records.

Declaration binder `82445798..57AC` is a five-word leaf: store D+2E24, OR
80000 into the 64-bit dirty word D+10, return. It has no AddRef/Release.
The shader binders likewise have no binding-time refcount operation. Actual
SDK AddRef/Release use atomic object+4 operations at `824416AC..B4` /
`82441728..30`; the reviewed binding paths do neither.

Textured only: r28 points to the texture object, whose +0 is a raster pointer.
`82756600..18` loads the SDK/native texture identity at
`raster + BE32[82E3DC94]`; `8275661C` calls `824408E0` with stage 0,
LR `82756620`, r6=80000000. This is a **direct SDK texture bind**, not the
RenderWare texture setter: no RW/application stage cache is updated here.
Resolve through the native texture/raster owner and its dynamic extension;
do not treat r28 itself as a raster or texture-resource identity. Validate the
original root/parent relationship supported by that owner. The binder updates
SDK texture/fetch fields, including inherited descriptor state and old-resource
usage tracking, but no logical AddRef/Release. Flat draws do not unbind or clear
the previously selected texture or sampler state.

Both branches copy color from r30+0/4/8/C to PS constant c0 at SDK
D+1780..178C and dirty bit 63 at D+8. Native replacement must supply the exact
RGBA float32 constant to the proven shader; there is no engine global c0 store
to fabricate. Native VS/PS objects and input layout must match the original
records, not just share a generic screen shader name.

## State, vertices and the work after submission

Before draw, the original calls set DepthEnable=0, Cull=0, AlphaTest=1,
AlphaCompare=4 (GREATER), AlphaReference integer=1. IDs are 28,38,60,68,64.
`8243A438` converts that integer and multiplies by float32(1/255); do not pass
float bits as the original request. Target-0 packed blend is 00010106,
00010706,00010186,00010001 for selectors 0,1,2,3. It is a direct packed-word
write, **not an update to the application's scalar blend shadows**.
Selectors other than 3 request expanded blend 1 at `827565C0`.

Textured draws additionally set stage-0 min/mag request 1 (calls `8275662C`,
`8275663C`) and clear U/V address fields to repeat (`82756658..84`). They
retain W, mip, LOD and the other effective sampler fields. Mirror these through
the native effective-state owner, not guest pending/applied or application
cache/dirty-frame updates: those higher-level services are absent in this body.

Vertices are an original four-vertex triangle strip: TL, TR, BL, BR. Flat
records are `(f31,f29), (f30,f29), (f31,f28), (f30,f28)`, stride 8, 32 bytes.
Textured records append `(f27,f26), (f25,f26), (f27,f24), (f25,f24)`, stride
0x10 (16 bytes), 64 bytes total. The last two XY stores for vertex 0 are shared at
`82756804/08`. Preserve shader Z=0/W=1; no extra transform or invented image.

`8244C450` is called with primitive 6, count 4 and the above stride, at
`827566E8` / `827567E8`. It flushes SDK state, obtains transient SDK upload
storage through `82456D60`, and returns the pointer subsequently filled by
these stfs instructions. `82756810 -> 8244C8F0` publishes D+3474 to D+30 and
may tail-call SDK submission `824605C0`. No resulting vertex pointer is
published to a game CPU owner or survives in an engine global in this helper.
The replacement needs actual native vertex upload/submission lifetime; it does
not need to preserve a guest command buffer or execute either SDK routine.

**After submission**, and before the proposed resume, original code performs:

1. `8275681C -> 8243A010(D,0)`: BlendEnable=0, and all four effective target
   blend words become 00010001. Other scalar blend requests remain retained.
2. `82756828 -> 82439F60(D,0)`: AlphaTest=0.
3. `82756834 -> 8243A6C8(D,1)`: requested DepthEnable=1; effective enable still
   depends on a bound depth surface. DepthWrite, compare and stencil are not
   changed by this setter.
4. For selector !=3 only, `82756848 -> 8243B3B0(D,0)`: expanded request 0.

This is not restore-to-previous state. Cull remains zero; alpha compare/reference,
textured sampler changes, texture, shader/declaration selections and c0 remain.
Selector 3 never calls the expanded setter, so it also does not forcibly clear
an inherited nonzero expanded request. Validate that inherited state explicitly.

## Ownership and safe publication plan

Stream/shader/declaration cache writes do not confer guest logical ownership.
Creation and release remain separate. In particular S+110/+114 originate from
direct declaration creations `827521C4/21D8 -> 824458E0`, with output stores
`827521CC/21E0`. Their direct releases are `827522EC/22FC -> 82441708`, followed
by field clears `827522F8/2314`. Preserve/replace those paired services at
their established engine boundary before allowing their identities to bind;
the regular `823EF838/823EFA18` declaration cache is a different creation path.
Do not bypass the enclosing initializer's genuine CPU allocator work.

For the screen skip, preflight the complete supported request: original frame,
entry/current camera relationship, current targets, finite emitted geometry and
color, selector, required source fields, six writable destination words, dynamic
raster extension, live owner identities and exact native shader/input-layout
capabilities. Prepare all fallible native artifacts/state/upload resources before
publishing new guest cache values. Record actual submission before reporting a
draw; propagate allocation/bind/unsupported failures, never leave the caches
claiming an unperformed bind. A native transaction may provide stronger rollback
than the original's cache-before-SDK order, but must not continue after a partial
GPU side effect as if the request succeeded.

Keep selected resources alive across draw submission and outstanding host use.
Separate this backing retention from original logical resource counts; binding
must not leak an extra guest retain or consume the creator's reference. Preserve
logical releases and reject later stale identities. A broad whole-game alias or
arbitrary shader binding proof is not claimed here.

This boundary does not remove the existing independent draw gates. See
`screen-shaders.md`, `native-screen-raster-policy.md`, `native-expanded-blend.md`
and `native-camera-pass.md`: native viewport depth 0..1 is a reversible host
screen policy while original logical depth remains 1..0; depth/stencil must be
off for the certified subset; non-endpoint/fractional coverage and expanded-blend
precision remain bounded by their actual evidence. Start with the checked flat,
opaque, full-target selector-3 request. A completed native clear is not proof of
this draw or of presentation.

## Byte checks and reproduction

The following read-only script verifies the image identity, complete `.pdata`
ranges for the core functions (the declaration setter is an explicitly bounded
leaf), critical BL/resume/store words and the four zero metadata fields. It uses
the existing frozen validator and writes no files. The original disassembler
was also run over the complete screen, stream, binders and SDK upload bodies.

```python
from pathlib import Path
import sys
sys.path.insert(0, 'tools')
import analyze_poststart_integration as a
b = Path('analysis/simpsons.pe').read_bytes()
a.validate_identity(b)
sections, pdata = a.layout(b)
pins = {
    0x823EEBD8: (0xA0, 'f3af2ae48cb3a44efa1b71f995ffc2cc6927b54d09ee44aaf67bf25c400e193a'),
    0x8243C5C0: (0x11C, 'b04f914d7e4eaa49cb042f49ac0b0fdd5ccd4b57ae34a144ad77d739eee1fae6'),
    0x82756480: (0x3DC, '7ac6f43c8dcf558cd9bbfb3789ff4fb94c62c45dc21f7d47552ac84d6d2fb0f9'),
    0x82445278: (0x1BC, '4241ad4dc5325e67142978e26f16173d38099fbb3fc04eb2bb6e04d198d77cb4'),
    0x82445578: (0x1CC, 'fd08c807ba8fd3e97c40394aa0845f6455a0799d4dfc59097c9162c62b13c9cd'),
    0x82445798: (0x14, '78c3fa2268ec0dcf0b36ff40bf4e6590c9b9faabd427b8f2d3fd357fd90e1cb8'),
    0x824408E0: (0x178, '1b2bb7654b7db274a459cbcf93793bbe2cdacd5b76dde5a4911a9b14e47f3711'),
    0x823EDD38: (0x15C, 'a2cb921b23bcabe1c5c9fe0e975e826c660f13ad9b677eb8e6dc0b70b621f5c5'),
    0x8244C450: (0x4A0, '34b978f6ac0e66ff382591058b98eeab1b2e180e66bec08e27899bb7f6cc4f31'),
}
for va, (size, digest) in pins.items():
    if va != 0x82445798:
        assert pdata[va][0] == size
    assert a.sha(a.span(b, va, size)) == digest
for va, word in {
    0x82756554: 0x4BCE4175, 0x8275684C: 0x382100D0,
    0x82756688: 0x4BC98551, 0x82756788: 0x4BC98451,
    0x823EEC28: 0x7C8B512E, 0x823EEC5C: 0x7D2BF12E,
    0x823EEC60: 0x7CEBE92E, 0x823EEC6C: 0x4804D955,
    0x827565DC: 0x908B1A6C, 0x827565F4: 0x908B1A70,
    0x82756698: 0x908B1A68, 0x82756758: 0x908B1A6C,
    0x82756770: 0x908B1A70, 0x82756798: 0x908B1A68,
    0x821DD350: 0x3C010204,
}.items():
    assert a.word(b, va) == word
for va in (0x821524C8, 0x821525E8, 0x82152708, 0x82152880):
    assert a.word(b, va + 0x14) == 0
    assert a.word(b, va + a.word(b, va + 0x18)) == 0
assert a.branch(0x823EEC6C, a.word(b, 0x823EEC6C)) == (0x8243C5C0, True)
assert (1 << 63) >> (((95 * 21846) >> 16) + 32) == 1
print('PASS: 9 spans, 15 critical words, 4 screen metadata/prefix pairs, stream call ABI mask')
```

Run this block from the workspace with `python -B`; use
`build/generator-ninja/SimpsonsDisasm.exe analysis/simpsons.pe 0x82000000
0x823EEBD8 40` and the same command with `0x82756480 247` for the two core
instruction listings. These are static evidence checks, not a native screen
execution test. Only this document was written for the task.

## Earlier observed boundary: mode-zero reset 823EFDA0

Follow-up observation: `build/boot-054.log` stops at SDK texture setter
`824408E0`, LR `823EFE48`, before the screen helper. The actual BL is
`823EFE44=48050A9D`. This is the first unconditional texture-null bind in
`823EFDA0`, not an attempted screen texture draw. The parent's entry guard at
823EFDA0 is appropriate while its complete mixed reset remains unported.
This addendum identifies the bounded prerequisite; it does not implement it.

Pipeline push `823F46A0` publishes its original stack first. If the selected
mode changes to zero, `823F46EC` tail-branches to 823EFDA0; nonzero selects
the different scalar-table reset 823F4618. Keep that original mode-stack work.
823EFDA0 itself consumes no entry arguments and has no explicit Boolean
success return. Its complete extent is `[823EFDA0,823EFFD4)`.

Exact body sequence:

1. Invalidate five engine caches `82CD1A64/68/6C/70/74` to FFFFFFFF and
   four stream records CAB0+10*i to `{FFFFFFFF,0,0,untouched}`.
2. For texture stages **0..7 only**, call original
   `82401AF0(r3=0,r4=stage)` at `823EFE28`, LR `823EFE2C`, then force
   `824408E0(D,stage,0,mask)` at `823EFE44`, LR `823EFE48`.
   The 64-bit mask is `(1<<63)>>(stage+32)` (stage 0 = 80000000).
3. Index cache `1A74` is now nonzero (the just-written FFFFFFFF), so clear
   it to zero and bind null index `8243C768` at `823EFE70`, then bind null
   again unconditionally at `823EFE7C`. The binder only updates D+308C and
   old-resource usage tracking; it has no AddRef/Release.
4. For streams **0..3**, call `823EEBD8(stage,0,0,0)` at `823EFE94`,
   LR `823EFE98`, and then force an additional null SDK bind `8243C5C0`
   at `823EFEC4`, LR `823EFEC8`. Stream masks from the actual arithmetic
   are 1,1,1,2. Do not confuse these four records with sixteen samplers.
5. Clear PS cache 1A70 and bind null PS at `823EFEF0`, then again at FEFC.
   Clear declaration cache 1A68, retain 1A64=FFFFFFFF, bind null declaration
   at `823EFF24`, then again at FF30. Clear VS cache 1A6C and bind null VS
   at `823EFF50`, then again at FF5C. The first calls are conditional on
   the caches, but that condition is true after this body's invalidation.
6. `823EFF6C -> 823EDB68(0,BE32[82D0CB00])` restores default color role 0.
   For slots 1..3, `823EFF7C -> 823EDB68(slot,0)` clears the engine cache,
   followed by forced `823EFF8C -> 8243DED0(D,slot,0)`. The helper compares
   and writes `82D0CF5C+4*slot`; only a changed cache calls its SDK target
   setter at `823EDBA0`, LR `823EDBA4`. It returns 1.
7. Restore depth cache `82D0CF58=BE32[82D0CAFC]` if different, calling
   `8243D598` at `823EFFB8`; call that same depth setter unconditionally at
   `823EFFC4` afterwards. Retain ownership of the genuine default roles.
8. `823EFFC8 -> 82400D50`, LR `823EFFCC`, rebuilds and commits RW state.

The repeated SDK binds force effective state despite invalidation/equality
logic. A native reset needs their resulting bindings and lifetime effects;
duplicating irrelevant host calls is not itself required. On success, cache
1A64 remains FFFFFFFF, caches 1A68/6C/70/74 become zero, all four streams'
first three words become zero, CF5C=CB00, CF60/64/68=0 and CF58=CAFC. Do not
zero cache record +0C, other texture stages, material creation slots or original
logical resource references. No resource allocation or logical release occurs
in the reviewed reset body.

### Null RW texture helper has real CPU side effects

The `r3=0` path of `82401AF0` branches at `82401B10` to `82401D40`.
For stage zero, if `BE32[82D0E3DC] != 0`, it first clears that word. If
`BE32[82D0E3D8]==0` too, it writes pending BlendEnable=0 at F590, marks
F594 dirty and appends ID 3C to the scalar queue only if not already dirty,
then calls `82400170(60,0)` at `82401DA4`, LR `82401DA8`. These are pending
CPU state effects, not permission to skip the later commit.

For each stage it reads texture/raster cache `82D0E3F8+18*stage` at
`82401DB4`. If nonzero, it clears only that word at `82401DE4`, then calls
`824408E0(D,stage,0,mask)` at `82401DEC`, LR `82401DF0`. It returns 1.
The other five words of this 0x18-byte (24-byte) RW stage record survive. A bounded null
texture implementation can retain this original path with checked stage/queue
bounds and replace its SDK call; the nonnull branch is much larger and remains
outside this null-binding proof. Reject nonnull r3 at this boundary until it is
supported. The caller's following forced null bind must still update actual
native bindings when the RW cache was already zero.

### Do not replace 82400D50 with startup defaults

This complete 50C-byte function rebuilds from **retained RW source fields**
around `82D0E3B0` and each stage record; it is not an application-82-scalar
reinitialization. It snapshots source words before clearing caches, resets the
scalar queue through original 823FFE78, fills the stage applied words with FF,
initializes 8*33 pending `{FFFFFFFF,0}` pairs, clears stage queue count and the
sampler cache, and repopulates pending scalar/stage state through original
82400170 / 824001E0. Its final `82401250 -> 82400040` commits that state.
Keep these original CPU helpers and their queue, dirty, stage callback and
failure semantics. Existing native startup initialization is not an equivalent
replacement: it would overwrite inherited RW requests with fresh defaults.

The eight-stage loop `[82400F78,82401128)` also contains mixed SDK work:

- Null texture bind at `82400F9C`, LR `82400FA0`.
- Minification at `82400FD8 -> 8243BA40`, LR FDC; magnification at
  `82401004 -> 8243BBD0`, LR 1008. Values come from the original filter table
  selected by the retained stage filter, not a universal linear constant.
- Inline mip-filter update `[82401028,8240104C)` contains CPU sampler-cache
  store `8240102C`, followed by SDK D+48C+18*stage and dirty-word writes.
- Inline AddressU update `[82401068,8240108C)` contains CPU sampler-cache
  store `82401070`, followed by SDK fetch-descriptor and dirty-word writes.
- Inline AddressV update `[824010A8,824010CC)` contains CPU sampler-cache
  store `824010B0`, followed by the corresponding SDK writes.
- Border/aniso calls at `824010D8/10E8` go through existing engine sampler
  service 82400278 with IDs 0C/24. Stage queue calls continue afterwards.

These three inline blocks are essential hazards: replacing only explicit BLs
still dereferences CAF8. Their native replacements must retain the listed CPU
cache stores and publish effective sampler IDs 18/00/04, respectively. The loop
stage is r27, input value r10. Preserve original table lookups and equality
branches; validate their indices before running the body. The scalar commit
may intentionally update retained effective native state even though the
application dispatcher cache was not touched. Do not synchronize the unrelated
application owner or expand this reset to sampler stages 8..15.

### Target rebind has a conditional viewport/scissor consequence

`8243DED0` tail-calls `8243D230`. A changed color-0 bind reaches
`8243D3E0 -> 8243D198`, which resets SDK scissor from `82069FBC` and viewport
from `82069FA4`. Those original words are `{0,0,FFFF,FFFF}` and
`{0,0,FFFF,FFFF,00000000,3F800000}`. SDK 8243CE80 limits the viewport to
the bound target dimensions: for the ordinary 1280x720 default role this is
full target with depth endpoints **0..1**. This is an actual target-rebind
side effect, distinct from the camera's explicit logical 1..0 setup; it must
not be justified by arbitrary native clamping.

The reset does **not** force color-0 SDK binding when CF5C already equals
CB00. On the previously selected default root camera that equality can make
this path a cache hit, preserving its prior viewport. The forced depth bind
only takes its analogous viewport-reset helper if SDK color-0 is null.
For the first bounded native implementation, preflight the already selected
default color/depth ownership and effective viewport/scissor consistency, or
implement the proved rebind distinction. Never assume every reset changes the
viewport, or that every reset preserves reverse depth. Expanded target-format
requests are also inherited when the SDK rebind applies the target descriptor.

Suggested next boundary: one checked native 823EFDA0 orchestration owning
native null bindings/default targets while retaining the null RW helper and
the original 82400D50 CPU body through narrowly intercepted SDK call/inline
blocks. Preflight/rollback must span the outer cache changes and inner queue
rebuild; a failure at 82400D50 after earlier cache publication is not success.
Keep the entry guard until that complete path is supported. No SDK object or
guest command-space emulation is needed for the described native services.

Additional checked complete `.pdata` byte pins:

```python
# Run after the verification block above; all reads are from the same image.
for va, size, digest in (
    (0x823EFDA0, 0x234, '67952b9fea05afbd737b4196b6446832b957b4b68d72d77f55515d320ace0465'),
    (0x82401AF0, 0x30C, 'a3c66fe9f528c442c0b5ef57cf55fb66c9b469b9ae4b5eefa7864881385a7fe7'),
    (0x82400D50, 0x50C, '3072be3311055078590442823bc3a6c62d73407bc6d48d4527ddad7e45a9334e'),
    (0x823EDB68, 0x50, 'f3d33a580ea766076d5a0eb31eba6335be2ff7b2f7f9d946d7c0d8c5caaebd03'),
    (0x8243C768, 0x90, '7f993e97a11e5e3366350028abd932846aecbb533c91edc79f4884df0b7e0e97'),
    (0x8243D598, 0x2D8, 'c60b36b7b2c101186e8f10d3f57f0f73c5be434e746836d283782f56105edeee'),
):
    assert pdata[va][0] == size
    assert a.sha(a.span(b, va, size)) == digest
for pc, target in ((0x823EFE44,0x824408E0), (0x82401DEC,0x824408E0),
                   (0x823EFFC8,0x82400D50), (0x82401250,0x82400040)):
    assert a.branch(pc,a.word(b,pc)) == (target,True)
assert [((1<<63) >> ((((95-i)*21846)>>16)+32)) for i in range(4)] == [1,1,1,2]
assert [a.word(b,0x82069FA4+4*i) for i in range(10)] == [0,0,65535,65535,0,0x3F800000,0,0,65535,65535]
print('PASS: 6 reset/helper spans, callsites, four stream masks, target-reset viewport/scissor constants')
```

### Native state rebuild implementation handoff

`EngineRenderState::preflightRebuild(EngineCpuCalls&, uint8_t*)` validates
retained RW source indices, the original conversion tables, dirty counts and
queued membership, writable cache/record ranges, and the callback stack without
publishing state. It requires the initialized native owner and CAF8 zero.
`rebuild` repeats that validation, then invokes the **original 82400D50 body**.
It does not call the first-start initializer or substitute startup defaults.

The review fragment `config/mode-zero-state-hooks.toml` pins six hooks:
entry 82400D50 continues normally; 82400FD8 resumes at 82400FDC; 82401004 at
82401008; 82401028 at 8240104C; 82401068 at 8240108C; and 824010A8 at 824010CC.
Existing engine hooks retain the final 82400040 commit and the two 82400278
sampler calls per stage. The min/mag replacements publish native state even
though the original callsite already stored its CPU cache. The three inline
replacements preserve their CPU cache stores. A scoped execution record checks
the exact callback context, loop registers, retained source values, and all
56 sampler updates in original order. A direct unscoped entry is rejected.

The null-texture BL at 82400F9C is deliberately retained. Main's driver owns
the scoped 824408E0 NULL-only interception (LR 82400FA0), all actual binding
ownership, and delayed native GPU unbinding. The rebuild owner creates no SDK
object, accesses no SDK layout, and performs no GPU operation. The original
scalar/stage queue helpers, source reads, equality decisions, CPU stage record
callbacks, pending/applied publications, and original C0-byte frame still run.

The original contiguous conversion arrays are bounded as follows: shade has
3 entries, fog 4, blend 12, address 5, filter 14 pairs, cull 4, stencil 9,
and compare 9. Table bounds do **not** imply native capability. The existing
effective owner accepts filter rows 1..6 and the mapped address/blend/cull and
alpha-compare requests; unsupported anisotropic/row-zero filters, non-baseline
stencil operations, border/aniso requests, or other unsupported values fail
preflight. CPU-only IDs >=194 keep their original raw words. Source words
199/19A and alpha reference are captured by the original body before its
destructive reset. Sampler stages 8..15 and fields not set by that body remain
inherited. Notably, 823FFE78 seeds applied/pending mask IDs 88/8C to zero;
a retained zero request is consequently elided by the original queue helper.
The native implementation preserves that equality behavior too.

On a rebuild exception, its three cache/record windows, effective state,
EngineCpuCalls register context, and 1C0 bytes of callback stack are restored.
The latter comprises the original C0 frame and the nested commit's 100-byte
callback frame. Main's driver separately owns rollback of the enclosing
null-binding CPU work and the commit of real native bindings. A GPU failure
after rebuild returns is not covered by this CPU transaction and must remain
terminal; no GPU rollback is claimed.

`tests/header/test_rw_rebuild_contract.h` provides
`rwRebuildContracts(runtime,cpu,base)` for the driver fixture after its RW
tests. Twelve nondefault retained-state cases cover all six supported filter
rows, both depth inputs, blend/cull/fog/shade/alpha tables, raw high engine
words, every sampler cache slot, stage pending/applied values, record-byte
preservation, independent application caches, and stages 8..15. A test-only
AOT dispatch wrapper verifies the actual original body's ABI/frame. A second
wrapper executes two real stage callbacks before throwing, checking rollback
after CPU and effective sampler mutations, followed by successful retry.
Malformed indices/counts and unsupported source values must reject before the
body begins. These wrappers change only test dispatch slots, never image bytes.

Validation at handoff: production and test header pass clang-cl C++20 syntax
checking; all six hook byte pins, five exact continuation addresses, and the
complete 50C-byte original body SHA-256 match. Main reports build092 executing
mode-zero successfully in the original startup path and reaching the next RW
selector guard. The new retained-state/fault-injection test header awaits its
parent fixture run; no pass count is claimed for it here.
