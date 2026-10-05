# Frontend texture CPU decode

All **15 base levels** from the exact original `frontend_global.itxd` sample have been decoded into PNGs: **5 BC1/DXT1, 9 BC2/DXT2_3, and 1 L8**, totaling **306,432 pixels**. The implementation is standard-library Python resource-addressing and compression math. It contains no GPU command processor, renderer, guest execution, or generated replacement texture content.

The 15 asset PNGs contain decoded RGBA component bytes at their original dimensions. A separate, explicitly derived checkerboard sheet makes transparent white textures visible for inspection. Source PNGs are not scaled or composited.

## Outputs and reproduction

- [Decoder](../tools/decode_itxd.py)
- [Tests](../tests/test_texture_decode.py)
- [Manifest](../build/decoded-textures/manifest.json)
- [Inspection sheet](../build/decoded-textures/inspection-sheet.png)

From `K:\SimpsonsNativeCopy`:

```powershell
python -B tools/decode_itxd.py --input build/extracted/frontend_global.itxd --source-str 'Simpsons Game, The (USA)/frontend/frontend_global.str'
python -B tools/decode_itxd.py --input build/extracted/frontend_global.itxd --source-str 'Simpsons Game, The (USA)/frontend/frontend_global.str' --verify
python -B -m unittest discover -s tests -p test_texture_decode.py -v
```

The first command writes only to an empty `build/decoded-textures`. It now already contains the completed outputs, so use the second command for a read-only replay, or use `--output-dir build/decoded-textures/new-empty-subdirectory` for a separate run. The CLI refuses to replace existing files. It decodes and validates the complete sample in memory before publication. A filesystem failure can leave a partial new directory; no successful manifest should be assumed until the command succeeds.

`--input` is explicit. `--source-str` additionally re-extracts the resource in memory through the existing read-only STR/RefPack parser and compares every byte. Without that flag, the exact known ITXD SHA256 is still required, while the manifest states that original-STR equality was not repeated. The existing inspector and all old files remain unchanged.

Only `--mip 0` is accepted. Other dictionary contents, other format/layout/endian/swizzle combinations, nonzero base address or mip minimum, and packed base levels are rejected. The output directory must resolve within `build/decoded-textures`; link/reparse paths and nonempty write destinations fail closed. Test fixtures write no files.

PNG encoding is RGBA8, filter zero, with explicitly constructed stored DEFLATE blocks. No encoder-version-dependent compression decisions, timestamps, color profiles, gamma tags, resampling, or premultiplication conversion affect the asset files. Sorted JSON and hashes make the output deterministic. Moving the workspace changes absolute provenance paths in the manifest, but not pixels or PNGs.

The final output directory contains **17 files / 5,705,143 bytes**:

- 15 original-size base PNGs: **1,228,481 bytes** total.
- `inspection-sheet.png`: **4,440,259 bytes**, SHA256 `ff38996174dc8f580855631936a70100598ba88777239b209537b9106dc0d46c`.
- `manifest.json`: **36,403 bytes**, SHA256 `2929f85de6929f4b69ca6d68d2c660d646d71069356bc3bd53e25be76e42345c`.

## Original-byte provenance

The input is **458,752 bytes**, SHA256:

```text
3e4909a83d0dc05698c43567aea90ae0838b5c21b03ecfc326d30431ad4c033f
```

It was again verified byte-for-byte against the `EARS_ITXD` resource `frontend_global.itxd` in entry 1 of original `frontend/frontend_global.str` (79,872 bytes, SHA256 `ae967348ebe5a0ed022c13cc372a3e53831a86f26804c76159ac1c8203b6388a`). The payload is at decoded-entry offset `0x100`; its offsets are not compressed-STR file offsets. The manifest records the extraction coordinates and hashes of the source, decoder, and reused inspectors.

Header and descriptor interpretation is inherited from the bounded [texture-format investigation](texture-format.md). This decoder adds verified storage traversal and component decoding; it does not reinterpret unknown wrapper words or recover C++ object boundaries.

## Base-level address and mip decisions

The six stored descriptor words establish tiled, non-stacked 2D surfaces. Original width/height pairs agree with the descriptor fields. Formats 18/19 use 4×4 compression blocks of 8/16 bytes; format 2 uses individual one-byte samples. The descriptor's pitch is in **texels**, so compressed texture addressing divides pitch by four to obtain block pitch. Treating logical image width as pitch would be incorrect for the small textures.

For example, `square` is 16×16 DXT1 but has a 128-texel / 32-block pitch. Its base allocation is 8,192 bytes, but only 128 bytes of compressed blocks are read. The greatest addressed byte end is `0x240`. Similarly, a 32×32 BC2 base reads 1,024 logical bytes across an addressed extent ending at `0x1740`, within a 16,384-byte allocation. Padding never becomes output pixels.

The address implementation follows the pure 2D block-address equation in the read-only [RexGlue texture utility](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/util.cpp), lines 424–436. A separate row/column decomposition in [conversion.cpp](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/conversion.cpp), lines 88–134, agrees. In terms of block coordinates `x,y`, block pitch `P`, and `L=log2(bytes_per_block)`:

```text
macro = ((x >> 5) + (y >> 5) * (P >> 5)) << (L + 7)
micro = ((x & 7) + ((y & 14) << 2)) << L
mixed = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4)
address = ((mixed & ~511) << 3) + ((y & 16) << 7)
        + ((mixed & 448) << 2)
        + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)
        + (mixed & 63)
```

The decoder validates alignment, uniqueness, and bounds of every addressed block. Its base allocation prefix follows descriptor pitch and height rounded to 32 block rows. For the exact sample all addresses fit these prefixes, including the one-byte L8 layout whose tiles cannot generally be treated as simple independent contiguous 32×32 byte squares.

The [reference layout documentation](K:/Simpsons/RexGlueCurrent/include/rex/graphics/pipeline/texture/util.h), lines 47–88, distinguishes base storage from mip storage. A packed tail can affect the base only when a base dimension is 16 texels or smaller. Here:

- The 16×16 `square` has packed mips **disabled**.
- `targeting_firearm` is 64×64 with packed mips enabled; the first packed level according to the reference is 2. Base storage uses origin `(0,0)`, not the packed-tail origin.
- The four 256×256 DXT1 textures have packed mips enabled; the first packed level is 4. Their base storage also uses origin `(0,0)`.

On those five textures the descriptor's mip-address byte field equals the computed base prefix: `0x4000` for `targeting_firearm`, `0x8000` for the four larger textures. This is a cross-check at the base boundary. It does **not** establish runtime relocation behavior, authored mip count, or individual non-base mip spans. No non-base mip is read or generated.

The 15 base prefixes total **290,816 bytes**; the decoder reads **175,232 logical base bytes** from them. The rest of the resource allocations contain padding and/or other level data outside this scope.

## Endian, channel and BC decoding evidence

Original compressed descriptors specify `8in16`; L8 specifies no swap. [xenos.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h), lines 1039–1054, defines adjacent-byte exchange for that endian mode. This permutation applies to every byte pair in a compressed block, including endpoint words, color selectors, and explicit alpha. The resulting BC fields are read little-endian. Entire DWORD reversal would be wrong.

The original channel selectors are `XYZW` for BC1/BC2 and `XXX1` for L8. The latter produces `(L,L,L,255)`, consistent with the read-only `D3DFMT_L8` reference from the prior investigation. No inferred palette, color remapping or gamma conversion is applied.

BC1 stores RGB565 endpoints and row-major 2-bit selectors. The endpoint comparison chooses four-color versus three-color/transparent-black decoding. For BC2, the first eight bytes encode 16 explicit alpha nibbles; each nibble expands to `17*n`. BC2 always uses four-color RGB regardless of endpoint ordering. These structural rules are documented in Microsoft's [BC1 description](https://learn.microsoft.com/en-us/windows/win32/direct3d9/opaque-and-1-bit-alpha-textures) and [explicit-alpha description](https://learn.microsoft.com/en-us/windows/win32/direct3d9/textures-with-alpha-channels).

The deterministic **RGB8 arithmetic** is pinned to the local RexGlue conversion references, whose headers include readable disassembly:

- [DXT1 conversion](K:/Simpsons/RexGlueCurrent/src/graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt1_rgba8_cs.h), lines 160–174 and 227–280.
- [DXT2/3 conversion](K:/Simpsons/RexGlueCurrent/src/graphics/shaders/bytecode/d3d12_5_1/texture_load_dxt3_rgba8_cs.h), lines 143–184.

Only their arithmetic was studied; no shader interpreter or GPU code is shipped. Five-bit components expand as `(v<<3)|(v>>2)`, six-bit components as `(v<<2)|(v>>4)`. Interpolants use truncated integer `(2*A+B)/3`, `(A+2*B)/3`, or `(A+B)/2`. The BC2 reference expands alpha independently and does not transform RGB by alpha.

**Rounding limit:** Microsoft's BC1 example adds 1 before dividing by three; the inspected RexGlue conversion uses unsigned division without that bias. The CPU decoder intentionally matches the latter and tests a case where they differ. This establishes a reproducible reference-backed RGBA8 reconstruction, **not bit-exact original Xbox GPU filtering or framebuffer rounding**. No hardware oracle was run.

**Alpha-association limit:** Microsoft states that DXT2 and DXT3 have identical encoded data/interpolation but differ in assumed premultiplication, which generally cannot be inferred from the bytes. The original descriptor combines them as DXT2_3. Accordingly, decoded RGB is preserved even where alpha is zero; no premultiply/unpremultiply is attempted. PNG pixel values preserve the stored components, while correct game blending remains outside this result. The checkerboard preview uses a conventional straight-alpha display assumption only.

All five original BC1 images decode as opaque: no block uses a transparent selector in three-color mode. This is an observed property of the sample, not a reason to remove transparent BC1 support from the block decoder tests.

## Visual inspection

Every original-size PNG was opened, followed by the enlarged checkerboard sheet. The sheet uses nearest-neighbor enlargement only, in five columns and three rows, following dictionary order. Its checkerboard and margins are synthetic inspection backgrounds, explicitly separated from the fifteen original asset PNGs in the manifest.

Observed content, in that order:

```text
00 square                 Near-white opaque square.
01 hud_target_center      Four white cardinal crosshair bars.
02 target_railshooter      White circular reticle with central plus.
03 targeting_nontarget     Soft-edged white X.
04 hittarget              Concentric outlined rings.
05 healthbar              White diagonal bar, lower-left to upper-right.
06 fakeshdw               Soft white luminance spot fading to black.
07 targeting_f2f          Asymmetric outlined symbol with vertical bars.
08 hud_targetlock         Soft translucent circular ring.
09 targeting_firearm      Circular reticle with four extensions.
10 hud_targetlock02       Gray tapered downward marker.
11 projtex_hog            Magenta radial ornament on white.
12 projtex_sax            Soft red spot fading toward a pale border.
13 texture_not_specified  Black "Texture Not Specified" text on magenta.
14 texture_not_found      Black "Texture Not Found" text on magenta.
```

The diagonal bar, asymmetric symbol, and readable original text are useful orientation checks. No tile-boundary scrambling was visible. Visual coherence supports the byte/mathematical checks; it alone would not prove the layout of symmetric rings or soft spots. The two text images are authored original fallback textures, not placeholders created by this task.

## Tests and archive checks

**22 tests pass.** Coverage includes:

- **93,120 coordinates** checked against an independently expressed physical-address-to-coordinate inverse, across 1/8/16-byte blocks, pitches 32/64/96/128 and 97 block rows. The inverse relationships are from the [Noesis Xbox360 untile source](https://github.com/leeao/Noesis-Plugins/blob/master/Textures/inc_xbox360_untile.py); this is an independent direction of calculation, not an independent hardware oracle.
- Six asymmetric physical-storage fixtures built using that inverse, rather than the production forward mapper: block rectangles 67×35, 37×65, 65×37, 39×67, 67×35 and 35×67. These cross micro/macro boundaries, use pitch greater than width, poison padding, and distinguish X/Y order.
- Explicit endian-lane checks, non-square macro-pitch vectors, asymmetric BC selectors, four- and three-color BC1, equal endpoints, BC2 ascending endpoints, full alpha ramps, preservation of RGB under zero alpha, and the chosen rounding/endpoint expansion.
- A 9×5 BC fixture spanning six distinct color blocks and cropped partial edges; L8 channel selection; malformed sizes, unsupported formats and unverified resource layouts.
- All 15 original linear-base hashes, byte bounds, RGBA lengths, original STR equality, and exact-sample hash rejection.
- Independently parsed PNG chunks/CRCs, zlib decompression, filter-zero scanlines, and exact RGBA comparison for every original PNG. Multi-block DEFLATE and asymmetric scanlines are covered separately.
- Deterministic generation, manifest/PNG/RGBA hash consistency, derived preview separation, output-scope guards, and rejection of nonzero mip requests before publication.

A fresh CLI `--verify` run matched all **17 saved artifacts byte-for-byte**. All **10 local reference snapshots** listed in the manifest were SHA256-checked. Original source/sample hashes and the parent-owned files read in this scope were checked unchanged after decoding. No prior report or inspector was updated.

The bounded result is the decoded level-zero assets, reproducible component decoder, tests and evidence. Non-base mips, broader format support, alpha-association recovery and a renderer are not implemented.
