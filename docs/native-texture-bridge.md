# Native loading texture bridge

Status: frozen bounded byte-verified contract. Reproduction and negative-input
fixtures pass; this is static evidence, not an implemented or executed bridge.
Only the embedded `frame1/frame2` loading dictionary is in scope. This does not
implement rendering, the camera/depth raster bridge, or general ITXD loading.

`analysis/simpsons.pe` is the flat image at `82000000`, 15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Addresses, offsets and integer constants below are hexadecimal unless stated.
Original instructions, not decompiler types, establish these contracts.

## Callback and stream ownership

`8240A278` is engine slot 26, `E+B0`: r3=original stream, r4=guest pointer to
texture output, r5=enclosing native-texture chunk length. `823FF8F0` invokes it
with r4=`SP+60`, after finding chunk `15`. The callee ignores incoming r5; it
returns r3=1 and stores the texture through saved r4 only at `8240A8C4/C8`.
Failure returns zero and leaves output untouched. A native replacement should
check the full output word before any allocation and bound parsing by r5 too.

Retain original `82862A28`: it opens a memory stream using
`823F9598(3,1,&{8215F820,20150})`, finds dictionary chunk `16`, calls
`823FF7A8`, and closes the temporary stream with `823F94A0`.
The dictionary reader finds the inner struct and each native chunk, invokes
the callback, then processes each texture's extension through
`823FA2C8(82CD1DB8,stream,texture)` at `823FF910` **before** insertion at
`823FF924 -> 823FE020`. Finally it processes dictionary extensions using
registry `82CD1DD0`. Do not consume either extension in the native callback.

Serialization order is **frame2, then frame1**, despite the opposite lookup
order. Both callback inputs have native-chunk length `10088`; struct size is
`1005C`. For frame2 the callback starts at inner chunk header `8215F848`, reads
struct bytes `8215F854..8216F8B0`, and must leave the cursor at `8216F8B0`.
For frame1 those addresses are `8216F8DC`, `8216F8E8..8217F944`, and `8217F944`.
Ranges here exclude their end address. These cursor endpoints are the next
extension chunk headers, not the enclosing native chunk ends.

The callback finds struct chunk 1 via `823F7F58(stream,1,&size,&version)`,
requiring version `35000..37002`, then reads 48+10 header bytes using
`823F90C0`. Retaining these CPU helpers preserves stream position and error
handling. For memory streams type 3, `823F90C0` copies from
`BE32[stream+14]+BE32[stream+C]`, limits reads to `BE32[stream+10]`, increments
`stream+C`, and returns the actual byte count. Require every exact read.

`renderer/native_texture_stream.cpp` already accepts the verified 58-byte BE
header, four-byte LE level length, and linear BC3 payload. The original stream
helper does not byte-swap payloads. Pass the exact struct contents to that
decoder; do not apply tiling or a second byte swap. Chunk parsing stays original.

## Raster and native resource cut

For both assets: format=`300`, nativeFormat=`1A200154`, dimensions=256x256
(decimal), depth field=16 (decimal), levels=1, type=4, flags=9.
The flags select `8240A304..A4AC`: original call `8240A334 -> 82408130` is
`(width,height,depth,384)`. Bit `80` suppresses allocation during raster
construction; original `8240A474 -> 82440578` allocates the texture later.

Retain `82408130` for original raster allocation, base fields and plugin
constructors. Its `E+58` callback at `824081BC` is
`823F7070(r3=0,r4=raster,r5=384)`. The native raster callback needs this exact
type-4 unallocated profile in addition to the parent's separate camera work.
Let R=raster and X=R+BE32[82E3DC94]; never hardcode the plugin offset.

The wrapper establishes R+0=self, R+4/+8=0, R+C/+10=dimensions, R+14=depth,
R+1C/+1E=0 (halfwords), R+21/+22=0 (bytes). For flags `384`, callback
`823F7070` and its format validator establish byte R+20=4, R+21=80,
R+23=3, and word R+14=10. The callback clears X+0/+4/+C/+18, sets
bytes X+8/+9/+A=0 and X+B=FF. No texture allocation occurs in this branch.
The constructor then runs the original raster plugin registry `82CD1E28`.
Do not zero unspecified raster fields merely because their meaning is unknown.

After successful texture allocation, the original reader clears bit 80 in
R+21, writes X+18=`1A200154`, X+8=1 and low nibble X+A=1; X+9 remains zero.
It clears bit 10 in R+23 (still 3 for this profile). X+0 originally holds an
SDK texture; native mode must associate this field with a checked, unmapped
identity backed by an owned real `Graphics::Texture`, never a fake SDK header.
Only publish the identity after successful resource creation/upload.

The type-4/80 branch at `823F721C` goes directly to success. It does **not** call
the camera list helper `823F5DA8`; the stream reader does not add that node later.
Do not extend the camera owner by unconditionally allocating its list node for
this profile. Original format validation `823F6E68` uses table row `82062B48`
(`1828014F 10 01 00 00`) to establish depth 10 and format byte 3. It calls
`823F65D0 -> 8244EA18` for a CPU-only format query, not device allocation.
The native bounded callback can validate the exact profile using the real BC3
backend and preserve the enumerated CPU fields without executing SDK helpers.

**Persistent lock/unlock side effects must also survive.** Original lock callback
`823F53D8` saves dimensions in R+28/+2C at `823F5538/3C` and writes level zero
to byte X+B at `823F5574`. Unlock callback `823F5588` clears R+18 (stride) and
R+4 (pixels), restores R+C/+10 from the saved dimensions at `823F5608..5620`,
and clears flags 2/4 in R+22 at `823F5680/84`. For this one-level profile final
R+28/R+2C=100, R+18=0, R+4=0, R+22=0, and **X+B=0**, not constructor value FF.
The auto-mipmap branch is not taken: the reader already cleared format bit 10.

In contrast, original X+C is a temporary SDK surface that unlock releases
without clearing its stale pointer; X+10/+14 hold its lock pitch/mapped pixels.
These are not native persistent resource identities. Native immutable textures
must keep those unsupported temporary slots unavailable/zero and reject guest
lock/subsurface access. Do not fabricate released SDK pointers or invent a
console pitch. This intentional native representation difference is separate
from the required stable CPU fields above.

Replace the complete engine stream callback rather than running its mixed
upload body against a native identity: `8240A79C` directly reads `[X+0]+30`,
and `82408208/82407BA8` lock/unlock dispatch to SDK services. Neither the
SDK allocation at A474 nor upload `82534228` should execute. The existing
native decoder plus `NativeBackend::createTexture` supplies actual BC3 backing.

## Texture CPU object and association

Retain `823FDE20(r3=raster)` after successful native backing acquisition.
It allocates from the original texture pool via E+138, stores T+0=raster,
T+4=0 (no dictionary), zeroes name/mask first bytes T+10/T+30,
sets BE32[T+50]=00001101, BE32[T+54]=1, and runs texture plugin constructors
from `82CD1DB8`. This transfers responsibility for the raster to the texture's
final destruction path; it does not add a raster reference.

The reader writes sampler low byte at T+53, address-U nibble at bits 8..11
and address-V nibble at bits 12..15 of BE32[T+50]. `1102` therefore yields
BE32[T+50]=00001102. Retain original bounded name/mask setters
`823FDEF0(T,name)` / `823FDF88(T,mask)`; they use the engine string callbacks.
Texture names remain original CPU fields, not native GPU labels only.

`82737260` registers texture plugin EA2F, size 20, with constructor
`82737160`, no destructor, copy callback `82737068`, and stream read callback
`82737190`. Offset is stored at `82CF0600`. Constructor zeroes eight words and
sets plugin+4=T. Stream read requires eight bytes and copies the two raw words
to plugin+0/+4, replacing that initial self pointer. Keep this original path;
the payload's higher-level meaning is not inferred from the two values.
The frame2 payload is `EC17D8E5 A1035778`; frame1 is `EC17D8E4 A1035777`.
The other six plugin words remain zero after this constructor/read pair.
The image's direct callsites to the texture-plugin and stream-registration
wrappers are `8273728C/B8`; this is not a proof excluding indirect registration.
Validate the live registry/offset instead of assuming a fixed texture size.

## Dictionary and destruction

`823FDC28` allocates the dictionary from the original pool, initializes its
texture list sentinel D+8 and global dictionary list link D+10, and executes
dictionary plugin constructors. `823FE020(D,T)` detaches an existing dictionary
link if present, writes T+4=D and links node T+8 into D+8. It adds **no reference**.
`823FE0A8(D,name)` is a case-insensitive borrowed lookup and also adds none.
Original loader stores D at `82E071EC`, frame1 at `82E071E4`, frame2 at
`82E071E8`; retain these original stores and subsequent CPU services.

`82862B18` calls `823FEC80(D)` at `82862B60`, then clears all three globals.
Dictionary destruction iterates textures using `823FDEC8`: decrement T+54;
only when no positive references remain does `823FD900` run texture plugin
destructors, unlink the texture, call `82407DC0(T+0)`, and return T to its pool.
`82407DC0` runs raster plugin destructors, calls E+5C
`823F62A0(0,R,0)` at `82407E00`, then returns R to its allocator.
Retain these wrappers. Replace the platform raster destroy callback for native
owned textures, including its eight-stage binding-cache obligations; do not
let `82441708` consume a native identity. A texture wrapper's reference count
and the host shared resource's references are separate lifetimes.

For bounded unbound teardown, `82401AC8(stage)` reads
`BE32[82D0E3F8+18*stage]` for stages 0..7; requiring all eight to differ from R
allows the original destroy loop's no-match behavior. Once bound, a native
destroy path must also retain the CPU effects of `82401940(0,stage)`: stage-0
alpha-state/dirty-queue updates and the stage raster cache clear, as well as
actual native unbinding. The helper's `82401AB8 -> 824408E0` is an SDK call;
calling the entire helper with CAF8=0 is not a solution. Existing parent state
ownership should handle this later rather than inventing an SDK texture object.

Failure ownership matters: before T exists, release the native resource and
destroy R with `82407DC0`; after T exists, release through `823FDEC8`, so original
plugin/raster cleanup runs exactly once. Never destroy R separately after T
has acquired it. The original dictionary reader's extension-failure branch
`823FF91C -> 823FF968` cleans the already inserted dictionary entries but does
not explicitly release the freshly returned, not-yet-inserted T. A future
failure-complete bridge must track this transfer or validate the exact loading
extensions before allocation; do not claim malformed-stream rollback merely
because the native callback's own transaction is correct.

The loading path starts each T at refcount 1, inserts without incrementing,
and keeps borrowed globals, so dictionary destruction reaches the final release
in the verified case. Extra guest references surviving dictionary destruction
are outside this contract: the original dictionary destructor does not explicitly
detach a texture whose decrement leaves a positive reference count. Native
shared ownership may retain GPU storage for in-flight work without adding a
guest texture reference or extending the guest wrapper's lifetime.

## Concrete integration order

1. Parent adds an entry replacement for `8240A278`, evidence
   `7d8802a64863211d9421feb0`, preserving the callback ABI and boolean return.
   Keep `823FF7A8` and all original outer loading code. Check the output word,
   memory stream type/range, exact native chunk/struct extent and supported
   profile before publishing anything. Original r5 was not a bounds guarantee.
2. Use the original find/read helpers with a checked callback frame. Preserve
   all stream advancement; assemble the decoder's bytes without interpreting
   the extension. The original one-level payload read uses scratch `82D101E0`
   (size 10000 is below its 40000 threshold); it allocates no large temporary
   buffer on this profile. The exact-pinned loading extension can be checked
   read-only before allocations while its original callback still executes later.
3. Retain original `82408130(100,100,10,384)` and raster plugin construction;
   provide the bounded type-4 create/destroy branch in parent ownership. Create
   the real native BC3 texture from the existing decoder, register its lifetime,
   and apply the enumerated post-allocation and post-unlock CPU fields. Do not run A474,
   the lock/unlock dispatch, A79C, or the tiled-upload body.
4. Retain `823FDE20`, original name/mask setters and texture plugin construction;
   apply sampler `1102`. Commit the output only after all native and guest
   ownership is valid. On error, use the correct pre-/post-T cleanup above.
5. Original dictionary code reads EA2F, links T, and processes dictionary
   extensions; original loading code closes the stream and borrows both frames.
   Verify T+0/R+0 identities, T+4 dictionary, T+54=1, names/sampler, dynamic
   EA2F bytes, exact stream cursor, and native BC3 byte readback. Exercise native
   allocation failure, second-texture failure, extension failure, and dictionary
   teardown before calling the integration failure-complete.

The actual parent `build/boot-034.log` reaches raster `E1A53F00`, flags `384`,
after successful native camera/depth construction. That supports the selected
original path, but does not establish any of the proposed texture bridge's
success, cleanup, native sampling, or game pixels.

## Reproduce without changing other work

From `K:\SimpsonsNativeCopy`:

```powershell
python -B tools/analyze_texture_bridge.py --self-test
python -B tools/analyze_texture_bridge.py --report
python -B tools/analyze_texture_bridge.py --verify-report
```

The report command writes **only** `analysis/native-texture-bridge.json`.
The other commands write nothing. The tool requires the existing
`build/generator-ninja/SimpsonsDisasm.exe`, checks every returned word against
the pinned flat image, and records disassembler/tool/asset-parser hashes.
It reuses `tools/extract_loading_art.py` for the already verified asset profile;
no asset decoding work or generated sources are changed. The JSON contains
49 bounded function bodies (including complete mixed bodies for evidence,
not permission to execute every path), 48 explicit byte pins, original extents,
direct call targets, exact chunk coordinates and CPU field requirements.
Four tests cover call-target decoding, byte pins/range checks, image rejection,
and both real loading profiles with malformed-image/chunk rejection. All pass.

Bounded integration still requires parent-owned native type-4 raster create /
destroy support and texture binding support. Existing screen helper sampler
choices are separate from the stored texture sampler word; do not change that
pipeline here. No camera/depth evidence is duplicated, and no runtime, renderer,
configuration, original image or reference project is modified by this work.
