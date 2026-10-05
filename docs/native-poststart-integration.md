# Native post-start integration

Status: bounded evidence complete; analyzer self-tests and reproducibility checked.
Original instructions are authoritative;
no runtime, configuration, renderer, original asset, or reference source is changed.

The narrow replacement is **the single `bl 82458260` at `82875E20`
(`4BBE2441`), continuing at `82875E24`**, within original function `82875BF0`.
Keep the original CPU reset and two allocations before it, and the original
alias stores and application initialization after it. The surrounding interval
`82875D60..82875E34` is the reviewed integration block, not a required broad hook.

Parent's revised contract fits this boundary: `823EE8F8` retains AOT `823EDD38`
and returns a checked, unmapped native backend identity backed by the real native
driver; `82D0CAF8` remains zero. AOT publishes that identity to `82D5DA74` and
`82D6D890`, preserving non-null CPU branches. This identity is **not** an SDK
device pointer and may never enter an original SDK dereference. Keep the direct
`82458260` guard as well as the known downstream consumer guards.

`826B78F0 -> 82723968` only stores the supplied device at `82D6D890`.
It does not construct the CPU state-stack object named by its unused r3 argument.
The following `82867A48` does initialize application objects, camera/rasters,
loading textures, and services. Keep that original function and its CPU callees.

## Input and exact segment

Flat `analysis/simpsons.pe`, base `82000000`, 15,466,496 bytes, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
All addresses and integer fields below are hexadecimal; guest words are big-endian.
Boot031 reached the existing guard at `823EE8F8`, LR `82875D64`. Allocations below
are verified original **requests**, not allocations observed after that guard.

- `82875D60 -> 823EE8F8`: calls original `823EDD38` at `823EE904`, then returns
  `BE32[82D0CAF8]`. This reset frees existing binding entries via engine callbacks,
  invalidates caches, and clears optional CPU storage. It must remain original AOT.
- `82875D64` saves the returned device in r30. The allocator is
  `BE32[82D57244]`, lazily resolved with `8268E7F0` if absent.
- `82875DA0` invokes allocator vtable `+20` with
  `(this,20000,FFFFFFFF,20,404,1)` in r3..r8. `82875DAC` publishes the result
  at `82E0759C`.
- `82875DE8` invokes the same slot with
  `(this,600000,FFFFFFFF,20,404,20000001)`. `82875E0C` publishes it at `82E075A0`.
- Stack `SP+70` receives six words `{0,20000,first,600000,second,0}`. r28 is
  the original zero established at `82875C14`. At `82875E20`,
  `82458260(r3=device,r4=SP+70)` consumes this record. Its return is ignored.
- `82875E2C` stores the borrowed device at `82D5DA74`. `82875E30 -> 826B78F0`
  passes the same device; its tail target stores it at `82D6D890`.

Neither allocation is checked by this caller before the next operation. A native
owner should validate and roll back its partial allocations explicitly.

## Allocation and lifetime evidence

Even before engine open/start, `82875C2C..C3C` resolves and publishes the allocator
if missing. The two later allocation sites repeat that check. Consequently,
skipping SDK-private allocations here does not omit its first initialization on
this original path.

The original static allocator object is `82D5724C`; its ctor `8268E510` installs
vtable `820B60B8`. The table's `+20/+24` entries are `8268E138/8268E6E8`.
Do not generalize this evidence to an arbitrary substituted allocator vtable.

For the first request, `8268E138` raises alignment from `20` to `1000`, rounds
the size, invokes vtable `+0` with `{2,1000,0}`, then calls `824338F8` to apply
protection `404`. Vtable `+0 = 8268DDA0` retains original heap allocation and
accounting. For the second request, flags bit `20000000` selects
`824337F8 -> MmAllocatePhysicalMemoryEx`: flags=0, size=`600000`,
min=0, max=`FFFFFFFF`, alignment=`20`. Protection enters the wrapper as
`20000404`; **`82433814..20` clears bit `20000000` if `BE32[82D51528]` is
nonzero**, yielding `404`. Retaining AOT preserves this conditional behavior;
do not hard-code one import argument without observing that global.
These are real memory-service effects, not SDK object headers.

`82458260` borrows supplied pointers into device `+3A18/+3A20` and cursor fields.
It writes device `+39DC/+39E0` **only for fallback allocations when the respective
input pointer is zero**. Its own `60`- and `20`-byte control allocations live at
`+2A90/+2A94`. Thus external-buffer storage is distinct from SDK-owned storage.
The original `340000` allocation stored at **`82E075A4`** is another resource:
`82875CB0 -> 826CECF0`, publication at `82875CC4`, and object vtable `+0C`
release at `82875ED8`. That release does not account for `759C/A0`.

Normal game shutdown `82875E78` first calls application cleanup `8287AF48` and
`82867C18`, calls `826B78F0(0)`, clears `82D5DA74`, then retains original
stop/close/terminate at `823EC950/823ECAA8/823ECB38`. SDK stop eventually calls
`824520F0 -> 824671B0`; `8246731C -> 82458260(device,0)` frees the owning
`+39DC/+39E0/+2A90/+2A94` slots, not the supplied `+3A18/+3A20` pointers.

**No matching free of the two caller-supplied buffers is established.** The
reviewed game shutdown does not read or clear `82E0759C/A0`. Direct-immediate and
literal-pointer searches are evidence of the reviewed access patterns, not a
complete indirect-alias proof. Do not claim these buffers are freed by device
release, and do not invent an original release site. The matching allocator free
service is proven (`+24`): heap addresses use the original heap free/protection
path; outside-heap physical addresses use `82433908 -> MmFreePhysicalMemory`.
Calling it at a new native cleanup point would be an explicit native ownership
policy, not a recovered original call.

## Console-only storage versus required CPU work

The first buffer is handed to `MmGetPhysicalAddress`, then to kernel ordinal
`1C3` (reference name `VdInitializeRingBuffer`). With size `20000`, original r4
is **0E (14 decimal)**, from **decimal** `28 - cntlzw(size)`; do not substitute
a byte-size logarithm. Writeback r4 is 8, from
`min(19 decimal, 31 decimal - cntlzw(size >> 9))`.
The SDK sets ring mask `7FFF` and ring base `device+3A18`.

The second buffer supplies command cursors: base at `+3A20`, end-minus-four at
`+3A24`, segment byte length `30000` at `+3A28`, initial cursor `base-4` at `+30`,
limit `base+30000` at `+34`, and reserve limit `base+2FF60` at `+38`. The SDK
writes a sentinel at its end and command words through the cursor. The two small
control blocks feed read-pointer writeback / GPU identifier services. Those SDK
buffers and fields have no verified application-data role here; they belong to
console submission and must not become a native command-stream model.

The **original caller allocation callbacks, heap accounting, protection effects,
cache reset, and explicit ownership remain CPU obligations**. Native D3D11
submission replaces the command consumer. Its own storage replaces the SDK's
private control blocks; this does not preserve the SDK's exact private allocation
trace, and is not a claim that those allocations originally did nothing.

### Configure side-effect closure and its limit

The complete `410`-byte `.pdata` body is included in the report:

- `82458274..D0`: conditional old-command flush/wait (`82458080`) and old
  secondary-buffer cache writeback (`8245DA50`, reviewed `dcbf`/`sync` leaf).
- `824582D4..332`: releases prior owning slots, clears ring cursors, unregisters
  GPU identifier, releases controls; null descriptor returns zero.
- `82458334..3FC`: descriptor defaults, optional fallback ring/secondary
  allocations, unconditional private controls. Null allocation returns
  `8007000E`; original caller ignores that result. No rollback is performed in
  the failure tail; owning slots persist for later SDK teardown.
- `82458400..524`: zero controls, physical ring and writeback registration,
  command-base/cursor/sequence fields, GPU identifier registration.
- `82458528..658`: builds stack command data, passes it to import ordinal
  `1DF`, copies it through `82456658` to the ring, writes secondary command
  words, and appends more through `82457EE8`. The existing export table calls
  `1DF` `KiApcNormalRoutineNop`; its use and reference naming are ambiguous.
  **Do not interpret that name as proof of a no-op.** The original thunk ordinal
  and all call bytes are pinned; this report does not emulate its behavior.

There is one transitive qualification: `82456658` conditionally calls an object
at device `+5404` through vtable `+18/+1C`, and a callback at `+5490` with
begin/end selectors 1/2. `82460D38` installs the former on paths in `82460DC0`
containing original `crashdump.pix2` / `unnamed.pix2` strings; `82461500` selector
`22` installs the latter at `824615D4`. These are optional SDK submission
observers, not registrations performed by the configure body. Their arbitrary
callback effects and all indirect registrations are **not** proved absent.
The report's same-offset store search also finds `8241425C`; this is not by
itself evidence that its different incoming object is an SDK device.

Exact unsupported registration guards:

- **`82461500`**: selector `22` reaches store `824615D4` into `+5490`.
  Reject at entry before the dispatcher obtains/dereferences an SDK device.
  Guarding the whole dispatcher is appropriate while its other selectors remain
  unported; no success stub for selector `22`.
- **`82460D38`**: PIX object installation at `82460D6C`. Reject before its
  initial `82458080` flush and `8246E3C0` factory call, not just at the store.
- **`82460DC0`**: outer capture controller, calls the installer at
  `82460E44/82460F44` and clears `+5404` at `82460E9C/82460F90`. Rejecting this
  entry also prevents earlier capture/file/state side effects. This is a
  defense for native mode, not an assertion that startup reaches capture setup.

For the exact native startup replacement, no additional essential game CPU
initialization was identified inside configure. The plan is supportable as a
native submission boundary with no registered SDK observers. It is **not** a
proof that every possible SDK configuration, capture mode, callback, or live
reconfiguration can be discarded without CPU effects. A future enabled observer
requires an explicit native equivalent or rejection. Old SDK flush/cache work
does not apply when there is no SDK object; native repeated configuration would
need its own checked lifecycle, not copied SDK fields.

## CPU services after the segment

`82875E34 -> 8287B5B8` is exactly `li r3,1; blr`. Then:

- `82875E4C -> 82867A48(width,height,original r23)` remains AOT. Its first
  `94`-byte allocation and `82718D48(object,2)` initialize real CPU storage,
  record the original timebase at `+88`, and publish `82D576A0`.
- `82867A88 -> 827142D8` creates the original camera and two rasters. Existing
  raster guards must continue to reject unsupported resource creation. Resuming
  at E34 does not certify completion of this next stage.
- Later instructions in `82867A48` acquire loading art (`82867B34 -> 82862A28`),
  publish callback `828678C8` at `82D57448`, and register four service callbacks
  with IDs `70B,70C,700,71D` through `82717FE8`. These side effects must not be skipped.
- `82867C18` reverses this CPU work: clears `82D57448` at `82867C7C`, destroys
  the camera at `82867CF0`, unregisters those four IDs at `82867D0C/D14/D1C/D24`,
  and invokes the `82D576A0` object's destructor at `82867D48`.
- `82875E50 -> 8287B4F8` calls `827B9968,8287A5D0,8287B1E8,8287ADF0`; retain
  this continuation too. A complete classification of its descendants is outside
  this bounded report.

Thus the console-specific block gates application initialization by control flow;
the block itself is not the application service initializer.

## Native hook plan and limits

1. Keep the revised getter's original reset and checked native identity. Let
   original `82875DA0/DE8` perform both allocator calls and publish `759C/A0`.
   The original instruction at `82875E20` is the only new hook proposed here.
2. Validate r3 against the active backend owner/generation; validate all 24
   readable bytes at r4 and the exact six BE words `{0,20000,first,600000,second,0}`.
   Require both pointers to match the original publications and genuine live
   allocation provenance, with bounded non-overlapping ranges. A mapped address
   alone does not prove allocation ownership. Reject unsupported descriptors,
   partial/null allocations, stale identity, and unproved repeated configuration.
3. Adopt the two allocations into explicit host ownership metadata as
   compatibility reservations. Bind configuration readiness to the already real
   native backend/submission owner; do not create rings, SDK layouts, mirrored
   control fields, console sentinel data, or fake kernel-init success. Do not
   label this operation a draw/present or completed frame. If validation fails,
   leave registration atomic; preserve cleanup ownership for any genuine partial
   allocation. The matching original free capability is `allocator+24`, but an
   added rollback call must be identified as native failure policy.
4. Resume at `82875E24` with nonvolatile registers/frame intact. Let AOT publish
   the non-null identity to `DA74/D890`, including original `826B78F0`. Keep CAF8
   zero. Direct SDK calls reject, and checked guest memory must reject dereference
   of the unmapped identity. Known engine consumers need guards before their
   SDK work; this is not a reason to zero aliases and alter CPU null branches.
   Keep `82875E34`, `82867A48`, and the full following CPU continuation AOT.
5. Keep original outer shutdown and alias clearing. Add a native integration
   owner teardown only with a stated policy for the unmatched external-buffer
   lifetime. Retain until terminal runtime teardown if no earlier original
   lifetime can be established; do not advertise faithful stop/restart accounting
   for this block yet. This is the principal remaining ownership evidence limit.

Known consumers show why a native identity is **not** an SDK pointer:
`826B08B0/826B0BB0` call SDK resolve; `826B0DF8` passes the device into further
graphics initialization; `8273EBA0` copies it into a state-save object and can
immediately query SDK state. `823C6EB0` is a plain getter, `82875A30` a setter.
`827236xx/82723xxx/82724038` use `82D6D890` for SDK getters/setters, direct device
stores, or state restore. Preserve their CPU cache semantics when porting; until
then reject at their relevant engine entries. This is not exhaustive indirect
consumer closure and not authorization to enable draws.

## Reproduction and validation

From `K:\SimpsonsNativeCopy`:

```powershell
python -B tools/analyze_poststart_integration.py --self-test
python -B tools/analyze_poststart_integration.py --report analysis/native-poststart-integration.json
```

The standard-library analyzer pins the image identity, parses flat PE and BE
`.pdata`, verifies every selected disassembler PC/word against the image, checks
the exact hook instruction and allocator table, and validates eight original
import thunks against a hash-pinned read-only export table. It records 52 selected
function/leaf extents (3,066 byte-checked words) and 19 reviewed facts with source
PCs, and validates all 34,176 original unwind records. The reviewed leaf
extents are explicitly identified; `.pdata` ranges may contain tables/padding,
so reported direct edges are candidates, not a fully recovered CFG.

Eleven self-tests pass: malformed PE/ranges, corrupted image, zero/duplicate/
out-of-code unwind records, signed/absolute branches, missing/extra/mismatched
disassembler output, and independently computed exact buffer arithmetic.
Repeated analysis was byte-identical to the persisted JSON. The CLI also rejected
an attempted report destination of `analysis/simpsons.pe`; the original image
hash remained unchanged. Default output
is JSON on stdout; the only permitted `--report` destination is the owned report.
No guest code, CPU/GPU interpreter, external API, renderer, or command backend
is implemented or executed by this analyzer.

Frozen JSON SHA256:
`d8c264c19a55855f4a070bf22a9980ab0f1052d5b9c90c22d905a8391f68b7ec`.
Its analyzer and disassembler hashes are embedded in the report.

Parent's proposed configure implementation was assessed as a contract only;
this task does not edit or certify the runtime implementation. Keeping allocation
ownership intact on validation failure and accepting an integration receipt only
after all checks is compatible with the evidence. Leaving payload bytes untouched
is appropriate: original SDK command/sentinel contents are backend-private, not
application resource data to reproduce in native reservations.
