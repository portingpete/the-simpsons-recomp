# Original crossfade snapshot ownership

The native crossfade snapshot is a1280x720 RGBA8 texture with one level and
unspecified initial pixels. Original construction, public raster allocation,
CPU format normalization, publication, message registration and paired deletion
remain executable AOT code. Native storage replaces the verified texture
factory request. Capture and composition are still unqualified; no original
game image or gameplay claim follows from this constructor.

## Live request and original CPU behavior

The diagnostic probe linked against build163 ran actual muted startup to the
existing flags580 failure. It observed82702028's owner at82D09850, dimensions
1280/720 from82E3DCE0/4, and the live stack chain through823F7278 and82408130.
Raster allocation is54 bytes with extension offset34. The crossfade object's
unwritten+C field still contained retired allocation bytes before the raster
returned. Its unused byte+17 also remained untouched. Neither field is assumed
zero before its original assignment.

The constructor passes format18280186 and flags500 to823F7278, returning at
827020AC. That helper adds80 for the CPU raster allocator82408130. Callback
823F7070 initializes the pointer/format fields and calls original823F6E68.
For580, the normalizer changes depth to32, type to0, flags to80 and the public
format byte to5. It does not allocate pixel storage or insert a device-reset
list node on this path. The native callback retains exactly that field set and
the original CPU normalizer, alongside its existing camera and BC3 profiles.

Original format check823F65D0 returns supported through8244EA18. Then the shared
factory82440578 is called with1280,720,1,1,0,18280186,1,3. Its r9 input is unused
by the original factory; the engine's r28=1 also suppresses reset-list insertion.
This is a distinct profile from the earlier off-screen camera rasters.

CPU header builder8243F928 verifies surface format6, endian2, tiled storage,
ZYXW swizzle and one mip. Its reported primary allocation is398000 bytes with
no secondary allocation, reflecting padded console storage for a logical
1280x720 image. The port stores logical RGBA8 pixels in native graphics memory;
no runtime tiled-address or console command processing is introduced.

The original header builder preserves two complete header words and portions
of five fetch words. The regression checks its complete output against the
original bit-insertion masks for four initial byte patterns. It does not assert
that reserved or untouched fields become zero. This corrected an initial test
assumption; production decoding/header code was not changed to satisfy it.

## Native ownership and paired deletion

A hook at823F7278 verifies the exact crossfade caller, dimensions, format,
owner fields and native thread, then runs the original helper. The existing
raster callback admits580 only within that registered original frame. It keeps
the original CPU fields, leaves reset-list membership absent and records a
pending snapshot association.

The existing82440578 factory cut creates a real native writable RGBA8 texture.
It returns a unique unmapped identity; original instructions publish it into
extension+0, clear flags80 and the public format nibble, and store18280186 at
extension+18. No initial clear or fabricated snapshot data is supplied.

The hook before823F7494 verifies those completed original fields. Original
82702028 then publishes the raster at owner+C and registers
iMsgCrossfadeReadyToCommit and iMsgCrossfadeCommitComplete. A hook before
827020DC verifies the completed parent association. The constructor performs
no subscription. Public native texture access rejects incomplete ownership.

Original82702100 destroys the raster through82407DC0, releases both message
descriptors, restores the secondary base interface, clears82D09850 and sends
the original deletion notification. Wrapper82702348 conditionally frees the
24-byte parent; thunk827020F8 adjusts the secondary pointer by-8.

The native raster destruction callback releases this texture's owner without
removing a list node that the original path never created. The original public
raster wrapper retains plugin traversal and allocation free. Bound textures,
changed metadata, foreign threads, incomplete or unknown owners fail explicitly.
The existing driver-retirement check also rejects pending snapshot construction.

## Verification and remaining rendering work

The244-check fixture runs four original constructor/deleting-destructor cycles
with different parent byte patterns. It checks original CPU normalization and
the full header masks, parent fields and preserved byte, raster metadata,
native storage dimensions/format, unique identities, absence of list insertion,
message descriptor restoration, native backing expiration, ABI preservation,
and malformed/bound/foreign/stale access. Initial texture bytes are deliberately
not compared because the constructor has not initialized them.

Build164 passes74/74 suites in121.65 seconds. Actual muted boots107/108 complete
the native snapshot and original constructor. Boot108's improved diagnostic
identifies the next unsupported private camera raster:1280x720, flags5,
callback LR824081C0. The current camera bridge has separately qualified square
16/256/1024 profiles; its rectangular camera/depth lifetime needs qualification.
Frozen results are in build/crossfade-texture/build164-summary.json and STATUS.md.

The original crossfade message handler82702180 first captures through826B0BB0,
which remains explicitly guarded. Subsequent composition uses original state
calls and82701F40 with the snapshot texture. Correct capture conversion from
the original front-buffer format, its synchronization, original shader and
blend behavior still need implementation and verification. The constructor
does not establish those semantics.

Source evidence: build/crossfade-texture/evidence.json and original-chain.txt
pin20 original spans/1,656 words and all181 current hook ranges. The historical
probe source/build/log remains in that directory; its target was removed after
promotion to the regression fixture. Original game and reference files remain
unchanged. Earlier cleanup gaps for other resources remain separate.
