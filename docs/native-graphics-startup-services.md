# Original graphics startup services: bounded native cuts

Status: bounded byte evidence complete; ten verifier self-tests pass. No runtime,
configuration, generated code, original image or reference file was changed.
Main owns `826B6F60` and the top-level orchestration. This document covers the
nine other requested helpers and their first resource dependencies.

## Immediate implementation decision

Retain original `8271BD10` and its actual `8273EB50(...,1)` child construction.
They allocate and initialize CPU objects, store the borrowed backend identity,
and skip the child's SDK branch. No additional native owner is needed for that
storage. The next resource dependency is the **25-effect registration** entered
through `82701B70`, not an ordinary vertex/pixel shader creation call.

Two useful checkpoint cuts have different partial-construction consequences:

- **`82701B70` entry**, pin `7D8802A69181FFF8FBE1FFF0`, caller LR `826B0E94`:
  both preceding CPU constructors have returned and the second object is
  published at `82D5DA78`. No effect-loop temporary allocation has begun. This
  is the simpler stop for a paired lifetime fixture of those two CPU objects.
- **`826B4B88` entry**, pin `7D8802A69181FFF8FBE1FFF0`, caller LR `82701A94`:
  the original effect wrapper/name allocations have run, but effect creation
  and SDK reflection have not. r3 is the `30`-byte original wrapper, r4 the
  embedded effect blob; first blob `820B8AA0`, literal name `fourtapblend`.
  The wrapper is not yet published to its table/tree at this stop. A fatal stop
  can retain this partial guest allocation until guest-memory teardown; normal
  global cleanup alone is not proved to reclaim the unpublished temporary.

The call `826B4BAC -> 82C1D0C0` (`48568515`, resume `826B4BB0`) receives
`r3=BE32[M+14]`, `r4=blob`, `r5=BE32[M+18]`, `r6=wrapper+10`. It is followed immediately
by `826B4828(&wrapper+14)`, which reads the SDK effect's parameter metadata.
Replacing only that SDK call with an opaque resource ID would be invalid.
The next coherent native resource operation needs real original effect ownership
and qualified reflection/parameter behavior at the engine boundary. Deferred
shader compilation is possible only after the CPU effect consumers themselves
have a native contract; an uncompiled shader record alone does not provide it.

## Authority, addresses and reproduction

Original flat PE `analysis/simpsons.pe`, base `82000000`, size 15,466,496,
SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
`build/boot-084.log` confirms the reached guard `826B0DF8`, LR `828620D4`.
The nine later helper effects below are static evidence; that log does not prove
they executed or that their allocations succeeded. Main reports build141 45/45.

Addresses, offsets, sizes and words below are hexadecimal unless stated otherwise.
Guest words are BE. All signed immediate addresses were calculated from the
original instructions: notably `82D60000 + sign16(DA74) = 82D5DA74`,
`82D10000 + sign16(8BFC) = 82D08BFC`,
`82CF0000 + sign16(FD20) = 82CEFD20`, and
`82150000 + sign16(EA94) = 8214EA94`.
`M=BE32[82D08BFC]`; its `+14` is the borrowed original context field.
The current native alias policy in `docs/native-poststart-integration.md` is
authoritative: `82D5DA74`/`82D6D890` hold an unmapped native identity, while
`82D0CAF8` stays zero. The identity is never an SDK device pointer.

Run from the workspace:

```powershell
python -B build/graphics-startup-services/verify.py
```

The verifier rechecks original SHA, PE/pdata bounds, all decoded words/PCs, 45
direct calls, 54 explicitly bounded code spans, 25 registration rows, three
embedded image profiles and the declaration. It writes deterministic
`build/graphics-startup-services/evidence.json` and `disassembly.txt` only.
Ten tests include modified-image/call/pdata rejection, declaration framing,
truncated/changed image rejection, modified registration output and disassembler
disagreement and 48 signed address computations. This is an evidence verifier,
not a general FX parser or emulator.

Final validation: ten tests passed; a second complete run produced byte-identical
JSON and disassembly. Original-image SHA was rechecked unchanged afterward.
No original CPU lifetime fixture or native graphics operation was run by this
sidecar; main owns that integration. These deliverables are frozen.

## 8271BD10: CPU child, complete construction and idle destruction

Original call `826B0E74`: `r3=O` (allocation `70`), `r4=M`; returns O.
Object writes are:

- `+60=M`, `+0=BE32[M+14]`, `+1C=BE8[82D5DA70]`, `+1D=0`.
- Zero words `+4,+8,+24,+28,+2C,+30,+34,+38,+3C,+40,+44,+48,+4C,+64,+68`.
- `+14=3F800000` from `82000BB0`; `+18=00000000` from `821DD0D8`.
- `+20` receives a real `58`-byte child allocated through `8269BD70`, or zero
  on the original null branch. Unlisted bytes are not blanket-initialized.

At `8271BDB4`, `8273EB50(child, identity, 1)` clears all `58` child bytes,
stores identity at child+0 and byte 1 at child+54. `8273EB88` branches around
`8273E9F8`; no SDK access or device AddRef occurs on the actual flag-1 path.
Original allocation failure still returns O with child zero; it must not be
described as a successfully allocated child.

`826B10A8` cleanup calls `8271CD00(O)` at `826B1148`, frees O through
`8269BEB0` at `826B1150`, and clears `82D5DA78` at `826B1158`.
`8271CD00` calls child cleanup `8273ECC8`, frees it, and zeros O+20.
`8273ECC8` returns immediately if child+54 is nonzero; otherwise it tail-calls
the SDK-dependent `8273EA90`. It then visits O's eight resource words
`+24..30` and `+38..44`, releasing nonzero entries through `82441708` and
zeroing them. Thus the freshly constructed flag-1/all-zero profile has a
complete CPU-only paired lifetime. Do not extend this to populated resources or
changed child flags. Later render/reset methods are not authorized by construction.

## 82701B70: embedded effects, typed CPU resources and lifecycle

`82701B8C -> 827019E8(82CEFD20,19)` registers **25 decimal** rows, stride `10`:
`+0=blob`, `+4=name`, `+8=created wrapper (initial zero)`, `+C=typed constructor`.
All blobs start `A3D70141`; the next two prefix words are captured without
inventing a complete blob-length or reflection schema. The report inventories
all names, VAs and callbacks. First rows are `fourtapblend`, `littextured`,
`particles`, `quad`, `shadows`; subsequent rows include skinned, terrain, road,
water, dual-texture and z-prepass effects. These are additional FX resources,
not the previously qualified four screen plus sixteen startup shader records.

Each iteration keeps original string/name construction including literal `.fxo`
at `8214E454`, allocates `30`, and calls `826B3880` at `82701A78`.
That CPU constructor initializes vtable `820B7140`, string at +4, M at +C,
SDK effect output +10=0, embedded helper +14=wrapper, and +1C/+20/+2C=0.
At `82701A90` it enters the native cut described above. On return it inserts
the wrapper into M+1C's name-hash tree and stores row+8 at `82701ACC`.

The optional row callback at `82701AE4` receives `(name,M)` and returns a typed
CPU object. Its +14 receives the row index; it is inserted into M+34's tree.
The `particles` row has callback zero: its branch skips construction and retains
the prior r29 value. Preserve that original control flow; do not manufacture
a new typed object for every row. All 25 callback addresses are in the report.

The FX creator `82C1D0C0` conditionally reads pool+100 at `82C1D124`, in
addition to its other SDK work. `826B4828` obtains wrapper+10 through vtable+8
(`826B3908`), stores it in the embedded helper, and reads SDK FX +200/+20C/+210
while constructing CPU parameter caches. `826B7218(M)` subsequently traverses
M+34, invokes each typed object's vtable+C, and resolves four shared-pool
parameters into M+21C/+220/+224/+228. No opaque SDK-layout stand-in is valid.

Paired `827011D0 -> 82701118(table,19)` looks up/removes typed resources from
M+34, invokes their deleting destructors, invokes each row+8 wrapper's deleting
destructor, and zeros row+8. Wrapper `826B4AC8 -> 826B3910` calls SDK FX release
`82C1D560` only if +10 is nonzero, clears it, frees its CPU cache/name storage,
and then conditionally frees the wrapper. A future native FX owner must replace
that paired release as well as creation/reflection. Typed destructor semantics
are not all qualified by this bounded pass.

Optional global hooks: `82D6302C` after registration, `82D63030` after FX cleanup,
`82D63038` after top-level startup, `82D6303C` during top-level cleanup. The
original file contains zero words; runtime values/registration coverage are
unresolved. Preserve the original tests/calls instead of assuming permanent null.

## 826FF0F8: environment resources and original constant images

Input `(r3=context,r4=flag)` uses flag's low byte. It allocates `90` and calls
`8273C2B8(O,context,N)` with N=`100` if nonzero, N=`10` if zero. It publishes O
at `82D6301C`; the zero-flag path also stores byte O+84=0 through `8273BDF0`.
Boot084 does not establish the actual flag value. On its null-allocation branch
the zero-flag path still calls the byte setter: do not invent successful fallback.

`8273C2B8` initializes CPU container O+64 via `82709208(...,400)`, O+4=0,
O+0=N, O+14=context, O+84=1. **First SDK identity dereference:** call
`8273C2F8 -> 82451ED0` (`4BD15BD9`), which increments device+3C.
A whole `8273C2B8` guard avoids this partial constructor; entry pin
`7D8802A6483001099421FF70`. A later native implementation must retain the CPU
container allocation rather than discard the entire operation.

Three `82440578` calls, exact r3..r10 tuples:

- `8273C324`: `(N,N,6,1,0,282801B6,0,12)`, result O+8.
- `8273C34C`: `(N,N,1,1,0,282801B6,0,3)`, result O+C.
- `8273C374`: `(N/2,N/2,1,1,0,282801B6,0,3)`, result O+10.

The original header helper `8243F928` maps resource selector `12` to dimension
code 3 and selector 3 to code 1. The constructor locks six distinct face
selectors 0..5 from `821503A0` stride `28`, level zero, via `82440310`, clears
exactly `N*N*4` returned bytes, and unlocks via `8243E058`. Together these prove
the six-face versus 2D allocation distinction. Packed format's low six bits are
54 decimal (extracted at `8243FA08`); read-only
`K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h` names that value
`k_2_10_10_10_AS_16_16_16_16`. That reference label does not prove a DXGI
substitute, filtering/expanded precision or every other packed flag. This is
not an evidenced RGBA8 substitution. Existing backend lacks this cube API.

O+80 borrows typed `quad` from M. O+60 receives camera `8273BC58(N)`: original
camera/frame helpers, a color raster `(N,N,0,5)` and depth `(N,N,20,1)`.
Camera type 5 adds a distinct resource-bridge dependency. No sampled/filtered
rendering or world-camera semantics are licensed by allocating these resources.

The following three `82B84838` calls use original image-memory payloads:

- `826FF1D4`: `82CED9A8`, size `102C`, output `82D63004`; 32x32, 32-bit
  uncompressed true-color header, repeated stored pixel `FFFFFFFF`.
- `826FF1F4`: `82CEECF0`, size `102C`, output `82D6300C`; same header,
  repeated stored pixel `000000FF`.
- `826FF214`: `82CEE9D8`, size `312`, output `82D63008`; 16x16, 24-bit,
  repeated stored pixel `808080`.

The bounded verifier checks the 18-byte LE TGA-style headers, exact payload
extents/uniform bytes and the first two `TRUEVISION-XFILE.` footers. The third
has no footer and an unused colormap-entry-size byte of 24 despite colormap=0;
do not reject it using an invented all-zero unused-field rule. Complete blob
hashes are in the JSON. These original pixels are available for real 2D assets;
the SDK loader's resulting format/mip policy still needs qualification before
claiming identical native creation. Stores alias `82D63010/18` to output 6300C,
and `82D63014` to output 63004, without additional retain calls here.

Important lifetime limit: top-level cleanup's `826FF248` is literally `blr`.
This does not prove that cube/camera/default-image resources are freed there.
Their complete global lifetime/release closure remains unresolved; do not invent
release ownership from the function's position or claim repeated initialization.

## 826B0D70 and 8271D648: retain the real CPU allocator

Static A=`82D5DA88`, initialization flag `82D5DAAC` bit0. First use sets the
flag and calls `827205E0(A,400000,1000,404,10)`: A+0=size, +4=alignment,
+8=protection, +10=CPU bookkeeping container, +20=0. The nested constructor
allocates `14*10=140` bytes through the original game allocator; preserve its
initialization and failure behavior. It also registers `82CC15B8` through the
original CRT registration helper `82A3CC28`.

`8271D648(A)` calls `8268ECE8(400000,FFFFFFFF,1000,404)` through the real game
allocator, including its lazy allocator resolution. Success stores pointer at
A+C and returns 0; the null branch obtains the original error result through
`82434E28`. This **4 MiB** allocation is not proven dispensable console command
space and must not be skipped, redirected into an unrelated GPU buffer or faked.

`826B1184 -> 8271D6B0(A)` is the normal cleanup call: frees A+C through
`8268ED48` and clears it. The separately registered `82CC15B8 ->8271F600(A)`
frees only container storage A+14 and clears A+14/+18/+1C; it does not free the
4 MiB block. Keep both distinct lifetimes and their original order.

## 82721948: one declaration and a borrowed effect

First call `82721968 ->824458E0`, word `4BD23F79`, resume `8272196C`.
r3=`82CF0428`, a 72-byte declaration of five data elements plus terminator:
stream0 offset0 type`001A23A6`, followed by offsets`10,18,20,28` of
type`002C23A5`. Method0; usages are position0 then texture5 with indices0..3.
Terminal bytes are `00FF0000 FFFFFFFF 00000000`. The previously qualified
type encodings in `docs/native-declarations.md` imply float4 plus four float2
inputs with minimum record extent `30`; this is not the two-field screen layout.

The returned declaration is stored at `82D6CCF0`; status is 0 only if nonnull,
otherwise `80004005`. M's typed `fourtapblend` lookup is stored at `82D6CCEC`.
`827213A8` releases the declaration via `82441708` and clears CCF0. It does not
release CCEC. A native immutable declaration owner can support the CPU resource
lifetime, but this direct SDK constructor path needs a qualified creation/release
bridge. Existing engine-cache support must not be assumed to intercept it.

Keep render consumer `827219B8` unported: it queries a texture descriptor, binds
the declaration, clears, applies the typed effect, draws and resolves. Creation
alone grants neither shader compilation nor this render/copy operation.

## 82721200: original default-only CPU object

Leaf constructor on allocation `F0`; publishes `82D5DA84=O`, vtable `8214EAB8`,
bytes +E0=1/+E1=0. Exact float words by offsets:

- +10/+20=`3ED6D6D7`; +14/+24/+50/+54=`3F000000`.
- +18/+28/+30/+34/+38/+3C/+44=`3F800000`.
- +1C/+2C/+40/+48/+4C=`00000000`.
- +58=`3DCCCCCD`, +5C=`41200000`, +60/+64=`38D1B717`.

These are this leaf's own original defaults, not names inferred from numeric
similarities. Unlisted bytes remain untouched. No calls, SDK access or native
resource allocation. `827212B8` resets vtable to `8214EAB4` and clears singleton;
deleting wrapper `827212D8` additionally frees O when r4 bit0 is set. Retain AOT.
Main's first manager `826B6F60` has separate constants; none are substituted here.

## 82720A38: CPU shadow-message receiver

Allocation `C`, vtable `8214EA40`, word +4=1. It registers four original message
descriptors with `826908B0`, then subscribes O to each through `826900B0`:
`82D6CCDC/iMsgDontOverrideSunDir`, `82D6CCD4/iMsgOverrideSunDir`,
`82D6CCCC/iMsgShadowTrackPlayerPos`, `82D6CCE4/iMsgShadowTrackCameraPos`.
Name literals are `8214EA94/80/64/48`, respectively. O+8 borrows M's `shadows`
typed resource through `826B7088`; orchestration publishes O at `82D5DA7C`.

Destructor `827209A0` explicitly unsubscribes all four with `8268F470`, releases
all four descriptors with `82690648`, and calls `82690790` deletion notification.
Deleting wrapper `82720B18` frees O when requested. O+8 has no release here:
the registered typed effect belongs to M's resources. Retain these CPU helpers
once real typed lookup exists; callback rendering is not qualified by construction.

## 82702028: crossfade raster and message descriptor owner

Allocation `18`, +4=1, primary vtable `8214E49C`, secondary interface at +8
eventually `8214E498`, singleton `82D09850=O`. Word +10 and bytes +14/+15/+16
are zeroed. Current width/height come from `82E3DCE0/+4`.
At `827020A8`, `823F7278(width,height,18280186,500)` creates O+C's raster.
Then it registers descriptors `82D6C06C/iMsgCrossfadeReadyToCommit` and
`82D6C064/iMsgCrossfadeCommitComplete`. The constructor does not subscribe O.

`823F7278` is the mixed engine cut; entry pin `7D8802A6486451419421FF70`.
For this request the two flag masks leave `500` unchanged, then add `80` for
the original `82408130(width,height,0,580)` allocation. Subsequent format check
`823F65D0 ->8244EA18` precedes SDK texture creation `82440578`. Its extension
is addressed using dynamic `82E3DC94`; do not hardcode a texture pointer offset
into the public raster or treat this as already-qualified camera type2/BC3 type3.
Actual packed format is `18280186`, low six bits6, corroborated by the reference
as 8:8:8:8; other packed layout/usage bits and this full resource bridge need proof.

Paired `82702100` calls original raster destroy `82407DC0` on O+C, releases both
descriptors, resets the secondary vtable, clears `82D09850`, and issues original
base deletion notification. `82702348` supplies the deleting wrapper; secondary
thunk `827020F8` adjusts this by -8. A native snapshot resource needs genuine
owned storage and this lifetime. Crossfade capture/filter/composite behavior is
outside this constructor evidence; a blank substitute is not sufficient.

## Bounded remaining work

Main can retain the first two CPU constructors and stop before the FX loop, or
advance through its genuine CPU wrapper allocation to `826B4B88` with explicit
partial-lifetime handling. Next resource milestone: one exact `fourtapblend`
effect's original metadata/parameter initialization and paired destruction,
followed by its typed consumer contract. Keep rendering unsupported until its
actual shaders, bindings and state have been qualified. Later environment,
declaration, crossfade and callback requirements above are not reasons to fake
the first effect or skip intervening original CPU effects.

This pass does not close every typed callback, optional callback registration,
environment teardown, FX technique/sampler state, expanded texture precision,
or subsequent rendering path. These are explicit fidelity limits, not requests
for new approval. Production adaptation decisions remain with main.
