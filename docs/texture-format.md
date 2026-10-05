# Frontend ITXD: verified sample profile

The inspector parses all **15 named textures** in the original `frontend_global.itxd` resource. It verifies linked metadata, repeated dimensions, stored texture descriptors, and an exact partition of the payload bytes. It reads stored resource data only. It contains no renderer, image generator, pixel decoder, guest execution, or GPU command-stream implementation.

This is a bounded profile for one sample, not a universal ITXD specification. The report distinguishes byte observations, reference-backed descriptor interpretation, and hypotheses. It preserves complete metadata windows, opaque words, raw descriptor words, offsets, and SHA256 hashes.

## Original provenance

The inspected file is [frontend_global.itxd](../build/extracted/frontend_global.itxd), **458,752 bytes (`0x70000`)**, SHA256:

```text
3e4909a83d0dc05698c43567aea90ae0838b5c21b03ecfc326d30431ad4c033f
```

It was independently compared byte-for-byte with `frontend_global.itxd`, type `EARS_ITXD`, in entry **1** of the original [frontend_global.str](<../Simpsons Game, The (USA)/frontend/frontend_global.str>). That STR is **79,872 bytes**, SHA256:

```text
ae967348ebe5a0ed022c13cc372a3e53831a86f26804c76159ac1c8203b6388a
```

The entry starts at STR offset `0x1000`, occupies 75,776 stored bytes, and decompresses to 469,488 bytes. RefPack consumes 75,716 stored bytes. The ITXD payload starts at decoded-entry offset `0x100` and occupies 458,752 bytes. These coordinates belong to different address spaces; an ITXD offset must not be added directly to the compressed STR offset.

The existing STR/RefPack parser is reused read-only for this comparison. The report records its SHA256 as well as the new inspector's SHA256. No old tools, asset report, extracted resource, original files, or runtime/generator files were modified by this scope.

## Reproduce

From `K:\SimpsonsNativeCopy`, with Python 3.10 or later and only the standard library:

```powershell
python -B tools/inspect_itxd.py --input build/extracted/frontend_global.itxd --source-str 'Simpsons Game, The (USA)/frontend/frontend_global.str' --output analysis/textures.json
python -B -m unittest discover -s tests -p test_itxd.py -v
```

`--output` creates a **new** JSON file and refuses all existing destinations. The checked-in/generated report already exists after this task; omit `--output` to reproduce it on stdout without changing it. Both output modes use canonical sorted JSON, UTF-8 and LF, with no timestamps. Input and source paths are normalized absolute paths, so moving the workspace changes provenance strings. `--source-str` is optional; without it the report explicitly says that original-resource equality was not checked during that run.

Output aliases, links/reparse points, and destinations in the original asset tree or the two read-only reference trees are rejected. The CLI emits an error on stderr and returns 2 on invalid input or an output error. It does not create output directories. The 16 MiB input/source limit is an inspector budget, not a discovered file-format limit.

## Byte layout and boundary evidence

All decoded words below are **big-endian**. All offsets are relative to the extracted ITXD payload unless explicitly stated otherwise.

The first 24 bytes are:

```text
757a0003 00000001 0000ea2f 00580020 06000000 00000000
```

Their meanings are not established. The parser uses these exact bytes as an observed-profile signature; it does not assign a version, platform, count, or plugin schema to them.

The link pair at `0x18/0x1c` contains `0x30/0xe30`. Traversal from `0x30` follows forward/back links through **15 nodes**, spaced `0x100` apart. The first backlink and last forward link point to sentinel `0x18`. The count is derived by traversal; the byte `06` at `0x10` is **not** treated as a texture count.

The report's `record_offset` anchors at the **forward/back link node**, at `0x30 + index*0x100`. Its 256-byte inspection windows are a reproducible coordinate convention, not proven C++ object extents. The reported 48-byte prefix likewise means “bytes before the first link,” not a recovered dictionary class size.

For an anchor `r`, the following relationships hold in all 15 windows:

- `r+0x00`, `r+0x04`: forward and backward links.
- `r+0x08..0x47`: nonempty ASCII name with a NUL terminator and zero padding, within a 64-byte inspected span. This does not establish the engine's maximum legal texture-name length or exclude multiple native string fields inside that span.
- `r+0x48`: candidate sampler word; three observed values are described below.
- `r+0x4c`: always `1`; `r+0x50/+0x54`: varying opaque words, preserved without calling them hashes or IDs.
- `r+0x70`: value `r+0x70`, a self-reference into the nested metadata.
- `r+0x7c/+0x80`: width/height. `r+0x98/+0x9c` repeat them, and both pairs equal the embedded descriptor dimensions.
- `r+0x84`: `16` for all 14 compressed textures, `8` for `fakeshdw`. Its native meaning is unproven; it is **not** the compressed storage bits per texel.
- `r+0x90`: raw word `04 00 00 xx`, with `xx` in `00,02,03,81,82,83`. The low-byte `0x80` bit agrees with nonzero descriptor mip maximum in 15/15 records. Other raster/type flag meanings are unproven.
- `r+0xa0`: opaque; `hud_target_center` contains `0x01001000`, the other 14 contain zero.
- `r+0xa4`: value `r+0xc4`. The nested resource begins with observed words `3,1,0,0,0,ffff0000,ffff0000`; the six descriptor words begin 28 bytes later, at `r+0xe0`.
- `r+0xac..0xaf`: retained raw bytes. Byte `+0xaf` equals descriptor mip maximum in 15/15. Byte `+0xae` is 1 on compressed entries and 0 on `fakeshdw`; that is a correlation, not a recovered enum. Meanings of `+0xac/+0xad` are unproven.
- `r+0xb4/+0xb8`: byte extent size and absolute payload offset. All 15 pairs are 4 KiB aligned, in bounds, and form an exact contiguous partition in list order.
- `r+0xbc`: scalar format-like word, with exact values discussed below.
- `r+0xf8`: equals the next node's `r+0x70` target, or zero on the last window. Prefix word `0x28` similarly equals `0xa0`.

The last point **does not prove a second linked raster list**. One plausible native object layout starts eight bytes before the link anchor, making `r+0xf8` the next object's leading pointer. The parser validates numeric relationships without committing to that hypothesis.

Inspected metadata windows end at `0xf30`; the next 208 bytes are zero. The last window's final eight bytes are also zero, so `0xf30` is not claimed as the native object's exact end. Payloads cover **`[0x1000, 0x70000)`**, totaling **454,656 bytes**. The remaining 4,096 bytes are prefix/metadata/alignment, rather than texture pixels.

## Stored descriptors and formats

The reference bitfield definitions match the original six BE32 words at `r+0xe0`. `w0..w5` below name those words, not commands:

```text
w0: type = bits 0..1; pitch_texels = bits 22..30 * 32; tiled = bit 31
w1: format = bits 0..5; endian = bits 6..7; base-address bytes = w1 & fffff000
w2: width = bits 0..12 + 1; height = bits 13..25 + 1
w3: channel selectors = four 3-bit values from bit 1
    mag/min/mip filters = bits 19..20 / 21..22 / 23..24
w4: mip minimum = bits 2..5; mip maximum = bits 6..9
w5: dimension = bits 9..10; packed mips = bit 11
    mip-address bytes = w5 & fffff000
```

All 15 descriptors are type 2, non-stacked 2D, and tiled. Width and height have **three agreeing encodings** per texture. The descriptor format inventory is:

- **5 DXT1**, format ID 18: `square`, `projtex_hog`, `projtex_sax`, `texture_not_specified`, `texture_not_found`. Scalar word `0x1a200152`; 4×4 blocks of 8 bytes (4 bits per texel), endian `8in16`, channel selection `XYZW`.
- **9 DXT2/3**, format ID 19: the remaining targeting/HUD entries listed below. Scalar word `0x1a200153`; 4×4 blocks of 16 bytes (8 bits per texel), endian `8in16`, channel selection `XYZW`. This format ID does not distinguish DXT2 alpha premultiplication from DXT3.
- **1 8-bit texture**, format ID 2: `fakeshdw`, 64×64. Scalar `0x28000102` is explicitly named **D3DFMT_L8** by the read-only reference. Its descriptor uses no endian swap and `XXX1` channel selection, corroborated by the reference's R8_UNORM/RRR1 mapping.

For all 15 scalar format words `F`, `(F&63)`, `((F>>6)&3)` and `((F>>18)&4095)` agree with descriptor format, endian and channel-swizzle fields. Proposed tiled/sign/number-format shifts 8/9/17 also agree, but those fields do not vary here. This is evidence for packing relationships, not a recovered full D3DFORMAT definition.

The complete bounded inventory is below. “Mip max” is a stored descriptor limit, **not an authenticated authored-level count**. Offsets and sizes are hexadecimal; dimensions and pitches are decimal.

```text
name                    dimensions  format   pitch   mip max   offset   size
square                  16x16       DXT1       128       0      01000   02000
hud_target_center       32x32       DXT2/3     128       0      03000   04000
target_railshooter       32x32       DXT2/3     128       0      07000   04000
targeting_nontarget      32x32       DXT2/3     128       0      0b000   04000
hittarget               64x64       DXT2/3     128       0      0f000   04000
healthbar               64x64       DXT2/3     128       0      13000   04000
fakeshdw                64x64       L8          64       0      17000   01000
targeting_f2f           64x64       DXT2/3     128       0      18000   04000
hud_targetlock          64x64       DXT2/3     128       0      1c000   04000
targeting_firearm       64x64       DXT2/3     128       2      20000   0c000
hud_targetlock02        128x128     DXT2/3     128       0      2c000   04000
projtex_hog             256x256     DXT1       256       4      30000   10000
projtex_sax             256x256     DXT1       256       4      40000   10000
texture_not_specified   256x256     DXT1       256       4      50000   10000
texture_not_found       256x256     DXT1       256       4      60000   10000
```

As a concrete byte example, `square` at link anchor `0x30` has descriptor words at absolute `0x110`:

```text
81000002 00000052 0001e00f 00000d10 00000000 00000200
```

Its logical base level needs only 128 bytes of DXT1 blocks, while its stored extent is 8,192 bytes. The inspector reports both numbers and does not mistake allocation padding for densely packed image rows. It reports pitch and tiling flags but computes no spatial untile address or individual mip byte span.

## Mip and sampler limits

`targeting_firearm` has descriptor minimum/maximum **0/2**, packed mips enabled, and mip-address field `0x4000`. The four 256×256 DXT1 textures have **0/4**, packed mips enabled, and mip-address field `0x8000`. The other ten have **0/0**, packed mips disabled, and zero mip-address fields. All base-address fields are zero. These are not established absolute ITXD file addresses. Allocation-relative relocation is a hypothesis requiring original loader evidence.

The wrapper word at `r+0x48` is `0x3302` on two textures, `0x1102` on eight, and `0x1106` on five. The low byte is 6 precisely where the descriptor has a nonzero mip maximum. A RenderWare filter byte followed by U/V addressing nibbles is plausible, but no matching local enum declarations were located. The JSON therefore uses `sampler_candidate`, emits bit slices, and assigns no filter/address names to this word.

The **stored descriptor** fields themselves decode through the reference to repeat addressing on all axes and point mag/min/mip filters, with disabled anisotropy and zero LOD bias. These fields differ from the wrapper candidates and must not be treated as the final engine sampler state without checking runtime setup.

Spatial swizzling/tiling, endian conversion on pixel bytes, block decompression, DXT1 alpha usage, DXT2 versus DXT3 premultiplication, authored mip content/count, packed-tail placement, and final runtime bindings remain unverified. No image or visual fidelity claim follows from this inspector.

## Validation and reference snapshots

The final suite has **27 passing tests**. Golden checks cover every original name, dimensions, format, pitch, mip maximum and payload range, plus original STR equality. In-memory corruptions reject truncated inputs, bad profile words, cycles, bad backlinks, premature list termination, conflicting nested pointers, invalid names, dimension disagreements, unsupported formats/dimensions, invalid pitch/swizzle/mip fields, sampler correlations, overlapping/out-of-bounds extents, and nonzero alignment padding. CLI tests verify deterministic JSON, canonical subprocess LF bytes, and source-overwrite/source-tree rejection without test writes.

Opaque-word and payload mutations are deliberately tested separately: structurally admissible changes remain inspectable and change their hashes; they are not falsely claimed to have valid pixels or to match the original. Supplying the original STR rejects any byte mismatch. Unsupported observed-profile checks may reject otherwise valid resources from other dictionaries; unknown variants are not silently interpreted.

The archived report is [textures.json](../analysis/textures.json). It contains the source/producer hashes, per-window and per-payload hashes, full descriptor words, raw metadata, and explicit hypotheses/limitations. A fresh CLI stdout replay was compared byte-for-byte with the archive. All five local reference snapshots were hash-checked. No reference implementation is imported at runtime by the core parser.

The final archive is **57,795 bytes**, SHA256 `4466c1b4b9b1b158c0ea7343c5a1944127d6446f091f00af0f3aaf7f6bda974f`. The CLI's refusal to overwrite that existing report was also exercised; its bytes remained unchanged.

References used for interpretation, with exact SHA256 snapshots:

- [RexGlue xenos.h](K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h), lines 96–137, 192–197, 444–490, 1008–1017, 1093–1098 and 1167–1267: dimensions, descriptor bitfields and enums. SHA256 `7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227`.
- [RexGlue info_formats.cpp](K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/texture/info_formats.cpp), lines 20–43: block geometry and bits per texel. SHA256 `36438c5c337d41c62de6c50b4b57971b2d583eaa055f8eff261411de82c31e3c`.
- [DarkRecomp texture_mip_layout.h](K:/DarkRecomp/renderer/engine/texture_mip_layout.h), lines 9–27 and 41–89: corroborates descriptor placement at resource+28 and warns about packed/tiled addressing and sampler-vs-layout distinctions. Its implementation is for another game and was not transplanted. SHA256 `7574fec962b9a299b5aab009b83e5f6da8759a904a5aefe7dcd6bc7149db95f2`.
- [UnleashedRecomp video.h](K:/DarkRecomp/refs/UnleashedRecomp/UnleashedRecomp/gpu/video.h), lines 119–135: exact L8 scalar constant. SHA256 `e0ae8a86ba7fef474732ec889d5027c0654baa5592d3e167efc3d32e92d7f970`.
- [UnleashedRecomp video.cpp](K:/DarkRecomp/refs/UnleashedRecomp/UnleashedRecomp/gpu/video.cpp), lines 3086–3088 and 3127–3129: L8 format and channel mapping corroboration. SHA256 `82616eed7512bec74d06155ef26712dcf35edae4b4795bee55cfd82601b9b420`.

This scope ends at the stable metadata inspector, tests and provenance report. It does not establish or implement the remaining texture decode/rendering milestones.
