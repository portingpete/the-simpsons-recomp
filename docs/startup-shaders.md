# Construction-time shader resource inventory

Scope: only the sixteen records selected by plugins `0120` and `0133` under the original capabilities, as identified in `native-driver-contract.md`. Original byte inventory and engine ownership assessment; no shader translation, renderer, GPU command processing, or parent-file changes. The four screen-shader proof/analyzer/tests are frozen and are separate from these records.

**Verified inventory:** all sixteen are **pixel-stage compiled records**. Their debug-name and reflection sections are absent: header offsets `+0x0C` and `+0x10` are both zero. There are no recovered original shader names for this set. The identities below are original addresses plus hashes, not invented material names. Each has a straight-line EXEC/ALLOC/EXEC_END schedule, with 1–3 texture fetch slots and 5–24 ALU slots. No native translation or compiled-ready state is claimed.

Original source: `analysis/simpsons.pe`, 15,466,496 bytes, flat mapping `file_offset = VA - 0x82000000`, SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. All words below are big-endian. Record extents exclude alignment padding.

## Framing and stage evidence

Every record starts with `0x102A1100`, matching the independently proved Screen_Xenon pixel records; the screen vertex records instead start with `0x102A1101`. All sixteen are created through engine service **`823EFB78(record, output_word)`**, whose call at `823EFB8C` reaches `82448178`, copies its result to the supplied output at `823EFB94`, and returns a Boolean. The separate engine vertex creation service is `823EFAA8`, calling `82448308` at `823EFABC`.

Original `82448178` copies `record[+4]` header bytes into object `+0x28` (`824481F0..82448210`), copies `record[+8]` payload bytes into separately allocated storage (`82448238..244`), and retains that payload pointer at object `+0x18` (`82448278`). It initializes reference count 1 at object `+4` and object flags/type word `0x00100007` at `+0`. Its optional callback at `[82D51544]` is an additional original dependency; this inventory does not establish its runtime value or behavior.

The first payload byte is **not always the first instruction**:

- Record `+4` is the payload start offset; `+8` is payload length.
- Record `+0x18` points to the code metadata inside the header.
- The metadata's first two words give the code offset within the payload and code byte length. Original object accessors prove this: `8245DF60` returns the copied metadata; `8245DF70` adds its first word to object `+0x18`; `8245DF88` returns its second word.
- Thus `code_address = record + BE32[record+4] + BE32[record+BE32[record+0x18]]`. The selected records have payload prefixes of 0, 64 or 128 bytes. Prefix bytes are retained as original data; their complete constant-binding semantics are not inferred here.
- Each code window comprises paired 48-bit control-flow instructions, the scheduled 12-byte fetch/ALU slots, and one final **all-zero 12-byte triplet that is not executed**. This differs from the four screen records' nonzero trailers.

The counts below are obtained from the original **EXEC address/count/sequence fields**, checked to cover every executable slot exactly once and to exclude both the CF region and final zero triplet. They are not a guessed meaning for an opaque header count field. ALU counts count 12-byte vector/scalar instruction pairs; they are not counts of scalar arithmetic operations.

## Plugin 0120: six selected records

Each item gives record address, output storage, creation callsite, complete record bytes, payload prefix bytes, code bytes, and scheduled fetch/ALU counts. Stage is pixel and original name is unavailable for every item.

- **`82199150`** → `82E2714C`, call `82A6D158`: record 328 bytes; prefix 64; code 144; **2 fetch + 7 ALU = 9 executed slots**; 2 CF triplets. SHA256 `51500945fc0256a35320c24dfaa461eb4ecf90e7abe40e5ad1b22e3cc47de2ea`.
- **`82199298`** → `82E27150`, call `82A6D168`: record 200 bytes; prefix 0; code 120; **2 fetch + 5 ALU = 7**; 2 CF triplets. SHA256 `23e34823b102c00af7fd6a5a47e780699d74a4e147033272531f8249cb0ce7b8`.
- **`82199360`** → `82E27154`, call `82A6D178`: record 184 bytes; prefix 0; code 108; **1 fetch + 5 ALU = 6**; 2 CF triplets. SHA256 `0b448c12b5e3740b58aabddaa6fe3bae4548263ed9321278fa293e553e786737`.
- **`82199418`** → `82E27158`, call `82A6D188`: record 380 bytes; prefix 64; code 192; **3 fetch + 9 ALU = 12**; 3 CF triplets. SHA256 `a10f87ebbe5a227902156f8a5f4f3fa8efc0b0b390ec39438d652c2eca208239`.
- **`82199598`** → `82E2715C`, call `82A6D198`: record 416 bytes; prefix 64; code 228; **3 fetch + 12 ALU = 15**; 3 CF triplets. SHA256 `6afaf3929d722ecbec6f907f75454b1c281deee15174e36c338894a2eabba5d7`.
- **`82199738`** → `82E27160`, call `82A6D1A8`: record 404 bytes; prefix 64; code 216; **3 fetch + 11 ALU = 14**; 3 CF triplets. SHA256 `761d88c371244b22e95160b84fd09a6b5ccf86db137f274bc73c888572fc53ef`.

## Plugin 0133: ten selected records

Listed in output-storage order, which differs from creation-call order. These are the selected `caps+0xCE >= 0x0104` variants, not the alternate lower-capability records.

- **`821B7050`** → `82E31B00`, call `82CB345C`: record 428 bytes; prefix 64; code 240; **2 fetch + 14 ALU = 16**; 3 CF triplets. SHA256 `cc06d8d1bdf50c22201e2d8f059905ade1eb6a0abc98af1124101e648f15143f`.
- **`821B7200`** → `82E31B04`, call `82CB3470`: record 440 bytes; prefix 64; code 252; **2 fetch + 15 ALU = 17**; 3 CF triplets. SHA256 `462b4a059eeedd41211690bbf7fc25e1778c8a112ac18cc502bcf1554087e60a`.
- **`821B7DD8`** → `82E31B08`, call `82CB34B8`: record 636 bytes; prefix 128; code 372; **3 fetch + 23 ALU = 26**; 4 CF triplets. SHA256 `883e732fe1e1fe1fe8215357d712f92379f8f5b5e24c7052a3ad8ec9711652dd`.
- **`821B8058`** → `82E31B0C`, call `82CB34CC`: record 652 bytes; prefix 128; code 384; **3 fetch + 24 ALU = 27**; 4 CF triplets. SHA256 `923c6388a441dae8f5c74c3d7a767c0d07af4be3bca40d871ed7faa2e3d5514c`.
- **`821B73B8`** → `82E31B10`, call `82CB3484`: record 364 bytes; prefix 64; code 180; **1 fetch + 10 ALU = 11**; 3 CF triplets. SHA256 `30c732a3a7f442f2301bbb2f099885acb5a183644081998ec9fc9112ab505cb6`.
- **`821B7528`** → `82E31B14`, call `82CB3498`: record 376 bytes; prefix 64; code 192; **1 fetch + 11 ALU = 12**; 3 CF triplets. SHA256 `d8970bae27097b022777c1fa072880c064386d80e8f3cda6f5a5b2b55628ed04`.
- **`821B82E8`** → `82E31B18`, call `82CB34E0`: record 584 bytes; prefix 128; code 324; **2 fetch + 20 ALU = 22**; 4 CF triplets. SHA256 `c29d3682679eed5a8d57536097725a866c98b3b339bb969be180829ce7f6a12d`.
- **`821B8530`** → `82E31B1C`, call `82CB34F4`: record 600 bytes; prefix 128; code 336; **2 fetch + 21 ALU = 23**; 4 CF triplets. SHA256 `faae2a35c8eab657af2e3e4fe5f557e7e9df580e9c87fd955e02ab463a2dffbd`.
- **`821B8B70`** → `82E31B20`, call `82CB3508`: record 612 bytes; prefix 128; code 348; **3 fetch + 21 ALU = 24**; 4 CF triplets. SHA256 `b3472e1289112d330eb75e511a10c043a12ac05b6379019fbe176b0fc1ecd89b`.
- **`821B8DD8`** → `82E31B24`, common call `82CB358C`: record 628 bytes; prefix 128; code 360; **3 fetch + 22 ALU = 25**; 4 CF triplets. SHA256 `3fd82e1ab39351a2d1cb780ed64f835d7373ed719d0218a5257fb74f0b570795`.

## Complexity limits relevant to deferred compilation

All recovered CF opcodes are NOP (0), EXEC (1), EXEC_END (2), and ALLOC (12); there are no conditional branches, calls or loops in these schedules. All exports are pixel color 0 with vector mask 15 and scalar mask 0; no depth export is present. Vector opcodes encountered are ADD (0), MUL (1), MAX (2), MAD (11), and DOT3 (16). Scalar opcodes are RETAIN_PREV (50) and, in some records, MAX (5). This inventory has not translated their swizzles, masks, modifiers, constants or complete arithmetic into native shaders.

Every texture instruction has fetch opcode 1 and **dimension field 2 (`3DOrStacked`)**, not the screen shader's dimension 1 (`2D`). Texture constants used are subsets of 0,1,2,3. These fields do not establish the actual bound resource dimensions or authorize substituting a 2D screen sample. Native compilation requires each material's real inputs, constants and texture-view semantics.

Declarative field/opcode reference only: read-only `K:/Simpsons/RexGlueCurrent/include/rex/graphics/format/ucode.h`, SHA256 `e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb`; EXEC fields at lines 218–255, CF unpack at 515–522, texture fields at 821–857, vector/scalar opcode declarations, and ALU fields at 2005–2057. Dimension names are from adjacent `xenos.h` lines 165–169. No reference backend implementation is copied.

## Do CPU callers query the objects after creation?

**Neither inspected constructor queries or dereferences a returned shader object.** The create service tests the returned pointer for null, stores it through the output argument, and returns a Boolean. Its caller can therefore retain an opaque engine resource identity during this construction phase without depending on an SDK object layout.

- Plugin `0120`: ctor `82A67C48` calls builder `82A6CF80` at `82A67CD8`. After the six calls at `82A6D158..1A8`, `82A6D1D0` returns 1. It does not inspect shader metadata, invoke a method on a created shader, or test the individual creation results.
- Plugin `0133`: ctor `82CB33C8` constructs pipelines first (`82CB3434 -> 82CB3828`). Following the selected ten creations, `82CB3590..359C` increments `[82E31B30]` and returns the saved plugin argument. Its shader-version decisions read the capability record, not the newly created shaders. It also ignores individual creation results.

That is a scoped constructor conclusion, not a claim that the SDK objects are never read later. The located later users perform slot loads, null/identity/cache comparisons, and pass the object to the original SDK binder:

- `82A6BD20`: slot `82E27150` is loaded at `82A6BEF0` and bound at `82A6BF08`; slot `82E27154` is loaded at `82A6C1CC` and bound at `82A6C1E4`.
- `82A6C5A0`: selection of `82E2715C/60/58` at `82A6C798/7A0/7AC` reaches bind `82A6C7C4`; selection of `82E2714C` at `82A6C880` reaches `82A6C898`.
- Additional located users: `82A6D3A0` binds slot `82E27150` at `82A6D77C`; `82A6E4E8` binds the `58/5C/60` selection at `82A6E640`, or `4C` at `82A6E69C`.
- `82CB2550`: selection loads at `82CB2A5C..2AEC` cover all ten `0133` slots and converge on bind `82CB2B30`.

Every listed bind calls **`82445278(r3=device,r4=shader)`**. The binder computes `shader+0x28` at `8244531C`, then **dereferences `shader+0x3C` at `82445320`** and follows embedded metadata. It can access the previously bound object even earlier (`824452A4/452B0`). The caller updates its shader cache at `[82CD1A70]` before entering this binder (`82A6BF04`, `82CB2B2C`, for example). An unsupported bind must stop explicitly before original binder execution; returning as if binding succeeded would also leave the guest cache falsely satisfied.

## Release and lifetime

Release must be part of the same owner implementation as creation:

- `0120` dtor `82A67BB8` decrements its shared count and calls `82A6D1E0` at `82A67C08` when the count reaches zero. Cleanup null-checks each of the six shader slots, calls `823EFBD0` at `82A6D234/24C/264/27C/294/2AC`, then clears the corresponding slot. These releases follow ascending shader slot order.
- `0133` dtor `82CB35A8` decrements `[82E31B30]`. If the result is not positive, it null-checks, releases and clears the ten shader slots. Release calls are `82CB35E4,82CB3600,82CB361C,82CB3638,82CB3654,82CB3670,82CB368C,82CB36A8,82CB36C4,82CB36E0`. Relative to `82E31B00`, their slot order is `+20,+24,+1C,+18,+14,+10,+0C,+08,+04,+00`.
- Original engine release **`823EFBD0`** tail-calls `82441708`. That implementation computes object `+4` at `8244171C` and atomically reads/decrements its reference count starting at `82441728`. A native engine resource identity cannot enter this original SDK release path.

The selected constructors ignore some/all creation return values. Native allocation/validation failure must therefore become an explicit supported failure path or stop with cleanup; simply returning zero from a hook does not ensure that these original constructors fail. Preserve the plugin counters, original pipeline ownership and slot-clearing behavior rather than bypassing the constructors.

## Assessment: legitimate deferred engine resources

**Yes, the inspected construction/lifetime paths support real CPU-owned engine shader resources with compilation deferred until bind.** This is a design conclusion from the opaque constructor uses and explicit create/release boundaries, not evidence that these sixteen native shaders have been translated.

A successful native engine create should mean that it has validated this exact original record, acquired real ownership of the full original bytes and decoded metadata, and published a managed resource identity with stage `pixel` and state **uncompiled**. These records have no reflection-name table: retain that absence rather than promising recovered source/reflection. `native_compiled_ready` stays false until a verified translation is compiled successfully. The original creator itself primarily allocates/copies the already compiled guest representation; native creation need not pretend that the copied Xbox bytecode is a D3D11 shader.

At the native bind boundary, resolve the resource through its owner, validate lifetime/generation and required inputs, then either compile/bind a supported translation or raise explicit unsupported before original SDK code or a draw can consume it. If compilation succeeds, the owner retains the real native shader object until matching release. Release of an uncompiled resource simply destroys its actual CPU ownership; release of a compiled resource also releases that native object. No fabricated COM object, fake D3D pointer, or success-only shader stub is part of this design.

The located bind sites and release entry establish the necessary guards for these inspected paths. This is not an exhaustive alias analysis of the whole game. Any additional consumer must be routed through a proved native service or rejected before it dereferences the resource. Likewise, accepting these engine resources does not establish whole-driver readiness, target/sampler completeness, a drawable material, or an original rendered frame. Broad renderer/driver integration remains the parent's scope.

## Verification and reproduction

Read-only byte validation passed for **all 16** record hashes, exact extents, header offsets, payload/code bounds, complete CF schedules, fetch/ALU counts and export/dimension fields. All sixteen documented creation callsites independently decode to original BL instructions targeting `823EFB78`. Totals: **7,232 record bytes**, **3,996 code-window bytes**, **36 texture fetch slots + 230 ALU slots = 266 executed slots**. Code-window bytes also include CF and the final unused zero triplet. Original create, code-accessor, constructor, release and located bind instructions were checked with the existing offline disassembler; AOT was used for navigation only.

The following read-only commands reproduce the key object-layout boundaries:

```powershell
python -B tools/disassemble.py 0x823efb78 --count 23
python -B tools/disassemble.py 0x82448178 --count 68
python -B tools/disassemble.py 0x8245df60 --count 14
python -B tools/disassemble.py 0x8244531c --count 5
python -B tools/disassemble.py 0x82441708 --count 11
```

For each listed address, hash exactly `BE32[record+4] + BE32[record+8]` bytes, then locate code using the verified formula above. Decode each three-word CF group `a,b,c` as two instructions `(a,b&FFFF)` and `((b>>16)|(c<<16),c>>16)`, truncating the first word to 32 bits. CF opcode is second-word bits 12..15. EXEC address/count/sequence are first-word bits 0..11, 12..14 and 16..27; each two-bit sequence entry selects fetch in bit 0 and serialization in bit 1. This reproduces the per-record counts without assuming full shader arithmetic or a generic container schema.

Only `docs/startup-shaders.md` was written for this narrowed scope. The screen proof/analyzer/tests, frozen boundary JSON, original image, parent renderer and loading-art files were not changed. No additional parser, runtime hook or native shader implementation was added.
