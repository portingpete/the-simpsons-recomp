# Native driver startup contract

Status: bounded original-code investigation; source/report only. No runtime,
renderer, hooks, CMake, generated code, reference project or original assets
were changed. Read this with
[render-boundary.md](K:/SimpsonsNativeCopy/docs/render-boundary.md), which owns
camera/raster/quad, screen material and texture evidence. This document covers
driver lifecycle and the dependencies that must exist **before retaining the
original engine plugin constructors**.

The viable replacement boundary is the engine's platform driver, plus its
resource-owner services. Replacing `823EDF20` with device creation and `return 1`
is insufficient. The original path allocates engine pools, initializes state,
creates targets and buffers, then runs constructors that create pipelines,
another index buffer, declarations and at least sixteen shader objects under
the original capabilities. A small D3D11 device object alone cannot satisfy it.

All hexadecimal addresses below are guest VAs; guest words are big-endian.
Behavioral names are descriptions, not recovered symbols. Facts are static
unless explicitly described as observed. The previously observed boot021
failure at `82466E7C -> VdInitializeEngines` does not establish successful driver
or plugin startup. Later parent builds were not executed for this investigation.

## Evidence and reproduction

Input: `K:/SimpsonsNativeCopy/analysis/simpsons.pe`, flat VA-minus-`82000000`
mapping, 15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
The original `.pdata` supplies non-leaf extents; explicitly reviewed leaf
extents are listed separately in the analyzer. No adjacent unwind entry is
used as a leaf's extent.

```powershell
python -B tools/analyze_native_driver.py --self-test
python -B tools/analyze_native_driver.py --report analysis/native-driver.json
```

The standalone analyzer reads the existing offline
`build/generator-ninja/SimpsonsDisasm.exe`; it never builds or regenerates.
Only `analysis/native-driver.json` is an allowed output. Its 125 function bodies
contain 6,564 checked original words, direct call/tail edges, conservative
literal arguments, 14 core registration records, 20 direct engine registrations
from the 25-entry attachment table, capabilities, mode formats and pipeline
node records. It verifies PC/word agreement with original bytes and records
input/tool hashes. Fourteen in-memory checks cover malformed evidence, signed
address construction, wrapping and loss of literal facts across calls/branches.
This is static expression decoding, not guest execution. The report does not
claim a complete indirect call graph or complete dynamically populated registry.

## Keep the original engine lifecycle

Let `E = BE32[82D0CA68]`, registry `R = 82CD1930`.

- `823ED028` initializes the core at `82D0C918`, allocators and plugin
  registries; `823ECB98` registers the 14 core plugins. `823EE5D0 -> 823F52C8`
  registers a **32-byte raster extension** with ID `040C`, publishing its
  runtime offset at `82E3DC94`. Its original ctor/dtor `823F52B8/823F52C0` are
  bare returns; do not assume they initialize the raster's platform payload.
- `823ECE30` allocates **BE32[R] bytes** through `E+108`, copies only the
  `0x14C` core, publishes the new E, requests 4, 0, then 11 with 29 slots.
  Preserve this allocation, including all extra plugin storage; never substitute
  a fixed-size engine. The caller `823ECF58` advances `E+144` from 1 to 2 only
  after successful open. Allocator/free/realloc slots are `E+108/+10C/+110`;
  pool allocate/free slots are `E+138/+13C`.
- `823EC9E0` requests 2 at `823ECA14`, then calls `823FB328(R,E)` at
  `823ECA2C`. It generates gamma tables through `82406560`, requests 17, then
  writes state 3 at `823ECA68`. Do not hook this whole function to success.
- Registry entry layout, verified at `823FB098/823FB328`: `+00` object-relative
  offset, `+04` unrounded size, `+08` ID, `+20` ctor, `+24` dtor, `+28` copy,
  `+30` next, `+34` previous, `+38` registry. Registry `+10/+14` are head/tail.
  Ctor ABI is `(r3=E,r4=offset,r5=size) -> E/nonzero or 0`. Construction follows
  registration order. On failure, the walker calls destructors for **previously
  completed** entries in reverse order; the failing ctor owns its partial cleanup.
- Normal stop `823EC950`: request 18 (original dispatcher returns 0, ignored),
  original reverse dtor walk `823FB3A8(R,E)`, request 3, state 2 if request 3
  succeeds. Ctor failure also requests 3, without ever publishing state 3.
  `823ECAA8` subsequently requests 1, restores/copies the core, frees extended E
  and sets state 1. Native driver stop must not repeat the engine dtor walk.

Gamma `82406560` is CPU work, not an SDK gamma-ramp submission. With
`G = E + BE32[82D10134]`, it writes the scalar at `G+20C` and two 256-byte
lookup tables at `G+0C/+10C`. Keep the image plugin ctor `82406680` and these
tables; they depend on its real allocations.

## Driver ABI and fields

Adapter `823EC490(descriptor,request,out,in,value)` reads descriptor `+4` and
calls `(r3=request,r4=out,r5=in,r6=value)`. The original descriptor is 56 bytes
at `82CD1A78`; request 4 copies it to `E+10`, and stores the supplied allocator
table pointer at `82E3DD64`. Thus the request callback is `E+14`. It is not a
COM vtable. Request 11 installs the 29 original standard callback slots at
`E+48`. Use the exact slot map in the boundary report: raster create/destroy
are slots **4/5**, not 2/3.

Retain these guest-visible CPU fields or supply their proven native equivalents:

- `82D0CAF4`: open flag, set 1 by open; request 1 clears it.
- `82D0CAF0`: selected mode index, open initializes 0. `82D0CB08`: started
  flag, original request 2 stores the start result; stop clears it first.
- `82D0CB10`: mode count; `82D0CB18`: engine-allocated mode table;
  `82D0CB20/+4`: requested width/height, set by `823ED9A0` before open.
- `82E3DD80`: subsystem name string; `82E3DF80`: copied mode count;
  `82E3DF84/+4/+8/+C`: width, height, mode field `+08`, format;
  `82E3DF94`: depth-like result of `823EDA10(format)`;
  `82E3DF98`: mode flags. These are data, not native device storage.
- `82E3DFA0`: 304-byte capability record. Original open's `8244ED90(0,1,out)`
  is a CPU copy from `8206AA30`, with no GPU access. `8244E5A8` likewise copies
  a 1,104-byte adapter-information record from `8206A5C8`. These functions may
  be reused as CPU copies; their contents describe the **original interface**,
  not measured native capabilities. Do not claim every copied capability is
  implemented. Required capability decisions below must be accounted for.
- `82E3DCE0`: 124-byte presentation description; meaningful dimensions and
  configuration can remain guest-visible without retaining its SDK ownership.
- `82D0CAF8`: SDK device pointer. **Leave it zero.** Native device/context/
  swap chain belong in host driver state. No small usable SDK object layout is
  established, and original code performs direct writes and indirect calls
  through this pointer. A nonzero dummy here is not a driver implementation.

Request numbers are decoded directly from `82062AC0`:
0 open; 1 close; 2 start; 3 stop; 4 descriptor/allocator registration;
5 mode count; 6 mode info; 7 select mode; 8 device-exists test;
10 selected mode; 11 standards; 12/15 write zero; 13 write one;
14 adapter info/name; 16/17 return one; 19 maximum of capability `+58/+5C`;
22 writes BE16 value 9; 9/18/20/21/out-of-range return zero.
The analyzer stores every case PC. A native request 8 must query native lifecycle,
since its original implementation tests `CAF8 != 0`. Mode select must preserve
its started/range checks, including refusal when `CB08 != 0`.

## Open: `823EFFD8`, extent `E4`

Incoming arguments are unused. Exact order:

1. Set `CAF4=1`; call `E+CC` to copy the original name at `82062ADC` (`Xenon`)
   into `82E3DD80` (`823F001C`). This compatibility string is not an HWND.
2. Copy capabilities (`823F0030`). Call `823EE440(requestedWidth,requestedHeight)`
   (`823F0044`). It queries the video-mode import `82CC24D4`, allocates 420
   bytes via `E+108(size=1A4,hint=00040411)` and writes **21 records of 20 bytes**.
   Width/height are supplied requests if both nonzero; otherwise the original
   video-mode tests choose 640x480 or 1280x720, with a 640x576 PAL special case.
   Preserve the existing video-mode service or base a native mode list on actual
   supported output; do not return a table of unusable modes.
3. Each record is `{width,height,0,format,flags}`. Seven formats at `82062A40`
   are `28280186,18280186,282801B6,182801B6,28280143,18280143,28280144`;
   each has flags `1,101,201` OR the video-derived `400` and/or `2` bits.
   The exact tests and stores are in `823EE440`'s report body.
4. Copy count and first record to the globals above; derive the depth-like
   value through `823EDA10`; set selected mode zero and `82CD1A60=2`; return 1.

The original allocator result is not checked before table writes. The native
replacement must fail without publishing an open driver if allocation or a
required output contract fails. Stop **also frees this mode table**; close frees
it only if still present. Ownership is driver open state, with stop-or-close
release, not two independent frees.

## Start: `823EDF20`, extent `288`

Incoming arguments are unused. Width/height W/H come from `82E3DF84/88`.

Presentation block is zeroed, then receives `+00=W,+04=H,+08=182801B6`,
`+28=1A220197,+34=1,+38=1,+3C=1,+40=28280136`.
The video import can additionally set `+60..78` for the original 576-line,
mode-field condition (`823EDFA0..DFF8`). `82D0CB0C=1` is written before device
creation. Preserve that write if original readers remain; its broader meaning
is not established here.

Console resource operations to replace at the **engine driver implementation**:

- `823EE03C -> 82452540(0,1,0,1,&presentation,&CAF8)` creates the SDK device.
  Negative result returns 0. Do not execute this SDK path in native mode.
- `823EE070 -> 82440698(W,H,182801B6,0,&{0,0,0})` publishes the default color
  surface at `82D0CB00`. `823EE08C -> 823ED930` computes the original eDRAM
  placement for the subsequent depth surface; this placement has no native
  D3D11 address meaning. `823EE0B0 -> 82440698(W,H,1A220197,0,&{placement,0,0})`
  publishes depth at `82D0CAFC`. Target setters at `823EE0C4/0E0` bind them.
- Four texture calls `82440578` use `(r3..r10)=(W,H,1,1,0,format,0,3)` and
  publish, in order: `82D0CF84` with `1A220197`, `82D0CF90` with `28280136`,
  `82D0CF8C` with `28280136`, `82D0CF88` with `182801B6`. Preserve separate
  resource roles/lifetimes. These numeric formats/flags are original values,
  not DXGI enums. This report does not establish an exact DXGI/color-space map.

The native driver owns actual host targets/textures and their lifetime records.
Guest platform fields may contain validated native resource IDs only when every
consumer is a native engine service; never expose those IDs to original SDK
release, sampler, lock or draw code. Prefer host side records keyed by engine
generation, guest owner and resource role. Keep guest camera/raster allocations
and plugin offsets real. Default target selection belongs to the camera/raster
services documented by Gödel.

The six ordered initialization calls after these resources are not optional:

1. `823EDD38` at `823EE184`: CPU binding-cache reset and engine pool setup.
   Clears `82D0CF58` and `CF5C..CF68`; invalidates `82CD1A64..1A74` to -1;
   resets four 16-byte records starting `82D0CAB0` (first three words -1/0/0).
   Maintains a real pool at `82D0CB3C`, using `823FBEA8(64,15,16,00030411)`,
   and 260 entry pointers at `82D0CB40..CF4C`; resets `CB38` and `CF50`, freeing
   `CF54` if present. Existing entries return through `E+13C`. This helper
   can remain original AOT with valid allocator/pool services. It resets the
   binding cache **after** initial target binding; a native cache must distinguish
   invalidation from actual host unbinding.
2. `823F4780` at `823EE188`: CPU cache initialization via `824103C0` and
   `8240EC28`. Clears `82E3D138/13C,82D503D0` and
   `82E3D140/144,82D50320`; initializes eight 40-byte records at `82D501E0`.
   Keep these original helpers. Teardown helpers are `823F47A8`'s
   `824103E8/8240EC68`; if their arrays acquire GPU objects later, their SDK
   releases must be replaced before teardown is reachable.
3. `823F69E0` at `823EE18C`: real pool at `82D0D020`, arguments
   `(8,127,4,00030411)` to `823FBEA8`. Reuse AOT. Matching CPU cleanup
   `823F6A20` drains `82D0D01C`, frees the pool, resets `D008/00C/010`, frees
   `D014` and optional pool `D018`.
4. `82409A90` at `823EE190`: renderer scratch resources. Base `B=82D101C4`.
   Initializes `B+00` use flag and `B+0C` cursor. If capability `+1C & 10000`
   is **clear**, allocates a `40000`-byte vertex buffer via
   `824417D8(size,28,0)` into `B+08`, with flag/cursor at `B+00/+04`.
   Always allocates a `4E20`-byte index buffer through
   `82441A08(4E20,28,1,0)` into `B+10`, and creates a declaration at `B+14`
   through `823EF838`. The stack declaration's original 12-byte elements are
   fully present in the report. Stop uses `82408E30`. Replace both resource
   owner helpers with native allocation/cleanup and preserve the cursor fields.
5. `824008E0` at `823EE194`: CPU state defaults **and device mutation**.
   Retain/port the CPU shadow-state setup (`823FFE78`, scalar cache setters
   `82400170`, texture-stage cache setter `824001E0`); initialize the same
   eight sampler records and default values. Replace sampler service
   `82400278` and commit `82400040` with native state application if reused.
   This function itself must be ported: `82400B94..BB4` directly accesses
   `device + sampler*24 + 48C` and `device+18`; `824002C0` and `824000C8`
   dispatch through SDK device fields. Hooking a few SDK callees leaves these
   accesses alive. The report's full body is the exact defaults/store ledger;
   no guessed D3D11 state defaults substitute for it.
6. `823FCF60` at `823EE198`: clears old state with `823FCD58`, then creates
   four real CPU pools with `823FBEA8(size,count,4,00040411)`:
   `D0C8=(16,16), D0CC=(20,100), D0D0=(8,100), D0D8=(20,42)`.
   `823FCAD8` then reserves dynamic vertex buffers through `823FC6D8`.
   Original capabilities choose **four** `40000`-byte buffers, published at
   `82D0D100..D10C`, cursor words `D0E0..D0EC`, size words `D0F0..D0FC`.
   They have linked ownership records at `D0D4`: `{size,inUse,resource,
   ownerOutput,next}`. Keep the CPU pools/records or implement equivalent
   native ownership; replace the SDK allocation and releases. Do not merely
   initialize the pool pointers while omitting their backing buffers.

Original start unconditionally returns 1 after these calls, ignoring several
allocation results. A native start must roll back its own partial resources and
return 0 (or report an explicit failure) rather than publish a ready driver with
missing backing. Do not run the original full stop on a half-created start:
`823EE200` dereferences the pool at `CB3C` unconditionally.

## Plugin construction needs native services too

The core registration order, IDs, sizes and callbacks are machine-readable in
the report. In particular retain `0401`'s math tables, `0402`'s object pool,
`0405`'s camera pool, `0406`'s image/gamma storage, `0408`'s texture/dictionary
ownership, `0409`'s pipeline heap and `040B`'s resources arena. Zero-byte engine
extensions can still have essential constructors; size zero never means skip.

Core plugin `040A` (`size=74`, ctor `82407660`, dtor `82407600`):

- Records its extension offset at `82E3D148`, increments `82E3D14C`, zeroes
  116 bytes at `E+offset`, publishes that pointer at `82D10184`.
- Creates the original CPU pipelines through `82416430` and `82416588`, storing
  pointers at extension `+1C` and `+20` (the latter is shared in `+20..30`).
  These are real engine pipeline objects, not fake GPU pointers. Keep the
  pipeline allocation, node construction and registration. Default node records
  `82CD1E58/1EB8` have zero construction callbacks at `+8/+10`; their `+4`
  callbacks are later processing bodies, not plugin constructors.
- `82416588 -> 82416C58` unconditionally creates a `1FFFE`-byte index buffer
  using original `(size,8,1,capabilityDerivedPool)` at `82416CA0`. Cursor is
  `82D507EC`, resource at `82D507F0`; creates three declarations at
  `82D507F4/F8/FC` through `823EF838` at `82416D6C/DDC/E30`.
  Native resource-owner init/cleanup hooks **`82416C58 / 82416BC8`** can replace
  this hardware-specific portion while retaining `82407660`, both pipeline
  builders and their actual CPU side effects. `824164F8` calls cleanup before
  unregistering/deleting the shared pipeline. Resource cleanup must be callable
  during a failed constructor as well as ordinary stop.

Game attachment `82C743A8` invokes 25 functions from `821B5688` before engine
open. The report preserves this table and its directly registered engine
extensions; there are additional nested attachments. Two verified shader
dependencies in that table are:

- Plugin `0120`: registration `82A69694`, ctor `82A67C48`, dtor `82A67BB8`.
  Ctor builds CPU pipelines in `82A6CF80`. With original capability BE16
  `[caps+CE]=0301 >= 0101`, six calls to `823EFB78` occur at
  `82A6D158/168/178/188/198/1A8`. Input records are
  `82199150,82199298,82199360,82199418,82199598,82199738`; outputs are
  `82E2714C/50/54/58/5C/60`. Pipeline pointers are `82E27144/48`.
- Plugin `0133`: registration `82CB1D54`, ctor `82CB33C8`, dtor `82CB35A8`.
  Capability `+1C & 10000`, BE16 `+C6>=0101`, BE16 `+CE>=0101` enable its
  shader path; original values pass all three and `+CE>=0104`.
  Ten shader outputs live at `82E31B00..1B24`, counter at `82E31B30`, enabled
  flag `82E31B2C`. Original selected input records by output order are
  `821B7050,821B7200,821B7DD8,821B8058,821B73B8,821B7528,821B82E8,821B8530,
  821B8B70,821B8DD8`. `82CB3828` first constructs two real pipelines at
  `82E31B34` and `82E31ADC`. Their original node init callbacks
  `823E6E58/823E1590` only install CPU callbacks; keep them.

Shader service ABI: **`823EFB78(r3=original shader record,r4=BE32 output)`**
calls SDK `82448178`, stores its returned object, and returns nonzero iff object
creation succeeded; **`823EFBD0(r3=object)`** tail-calls SDK release `82441708`.
Hook these engine shader services with native implementations and real lifetime
tracking. The original constructors ignore some shader failure results, so a
silently returned zero can still let the engine announce success. Preflight
required native shader implementations, or raise an explicit unsupported/resource
failure; do not manufacture nonzero objects. Their shader arithmetic/native
equivalents are **not recovered by this investigation**. They are additional
startup resources, not the four screen shaders in Gödel's material scope.

Original capability `+1C=01F91FC0` has bit `10000` set: scratch initializer
`82409A90` skips its optional vertex buffer, `823FCAD8` creates four dynamic
buffers, and the shader paths above are enabled. Clearing this bit or reducing
shader-version fields merely to bypass work changes these original decisions.

## Stop: `823EE1A8`, extent `290`

Engine plugin dtors have already run, or constructor rollback has unwound the
completed subset. Platform stop then:

1. Clears `CB08`; frees mode table `CB18` via `E+10C`, zeroing `CB18/CB10`.
2. Sets pool `CB3C+18` flag bit 2, returns all 260 entries through `E+13C`,
   destroys the pool through `823FB708`, zeros `CB3C/CF50`, frees/zeros `CF54`.
3. Unbinds eight textures, the index buffer, four streams and shader/declaration
   state using SDK calls. Replace with real host unbinding/release ordering.
4. Drains the declaration cache: count `82D0CB28`, capacity `CB2C`, array
   `CB30`. Each 12-byte record is `{resource,elementCount,copiedElements}`.
   Original code repeatedly releases resource refs until zero, frees the copied
   element data, then frees/zeros the array/count/capacity. Engine service
   `823EF838` owns lookup/dedup and copies the complete declaration, including
   its `-1` terminator. `823EFA18` releases one matching resource reference and
   clears its slot when it reaches zero. Native IDs need matching refcount and
   host ownership; never send them to SDK release.
5. Releases and zeroes textures `CF88,CF8C,CF90`, then calls
   `823F47A8,823F6A20,823FCD58,82408E30` in that order, releases the SDK device
   through `824520F0`, zeros `CAF8`, returns 1.

The body does **not** explicitly release/zero `CF84`, `CB00` or `CAFC` at those
last texture-release sites. Their SDK/device-internal ownership is not proven
here. A native owner must explicitly retain/release its own default depth/color
and auxiliary resources exactly once; do not infer that these three globals
authorize leaks or duplicate releases. CPU helpers with SDK resources in their
arrays must receive native equivalents before a restart/stop test is valid.

## Smallest actionable native implementation

This is a proposed bounded implementation, not a claim that a complete hook
closure has been proved. Implement it in this order:

1. **Native platform request dispatcher at `823F0630`.** Preserve descriptor,
   allocator registration, modes, callback installation and original verified
   request results. Implement native open/start/stop/close bodies with the
   original CPU field contracts above; request 8 uses native readiness. A single
   dispatcher hook can own those four lifecycle functions, so separate entry
   hooks at `823EFFD8/823EDF20/823EE1A8` are not required for this call path.
   Keep the original engine allocation/state transitions and constructor walk.
2. **Real native driver owner.** Device/context/window swap chain, default
   color/depth targets, four auxiliary texture roles, scratch index buffer,
   four dynamic vertex buffers, declaration/state services and the real CPU
   pools above. Port the mixed resource/state initializers and their teardown;
   retain safe CPU helpers. Host resource ownership is separate from guest
   allocation ownership, keyed by engine generation to prevent stale reuse.
3. **Construction-time engine services.** Native `82416C58/82416BC8` for the
   default pipeline resources; `823EF838/823EFA18` for declaration ownership
   wherever called; `823EFB78/823EFBD0` for shader objects. Preserve all original
   plugin constructors, including zero-sized ones. Retain their pipeline-node
   constructors and callbacks; replace hardware work at these smaller boundaries.
4. **Before any callback can draw or destroy a native resource**, replace the
   engine standard camera/raster/present/clear services and the retained
   descriptor render-state/immediate callbacks. The descriptor's six function
   entries are `824025A8,82401260,82408EB0,824090A8,82409308,82409570`.
   Their full drawing ABIs are not established here: install explicit unsupported
   guards until ported. Follow Gödel's verified callback slots and quad contract.
   Native state commit must cover `82400040/82400278` if original state-cache
   setters remain. A global SDK success stub is not part of this design.

   **Later verified exception:** `82401260` is a complete136-word CPU-only
   RenderWare state query, with a30-byte switch table. Its guard has been removed;
   original AOT code reads retained CPU requests and writes the caller's result.
   It does not query a device or commit state. The fixture verifies all30 selectors,
   including U/V agreement/disagreement, the float field, invalid-selector output
   behavior, bounded writes and ABI. Evidence is in
   `build/unbuffered-assets/render-state-query-evidence.json` and the checked
   original listing alongside it. This does not qualify the immediate draw calls.

Native readiness must mean the required backing resources/services exist.
Request 2 must finish the platform portion; the original caller still performs
the plugin work. Do not publish state 3 from a hook. Run original post-plugin
gamma and request 17; request 17's original success is independently verified,
not permission to report success from unrelated requests.

There is a separate **post-start console integration boundary** immediately in
the orchestration: `82875D60 -> 823EE8F8` resets caches and returns `CAF8`;
`82875E20 -> 82458260` then receives that device and a block holding allocations
of `20000` and `600000` bytes. `82875E2C` publishes it at `82D5DA74`, followed
by `826B78F0` and further initialization. Returning zero or a native token as an
SDK device does not make this safe. Parent needs a reviewed native replacement
for that platform-integration segment (preserving its CPU allocation/lifetime
side effects) before allowing execution there. Do not skip all of `82875BF0`:
it contains engine initialization, attachments, open/start and later services.
Its `8287B688` mode-query/selection helper also runs after start; preserve the
original request 7 refusal while started rather than resizing behind it.

## Remaining blockers and acceptance boundary

- Native shader semantics for the sixteen construction-time records are still
  needed if the original capability path is retained. This report proves their
  identities, ownership and call sites, not a native shader translation.
- Exact native format/usage mappings for presentation/auxiliary textures and
  declaration elements require implementation review. Raw original arguments
  and element construction stores are preserved for that work.
- The live registry contains nested attachments and indirect pipeline callbacks.
  The 14+20 report records are evidence, not a whitelist allowing all other
  constructors to be dropped. Verify actual registry traversal/constructor
  results against the populated original list at integration time.
- The post-start SDK-device integration at `82875D60..E30` remains a precise
  additional boundary; none of the startup hooks alone permits that code to
  consume a fake `CAF8` object. Loading quads and first original pixels remain
  separate milestones owned by the renderer/material work.

Integration checks should record actual E allocation size and registry entries;
run open/start/stop/close and a second cycle; force failure during native target,
buffer, declaration and shader creation; verify reverse plugin unwind and no
remaining native resources/guest pool leaks; assert `CAF8` stays zero and fail
explicitly if an original SDK device/packet path is reached. These are proposed
backend tests, not tests run by this source-only investigation.
