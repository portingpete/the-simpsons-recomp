# Loading-quad expanded blending

**Storage and lifecycle are established; exact console blend arithmetic is not.** The later [original screen implementation](native-original-screen.md) supersedes this investigation's recommendation to keep the whole draw blocked. It implements the recovered equations with explicit native float arithmetic and integer packing, while leaving console pixel parity unverified. Original code plus the local reference prove that format 2→10 changes a blending interpretation while retaining the same packed 10:10:10:2 UNORM storage. They do not establish source/factor precision or rounding needed to certify pixel parity.

This bounded investigation writes only this document, `tools/analyze_expanded_blend.py`, and `tests/test_expanded_blend.py`. It does not change the host state owner/backend or implement a command processor. The tool emits deterministic JSON to stdout and marks `native_exact_blend_contract_proven: false`. No generated report file is required.

## Evidence and its authority

Original image: flat base `82000000`, 15,466,496 bytes, SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. The analyzer independently checks that identity and sixteen original code-window hashes, covering **2,210 instruction words**. Original disassembly determines the state transitions, allocations, bindings and call arguments below. AOT comments were navigation/cross-check aids, not the semantic authority.

Seven read-only local reference files are pinned by complete SHA256 in the analyzer and its JSON. These are reference software, **not hardware measurements**:

- [xenos.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h:298): format IDs, component/storage definitions and resolve compatibility.
- [render-target cache declaration](K:/Simpsons/RexGlueCurrent/include/rex/graphics/pipeline/render_target/cache.h:34): explicitly describes the host-target path as approximate and excludes internal blend precision from its direct-format mapping claims.
- [render-target format information](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/render_target/cache.cpp:63): shared clamp range and write masks.
- [DXBC pack/unpack](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/dxbc_translator_om.cpp:1110) and [SPIR-V pack](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/spirv_translator_rb.cpp:2428): both group formats 2 and 10 into the same component packing.
- [D3D12 resource mapping](K:/Simpsons/RexGlueCurrent/src/graphics/d3d12/render_target_cache.cpp:1580): both use `R10G10B10A2_UNORM` backing.
- [resolve selection](K:/Simpsons/RexGlueCurrent/src/graphics/util/draw.cpp:1114): conditional packed-copy compatibility, separate from blending.

No evidence found in the inspected local sources supplies a hardware-validated format-2/format-10 numerical comparison. Agreement between related reference backends is not an independent hardware oracle. No reference GPU backend code is shipped by these files.

## Exact original state and storage lifetime

Startup `823EDF64..6C` constructs `182801B6`. At `823EE070`, `82440698` receives that format, multisample selector 0 and a nonnull pointer to three zero words. The surface is published at `82D0CB00` and bound at `823EE0C4`. In builder `8243FC38`, surface format index 54 is remapped to 7 at `8243FC84..8C`; original BE16 `[8206A036]=3220` yields render-target nibble **2** at `8243FDCC..D0`. This is integer normalized 10:10:10:2, not float10.

`82440698` allocates a 48-byte surface object. Because startup supplied nonnull r7, the path skips the later `8245D8B8` allocation branch. This must not be confused with allocating or clearing a new expanded pixel buffer. The descriptor builder retains the original SDK format word at surface `+28`, writes target metadata at `+1C`, and size at `+2C`. Allocation alone is not proof of initialized pixels.

The complete leaf `8243B3B0..8243B454` has 41 instructions and no calls:

1. Store the supplied request at `device+2EF4` (`8243B3B4`), even when no target is bound. The original does not canonicalize it; the analyzer models only the observed Boolean values 0/1.
2. Read surface pointer `device+3090`, then format nibble `(surface[+1C] >> 16) & F`.
3. Only formats 2,3,10,12 can change. If descriptor bit 19 already matches the Boolean request, return without a metadata/dirty update.
4. Request 1 maps 2→10 and 3→12. Request 0 maps 10→2 and 12→3.
5. Replace only mask `000F0000` independently in surface `+1C` and device mirror `+2884`; preserve all other bits. OR `0100000000000000` into the BE64 dirty word at `device+10`.

The setter does not allocate, copy, resolve, clear or convert pixels. It leaves the target pointer, location bits, original format word, dimensions and size unchanged. Formats 3/12 are included in the descriptor transition proof only; this document's numerical/storage contract is restricted to integer formats 2/10.

The request also survives a null/unaffected target. Binding `823EDB68 → 8243DED0 → 8243D230` stores the new surface at `8243D24C`, copies its metadata to the device mirror at `8243D28C`, reloads the saved request at `8243D2AC`, and reapplies the same conditional format transformation through `8243D374`. Therefore a host owner must retain both the requested mode and each surface's active interpretation. Unbinding does not itself reset that surface's metadata.

For the supported selectors 0/1/2, quad `82756480` enables expansion at `827565C0`, finalizes the draw at `82756810`, and disables expansion at `82756848`. Cleanup writes zero; it does **not** restore the previous request. Selector 3 skips both writes, retaining any inherited request. The early return at `827564C0` precedes all this setup. The actual original selector condition is “not 3,” but the native bounded contract must continue rejecting out-of-range selectors rather than inventing more modes.

## Persistent representation and algebra

The reference's `GetStorageColorFormat(10)` returns 2, and its 64-bpp predicate is false for both. The persistent render-target word has X/Y/Z/W fields at bits **0/10/20/30**, widths **10/10/10/2**. Their normalized code denominators are **1023/1023/1023/3**. Alpha therefore has four persistent values, not 1,024. The names `AS_10_10_10_10` and `AS_16_16_16_16` describe neither a larger allocation nor proof of extra alpha surviving later draws.

Those bit fields describe the abstract render-target component word. Serialized texture byte order, tiled addressing, `182801B6` ZYXW interpretation and `28280136` ZYX1 sampling policy are separate. The inspector does not mistake its word packing for a complete raw-memory resolve or change the established logical RGBA convention of the native resource owner.

The original packed blend words establish these equations, before the unresolved numeric conversions. Let S be the existing screen shader's output (sample × c0 for textured, c0 for flat), and D the prior stored destination:

- Selector 0, `00010106`: RGB = S.rgb × S.a + D.rgb.
- Selector 1, `00010706`: RGB = S.rgb × S.a + D.rgb × (1−S.a).
- Selector 2, `00010186`: RGB = D.rgb − S.rgb × S.a.
- All three: alpha = S.a, from ONE/ADD/ZERO. There is no destination-alpha accumulation.

Both formats are fixed-point in the reference, with color/alpha clamped to [0,1]. The reference clamps sources/factors, evaluates blend arithmetic, then packs RGB×1023 and alpha×3 using a half-unit offset and conversion to unsigned integer. **This is a description of that implementation, not a selected Xenos rounding specification.** The inspected packing/flag paths group formats 2 and 10; they do not independently prove the numerical difference requested by the original engine.

The native persistent image must keep the original storage values across 2→10→2 without clearing or allocating a replacement. If a higher-precision temporary is used internally, it cannot simply retain extra precision across subsequent pixel writes and quantize only on disabling the mode or resolving. That would introduce storage the reference contract does not contain. The exact conversion applied at each write still needs proof.

## Resolve and presentation boundary

The narrow original loading chain is:

```text
82862D50 @82862E40 -> 828625A0 (loading quads)
          @82862E58 -> 823F1BD0
823F1BD0 @823F1BE8 -> 82408030
82408030 @82408068 -> engine+98 callback, 823EE820
823EE820 @823EE87C -> 82455570
          @823EE8A8 -> 824544F0
```

`823EE820` swaps globals `82D0CF90/82D0CF8C`. The previous CF90 texture is passed to `82455570` in r6; r4/r5/r7/r8/r9/r10 and f1 are zero, as are stack arguments at caller SP+5C/+64. The same texture, now at CF8C, is subsequently passed to `824544F0`. The resolve boundary selects the bound target through selector zero and separately remaps destination texture surface format 54 to 7 at `824557B4..BC`. Camera-end leaf `823EE7F0` itself is not the resolve.

The reference classifies render-target formats 2 **and** 10 as bitwise-compatible with texture formats 7 and 54. Its fast packed resolve additionally requires a single sample selection and zero exponent bias; other sampling/conversion cases use a different path. This supports preservation of already stored 10:10:10:2 codes where those conditions hold. It does not prove every argument interpretation, averaging/rounding behavior, endian/layout conversion or the final display transfer for the game's complete present path. In particular, mode disable is not a resolve or delayed alpha reduction step.

## Why no exact native blend is enabled yet

The enum does not specify whether source alpha is first converted to 10 bits, how a normalized factor is scaled internally, multiplication/accumulation widths, intermediate rounding, or final tie handling. CPU disassembly configures this hardware operation; it cannot reveal the arithmetic circuit. These are observable uncertainties even with finite, in-range inputs and no depth/stencil complications.

The analyzer generates **synthetic counterexamples**, not captured game pixels or proposed conversions. With S.rgb component 175/255, S.a=2/255, D=0 and assumed final nearest/half-up 10-bit storage conversion, selector 0 yields:

- Code **0** if the source alpha is first rounded to two bits.
- Code **5** if first rounded to ten bits.
- Code **6** if used without that prior quantization.

This alpha exceeds the quad's 1/255 rejection threshold. None of these models is selected as the hardware behavior. Likewise a half-code tie distinguishes half-up from nearest-even/truncation. Four quarter-code additions distinguish storing after each write from keeping an extended accumulator until the end. These fixtures demonstrate why “looks similar” and “same target format” do not establish parity.

A viable exact native implementation, once the arithmetic is established, can remain entirely at the engine resource/draw boundary: retain packed 32-bit resource contents and mode metadata; evaluate only the three verified equations with explicitly specified conversions; commit the original component codes after each covered update; preserve alpha test, masks, ordering and resource ownership; and resolve through the existing native resource service. A bounded per-draw destination snapshot plus native pixel/compute shader or ordered per-pixel writes could implement that contract without interpreting guest GPU commands. Overlap/order and actual API resource capabilities must be verified for whichever method the parent chooses. This is an implementation design, not a completed shader or readiness claim.

Ordinary `R10G10B10A2_UNORM` fixed-function blending is a viable **storage** implementation, but current evidence cannot certify its arithmetic as equivalent. RGBA16F/32F accumulation is also not a fidelity shortcut. Keep the unsupported gate until a hardware specification or original-hardware readback fixtures establish format-2 and format-10 results around source-alpha thresholds, RGB/alpha ties, saturation, reverse subtraction, repeated writes and toggling. Readbacks should capture stored codes before/after resolve to distinguish blend rounding from resolve conversion. No such measurement was available in this bounded task.

## Reproduction and validation

Microsoft's [D3D11.3 blending precision specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#BlendingPrecision)
permits fixed-point blending at the output format's precision or greater.
That does not independently guarantee a wider source-alpha calculation for
every RGB10A2 implementation. Native output-merger tests therefore establish
the tested adapter's behavior, not original expanded-mode equivalence. A native
implementation that requires a particular intermediate precision needs an
explicit arithmetic contract rather than relying on optional host precision.

```powershell
python -B tools/analyze_expanded_blend.py --image analysis/simpsons.pe
python -B -m unittest discover -s tests -p test_expanded_blend.py -v
```

The analyzer accepts `--reference-root` for the same pinned source snapshots at another location. JSON has no timestamps or machine-specific absolute paths. Changed/truncated images, changed/missing reference files, invalid ranges and unproved numeric inputs fail explicitly; no report indicating success is emitted on failure. Tests mutate bytes only in memory.

**17 tests passed.** They cover all sixteen format nibbles and both Boolean requests, preservation of every other descriptor bit, idempotence and reverse transitions, no-target behavior, asymmetric channel packing, every individual representable component code, 1,027 complete packed-word round trips, rejection of formats 3/12 and malformed fields, explicit rounding counterexamples, original/reference hashes, and deterministic CLI success/failure behavior. These tests certify the bounded evidence/math implementation; they do not validate Xenos hardware blending or produce an original frame.
