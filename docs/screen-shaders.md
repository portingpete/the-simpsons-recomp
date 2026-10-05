# Screen_Xenon: original shader instruction proof

This shader-only supplement records the four compiled materials selected by the quad helper at 0x82756480. It supplements screen-material.md without changing the frozen boundary report or analyzer. No runtime shader, renderer, device initialization, or GPU command handling is implemented here.

**Result:** both vertex shaders explicitly construct position Z=0 and W=1. PSFlat exports the screen_color constant; PSTextured multiplies sampled RGBA by screen_color component-wise, including alpha. These are decoded original instructions, not deductions from the shader names.

## Source and record extents

Source: analysis/simpsons.pe, SHA256:

    6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0

The flat file offset is VA minus 0x82000000. All instruction words below are read big-endian. Record offsets are relative to the named record. Ranges are half-open.

The record header words at +4 and +8 contain the instruction-window offset and byte length. Each window includes a final 12-byte triplet beginning 4E4A0000 that the decoded control flow does not execute. VSFlat and PSTextured are followed by four bytes of alignment padding before the next listed record; padding is excluded from these extents and hashes.

- PSFlat: record 0x821524C8; window +0xFC, length 0x24; extent [0x821524C8,0x821525E8).
  SHA256: bd254ae4adf48d461ab2ddc1958156ca0dafa3d2a1bc4ed67d1a9a3238ae1709
- VSFlat: record 0x821525E8; window +0xD4, length 0x48; extent [0x821525E8,0x82152704).
  SHA256: 58a5e69479abbfb250270590a3c2254720aa1dc04b5a60858c6fc3f05376cccc
- PSTextured: record 0x82152708; window +0x138, length 0x3C; extent [0x82152708,0x8215287C).
  SHA256: 3257ec7a18a3daa21fe5c570da1d9cdac2941f33c1e7f7cf4df72248b4f83780
- VSTextured: record 0x82152880; window +0xE4, length 0x60; extent [0x82152880,0x821529C4).
  SHA256: 475f31982d13ae15928e0498686bcd9bdb6da902e5c5479228f4a390efe678a9

All four complete spans were compared byte-for-byte with K:/Simpsons/logs/simpsons-loaded-xex-title-menu-ida-20260604.bin and matched. This comparison is of embedded records, not a capture of runtime-patched shader objects.

Each record contains its Screen_Xenon_*.updb path, a shader-model string, and compiler string 2.0.5632.0. No original HLSL source text was found in these records.

## VSFlat: position (x,y,0,1)

Executed fetch/ALU range: [0x821526E0,0x821526F8).

    record+0F8  VA 821526E0  code+24  05F80000 00000B08 00000000
    record+104  VA 821526EC  code+30  C80F803E 00000000 C2000000

The fetch destination is temporary r0. Its destination swizzle is 0xB08, whose four 3-bit selectors are [0,1,4,5]: fetched X, fetched Y, constant 0, constant 1.

The ALU exports to register 62 (VS position), vector mask 0xF. Vector opcode 2 is MAX; both sources are temporary r0 with identity swizzles. Consequently the exact operation is:

    r0 = (fetched.x, fetched.y, 0, 1)
    position.xyzw = MAX(r0.xyzw, r0.xyzw)

In particular, the original shader explicitly writes Z=0 and W=1. MAX of identical inputs is the compiled identity operation here; no matrix, reciprocal, or depth constant is involved.

## VSTextured: position (x,y,0,1), interpolator XY

Executed fetch/ALU range: [0x82152988,0x821529B8).

    record+108  VA 82152988  code+24  05F81000 00000B08 00000000
    record+114  VA 82152994  code+30  05F80000 00000FC8 00000000
    record+120  VA 821529A0  code+3C  C80F803E 00000000 C2010100
    record+12C  VA 821529AC  code+48  C8038000 00B0B000 C2000000

The first fetch writes temporary r1 with destination selectors [X,Y,0,1]. The second fetch writes r0 with 0xFC8 selectors [X,Y,keep,keep].

The first ALU exports MAX(r1,r1), identity-swizzled, to position register 62 with mask 0xF. The second exports MAX of two copies of r0 to interpolator register 0 with mask 0x3. Its source swizzle 0xB0 is component-relative and resolves to [X,Y,Y,Y]; only XY is exported.

    r1 = (first_fetch.x, first_fetch.y, 0, 1)
    r0.xy = second_fetch.xy
    position.xyzw = MAX(r1.xyzw, r1.xyzw)
    interpolator0.xy = MAX(r0.xy, r0.xy)

Interpolator Z/W are not written by that export. They are not used by this material's 2D texture instruction.

## PSFlat: screen_color RGBA

Executed ALU range: [0x821525D0,0x821525DC).

    record+108  VA 821525D0  code+0C  C80F8000 00000000 02000000

The ALU exports to pixel color register 0 with vector mask 0xF. Vector opcode 2 is MAX. Both sources select constant register 0, with identity swizzles:

    color0.rgba = MAX(c0.rgba, c0.rgba)

Reflection entry at record +0x94 names screen_color and encodes register set 2, register index 0, count 1. Thus the identity operation exports the screen_color float4, including its alpha.

## PSTextured: sampled RGBA times screen_color RGBA

Executed texture-fetch/ALU range: [0x82152858,0x82152870).

    record+150  VA 82152858  code+18  10080001 1F1FF688 00004000
    record+15C  VA 82152864  code+24  C80F8000 00000000 81000000

Texture instruction fields:

- Opcode 1 (texture fetch), source r0, destination r0, texture fetch constant 0.
- Dimension 1 (2D). Source swizzle is 4, selecting X/Y for the two coordinates. Destination swizzle 0x688 selects X/Y/Z/W unchanged.
- Unnormalized-coordinate flag 0: normalized texture coordinates.
- Computed LOD 1; register LOD 0; register gradients 0; instruction LOD bias 0; X/Y/Z instruction offsets 0.
- Min/mag/mip and volume min/mag selectors are 3; anisotropy selector is 7. These select fetch-constant state, so the instruction does not fix those sampler settings itself.
- Predication 0; fetch-valid-only 1; sample-location field 0.

The ALU has vector opcode 1 (component-wise MUL), source1 temporary r0, source2 constant c0, and identity swizzles. It exports all four lanes to pixel color register 0:

    sampled = texture_fetch_2D(texture_constant0, interpolator0.xy, inherited_sampler)
    color0.rgba = sampled.rgba * c0.rgba

Reflection entry +0x98 names screen_color (register set 2, index 0, count 1). Entry +0xAC names screen_texture (register set 3, index 0, count 1).

There is no separate shader premultiplication, alpha replacement, alpha test, depth export, or clamp instruction. Post-shader alpha testing/blending and texture descriptor conversion remain the separate material-state contract in screen-material.md.

## Control flow: completeness of the executed instruction list

The following original triplets contain paired 48-bit control-flow instructions. Instruction addresses below index 12-byte slots from the start of the record's instruction window.

    PSFlat:
      +0FC  00000000 1001C400 22000000
      ALLOC colors; EXEC_END address=1 count=1 sequence=0

    VSFlat:
      +0D4  10011003 00001200 C2000000
      +0E0  00001004 00001200 C4000000
      +0EC  00000003 00002200 00000000
      EXEC address=3 count=1 sequence=1; ALLOC position
      EXEC address=4 count=1 sequence=0; ALLOC interpolators
      EXEC_END address=3 count=0; NOP

    PSTextured:
      +138  00011002 00001200 C4000000
      +144  00001003 00002200 00000000
      EXEC address=2 count=1 sequence=1; ALLOC colors
      EXEC_END address=3 count=1 sequence=0; NOP

    VSTextured:
      +0E4  30052003 00001200 C2000000
      +0F0  00001005 00001200 C4000000
      +0FC  00001006 00002200 00000000
      EXEC address=3 count=2 sequence=5; ALLOC position
      EXEC address=5 count=1 sequence=0; ALLOC interpolators
      EXEC_END address=6 count=1 sequence=0; NOP

Each sequence uses two bits per instruction: bit 0 selects fetch versus ALU, bit 1 serialization. There are no conditional branches or additional reachable ALUs in these schedules. The final triplets beginning 4E4A0000 lie beyond every executed range.

## Matching reference definitions and extraction rules

The reference used only for declarative instruction layouts and operation definitions is K:/Simpsons/RexGlueCurrent/include/rex/graphics/format/ucode.h, SHA256 `e820997c448f0fe4734738cac1526b841dde947e2dac568f56c022d1a55a59fb`:

- Lines 218-255: EXEC fields. Lines 515-522: paired control-flow unpacking. For original words a,b,c, the two instructions are (a,b&0xFFFF) and ((b>>16)|(c<<16),c>>16), truncating each word to 32 bits.
- Lines 671-686: fetch destination selectors, including 4=constant 0 and 5=constant 1. Selector for lane j is (word1>>(3*j))&7.
- Lines 735-765: vertex-fetch fields. Lines 821-857: texture-fetch fields.
- Lines 1336-1359: vector opcode 1=MUL, 2=MAX. Line 1310: scalar opcode 50=retain previous.
- Lines 1767-1816: export register 62=VS position, 0=VS interpolator0 or PS color0 according to shader stage.
- Lines 1967-1970: ALU source swizzles are relative. Absolute lane = ((swizzle>>(2*lane))+lane)&3.
- Lines 2005-2057: ALU layout. Word0 bits 0-5 select vector destination, bit15 export, bits16-19 vector mask, bits20-23 scalar mask, bit24 vector clamp. Word2 bits24-28 select vector opcode; bits31/30 select temporary versus constant for sources1/2; bits16-23 and8-15 give their register indices. Word1 bits16-23 and8-15 give their swizzles.

K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h lines 130-146 define filter selector 3 and anisotropy selector 7 as using fetch constants. Lines 165-169 define dimension 1 as 2D.

All five executed ALUs have scalar opcode 50, scalar write mask zero, no predication, no negate or absolute modifiers, no relative addressing, and no vector/scalar clamp. No ALU source or destination selector remains unresolved.

## Limits of this proof

The embedded vertex-fetch instructions have placeholder format, stride and offset fields and fetch-constant index 95. This document does not reconstruct runtime declaration patching or assert that the embedded fetch words are already runnable unchanged. It proves the original shader's explicit component selectors and arithmetic; runtime-patched shader objects were not captured here.

The quad's separate declarations are at 0x82151748 (flat) and 0x82151724 (textured); the textured declaration has element offsets 0 and 8. Their runtime relocation into these fetch instructions remains separate from the proven Z/W constants and ALU operations.

Shader arithmetic does not establish texture descriptor decoding, sampler state, interpolation/rasterization settings, render-target conversion, or exact host-versus-Xenos exceptional floating-point behavior. Those limits do not leave a choice of Z/W or color arithmetic to invent.

## Reproducible byte verifier and tests

Run from `K:/SimpsonsNativeCopy`:

```powershell
python -B tools/analyze_screen_shaders.py --image analysis/simpsons.pe
python -B -m unittest discover -s tests -p test_screen_shaders.py -v
```

The new stdlib analyzer writes deterministic JSON to **stdout only**. It pins the whole original image size/hash and all four record sizes/hashes; decodes the paired control-flow schedules; bounds-checks every executed slot; accounts for all non-trailer code slots; and extracts the nine fetch/ALU instructions' original words and fields. It accepts only these four original records. It rejects altered headers, unknown profiles/bytes, malformed ranges, unknown control flow, execution into the trailer, and unsupported ALU/fetch forms. No reference backend or command stream machinery is shipped.

**16 tests passed**, including real original records, asymmetric fetch/ALU swizzles, Z/W selectors and export masks, full RGBA constant/multiply behavior, normalized coordinates and inherited filter selectors, two byte-identical JSON runs, and in-memory mutation rejection. The tests write no files. Independent checks also compared all four embedded record spans with the read-only loaded-title dump. This is static original-byte verification, not a native shader compile or rendered pixel comparison. The parent owns native shader creation and offscreen validation.

The frozen boundary analyzer and JSON retain their prior hashes. This supplement supplies the completed shader arithmetic proof without changing the driver, runtime, generator, or loading-art files.
