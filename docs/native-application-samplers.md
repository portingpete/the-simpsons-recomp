# Original application sampler boundary — frozen evidence

Implementation update: the original evidence below describes the earlier
eight-stage/eleven-field host baseline. The live bridge now supports sixteen
application stages and bounded requests for all twenty named fields, verified
against actual original AOT setters. See
[native-application-sampler-bridge.md](native-application-sampler-bridge.md) for
current support, guards and executable validation. Original byte evidence and
the historical host-subset analyzer fixtures below remain unchanged.

The narrow replacement is **entry preflight at `82723C80`, preserve the original CPU body, replace `82723CD8..82723D04` inclusive, resume `82723D08`**. This block performs only SDK context/method lookup and the indirect sampler setter call. Keeping the suffix preserves the application owner cache, forced-equal updates and saved-frame dirty bits. Do not route this through an equality shortcut based on the separate engine cache at `82D0D170`.

This sidecar owns only this document, `tools/analyze_application_samplers.py` and `analysis/native-application-samplers.json`. No native implementation, scalar-dispatch mapping, SDK device layout, GPU interpreter, command-stream processing or draw authorization is supplied. Original instructions, tables and `.pdata` are authority; Ghidra pseudocode was not used for ABI proof.

## Reproduce and evidence limits

```
python -B tools/analyze_application_samplers.py --self-test --output analysis/native-application-samplers.json
```

The stdlib analyzer validates the original flat image SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. Image size is 15,466,496 bytes, base `82000000`. It pins 15 `.pdata` functions, six reviewed code ranges and two tables, then checks every disassembler address/word against original bytes. Shared PE-validation helper is hash-pinned. Read-only `K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h` corroborates enum labels; original code proves bit placement. No reference backend is copied.

**18 self-tests pass**, including literal static-owner address formation, malformed/endian-changed method tables, category sentinel/permutation, changed original words, asymmetric stage/category addresses, low-byte force behavior, exact push/frame alignment, dirty-bit set/clear, allocation bounds/overflow, current host subset rejection and asymmetric mip intersections. The report contains **1,693 byte-checked disassembly words**, plus separately checked critical instructions/constants. Fixtures check equations and rejection behavior; they do not execute the PPC function or prove a live owner's allocation/lifetime. Unrecognized image, dependency, framing or output path fails before report publication. JSON is deterministic; omitting `--output` prints it without writing.

## Entry ABI and precise hook contract

`82723C80` has original `.pdata` length `FC`; entry arguments are:

- `r3=O`: application state owner, **not** SDK device or native context token.
- `r4=S`: sampler stage; `r5=T`: application selector.
- `r6=V`: raw 32-bit value; `low8(r7)`: force. `100` is false; `101` is true.
- Public wrapper `826B79E8` takes `{stage,selector,value,force}` in `r3..r6`, materializes the **literal static owner address `82D5DB78`**, shuffles to this ABI and tail-branches at `826B7A04`. It does not load an owner pointer from that address: `826B79F0 = 3D4082D6` (`lis r10,-32042`), `826B79FC = 386ADB78` (`addi r3,r10,-9352`).

Static initializer `82CB9580/82CB9584` independently forms the same address with words `3D6082D6/386BDB78`, then tail-branches at `82CB9588` to `82725848`. The production public-wrapper owner is therefore the static root itself. Generic mapped-owner arithmetic fixtures in this analyzer do not authorize arbitrary production owners. Parent's proposed live-driver policy restricts this root and preflights `41D4` bytes, including eight saved frames ending at `D2C+8*694=41CC`; that full capacity/flag policy remains parent-owned validation, separate from this corrected literal-address proof.

`82723C84` calls save helper `82A3C3C0` (LR `82723C88`); its entry is not a five-argument shader or device method. The original has no selector, stage or allocation bounds checks.

At replacement-block entry `82723CD8`, live values are `r30=O`, `r31=S`, `r29=V`, `r28=category K`, `r27=20*S+K`, `r26=4*(1F7+20*S+K)`, `r11=4*T`. The original then loads context `D=[82D6D890]`, offset ID `[821506E0+4*T]`, and callback `[D+1D4+ID]`; it calls with `{r3=D,r4=S,r5=V}` at `82723D04`, LR `82723D08`. A native callback must preserve the saved registers and return to that exact continuation. It can use the checked native context identity without dereferencing it as SDK memory. The SDK-only stack spill at `SP+50` is not read by the remaining dispatcher.

No callback failure result is checked. There is no Boolean success return: the cache-hit path leaves `r3=O`; the pinned SDK setters preserve their incoming `r3=D`. Do not introduce an invented success code or swallow a rejected native operation. Preflight the full transaction before host mutation; failure must stop before the original suffix publishes any cache/dirty word. Preserve the original AOT CPU path, including the original forced-equal branch, instead of reimplementing the bookkeeping.

## Selector, category and SDK method tables

There are three distinct indices. `82D6D648[T]` is a CPU cache category; `821506E0[T]` is an SDK offset ID; category order does not equal selector order. The latter table is exactly 21 BE words: `{0,0,4,8,...,4C}`. **T=0 is invalid**, despite the first zero looking like AddressU.

Initializer `82724840` fills 21 category slots with `7FFFFFFF` at `827248AC..C0`. Its sampler registration suffix `827254C0..82725840` assigns the categories below, publishes name pointers at `82D6D448`, reverse category-to-selector entries at `82D6D5F8`, and writes default words at argument `r3+148+4*K`. Counter `[82D6D7EC]` starts zero in the image and is incremented by twenty; this function does not reset it. The table below is for the **first initialization**. A live preflight should check count 20 and the exact map, not confuse image-time BSS zeros with a valid map. Repeated registration/appended categories are outside the bounded contract.

Names below omit original ASCII prefix `SamplerState_`. “App” is the first initialization's default record, not proof the device has already received that value. “SDK” is the separate default in the original method triple. T, K and default values are decimal; SDK IDs and code addresses are hexadecimal.

| T | K | SDK ID | Original name | App / SDK | SDK setter | Current host field / accepted raw values |
|---:|---:|---:|---|---|---|---|
| 1 | 0 | 00 | ADDRESSU | 2 / 0 | 8243C180 | AddressU / 0..7 |
| 2 | 1 | 04 | ADDRESSV | 2 / 0 | 8243C1D0 | AddressV / 0..7 |
| 3 | 2 | 08 | ADDRESSW | 2 / 0 | 8243C220 | AddressW / 0..7 |
| 4 | 8 | 0C | BORDERCOLOR | 0 / 0 | 8243C110 | BorderSelector / 0 |
| 5 | 3 | 10 | MAGFILTER | 1 / 0 | 8243BBD0 | Magnification / 0..1 |
| 6 | 4 | 14 | MINFILTER | 1 / 0 | 8243BA40 | Minification / 0..1 |
| 7 | 5 | 18 | MIPFILTER | 2 / 2 | 8243BD60 | MipFilter / 0..2 |
| 8 | 9 | 1C | MIPMAPLODBIAS | 0 / 0 | 8243BF70 | LodBiasBits / +0 bits only |
| 9 | 10 | 20 | MAXMIPLEVEL | 0 / 0 | 8243C010 | **MinimumMip / 0** |
| 10 | 13 | 24 | MAXANISOTROPY | 1 / 1 | 8243BE50 | MaximumAnisotropy / 1 |
| 11 | 6 | 28 | MAGFILTERZ | 1 / 0 | 8243BCC8 | Unsupported update |
| 12 | 7 | 2C | MINFILTERZ | 1 / 0 | 8243BB38 | Unsupported update |
| 13 | 15 | 30 | SEPARATEZFILTERENABLE | 0 / 0 | 8243BDB8 | Unsupported update |
| 14 | 11 | 34 | MINMIPLEVEL | 13 / 13 | 8243C090 | **MaximumMip / 13** |
| 15 | 12 | 38 | TRILINEARTHRESHOLD | 0 / 0 | 8243C270 | Unsupported update |
| 16 | 14 | 3C | ANISOTROPYBIAS | 0 / 0 | 8243BEC8 | Unsupported update |
| 17 | 16 | 40 | HGRADIENTEXPBIAS | 0 / 0 | 8243C2C8 | Unsupported update |
| 18 | 17 | 44 | VGRADIENTEXPBIAS | 0 / 0 | 8243C320 | Unsupported update |
| 19 | 18 | 48 | WHITEBORDERCOLORW | 0 / 0 | 8243C378 | Unsupported update |
| 20 | 19 | 4C | POINTBORDERENABLE | 1 / 1 | 8243C3D0 | Unsupported update |

This maps to `renderer::SamplerState` by **offset ID**, with no desktop D3DSAMP enum translation. The existing namespace is `Simpsons::Graphics`; the bridge class is `Simpsons::EngineRenderState`. `MAXMIPLEVEL`/`MINMIPLEVEL` names are misleading for the host field names: use the verified min/max arithmetic below.

Initializer `82466800`, sampler suffix `82466874..8246690C`, reads **20 triples** `{getter,setter,rawDefault}` from `82CD2D78`. Stores at `824668B0/B8` install methods at `D+1D4+ID` and `D+3B8+ID`; `824668C8` calls each setter (LR `824668CC`). It initializes stages `0..25` and null-binds each via `824408E0` (call `824668FC`, LR `82466900`). The report lists all exact getters, defaults, names and registration store PCs.

Application loops/arrays independently prove **16 stages, 0..15** (`82724794`, `82724574`, `827246B4`). Existing host state supports **8, 0..7**. None of these counts authorizes silently wrapping stages, accepting SDK's other ten slots as application slots, or expanding host support. Reject stages 8..15 until implemented. Current screen draw validation is narrower still: repeat addressing, linear filters and base-map mip selection. Accepting a state update is not permission to draw with it.

## CPU cache, saved frames and ownership

All numbers in these equations are hexadecimal except the factor 20 (decimal categories per stage). Words are BE. For a checked selector, `K=[82D6D648+4*T]`:

```
currentAddress = O + 0x7DC + 0x50*S + 4*K
N              = signed32([O + 0xD28])
F              = O + 0x698 + 0x694*N                  // only if N > 0
baseline       = [F + 0x148 + 0x50*S + 4*K]
dirtyAddress   = F + 0x654 + 4*(S + (K >> 5))
mask           = 1 << (K & 31)
```

If the cached raw word equals `V` and `low8(force)==0`, the original returns at `82723CD4` with **no setter or writes**, without consulting `N`. Otherwise it invokes the SDK. On return, if signed `N>0`, `V==baseline` clears only `mask`; otherwise it sets only `mask`. It stores that dirty word first (`82723D58` or `D6C`), then publishes the raw current word (`D5C` or `D70`). Signed `N<=0` only writes the current cache. No value normalization occurs before comparison or publication.

`82723978` pushes by copying `694` bytes from `O+694` to `O+D2C+694*oldN`, increments the count, and clears the saved dirty masks at `+648/+64C/+650` and the sixteen sampler masks `+654..693`. There is **no offset discrepancy**: after increment, `O+D2C+694*(N-1) == O+698+694*N`. Pop `827243E0` decrements first and uses that saved frame; its sampler suffix `827244CC..82724578` calls `82723C80` with values at frame `+148+50*S+4*K` and the original force flag. These frames reside in the owner allocation; the sampler dispatcher allocates, retains and frees nothing.

`82724728` reapplies one default row from **O+148**, repeating the same twenty values at every stage with force=1. `827245F0` applies current cached stage rows. Neither establishes that all twenty types are natively supported. Guarding unsupported auxiliary states must also apply to equal/default updates; do not convert an unsupported operation into success because a different cache happened to match.

Three direct public call sites have an additional CPU save/restore layer: `826B36D8`, `826B3790`, `826B3850`. Their sixteen-byte records contain `{stage, selector, desiredValue, savedValue}`. First application reads the cached value through `826B79B0` and saves it at `+C`; apply sends `+8`, restore sends `+C`. All send force=0. There is no color packing, enum remap or floating conversion in these callers. Keep their original CPU execution.

Preflight must validate a live owner, initialized category/reverse maps and count, stage/type/value subset, current-cache readability/writability, and every saved baseline/dirty word the suffix will access. Use checked 64-bit arithmetic against the **actual owner allocation extent** before converting to guest addresses. Do not invent a maximum frame capacity or trust a numerically in-range pointer alone. Saved frame reads/writes must already be safe before invoking the host setter; otherwise host publication followed by an original CPU memory fault leaves an inconsistent transaction. Original `N` is signed; rejecting negative counts as corruption would be an additional native policy, not the original branch behavior.

## Raw value transforms and binding interplay

The dispatcher forwards **unchanged raw DWORDs**. Original SDK setters operate on six-word descriptor `D+480+24*S`; bit numbers here count from the **LSB**, not PPC mask numbering. This describes evidence and does not propose maintaining a native SDK descriptor.

- U/V/W insert raw low three bits into word0 bits `10..12`, `13..15`, `16..18`. Reference labels are repeat=0, mirrored repeat=1, clamp-to-edge=2, mirror-clamp-to-edge=3, halfway variants=4/5, border variants=6/7. App defaults clamp-to-edge=2 differ from SDK/host startup repeat=0.
- Normal MAG/MIN fields occupy word3 bits `19..20`/`21..22`. For raw 0/1 these mean point/linear. General setters combine `raw | (raw>>2)`, retain anisotropic flags in word4 bits 10/11, use the lookup `82069FD0`, and recompute Z filtering. **Do not reduce arbitrary raw values to low two bits**, or claim all upper-bit values have native support. The host rejects them.
- MIPFILTER is low two bits in word3 `23..24`: reference point=0, linear=1, base-map=2, use-fetch-constant=3. Host permits the first three; screen drawing currently requires 2.
- BORDERCOLOR is a **zero/nonzero selector**, not packed ARGB: word5 low two bits are replaced with 0 or 1. Getter returns 0 or `FFFFFFFF`. The native accepted subset is 0 only.
- MIPMAPLODBIAS interprets raw bits as float, multiplies by original `32.0f` (`821DD3F4`), truncates with `fctidz`, inserts low ten bits in word4 `12..21`. Getter sign-extends ten bits and multiplies by `1/32` (`8206A020`), returning float bits. ANISOTROPYBIAS similarly multiplies by `-8.0f` (`8206A01C`), inserts low four bits in word5 `5..8`; getter uses negative signed4 / 8 (`8206A014`). No NaN, overflow or arbitrary-bias host semantics are claimed. `fmuls` is single precision; a future implementation must preserve that rounding before truncation.
- MAXANISOTROPY retains the low request byte at `D+2E8C+S`. When either anisotropic flag is set it indexes `82069FD0` by the original raw DWORD and updates word3 `25..27`. The verified 0..16 lookup is `[0,0,2,2,3,3,3,4,4,4,4,4,4,5,5,5,5]`. With neither flag set it only stores the byte, without dirtying the descriptor. MAG/MIN setters also consult this retained request. Values beyond the lookup are not made safe by this analysis.
- MAGFILTERZ, MINFILTERZ and SEPARATEZFILTERENABLE share byte `D+2EDA+S`. Their exact stored-byte updates are `low8((old&~1)|raw)`, `low8((old&~2)|(raw<<1))`, `low8((old&~4)|(raw<<2))`. They recompute word4 low two bits from normal filters and this byte. Non-Boolean input is **not** masked before OR and may change other flags. Their full schedules are byte-checked, but native Z/3D filtering is unsupported.
- TRILINEARTHRESHOLD inserts low two bits in word5 `3..4`; H/V gradient biases insert low five bits into word4 `22..26`/`27..31`, with signed-five-bit getters. WHITEBORDERCOLORW inserts one bit at word5 bit2. POINTBORDERENABLE writes **raw==0** to word1 bit11, with the inverse getter. These remain unsupported native updates, including default-equal calls.

Descriptor-changing setters OR bit `1ULL << (31-S)` into SDK's BE64 dirty mask at `D+18`. Mip-limit setters do so only when a texture is bound; anisotropy does so only when its filter flags activate a descriptor change. These are console descriptor dirty effects, distinct from the **application frame masks that must remain original CPU effects**. No SDK dirty-mask emulation is proposed.

For auditing the coupled normal/Z arithmetic without shipping a descriptor interpreter: with uint32 arithmetic, let `q=raw>>2`, `a=lookup[retainedAnisotropyByte]`, and `o` be the other normal-filter flag. MAG computes `g=raw|q|((a & ~((o|q)-1))<<6)`; MIN uses shift 4. It stores low2(g) in its normal filter field, low3(g>>6) or low3(g>>4) in the shared anisotropy field, and low1(q) in its own flag. With descriptor word3 `B`, the Z recomputation forms `P=((B>>19)&1)|(((B>>21)&0x7FF)<<1)`. In normal setters `F` is the retained Z byte; in a Z setter it is the **new untruncated flag word**, before `stb`. For `m=(F>>2)-1`, the new word4 low2 is `low2((P&m)+(F&~m))`. Boolean separate-enable therefore selects normal MAG/MIN low bits or the separate Z bits. These equations follow `8243BA6C..BAE8`, `8243BBFC..BC78`, and the three Z setters; lookup bounds and non-Boolean inputs remain unsupported native behavior.

For ID20 (`MAXMIPLEVEL`), setter `8243C010` reads the bound texture pointer `[D+30F8+4*S]`. If nonnull, effective minimum becomes `low4(max(texture.word2C.bits2..5, rawValue))`, then it stores the requested low byte at `D+2EA6+S`. ID34 (`MINMIPLEVEL`) uses `low4(min(texture.word2C.bits6..9, rawValue))`, and retains a byte at `D+2EC0+S`. Their getters return the **requested byte**, not the effective clipped limit. If unbound, setters only store that byte.

Binding `824408E0` imports resource words `+1C..30`, preserving sampler word0 `10..21`, word1 bit11, word3 `19..30`, word4 `0..1` and `10..31`, word5 `0..8`. It recomputes min/max using the **retained request bytes**, not the untruncated DWORDs. It neither orders an inverted interval nor resets address/filter requests. Null binding retains inactive descriptor bits while changing the pointer. Old-resource bookkeeping at `824409E8..82440A4C` concerns submission tracking; this report does not infer a COM-style retain/release contract from it. Native resource binding/lifetime remains a separate owner responsibility.

## Existing host integration and required guards

`Graphics::EngineState::setSampler(stage, sdkId, raw)` already validates the eleven named IDs/values in the table. It is the effective host-state operation to reuse. The separate `EngineRenderState::setSampler` in `runtime/engine_state_bridge.cpp` currently checks guest cache `82D0D170+320*S+4*ID` and may return before validating/publishing host state, otherwise writes that cache. **The original application dispatcher calls the SDK directly and never writes that engine guest cache.** Reusing that bridge method unmodified would introduce CPU side effects and allow its cache equality to suppress original forced updates. A native application observer should validate/update effective state directly, with no new engine guest-cache write. The original application suffix should own its own cache publication.

Before wiring the block, explicitly reject unknown selectors, unsupported stages/values and all nine auxiliary updates even if an application or engine cache is equal. Verify the native context is live without interpreting it as SDK data. Preflight must not mutate host state on the original no-call path. On changed/forced calls, publish effective state once and continue at `D08`. Preserve inherited state; do not reset unrelated stages/fields, create SDK tables or mark a draw ready.

Known bypasses must remain guarded or receive separately verified replacements:

- `827238B8`: SDK-only sampler setter (indirect call `827238F4`), no application cache update.
- `82723858`: SDK getter to output word; `82723AB0`/`82723B40`: SDK getter synchronization into application CPU cache. Noncanonical raw values, Boolean border getter and retained-byte mip getters mean a universal “return last raw word” is incorrect. Keep original getter synchronization CPU effects if these paths are later ported.
- `82724038`: combined U/V/W setter, reached via public wrapper `826B7A08`, directly edits SDK descriptor and application caches. It bypasses `82723C80`.
- `82724230`: combined mag/min/mip setter, reached via `826B7A30`; calls `827237D0` at `827242D4`. The helper calls SDK MAG/MIN setters (`827237F8`, `82723808`) and inserts MIP directly. It also bypasses the single-selector boundary. A later replacement must validate the whole triple before mutation.

Unknown indirect aliases and future demand for stages 8..15 are not closed by this bounded scan. Runtime owner allocation/generation must come from the parent, not inferred from cache equations. No actual loading draw, sampler selection log or end-to-end restoration execution was established in this task. The actionable next step is the same original-CPU-preserving hook strategy as the independently owned scalar path, using the exact sampler block and supported subset above.
