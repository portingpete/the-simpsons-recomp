# Original shadows texture ownership contract

The three texture calls in the real shadows constructor require a native owner
covering **create, lock, unlock and release together**. Native IDs must not enter
any of those original SDK bodies. The original constructor, procedural pixel
writes and game CPU containers can remain intact. This contract does not qualify
depth resolve, texture binding, shader sampling, draw/apply or complete manager
cleanup.

Build148 independently reaches the first request at `82440578`, LR `82706610`,
after the two real shadow cameras and their four owned rasters. The camera proof
is in [the raster contract](native-shadow-raster-contract.md); it is not repeated
here. The source authority is the same flat original PE: 15,466,496 bytes,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Addresses, offsets and packed values below are hexadecimal; dimensions, sizes,
pitches and ordinary counts are decimal unless prefixed `0x`.

## Exact caller and return ABI

Let O be the real shadows object and T one returned texture identity. The three
calls to `82440578` pass all inputs in registers:

- r3/r4: width/height; r5: depth or slice count, exactly1; r6: level count,
  exactly1; r7: usage; r8: packed format; r9: zero; r10: resource selector3.
- At `8270660C`, LR `82706610`: `(1024,1024,1,1,2,1A220197,0,3)`.
  The returned r3 is stored at O+F0 by `82706614`.
- At `82706638`, LR `8270663C`: the same tuple. The returned r3 is stored
  at O+F4 by `8270663C`.
- At `8270666C`, LR `82706670`: `(32,32,1,1,0,18280086,0,3)`.
  The returned r3 is stored at O+FC by `82706670`.

The top helper returns a resource pointer in r3, or zero on its explicit
allocation-failure paths (`824405BC`, `8244062C..3C`, `82440654..70`). This
is not an HRESULT/out-pointer API. These constructor sites do not test the
returned allocations before subsequent uses. A native failure must not publish
a usable-looking identity or let the original continue with partial ownership.

The input r9 is not consumed by this helper: the first subsequent r9 use assigns
the format at `824405DC`. Keep the caller's zero as part of the admitted tuple;
do not invent storage or sharing semantics for it. Usage2 is likewise an exact
input, not permission to copy an SDK flag word into D3D11 bind flags.

O+F8 is a separate borrowed value obtained from `BE32[82D0CF84]` through
`823ED9C8`. It is not a fourth creation, and the shadows destructor does not
release it. The original constructor and destructor spans are reused from
[the independently checked constructor evidence](../build/shadow-raster/constructor-evidence.json).

## What original creation owns

`82440578` first requests a 52-byte SDK header via `8238E880` with flags
`64800000` (`82440590..B0`). It invokes descriptor/size builder `8243F928`
at `82440604`, passing the resource selector, dimensions, level count, usage
and format. Its stack arguments supply zero-valued optional fields, the header,
and two size output pointers. This stack frame is SDK-internal and is not a
native owner layout.

Selector3 takes `8243F95C/964 ->8243F9D8`, setting dimension1 (2D). The
single requested level chooses packed-mip flag0 at `8243F9C0..D4`. Header
initialization writes resource kind3 at `8243FA88`, reference count1 at
`8243FA98`, and zero observer/token field +8 at `8243FA90`. Usage0 and2
take the same tested header-flag branches; the usage tests here are bits
`0x4` and `0x200`, not `0x2`.

The size builder `8243E640`, its alignment helper `8243E520`, and block helper
`8243E2A8` establish these two exact profiles:

- Each 1024x1024 `1A220197` texture: surface selector23, 32 bits per texel,
  tiled, one level, row pitch4096, primary SDK allocation4,194,304 bytes,
  secondary allocation0. These are two distinct textures, separate from both
  private camera depth surfaces. Their format is floating 20e4 depth and 8-bit
  stencil, as qualified in the existing raster/depth contract.
- The 32x32 `18280086` texture: surface selector6, 32 bits per texel, linear,
  one level, padded width64 texels, pitch256, primary allocation8,192 bytes,
  secondary allocation0. The format is unsigned normalized 8:8:8:8 with
  endian selector2 and sampled swizzle ZYXW. The actual pixel payload in this
  constructor consists only of zero and all-ones words, so byte/channel
  permutations do not change it.

These allocation sizes are statically derived original SDK sizes, not required
D3D11 memory sizes. `8206A034=3020` and `8206A056=1120` supply the checked
32-bit format entries. The block helper returns 1x1 for selectors6/23. For the
linear case, `8243E598..AC` selects a width quantum of `256/4=64` texels;
the tiled case retains32. `8243E5B0..EC` rounds width and height; the single-level
paths in `8243E640` leave secondary bytes zero.

The top helper requests primary backing at `82440624`. For usage0 or2 the
computed allocator flags are `BC800000`, following `82440608..1C`. It only
requests secondary backing if that computed size is nonzero. At `82440674..8C`
it merges backing addresses into the SDK header +20/+30. Neither these address
bitfields nor the header itself should be materialized for a native identity.
Backing allocation is not initialized to a defined color/depth value by this
helper; the border texture's later CPU writes are its evidenced initialization.

## Lock output and procedural write bounds

At `82706688`, LR `8270668C`, the original caller supplies:

`r3=T, r4=0 (level), r5=&callerStack[50], r6=0 (rectangle), r7=0 (flags)`.

Leaf `82440238` only reshuffles these registers and tail-branches to
`8243F7F0`, inserting r4=0 as the face/slice selection. The resulting arguments
are `(T,0,level,out,rect,flags)` in r3..r8. There is no implicit device argument.

`8243F7F0 ->8243F728 ->8243F268` reads the actual SDK resource descriptor;
the first unsafe texture-header load is `8243F29C: lwz r9,32(r31)`.
Low-level lock `82441238` also reads resource state and the original SDK
device through `82000710`, performs device synchronization for flags0, tracks
dirty ranges, and increments the packed lock count by `0x100`. It cannot be
retained as a harmless CPU helper for a native ID.

The public lock's two required outputs are exactly:

- `BE32[out+0] = row pitch in bytes`, written at `8243F86C`.
- `BE32[out+4] = writable pixel address`, written at `8243F870`.

The low-level path returns the mapped base address in r3 at `82441460`.
The outer lock does not replace it with an HRESULT. For the admitted level0,
face0, null-rectangle request, this is the pixel address. The constructor ignores
r3 and consumes only the two output words. Preserve the nonvolatile register
and stack ABI; no return-register value should be advertised as a success code.

The exact original pitch is256, not the visible pixel row size128. If D is the
returned data address and P=pitch/4, the retained caller does:

1. `memset(D,0,P*128)`, exactly8,192 bytes, at `827066A4`.
2. In 32 iterations, write `FFFFFFFF` at `D+4*x`, `D+31*pitch+4*x`,
   `D+x*pitch`, and `D+x*pitch+124` (`827066C4..E8`).
3. Unlock T at `827066F4`.

There are124 distinct white border pixels and900 black interior pixels.
The last white byte is offset8,063; the zeroing extends through8,191. Each
row has128 bytes of pixel data followed by128 zero padding bytes. No binary
pixel payload is copied into these proof artifacts.

A native lock adapter must preflight the writable eight-byte output and the
entire8,192-byte staging extent, including overflow and overlap, before exposing
either word. The address must be real guest-writable CPU storage owned for the
lock lifetime, not a native ID, SDK header, or host pointer truncated to32 bits.
Retaining this original output layout is the caller's data ABI, not SDK object
emulation. Only this one full level0 write lock is established; depth locks,
rectangles, extra levels, readback locks and repeated locks remain separate
contracts.

## Unlock and native upload boundary

At `827066F4`, LR `827066F8`, inputs are r3=T and r4=0. Leaf `8243E040`
immediately reads T+30 and T+20 and extracts backing addresses. It overwrites
r4/r5 and tail-branches to `82441470`; the input level is not used by this body.

`82441470` atomically subtracts `0x100` from the packed lock count. On the
last lock it consumes/reset dirty ranges, translates console addresses and calls
cache flush `8245DA50`, then executes `sync` at `82441568`. This is not a
native upload service. It establishes neither a stable HRESULT return nor an
output structure; the constructor does not read a return value.

For a native owner, unlock is the point to snapshot the initialized pixels,
extract128 bytes from each256-byte row, and create/update an actual native
RGBA8 texture. Existing `NativeBackend::createTexture` expects tightly packed
immutable data: pass4,096 pixel bytes, not the8,192-byte staging allocation.
The all-zero/all-ones payload needs no guessed general endian conversion.
Reject malformed state and unsupported pixel content before claiming the
specific procedural texture has been uploaded. Retire staging only after the
backend has taken its own copy; do not retain the caller's stack output address.

The original texture exists before this lock. If an implementation defers its
GPU upload until unlock, the record must explicitly remain unuploaded and reject
all readers/binding until the real resource exists. Alternatively, own a mutable
native allocation from creation and upload it on unlock. Either policy must
preserve T's identity and must not expose an uninitialized texture as ready.

## Release and lifetime

Shadows deletion invokes `82441708(T)` for O+F0, O+F4, O+FC in that order,
at `827057A8`, `827057BC`, `827057D0`. The corresponding LRs are
`827057AC`, `827057C0`, `827057D4`; the caller then clears the respective
field. O+F8 is not released. The complete surrounding destructor order and its
unresolved attached-camera ownership are in the previous contract.

`82441708` atomically decrements `BE32[T+4]` and returns the new count in
r3 if nonzero. Zero invokes `82441050` and returns0. These fresh textures start
with count1, so their first release, absent later retains, destroys them.
The nested parent release at `82441760..64` requires kind4 plus bit
`40000000`; it does not apply to these kind3 textures.

Original jump table byte `8206A0AA=11` selects `824410EC` for kind3.
That path optionally notifies the SDK observer, frees addresses extracted from
T+20 and T+30 via `8238EB00` (`82441124/30`), then frees the header at
`82441228`. Both the release entry and destruction path dereference SDK memory.
Never route native T to these bodies, even when a shadow field has been cleared.

The native owner should hold two independent native depth resources and the
border texture's native resource/staging state, with checked runtime/driver
provenance and IDs that cannot alias guest allocations or another resource kind.
Preflight unknown/stale IDs, lock state and any supported retained aliases before
changing counts or freeing staging/backing. An owner record, not an O-field
address or an SDK-shaped block, carries this lifetime. Retire IDs rather than
letting a stale alias acquire a newly created resource. Neither renderer resource
retention nor complete driver restart is established merely by these three
destructor calls.

## Later readers and the next boundary

The [bounded reader review](../build/shadow-textures/readers-review.md) and
[reader evidence](../build/shadow-textures/readers-evidence.json) establish the
following original dataflow. Its candidate search is `[82704000,82709000)`,
aligned `lwz` instructions with immediate offsets F0/F4/F8/FC. Sixteen matches
include four constructor/destructor loads and twelve later-reader loads. Two
fixed-target direct-call scans of the original `.text` establish caller chains;
the method receiver is tied to the actual named `shadows` object, not inferred
from matching field offsets. Indirect/indexed aliases and eventual generic
sampler consumers are not exhausted.

The vtable slot+C method `82706A68` copies texture values without dereferencing
their pointees or retaining them. Its five assignments are:

- O+F0 -> selected FX `kShadowBackDepthSampler`, handle O+690, store at
  `82706C50`.
- Borrowed O+F8 -> selected FX `kFirstDepthSampler`, handle O+69C, store at
  `82706CD8`.
- O+F0 -> pool `kShadowDepthSampler`, handle O+698, store at `82706DD4`.
- O+F4 -> pool `kShadowCharDepthSampler`, handle O+694, store at `82706E14`.
- O+FC -> pool `kShadowEdgeSampler`, handle O+6A0, store at `82706E54`.

These are useful assignments for a native parameter owner, but **the original
destination accesses still require SDK FX/pool layout**. The first texture
assignment reads FX+10C at `82706BEC`, then selects private/shared descriptor
and storage bases. The three pool assignments use P obtained from
`827225B0`, which returns `BE32[82D6D2F8]`; they use P+100/+108 and dirty
bits. Validate this live root's alias/provenance before associating it with any
native shared owner. Do not substitute an SDK-shaped FX block to preserve these
inlined writes. Guard/adapt the setup method at entry, before its earlier wrapper
activation/name queries at `82706A80`, until all its metadata accesses are
handled. Guarding only the eventual texture load is too late.

The diagnostic QUAD path `82705BE0` passes O+F0 and O+F4 to `82704968`
at `82705C74/9C`, then to `826B6148` at `82704990`. This setter copies
the texture value, but first activates an FX through `826B5FC0`, which can
read FX+200 at `826B601C`. It subsequently reads SDK parameter fields and
calls `82C1DBA0` at `826B628C`; that routine reads FX+12C at `82C1DBB4`.
Guard/adapt `82704968` or `826B6148` entry, not merely the final commit.
The QUAD receiver's coincident +F4/+F8 offsets are handles, not shadow texture
ownership fields.

There is a direct texture-header dependency in the resolve path:

- `82707220(O,selector)` uses O+F0 for selector0 and O+F4 otherwise only
  in its `BE8[82CEFF5C] != 0` branch. It calls `82704BE8`, which passes T
  in r6 to `82455570` at `82704C2C`.
- In the other branch, calls `827074A4` and `82707514` both pass **O+F0**,
  irrespective of the selected camera. Preserve that distinction.
- `82707558` passes borrowed O+F8 to the same entry at `827075F8`.

Those calls pass the device returned by `823EE8F8`, r4=4, r5=0, r6=T,
r7..r10=0, f1=0, and zero caller-stack words +5C/+64. No broader signature
is inferred from zero arguments. `82455570` retains T in r19 at `82455598`,
reads device+30A0 at `824555C0`, dereferences that selected state at
`824555D4`, and reads **T+30** at `82455624`. There is no preceding
native-ID lookup. Reject/replace those four calls before entry; keep their
enclosing camera/pass paths guarded while earlier operations remain unsupported.

O+FC has no later direct texture-header load in the stated candidate range; its
value enters `kShadowEdgeSampler` storage. This does not establish that generic
parameter upload or sampling can consume it safely. None of the observed inline
assignments performs a texture retain, but that bounded observation is not an
exhaustive lifetime/alias proof for all future consumers.

For the constructor itself, adapting creation alone immediately exposes
`82440238`; adapting create and lock then exposes `8243E040`. All three
resources also need native `82441708` dispatch before the original destructor
can safely encounter their IDs. Completing these four entry contracts does not
predict which later registration callback the real boot will reach next.

## Reproduction and scope

Run `python -B build/shadow-textures/verify.py --check`. Without `--check`, it
only regenerates [SDK evidence](../build/shadow-textures/sdk-evidence.json) and
[checked original disassembly](../build/shadow-textures/sdk-original.txt).
It pins the image, existing disassembler, helper and independently collected
constructor evidence. Whole functions use original `.pdata`; six reviewed
leaf extents end at their actual tail branch or return. It cross-checks411
constructor/destructor words without duplicating their original report.

Current checks cover17 SDK spans,1,485 instruction words,19 branch/link pins,
40 instruction-shape pins, three data spans, and22 rejected negative cases.
The profile arithmetic and border bounds are deliberately limited to these two
tuples; they are not a general SDK decoder or runtime emulator. No guest code,
SDK service or shader was executed. No production/test files, original assets,
reference files or build configuration were changed, and no build was run.

Run `python -B build/shadow-textures/readers_verify.py` for the separate reader
proof:20 bounded spans,1,372 original instruction words,45 critical pins,
the fixed field/caller inventories, and two rejected proof mutations. It does
not generate files. Both proofs use the existing host disassembler only.
