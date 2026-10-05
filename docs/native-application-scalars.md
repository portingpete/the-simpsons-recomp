# Remaining original startup scalar states

Scope: independent original-byte evidence for the startup failure at application selector **51**, SDK ID **168**, caller LR **8272470C** in `build/boot-044.log`. All addresses, selectors, IDs and raw words below are hexadecimal; registration ranks, bit positions and shift counts are decimal and zero-based. This document supplies no runtime implementation or permission to render unsupported states. The three owned outputs are this document, `tools/analyze_application_scalars.py`, and `analysis/native-application-scalars.json`; sampler evidence and parent files remain untouched.

## Immediate implementation input

`168 = RenderState_HISTENCILENABLE`, `16C = RenderState_HISTENCILWRITEENABLE`, `170 = RenderState_HISTENCILFUNC`. Their application and SDK defaults are all zero. Setters `8243B960`, `8243B990`, `8243B9C0` insert **raw low bit** into context `+2940` bits **3, 2, 5**, respectively, then OR `100` into BE64 dirty mask at `+10`. Getters return the corresponding bit. Bits count from the LSB. `HISTENCILFUNC` is **not the ordinary 3-bit stencil comparison enum**. The original name does not establish which comparison its one-bit values mean.

The complete remaining startup sequence is below. Float words `3F800000`/`40000000` mean 1.0f/2.0f. App defaults are original first registration values; SDK defaults come from method table `82CD28B8`. They must remain distinct. The stated build073's 45 supported IDs cover all earlier ranks and rank 52; **37 additional registered IDs** remain from this failure onward.

| Rank | Selector | SDK ID | Original RenderState suffix | App default | SDK default | Setter |
|---:|---:|---:|---|---:|---:|---|
| 44 | 51 | 168 | HISTENCILENABLE | 0 | 0 | 8243B960 |
| 45 | 52 | 16C | HISTENCILWRITEENABLE | 0 | 0 | 8243B990 |
| 46 | 53 | 170 | HISTENCILFUNC | 0 | 0 | 8243B9C0 |
| 47 | 43 | 130 | VIEWPORTENABLE | 1 | 1 | 8243B260 |
| 48 | 29 | C8 | SCISSORTESTENABLE | 0 | 0 | 8243D0E8 |
| 49 | 48 | 144 | HALFPIXELOFFSET | 1 | 0 | 8243B718 |
| 50 | 4D | 158 | GUARDBAND_X | 3F800000 | 40000000 | 8243B810 |
| 51 | 4E | 15C | GUARDBAND_Y | 3F800000 | 40000000 | 8243B840 |
| 52 | 04 | 34 | FILLMODE — already supported | 0 | 0 | 82439F30 |
| 53 | 22 | AC | CLIPPLANEENABLE | 0 | 0 | 8243AA48 |
| 54 | 27 | C0 | MULTISAMPLEANTIALIAS | 1 | 1 | 8243AC10 |
| 55 | 28 | C4 | MULTISAMPLEMASK | FFFF | FFFFFFFF | 8243AC40 |
| 56 | 23 | B0 | POINTSIZE | 3F800000 | 3F800000 | 8243AD70 |
| 57 | 24 | B4 | POINTSIZE_MIN | 3F800000 | 3F800000 | 8243ADD0 |
| 58 | 25 | B8 | POINTSPRITEENABLE | 0 | 0 | 8243AD60 |
| 59 | 26 | BC | POINTSIZE_MAX | 3F800000 | 42800000 | 8243AE28 |
| 60 | 30 | E4 | TESSELLATIONMODE | 0 | 1 | 8243B6E0 |
| 61 | 31 | E8 | MINTESSELLATIONLEVEL | 3F800000 | 3F800000 | 8243B670 |
| 62 | 32 | EC | MAXTESSELLATIONLEVEL | 3F800000 | 3F800000 | 8243B6A8 |
| 63 | 55 | 178 | PRESENTINTERVAL | 1 | 0 | 8243BA10 |
| 64–79 | 33–42 | F0–12C, step 4 | WRAP0–15 | 0 | 0 | Individual entries in JSON |
| 80 | 49 | 148 | PRIMITIVERESETENABLE | 1 | 0 | 8243B750 |
| 81 | 4A | 14C | PRIMITIVERESETINDEX | FFFF | FFFF | 8243B780 |

The loop `827246C8` walks the **registration order**, not numerical selector order. At `827246F8` it sets force=1; `827246FC` reads successive default words from the owner; `82724704` reads successive selectors from `82D6D6A0`; `82724708` calls `82723D80`, returning to `8272470C`. Its bound is `[82D6D7E8]`, 82 after first initialization. Reaching SDK 168 therefore does not imply the next request is 16C through every consecutive SDK ID.

## Nearby unregistered SDK rows

The selector-to-ID table `82150580` has 87 BE words: entry zero is zero, and selectors `1..56` map to `4*(selector+9)`. The first application initializer registers only 82 of those 86 nonzero selectors. **4F→160, 50→164, 54→174 and 56→17C remain invalid sentinel categories.** Do not infer registration or accept them solely from the arithmetic mapping.

- **174**, setter `8243B9F0`, getter `8243BA08`, SDK default 0: `stb r4,2942(r3)`, then dirty64 `+10 |= 100`. On BE storage this replaces bits 8–15 of word `+2940`, preserving the adjacent hi-stencil flag bits. Getter returns the byte. **No verified semantic name here**; proximity to hi-stencil does not prove “reference” or “mask.”
- **178**, setter `8243BA10`, getter `8243BA18`, SDK default 0: stores the entire raw word at `+3504`, with no dirty-mask store or call. The application registration independently names it **PRESENTINTERVAL**, default 1. Retaining the request is a real CPU state operation; presentation timing/interval semantics must be implemented or gated at the native present consumer.
- **17C**, setter `8243BA20`, getter `8243BA30`, SDK default 0: inserts raw low seven bits into word `+2E44` bits 23–29, with no dirty-mask store or call. Getter returns those seven bits. **No verified semantic name here**; leave unregistered application use rejected.
- **160/164**, setters `8243B870/8243B898`, SDK default `3F800000`: float-bit transfers to `+29D0/+29C8`, with dirty64 `+20` bits 31/33. They are not startup registrations. No application names are invented.

**Exact getter proof:** `8243AA98 = 80632E48` (`lwz r3,2E48(r3)`), followed by `4E800020`, returns the full retained scissor request with no rectangle read or helper call. The hi-stencil getter extraction words at `8243B984/B9B4/B9E4` are `5563EFFE/5563F7FE/5563DFFE`, selecting bits 3/2/5. `8243BA08 = 88632942` is an unsigned-byte load, whereas `8243BA18 = 80633504` is a full-word load. `8243BA34 = 55634E7E` extracts seven bits starting at bit23. The report annotates all remaining getter results, including normalized mask/nibble/Boolean values versus retained raw requests. A single generic “return application cached word” is not generally equivalent.

## Exact remaining setter effects and validation consequences

`D` means the original SDK context only; a native opaque context must never be treated as this layout. The offsets below are evidence of original side effects, not a proposal to reproduce an SDK object or command stream. Dirty masks are BE64 fields, while ordinary words are BE32.

- **VIEWPORTENABLE 130 / 8243B260:** zero writes `D+294C=400`; any nonzero writes `43F`. It inserts `(raw==0)` into `D+2944` bit16, then dirty `D+10 |= A0`. Reference labels identify the six viewport scale/offset enable bits and clip-disable bit. A bounded native contract may admit canonical 0/1, initially 1. This is active transform/clipping policy, not an inert registration; actual native viewport/draw implementation remains separate.
- **SCISSORTESTENABLE C8 / 8243D0E8:** writes the entire raw word to `D+2E48`, then tail-calls `8243C430(D,D+317C)`. That helper copies the stored rectangle, truncates viewport x/y/width/height float fields, uses the viewport rectangle when disabled and its intersection with the stored scissor rectangle when enabled, packs low 15-bit coordinates into `+28C4/+28C8`, and calls **82439C20 at 8243C514**. The latter is a console submission helper and may call the command-buffer allocator. **Do not run this SDK path on a native identity.** Native disabled-scissor behavior must preserve effective viewport bounds; enabled scissor additionally needs a checked rectangle. Overflow, negative/empty rectangles and generalized float conversion are not newly authorized.
- **HALFPIXELOFFSET 144 / 8243B718:** replaces `D+29C0` bit0 with raw low bit and sets dirty `D+20` bit35. Reference `PA_SU_VTX_CNTL.pix_center` labels 0 as integer pixel centers and 1 as half-integer centers. Original startup requests **1**, whereas SDK default is **0**. This does not authorize adding an arbitrary half-pixel shift to the already-proven screen shader; match the complete vertex/viewport/rasterizer convention at draw time.
- **GUARDBAND_X/Y 158/15C:** transfer raw float bits via `lfs/stfs` to `D+29CC/+29C4`, setting dirty `D+20` bits32/34 respectively. Startup changes 2.0f to 1.0f. No geometry clipping/guardband native implementation is proved by these stores. A narrow owner can reject every value except exact `3F800000`, while separately gating unsupported geometry behavior.
- **CLIPPLANEENABLE AC / 8243AA48:** writes `D+28B4 = raw ? 1000 : 0`; clears `D+2944` low six bits, then ORs the **unmasked** raw word. Dirty `D+10 |= 80 | (1<<44)`. Getter returns low six bits. Reject values above `3F` rather than permit higher bits to corrupt unrelated controls; the initial no-user-clip-plane subset is raw 0.
- **MULTISAMPLEANTIALIAS C0 / 8243AC10:** low raw bit becomes `D+2948` bit15; dirty `D+10 |= 40`. **MULTISAMPLEMASK C4 / 8243AC40:** stores raw low 16 bits at `D+2A00`; dirty `D+20 |= 80000`. SDK `FFFFFFFF` and app `FFFF` have the same effective mask but different requested words. Real sample-count/coverage behavior belongs to the target/draw contract; raw enabled is not evidence of an MSAA resource.
- **POINTSPRITEENABLE B8 / 8243AD60:** stores the entire raw word at `D+2E64`, with no dirty write. Do not infer it has no later consumers. Initially zero; point sprites remain unsupported.
- **POINTSIZE B0 / 8243AD70:** retains float at `D+2E6C`; computes single-precision `value*8.0f` (`821DD23C=41000000`), truncates via `fctiwz`, writes low16 to **both** halfwords `+2964/+2966`, and sets dirty `D+18` bit54. **POINTSIZE_MIN/MAX B4/BC:** retain floats at `+2E70/+2E74`, compute single-precision `value*16.0f` (`821DD220=41800000`), truncate to low16 at **+296A/+2968** respectively, and set dirty `D+18` bit53. Getters return the retained floats. Initial 1.0f gives size halfwords 8 and min/max halfwords 16; do not interchange min/max BE halfword positions. NaN, overflow and arbitrary point rendering are unproved; exact 1.0f is the proposed bounded request.
- **TESSELLATIONMODE E4 / 8243B6E0:** replaces `D+2978` low two bits, dirty `D+18` bit49. Local reference names value 0 **discrete**, 1 continuous, 2 adaptive. **Mode 0 is not proof tessellation is disabled.** **MIN/MAXTESSELLATIONLEVEL E8/EC:** transfer floats to `D+2980/+297C`, dirty `D+18` bits47/48. Retain mode 0 and levels 1.0f for startup only; tessellated draws remain rejected until separately implemented.
- **WRAP0..15 F0..12C:** eight four-bit slots per word at `D+292C` (0..7) and `D+2930` (8..15), dirty `D+10 |= 2000/1000`. Slot 0 clears its nibble then ORs raw unmasked; slots 1..6 clear their nibble and OR `raw << (4*slot)` truncated to uint32; slot 7 inserts low4. Higher raw bits can therefore affect neighboring slots. Canonical validation is 0..F, with startup-only acceptance 0. These are distinct from texture sampler U/V/W address modes; coordinate-wrap rendering is not proved here.
- **PRIMITIVERESETENABLE 148 / 8243B750:** inserts low raw bit at `D+2948` bit21, dirty `D+10 |= 40`. **PRIMITIVERESETINDEX 14C / 8243B780:** stores full raw uint32 at `D+28D8`, dirty `D+10` bit38. Startup requests enabled and index FFFF. Native indexed topology/restart handling must honor this or reject affected draws; a retained field is not completed primitive restart support.

For the immediate hi-stencil trio, a baseline-only policy of **exact zero** is supported by the observed application and SDK defaults. Canonical 0/1 matches their stored width but does not prove native enabled behavior. Keeping depth/stencil-disabled screen draws bounded still requires explicit host gates for these and the other active fields. The analyzer provides no native success stub.

## Provenance and reproduction

Original flat PE: base `82000000`, size 15,466,496, SHA-256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. Original registration names and stores are authoritative; SDK triples provide getter/setter/default identity. The analyzer extracts only the pinned initializer's finite first-initialization constant assignments into symbolic table/object/stack offsets. It does not execute guest code, traverse arbitrary branches, instantiate SDK memory or consume GPU packets. First-init BSS-zero assumptions, both default-float setup paths, every load/store shape, exact registration order and final count are checked; unexpected code or uninitialized values reject.

Read-only enum/field corroboration comes from `K:/Simpsons/RexGlueCurrent/include/rex/graphics/{registers,xenos}.h`, each hash-pinned. No reference backend is copied. Reference descriptions containing uncertainty are not promoted to facts about this original. In particular, original hardware precision, primitive restart width/timing and enabled hi-stencil semantics are not established by this report.

Reproduce with:

```
python -B tools/analyze_application_scalars.py --self-test --output analysis/native-application-scalars.json
```

Omit `--output` for JSON on stdout. Any other output path, changed image/reference/dependency, malformed table, unresolved constant assignment or changed call/word rejects before report publication. Final validation: **13 tests pass**, 82 registrations, 37 additional IDs relative to the stated build073 snapshot, **16 byte-pinned extents and 1,749 byte-checked disassembly rows**. Tests cover first-init order/defaults, one-bit hi-stencil masks, getter load widths/extraction, BE halfword positions, neighboring-bit preservation, application-vs-SDK defaults, absent registrations, table truncation/endianness, changed code/BSS and unsupported point conversions. These are offline evidence tests, not PPC execution equivalence or native consumer tests. The report is byte-deterministic on repeated generation.
