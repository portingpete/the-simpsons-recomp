# Native ITXD copy, registration and lifetime contract

This is a bounded implementation plan for the unowned raster reached in
actual156. No runtime implementation or live load/release execution is claimed.
The existing `analysis/render-boundary.json` supplies eight already-reviewed
functions; the accompanying proof rechecks their literal bytes and adds only
small ownership cuts. Asset identification, texture decoding and actual157
capture belong to Main. No asset scan is repeated here.

Reproduce this sidecar only with `python -B build/itxd-lifetime/verify.py`.
Outputs stay in `build/itxd-lifetime`. The immutable flat image is 15,466,496
bytes, base `82000000`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
`verification.json` records the exact checked word/pin counts and report hash.

## Immediate finding

The normal load entry explicitly selects **copy mode**, not source borrowing.
It creates a **100-byte metadata allocation and a separate N-byte pixel
allocation** (all sizes/offsets below hexadecimal). Native texture registration
must retain both allocation identities and the original payload size before
later CPU code clears their extension bookkeeping. The metadata record is not
an `EngineRasters` pool allocation, and its embedded SDK header is not a native
GPU identity. Reusing the loading BC3 attachment routine would misclassify it.

Actual156 reports R=`E1AC6548`, extension offset34, X=`E1AC657C`,
`[X]=E1AC659C`, X+4=0, X+8=`01000100`, X+18=`1A200153`.
These addresses match the copy layout exactly with T=`E1AC64D0`:
R=T+78 and H=T+CC. Actual157 independently logs that T, the name
`HighlanderStdBold6060b`, dictionary0, sampler3302 and refs2. It also logs
H+20=`E6957053`, identifying the relocated descriptor base `E6957000`.
Main identifies the corresponding original `frontend_split16.itxd` record930;
this sidecar does not repeat that asset comparison. The descriptor base alone
does not prove the original allocator identity or N: capture both at copy time.

## Load context, cache and original CPU work

At `826F26B0`, r4=L is the load-request record. `826F26CC=83DF0004`
loads L+4 as the resource-owner input. Original `8271AD48`, called at
`826F26D0=48028679`, computes B=`[S+C]+[S+14]`, where S=`[L+10]`.
L is a temporary load request, not the persistent resource wrapper.
`826F26D8=80BF0014` forwards L+14 as r5. The bounded named-resource
parser proof below now establishes **L+14 as the declared payload byte count**.
`826F26DC=38C00000` sets r6=0 before `826F26E4=4BFFFDF5` calls
`826F24D8(owner,B,L+14,0)`.

The load walker first creates a group with destructor `82736D50`
(`826F2508=388B6D50`, `826F250C=480050AD`). Its sentinel is
B+8*(BE16[B+4]+BE16[B+6]+1)+8. Links are offsets relative to B; each
texture record begins eight bytes before its link node. Check arithmetic,
sentinel, cycle bounds and every selected record against the actual resource
envelope before allowing this walk. The walker itself does not enforce these
bounds. Neither the four-byte resource count nor the inspector's 16MB budget
should be invented as an original size limit.

Copy-mode lookup uses the original EA2F texture-plugin offset `[82CF0600]`:
`827370E0` returns T+that offset. The walker queries plugin+4 through
`826F25F0=48005F31` -> `826F8520` (global index `82D62FB4`). A hit
branches at `826F25F8=409A0038` straight to owner attachment at `826F2630`.
It must reuse the existing native registration; do not copy or upload again.
A miss executes these original calls in order:

| Call PC / original word | Original responsibility |
|---|---|
| `826F2604 48044955` | `82736F58(sourceTexture,B)`: allocate and copy |
| `826F260C 4804489D` | `82736EA8(T,T)`: relocate the copied record |
| `826F2614 48044725` | `82736D38(T)`: clear raster+44/+48 |
| `826F2628 480064C1` | `826F8AE8`: retain plugin flag semantics and publish lookup indices |
| `826F2634 48004FE5` | `826F7618`: original owner-list insertion and reference increment |

Keep the full walker, cache lookup, plugin data, iteration, flags and index
publication in AOT. The separate low8(r6)!=0 branch relocates the resource in
place and installs `82736DF0`, an original no-op destructor. Its backing is
borrowed from the surrounding resource lifetime. Do not accept it under a
copy-owned cleanup contract. The normal `826F26B0` path sets r6=0; other
callers need explicit validation rather than a blanket global hook permit.

## Source envelope: named 0716 parser now closes the length field

The extra trace is bounded to the descriptor walker and its immediate request
stores. It does not audit the streaming subsystem. `82711D5C=38600716`
sets tag716, `82711D60=388B1B28` selects callback `82711B28`, and
`82711D64=48006285` registers it through `82717FE8`. The neighboring
723 handler is a different format; it is not automatically admitted here.

At named callback `82711B28`, r3=Q (parsed chunk header), r4=S (stream).
Let c0 be its initial S+C, V=[S+14], and D the BE32 at V+c0.
`82711B48=4BCE7579` reads those four bytes via original `823F90C0`
into SP+50. That stream call advances its cursor by four. A=V+c0+4 is
the descriptor start. `82711B54=81210050` loads D;
`82711B5C=7D6B4A14` and `82711B64=917F000C` advance the cursor
by D. Therefore at the typed callback, B=V+[S+C]=A+D is the first
payload byte. This matches the existing `inspect_assets.resource_chunks`
envelope coordinate: chunk start+12+4+D, not a compressed STR offset.

The original named-descriptor walk maps its tail precisely:

```
I = A + 4 + BE32[A]                 # after padded resource name: four ID words
U = I + 10                         # type-name length word
V1 = U + 4 + BE32[U]                # source-path length word
W = V1 + 4 + BE32[V1]               # extra-string length word
tail = W + 4 + BE32[W]              # opaque tail word
M = BE32[tail + 4]                  # declared resource payload length
```

These remain byte offsets; 10 above means sixteen bytes. Instructions
`82711B68..6C`, `82711B7C..84`, `82711B88..90`, and `82711B94..98`
perform that walk. At its end r27=tail-4, so **`82711C14=80FB0008`
loads exactly M from r27+8 into r7**. It does not load an extra-string
length or a whole decoded-entry size. `82711C10=7FE6FB78` passes S in
r6; `82711C20=4BFFFC09` calls original `82711828`.

In that dispatcher, `82711844=7CDA3378` saves r6 as r26 and
`82711848=7CF93B78` saves r7 as r25. L is its SP+60 temporary request:
`827118B0=93410070` stores S at L+10 and
**`827118B8=93210074` stores M at L+14**. Its indirect type-loader
call is `82711918`, LR8271191C. The independent generic copy helper
`8271AC80` corroborates the length: `8271ACEC=809F0014` uses L+14
as allocation size, and `8271AD28=80BF0014` uses it as copy length
before `8271AD34=4832204D` copies from the same B. This corroboration
does not replace the named descriptor's original source proof.

S is the ordinary RW memory-stream layout, corroborated by the earlier
`native-texture-bridge.md` proof and the bounded original reader:
`823F90EC=2F0B0003` selects type3, `823F9110=817F0010` reads total
stream length, `823F9114=815F000C` reads its current offset, and
`823F9144=817F0014` reads the backing base. The reader clips actual
read bytes to length-offset and advances S+C. Consequently the minimal
typed ITXD entry guard can establish the exact declared resource span:

```
require original named-loader scope and [S+0] == 3
cursor = BE32[S+C]; streamLength = BE32[S+10]; streamBase = BE32[S+14]
M = BE32[L+14]
require cursor <= streamLength
require M <= streamLength - cursor
B = uint64(streamBase) + cursor
require B + M <= 0x100000000 and readable guest backing for [B,B+M)
```

Then require every dictionary/texture/header/payload interval inside [B,B+M),
including Psrc..Psrc+N, with checked arithmetic. M is the dictionary resource
length; N is each separately copied texture allocation's requested length.
Do not substitute M for N. S+10 can cover other resource chunks, so bounding
only by that larger stream would permit cross-resource reads.

The typed entry can verify `lr=8271191C`, L=r1+60 and its original request
fields before entering the ITXD frame. It must retain the underlying stream
only through the synchronous copy; the native atlas must not retain a borrowed
pointer into this decoded resource after the original stream advances/frees.
The ITXD source need not be writable on the admitted copy-mode path.

For malformed named-descriptor rejection before the original walk itself,
add a preflight-only hook at `82711B28` (entry bytes
`7d8802a64832a8959421ff70`). Capture Q, S, c0 and Q+4 (body size).
Validate four-byte length read, D and all descriptor string/tail reads against
both the stream remainder and that body. The original suffix
`82711C24..38` computes its remaining skip as `[Q+4]-D-4`, corroborating
that Q+4 is the body byte count. For the known 716 profile, require
`4+D+align4(M)==[Q+4]` as in the existing asset inspector. No malformed
descriptor protection before those reads is claimed from a later L-only hook.

The smallest diagnostic, if another live profile breaks these guards, is
`82711C20` immediately before dispatch: r31=S, r27=tail-4, r7=M,
r26=Q, r29=resource-name pointer, r28=type-name pointer, SP+50=D.
Capture S+0/C/10/14, Q+0/4/8, the tail's two words and calculated B.
This exposes the complete bounds without tracing additional stream routines.
Actual157 is a bind capture, so its particular live M, S and source envelope
have not yet been observed; the field semantics above are original-byte facts.

## Separate metadata and payload allocations

In `82736F58`, source texture U is r25. Rsrc=B+[U] and
Xsrc=Rsrc+[82E3DC94]. The helper loads payload source offset Xsrc+14 at
`82736F8C=816A0014`, forming Psrc=B+offset, and N=Xsrc+10 at
`82736FA4=838A0010`. Validate U..U+100, raster/extension/header ranges,
and Psrc..Psrc+N against the containing resource bytes. Require nonzero
usable fields on the accepted profile; null branches are not robust graceful
failure handling. Page accessibility alone is not resource ownership.

The original allocator comes from `82D57244`, initialized through `8268E7F0`
if needed. Its vtable+0 call at `82736FE4=4E800421` requests 100 bytes,
with three zero option words. `82736FF4=4843D11D` copies exactly 100 bytes
from U into T. The following original stores prepare relative pointers:

| Field | Copy-helper value |
|---|---|
| T+0, raster at T+78 | 78 (raster and self-root offsets) |
| X=T+78+live extension offset; X+0 | CC (embedded header offset) |
| X+14 | P-T, written after the separate pixel copy |

For extension offset34, X=T+AC and H=T+CC. H+0..33 fits precisely through
T+100. R is not a standalone allocation, and H must never be individually
freed. The source texture-name/plugin bytes remain from the original 100-byte
copy; preserve them.

At `8273703C=4E800421`, allocator vtable+20 receives r3=allocator,
r4=N, r5=FFFFFFFF, r6=1000, r7=404, r8=1. Preserve this call and its
real mapped allocation. `8273704C=48305D35` copies N bytes from Psrc
into the returned P. `82737050=7D7FF050` computes P-T and
`82737058=917D0014` stores it at X+14. The payload allocation may lie
outside the metadata allocation; requiring P inside T..T+100 is wrong.

Relocation `82736EA8(T,T)` adds the base to nonzero T+0, R+0, X+0
and X+4. It computes the pixel argument from X+14 but does not replace
that relative field with an absolute pointer. At `82736F38=484E9F49`
it calls `82C20E80(H,P,P)`. That helper patches descriptor address bits
in H+20 and conditionally H+30, preserving their low12 bits. These are
original serialized-header address adjustments, not native resource creation.
They can stay CPU AOT while H remains original metadata. Native registration
must capture the pre-adjustment descriptor for decoding; do not decode the
adjusted guest addresses as original file offsets or relocate twice.

Crucially, `82736D38` then writes zero to R+48 and R+44
(`82736D40=914B0048`, `82736D44=914B0044`). With extension34 these
are **X+14 and X+10**. Thus actual156's zero fields do not imply an empty
texture. Binding-time reconstruction from those fields is too late.

## Proposed narrow hooks

These are implementation choices, not installed changes:

1. Preflight at `826F24D8` (original entry bytes
   `7d8802a648349ee19421ff70`) under checked normal-load context. Record
   the original resource envelope and mode. The envelope comes from the proved
   named-resource request: S=[L+10], M=[L+14], B=[S+14]+[S+C], with the
   type3 stream and M bounded as below. Per-record N alone cannot prove that
   all source reads remain inside that envelope.
2. Observe before `82736F58` and at `82737050` (word `7d7ff050`,
   continue original instruction) to associate U/B, T=r31, P=r30, N=r28,
   allocator and pre-relocation header. Preserve both allocations/copies.
   Keep this as a pending transaction until relocation and decoding succeed.
3. Observe at `82736F3C` (word `7fe3fb78`) after relocation, before
   `82736D38` clears X+10/+14. Validate the owned copied record, create the
   real native texture from an owned snapshot, and register by R/T/generation.
   Also verify original index/owner publication at the load return. Preserve
   H and X+0 as original CPU metadata; resolve R through a separate typed
   registry. Do not feed H to the loading `attachTexture` API, whose type4
   metadata contract is specifically the embedded BC3 stream profile.
4. Preflight final copied release at `82736D50` (entry bytes
   `7d8802a6483056799421ff80`). Replace only `82736DA0..DB0` with a
   scoped native release adapter; exact bytes are
   `4bd09499388000007fc3f3784bd07295`. This replaces lock/unlock calls,
   not the allocator frees. Supply the recorded P at SP+54, the sole lock
   output consumed by this release suffix; do not advertise a general lock
   operation or invent a pitch contract. Continue at `82736DB0`.
5. Retire the registry generation as part of this checked final-release
   transaction; confirm both original frees at `82736DE4` (word `38210080`).
   Native GPU references retain outstanding use independently of guest memory.

Current `82440238`/`8243E040` hooks are shadow-only lock/unlock adapters.
They reject this destructor's callers `82736DA4/82736DB0`. Broadening them
without lifetime checks risks freeing unrelated shadow staging. An alternate
scoped extension there needs the same recorded-P contract. `82441708` is
not called by the copied-record destructor and is not its release boundary.

For that scoped release substitution, require the admitted descriptor's level0
base offset to be zero and its recovered base to match the recorded allocation
P. Main's matched source has zero pre-relocation base address; actual157 logs
the corresponding relocated field. The association with the allocation returned
at `8273703C` still needs a load capture.
Do not generalize P to a subresource/interior pointer for other descriptor
layouts. The existing shadow-lock proof supplies the output ABI, not proof of
this ITXD's flags1003 synchronization behavior; native outstanding-use ownership
replaces that SDK bookkeeping rather than running its device-dependent code.

## Group ownership and final release

`826F7618` allocates an eight-byte group list node, retains primary/additional
owner links in the texture plugin, and increments T+54. `826F8168` decrements
T+54, removes/promotes owner links, and invokes `826F80C0(T,group+8)` only
when the departing primary owner has no remaining owner-list successor.
Preserve this decision: **a generic T+54==0 test is not equivalent**. The
copied initial reference and group references are not native COM references.

`826F80C0` removes original lookup entries (`826F80E8`, `826F80F8`),
runs plugin callbacks, then dispatches the copied destructor at
`826F8158=4E800421`. `82736D50` obtains H from the raster extension,
locks/unlocks only to recover its allocation pointer, frees P using allocator
vtable+24 at `82736DC4=4E800421`, then frees T with size100 through
vtable+4 at `82736DE0=4E800421`. Preserve these allocator families and
ordering. Never free H/R, substitute `freePhysical` for an unverified allocator,
or release the source dictionary instead of the owned copies.

This callback does not run `82407DC0` or the ordinary eight-stage raster
unbind loop. Initially reject final release while R is still in a bound engine
cache, before index/list mutation where possible; do not silently add CPU
cache changes and call them original behavior. Shared-group release must not
invalidate a still-owned native texture.

## Failure handling, validation and limits

Validate complete resource spans, relative-add overflow, mutable metadata and
allocator identities before original publication. Allocation failure after T
but before P needs a real partial-construction cleanup; these original helpers
do not supply a complete rollback path. Once original lookup/list publication
has occurred, a failed native upload cannot merely erase a host map and return
success. Predecode/preflight before publication, or retain a scoped transaction
with an explicit terminal failure boundary. Repeated registration, cache-hit
reuse, stale generations and wrong-owner frees must fail visibly.

Main's focused integration should execute actual AOT load/copy/relocate and
group release on an identified original asset: byte-check both copies and
pointer changes, assert only X+10/+14 clear, validate owner refs/indices, and
prove two groups share one native texture. Release one group, draw/read back,
then release the last and verify both original frees and stale rejection.
Negative cases should cover truncated envelope, oversized N, bad offset,
cycle, unsupported borrow mode and allocator failure without partial exposure.
Compare native texture bytes/pixels with Main's independent original decoder.

The live texture name is verified by actual157. Independent source-byte match,
authored mip layout, alpha premultiplication, interpolation and live release
success are not established by this sidecar. Format1A200153 is consistent with
the existing BC2/DXT2_3 descriptor evidence. The source-length field and its
parser origin are now proved; its live value, payload decoding and original
allocator association remain Main's load capture and asset work.
