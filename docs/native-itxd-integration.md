# Native copied ITXD integration

Build187 Main validation: all96 suites passed in163.30 seconds. The original
ITXD lifecycle fixture passed127 checks, including actual named-resource load,
matching original pixel copies, native BC2 readback, second-group cache reuse,
owner promotion, original frees, stale-owner rejection and fresh generation.
The independent atlas storage fixture passed1,048,595 byte/block checks. These
results establish the tested load/ownership path, not a rendered menu or gameplay.

Implementation is confined to `runtime/engine_itxd_textures.{h,cpp}` and
`tests/test_itxd_lifecycle.cpp`. Main owns driver/configuration/build integration.
The byte authority is `native-itxd-lifetime-contract.md` and its verified
660 original words / 82 literal pins. No additional broad audit is required.

## Hook handoff

All hooks take `(PPCContext&, uint8_t*)`. Except the one release adapter, they
observe/preflight and continue at the SAME address, preserving the instruction.
Use ordinary non-replacing mid-assembly hooks, not entry replacements.

| Hook suffix after `SimpsonsNativeITXD` | PC | Original bytes | Continuation |
|---|---|---|---|
| BeginLoad | 826F24D8 | 7d8802a648349ee19421ff70 | same PC |
| EndLoad | 826F2654 | 7f63db783821009048349db0 | same PC |
| BeginCopy | 82736F58 | 7d8802a6483054619421ff60 | same PC |
| MetadataAllocation | 82736FE4 | 4e80042138a001007f24cb78 | same PC |
| PayloadAllocation | 8273703C | 4e8004217f85e3787f64db78 | same PC |
| FinishCopy | 82737050 | 7d7ff0507fe3fb78917d0014 | same PC |
| FinishRelocate | 82736F3C | 7fe3fb78382100608181fff8 | same PC |
| PreflightRelease | 826F80C0 | 7d8802a6483443059421ff70 | same PC |
| PreflightRelease | 82736D50 | 7d8802a6483056799421ff80 | same PC |
| RecoverReleasePayload | 82736DA0 | 4bd09499388000007fc3f3784bd07295 | **82736DB0** |
| FinishRelease | 82736DE4 | 3821008048305634 | same PC |

Main adds `EngineDriver::itxdTextures()` returning this service and constructs it
with `(Runtime&, NativeBackend&, context)`. There are ten exported hook symbols,
eleven hook locations (release preflight is used twice). The allocation observers
call `observeAllocation(ctx,base,payload)`. Register its source in native sources
and normal build targets; link `renderer/itxd_blocks.cpp`.

The accessor must allow CPU worker threads. `textureRaster(R)` should first
resolve this service when `ownsRaster(R)` returns true. Native texture readback
must use that same checked resolution. This includes
`EngineDriver::readbackTextureRaster`, which the fixture uses. Neither the
embedded H nor X+0 is a native identity or an `EngineRasters` allocation.
Driver shutdown calls `requireReleased()`: it locks the registry and requires
records, loads and releases all empty, without reading guest context or requiring
successful driver startup. `count()` remains a live-context observation.

## Ownership and original execution

`beginLoad` admits the original normal loader's call at826F26E4 and verifies
the saved named-dispatch LR8271191C, request location, type3 memory stream,
cursor, declared M and owner arguments. It bounds the entire source envelope,
sentinel, finite link walk, every 100-byte source record and every N-byte source
payload before the original walker allocates its group. Relative-address
arithmetic is checked. The texture plugin must fit inside the copied record,
the raster extension must be34, and embedded raster/header must match the
original copy layout. Borrowing, source mutation, cycles, null payloads and
out-of-envelope reads reject explicitly.

Original read-only index lookup verifies existing keys against registered,
published generations and their name, descriptor, raster metadata and payload
bytes before a new group is allocated. Original calls still perform all index
and group mutations. At completion, the actual group list and original lookup
must agree with every source record. Shared-group attachment reuses the same
native record and eventual GPU texture.

The allocation observations capture the actual allocator, vtable slots0/4/20/24,
indirect-call target and original request arguments. The metadata request is100
with three zero options. The payload request is exactly N with FFFFFFFF/1000/
404/1. The returned metadata identity is retained before the pixel allocation;
the returned P is captured after its N-byte copy. Both CPU copies must match the
bounded source bytes, with only the known copy-helper stores changed. The
allocations must be separate, non-overlapping and not aliases of the source.

The registry captures its own raw pixel bytes and the six original descriptor
words before relocation clears bookkeeping. It checks the actual relocated
100-byte record, including the unconditional H+20 address adjustment and either
exact result of the original conditional H+30 adjustment. It does not infer
unsupported mip semantics from that conditional check. The record becomes a
typed CPU owner after relocation and a consumable owner after group/index
publication. Generation numbers prevent a pending release or copy from being
mistaken for a newly allocated record at the same address.

Only `texture(base,R)` touches the GPU. It enforces the backend's existing owner
thread, original format1A200153 and zero auxiliary header, then invokes Main's
`decodeITXDBC2Base` using the owned pre-relocation descriptor and pixel snapshot.
That decoder qualifies one-level BC2 and returns native tightly packed block
bytes. Other copied formats remain valid CPU owners; attempting to consume an
unqualified format fails visibly. Creating a CPU owner does not claim rendering
support. Repeated consumption returns the same shared native texture. Source
streams may retire once synchronous copying ends.

## Release

At826F80C0 the service validates a published generation, all eight original
stage caches (`82D0E3F8 + 18*stage`, hexadecimal), allocator family, original
raster/header, no additional group owner, zero auxiliary header and a level-zero
base that exactly names the recorded P. This precedes both original index
removals and plugin callbacks. The original group walker has already decremented
its reference count at this point: a failure is terminal, and no rollback of
that earlier decrement is claimed. The service never chooses final release
using a generic reference-count-zero test.

The destructor entry must belong to that checked transaction and generation.
The only replaced instructions are the lock/unlock calls atDA0..DB0. They supply
recorded P at SP+54 after checking the exact destructor frame and registers.
No general lock result, pitch, success code or GPU header is fabricated. The
original suffix invokes allocator+24(P), then allocator+4(T,100). Only after
both calls return toDE4 is the registry generation retired. Freed guest memory
is not dereferenced. Outstanding shared native texture references retain their
own GPU storage independently.

All predictable checks precede publication by this service. Unexpected original
allocation failure, callback failure or partial construction is terminal; the
service does not fake successful cleanup or substitute free families. Terminal
driver teardown drops native storage and logs remaining CPU scopes/records; it
does not claim that the original game finished resource cleanup.

## Fixture and Main validation

`tests/test_itxd_lifecycle.cpp` is ready for Main to build as
`ITXDLifecycleTests` with the same original/runtime dependencies as the other
original lifecycle executables. It uses the optional service
`boundaryObserver`, installed before loads and called outside the registry mutex.
Returning preserves execution; the fixture throws a distinct marker after the
real font group's original copy/relocation/index/list work completes. Production
leaves the callback empty.

The fixture resumes the real normal load walker for a second group, verifies
cache/ref reuse and exact native BC2 readback against Main's independent atlas
fixture, then uses original826F8168 to release each group. It verifies owner
promotion, continued readback after the first release, final index removal,
stale raster rejection, and a fresh GPU generation on reload. Negative inputs
cover borrowed mode, incorrect B, truncated M, invalid stream remainder,
oversized N, a link cycle and each of the eight bound-cache slots. CPU ownership
lookup is exercised from a worker while GPU consumption there must reject.
The copied tiled bytes and name are checked directly against the original
asset fixture before any native decoder output is used.

Preparation and invocation, for Main (not run by this worker):

```text
python -B tools/prepare_itxd_font_fixture.py
ITXDLifecycleTests.exe analysis/simpsons.pe build/itxd-font
```

Use a 180-second test timeout to allow real startup/resource loading. The fixture
requires the known font load to occur on the main test thread for controlled
stack interception; it fails explicitly if that assumption differs in the live
run. Production CPU ownership itself is scoped by thread and mutex and does not
have that restriction. Adding a worker-load fixture barrier remains necessary
if the observed load is asynchronous.

No build, AOT generation, tests, game or UI were run by this worker. Main must
compile, run the fixture and actual executable, and inspect raw rendered output.
Malformed named-descriptor reads that occur before this typed loader are outside
this implementation; the source-envelope guard starts after the existing named
parser has constructed L. Broader texture formats/mip windows, successful live
final release, full cleanup, physical-console filtering and image fidelity remain
unverified until the appropriate validation runs succeed.
