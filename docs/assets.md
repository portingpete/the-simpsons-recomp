# Original asset inspection

Scope: read-only investigation of `K:\SimpsonsNativeCopy\Simpsons Game, The (USA)` for a faithful native port, followed by the requested StreamTOC parser and named-resource extraction milestone. Owned source/document outputs are `tools/inspect_assets.py`, `tools/extract_resource.py`, and this document. Derived outputs are `analysis/assets.json` and the explicitly selected resource files under `build/extracted`. The separately supplied `analysis/simpsons.pe` and `analysis/executable.json` are inputs, not generated or modified here. No runtime, generator, viewer, or gameplay implementation is included.

## Reproduce

From `K:\SimpsonsNativeCopy`, with Python 3.10 or newer and only its standard library:

```powershell
python -B tools/inspect_assets.py --root 'K:\SimpsonsNativeCopy\Simpsons Game, The (USA)' --decode-str --image analysis/simpsons.pe --image-layout flat --output analysis/assets.json
```

Omit `--decode-str` for inventory, hashes, SToc tables/payload headers, and media framing without resource decompression. With `--decode-str`, StreamTOC payloads are also parsed and their names/block metadata checked against the inventoried original `.str` files. Omit `--image` when the independently derived PE is unavailable. `--output -` writes JSON to stdout; it is also the default. `--image-layout raw` uses ordinary PE section raw offsets instead of RVA offsets. The supplied image is **flat**, so `flat` is the correct setting here. The image scan covers the backed `.rdata` section; it does not load or validate the whole executable.

Every original file receives a relative path, byte count, SHA-256, 32-byte header preview, and explicit inspection scope. Records and JSON keys are sorted, strings preserve their bytes through ASCII/UTF-8 JSON round trips, and there are no timestamps, elapsed times, or absolute machine paths in the report. `summary.manifest_sha256` hashes, in path order, UTF-8 records of `path + NUL + decimal_size + NUL + sha256 + LF`. Identical inputs/options produce identical JSON. Identical stored SToc payloads share an in-memory inspection cache; no cache files are written.

Exit status is 0 for accepted inspected profiles, 1 when any file/profile is rejected, and 2 for invocation, filesystem, or inventory failures. This corpus has a known movie trailer rejection described below; exit 1 still writes its report. Rejected files are retained with their hashes and error, rather than omitted. Unknown extensions and unparsed resource bodies remain explicitly opaque. A successful profile check does not certify an opaque codec bitstream or prove runtime behavior.

Inputs use binary read-only handles and read-only memory maps. Directory traversal rejects links, junctions, and special files. The report destination cannot be inside the original tree, overwrite the image or inspector, or be an existing linked/non-regular file. The script writes only its selected report; `-B` also prevents import bytecode files during development checks.

## Inventory measured from original files

**7,968 files, 4,385,078,597 bytes** (about 4.084 GiB), including the executable and system update:

- `.str`: 490 files, 1,065,078,784 bytes.
- `.snu`: 7,413 files, 977,556,560 bytes.
- `.mus`: 17 files, 899,970,176 bytes.
- `.vp6`: 42 files, 1,420,889,368 bytes.
- `.lua`: 3 files, 87,824 bytes.
- `.xex`: 1 file, 14,200,832 bytes.
- `.txt`: 1 file, 77 bytes (`text/localetable.txt`).
- No extension: 1 file, 7,294,976 bytes (`$systemupdate/su20076000_00000000`).

The optional derived PE is additional input and is excluded from these totals. There are no loose shader-source files among these extensions. Exact per-directory totals and every file hash are in the JSON.

Original-tree manifest SHA-256: `d2ceee68748294ded48e0f6bf2036b7bf98d5cd650d91c6fbf7d36d43225b232`.

## SToc / `.str`: verified outer structure

All 490 files start `53 54 6f 63 00 00 00 07` (`SToc`, followed by big-endian 7). These observations describe this corpus, not every format ever named `.str`:

- Byte `0x08` gives the observed entry count, ranging from 0 to 50. Bytes `0x09..0x0b` are always `04 00 00`. The inspector intentionally does not invent a wider count/flags schema.
- BE32 at `0x0c` is either zero or `0x14`, marking the observed optional metadata area. BE32 at `0x10` locates the entry table. With no metadata, the table starts at `0x14`. Metadata contains readable stream/dependency-like paths; its internal schema is not parsed.
- Each table row is **24 bytes**, six BE32 words. Relative row offsets: `+0x00` opaque tag; `+0x04` storage tag; `+0x08` decoded byte count; `+0x0c` unresolved word; `+0x10` stored span length; `+0x14` unresolved word. The JSON calls unresolved fields `word_12` and `word_20`; they are not asserted to be offsets or allocation sizes.
- Data begins at the next `0x800` boundary after the table. Stored spans are consecutive and each is a positive multiple of `0x800`. The spans end exactly at EOF in every file. Padding from the table to data is zero.
- Storage tag `0x0eac15c8` occurs **204** times and identifies raw entries in this corpus. Tag `0xb9f0b9ec` occurs **2,549** times; every corresponding payload begins `10 fb` followed by a BE24 decoded length matching the row. There are **2,753 entries**, declaring **2,722,409,464 decoded bytes** in total.
- **18 files have zero entries** and occupy 2,048 bytes each. These are observed empty containers, not missing-data assumptions.

The compressed entries decode with the RefPack command layout. The implementation bounds every command read, literal run, backreference, output length, and stop code; overlapping backreferences are supported. It requires the decoded length to match both headers and the remainder of the stored span to be zero. The public [RefPack implementation in SSXModding/bigfile](https://github.com/SSXModding/bigfile/blob/master/src/bigfile/RefPack.cpp) was used to cross-check command encoding; original payloads independently establish that it applies here.

## Named resource boundary inside decoded entries

Decoded entries contain consecutive 12-byte little-endian chunk envelopes: `tag`, `body_size`, `version`. This is deliberately distinct from the big-endian SToc table. All observed chunk versions are `0x1802ffff`.

There are **25,932 chunks tagged `0x716`** and **452 tagged `0x722`**. The latter's bodies remain opaque. All chunk lengths are walked to each decoded entry's exact end. The header shape resembles RenderWare/RWS chunking; the numeric ID's official name and runtime handler ABI are not established here.

For `0x716`, the parsed body has:

1. BE32 descriptor span length.
2. A descriptor containing a name string, four opaque BE32 words, a type-name string, a source-path string, an extra string, an opaque BE32 tail word, and a BE32 payload size. The report's `id_words_be` field retains the four words without assuming their identifier/hash semantics. Strings have a BE32 padded byte count, printable ASCII, a NUL terminator, then `bf` padding. Remaining descriptor alignment bytes are zero.
3. Resource payload at `chunk_start + 12 + 4 + descriptor_span_length`. The declared payload must fit the body; the body ends at its four-byte rounded boundary. Payload contents and final alignment bytes are not generally interpreted.

These are bounded descriptors, not arbitrary matches found by searching compressed bytes. JSON records retain entry file offsets, decoded chunk/payload offsets, names, types, original build paths, unresolved words, payload header previews, and payload SHA-256. A decoded offset cannot be treated as a physical file offset for a compressed entry.

Examples in `frontend/frontend_global.str`:

- Table begins at file `0x48`. Raw entry 0 begins at `0x800`, decodes to 404 bytes, and contains `StreamTOC`, type `StreamTOC`, source `stream.toc`. Its resource payload begins at decoded `0x60` (physical file `0x860`) and is 306 bytes.
- Compressed entry 1 begins at `0x1000`, declares 469,488 decoded bytes, and contains 13 chunks. The first names `frontend_global.itxd`, type `EARS_ITXD`, with a build path under `x:\build\XEN\ntsc_en\assets_rws\frontend\frontend\texture_dictionary`. Its payload begins at decoded `0x100` and is 458,752 bytes. Subsequent chunks include `healthbar.vfx`, type `VFX`.
- `gamehub/gamehub.str` entry 0 begins at `0x800`. Its decoded resources include `char_collision_spheres.hko`; the payload exposes the `Havok-4.1.0` string. Build paths also expose `.rws.XEN.preinstanced` resources. These strings establish provenance/platform hints, not complete mesh or physics schemas.

Resource type occurrences include VFX 8,770; EARS_MESH 5,533; MetaModel 2,617; RCB 1,226; BNK 1,213; HKO 1,021; HKT 878; EARS_ITXD 863; LH2 594; CHA 529; CHT 528; GRAPH 425; RCM 346; ACS 267; SBK 156; and StreamTOC 20. Counts are occurrences, including repeated assets, not unique resources or deduplicated payloads. The report includes all 36 type labels. No descriptor name/type/source-path in this scan identifies `.fx`, `.hlsl`, `.vsh`, `.psh`, or contains `shader`; that is a scoped negative observation, not proof that resource payloads contain no shader binaries.

**Promising integration boundary (proposal):** retain the original streaming packages and route validated named resource descriptors to native resource handlers keyed by their original type labels. This gives a concrete boundary covering meshes, textures, animation, collision, effects, UI, audio banks, and stream metadata. The observed Lua-to-engine package calls below provide a separate gameflow boundary. Handler implementations, identifier meanings, relocation, ownership/lifetime, texture tiling, material/shader bindings, and native physics semantics still need verification. None are claimed to be recovered by this inspector.

## Loose Lua evidence

The three files are readable UTF-8 source, not `1b 4c 75 61` Lua bytecode:

- `simpsons_gameflow.lua`, 30,050 bytes: episode/mode construction and literal map/movie references. Line 63 supplies `NewMap("Springfield", "spr_hub", "spr_hub.str")`; line 70 supplies `loc` / `loc.str`. Their corresponding original files exist.
- `simpsons_gameflow_helpers.lua`, 3,765 bytes: line 77 calls `MoviePkg:New(movieName, stream, movieType)`; line 92 calls `MapPkg:New(mapName, folder, stream)`. Calls to `tolua.takeownership` explicitly expose a native binding/ownership interface.
- `simpsons_scores.lua`, 54,009 bytes: `ScoreKeeper:GetScoreKeeper()` and `ScoreKeeper:SetScoringVersion(6)`, with score events and progress gating.

The report's `lexical_evidence` gives source lines. It does not execute Lua, parse its entire grammar, identify the precise Lua runtime version, or claim that these configuration scripts contain the game's full native behavior.

## Embedded shaders in the supplied PE

Independent SHA-256 of `analysis/simpsons.pe`:

```text
6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0
```

This matches `image_sha256` in the separately supplied `analysis/executable.json`. The inspector independently reads the PE32 header: machine `0x01f2`, image base `0x82000000`, and `.rdata` beginning at RVA/file offset `0x400`, virtual length 1,975,936 bytes. The derivation from XEX is outside this tool; a matching hash identifies the supplied bytes, not an independent re-derivation.

The scan uses maximal printable ASCII runs **including tab, CR, and LF**, so multiline sources are preserved rather than split at newlines. It records each run's file offset, VA, byte count, SHA-256, NUL-termination observation, and exact text inside JSON. No shader files are written separately.

**Source recovered:** one 21,677-byte, NUL-terminated source candidate at file `0x63228`, VA **`0x82063228`**, ending immediately before VA `0x820686d5`. The text names `vsh_ff_all.hlsl` and describes fixed-function vertex processing for Xenon. It contains `VS_OUTPUT`, `vs_main`, vertex semantics, lighting, fog, material-source constants, texture-coordinate selection, and texture-transform controls. Its SHA-256 is:

```text
9fbcd5e2391752e25a6e7ddf22704e0f271fb562747b157b1cc6f5421f55b78b
```

The supplied addresses `0x8206464e` and `0x82066c2b` fall inside this same string. **`0x8206af70` is the short label `Vertex shader`, not another multiline source.** This recovered generic vertex-processing source is useful original renderer evidence; it is not established as the source for the Simpsons-specific material and postprocessing shaders.

**Descriptions:** 42 printable runs contain Simpsons shader descriptions, representing 23 distinct description phrases. Three runs include one or two preceding printable bytes; the report preserves those bytes and separately records the phrase VA. Examples begin at `0x8200b184` (skinned), `0x8200ee30` (rigid), `0x8202f2dc` (edge detection), `0x82030b78` (blur), `0x820322d4` / `0x82033964` (row/column anti-alias), `0x82057030` (paletted multitone), and `0x8205c988` (VFX rigid mesh particles). Other variants mention alpha, dual textures, animated UVs, and flipbooks. These are descriptions, not 42 recovered executable shader programs. Other multiline diagnostics are labeled separately from source candidates.

The PE also contains `StreamTOC` at `0x820b7884`, `EARS_ITXD` at `0x820b8500`, and `frontend\frontend.str` at `0x8215f6f8`, corroborating the resource vocabulary found in original streams. No compiled shader binary count, entrypoint binding, successful compilation, or renderer equivalence is claimed.

## Video and audio

### Movies

All 42 `.vp6` files use EA chunks, **not bare VP6 elementary streams**. A chunk begins with FourCC and LE32 total byte length, including the eight-byte header. Verified initial chunks are `SCHl` at `0x00` (40 bytes), `MVhd` at `0x28` (32 bytes), and `SCCl` at `0x48` (12 bytes); packet data begins at `0x54`.

`MVhd+0x08` contains `vp60`; `+0x0c/+0x0e` are LE16 width/height; `+0x10` is LE32 frame count; `+0x18/+0x1c` hold numerator/denominator `982026/32767` (about 29.97 frames/s). `+0x14` stays opaque. Dimensions are **1280x720 in 38 files** and **640x480 in four**. The scan counts 77,301 video packets (`MV0K`/`MV0F`) and 77,173 `SCDl` audio packets; per-file counts match declarations.

The `SCHl` audio header contains tag/length/BE-value elements starting at `0x10`, with standalone `fd`/`ff` markers. Revision tag `80` is 3 and channel tag `82` is 2. Sample-count tag `85` uses three or four bytes, so its width and following offsets must not be hardcoded. `SCDl+0x08` gives BE32 samples and `+0x0c/+0x10` give channel offsets relative to `chunk+0x14`. The inspector validates these offsets and sample/count totals, leaving compressed channel contents opaque.

The `vp60` and revision-3 interpretations map to VP6 video and EA ADPCM R3 audio in [FFmpeg's EA demuxer](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/electronicarts.c). Movie audio's 48 kHz interpretation comes from FFmpeg's revision-3 default when tag `84` is absent, rather than a rate field asserted to exist in these headers. `SCEl` ends audio; seven movies continue video afterward, so it must not terminate the container scan.

**Unparsed original trailer:** `movies/en/ealogo.vp6` has a valid counted chunk prefix ending at **`0x85760`**, followed by **512 bytes** beginning `65 66 30 37 39 32 39 61`. The bytes are original; their meaning is unverified. The report retains parsed prefix facts but marks the file `rejected`, with exact tail offset, length and header. It neither skips the tail nor declares the source damaged. The other 41 movie chunk chains reach EOF.

### SNU and MUS

EA Audio Core (EAAC) header fields are two BE32 words: version `(W1 >> 28)`, codec `(W1 >> 24) & 15`, channels `((W1 >> 18) & 63) + 1`, rate `W1 & 0x3ffff`, type `W2 >> 30`, loop flag `(W2 >> 29) & 1`, samples `W2 & 0x1fffffff`. All examined streams use version 0, codec 3, streamed type 1 and 48,000 Hz. Codec 3 is EA-XMA according to [vgmstream's EAAC implementation](https://raw.githubusercontent.com/vgmstream/vgmstream/master/src/meta/ea_eaac.c). This does not imply that the container can be passed directly to an ordinary WAV/XMA reader.

- **SNU:** wrapper `+0x08` is a BE32 absolute audio offset, aligned to 16 bytes; `+0x10/+0x14` hold EAAC words. Intermediate metadata stays opaque. There are 7,204 mono, 147 stereo and 62 four-channel files. Wrapper `+0x04` is preserved as an uninterpreted word; a duration-like correlation is insufficient to make it authoritative.
- **MUS:** 17 indexed containers hold **2,057 streams**, all six-channel. LE32 `+0x04` gives the index count; the table begins at `0x28` with 28-byte rows. A row contains opaque ID bytes at `+0`, BE16 ordinal and zero at `+4/+6`, BE32 header offset divided by 16 at `+8`, BE32 audio offset divided by 128 at `+0x0c`, header length 8 at `+0x10`, audio extent length at `+0x14`, and zero at `+0x18`. Each header occupies a 16-byte slot with eight padding zeros. Index/header/audio ranges are checked for overlap and bounds; inter-span and EOF padding is zero, with final size aligned to 128 bytes. Outer header values otherwise remain opaque.
- **EAAC blocks:** one-byte flag (`00` normal, `80` segment end), BE24 total length including the header, then BE32 sample count. The block contains `ceil(channels/2)` layers; each starts with a BE32 word whose upper 30 bits give the layer byte length including that word. All observed low bits are 3 and the following four bytes are `08 00 00 00`. Layer spans and end padding are validated; XMA packet bitstreams are not decoded.
- The scan walks **265,077 SNU blocks** and **112,566 MUS blocks**, checks all layer spans, and verifies each stream's sample sum and declared end. Normal blocks end immediately after their layers; segment-end blocks may add up to 63 zero bytes.
- **63 SNU files loop**, comprising 62 four-channel files and one stereo file. They have two `80` segment ends. Wrapper `+0x18` is loop-start sample (observed value 1) and `+0x1c` is loop offset relative to audio start. The inspector verifies this offset/sample boundary and continues after the first segment end.

For a small reproducible SNU example, `audiostreams/01_xxx_0/d_as01_xxx_0003bb5.exa.snu` points to audio at `0xb00`, beginning `00 00 06 b8 00 00 12 80 00 00 1a c3 08 00 00 00`: block length `0x6b8`, samples `0x1280`, layer length `0x6b0`. `audiostreams/gts_mus.mus` entry 0 points to header `0x100`, audio `0x180`, extent `0x395680`; the EAAC header is `03 14 bb 80 40 1f dd 1c`.

## StreamTOC milestone: implemented and verified

`stream_toc(payload)` now parses all **20 original occurrences**. Together they have **490 records**: 20 self/global records and **470 named references**. Their resolved paths cover all 490 original `.str` files exactly. All **2,656 encoded block descriptors** in the references match the corresponding original SToc entry's `word_12` and tag; entry counts also match. The 20 self/global records have no encoded block list, so their own 97 SToc entries are validated by the outer container inspector instead.

Offsets below are relative to the StreamTOC resource payload; integers are big-endian unless stated otherwise. The following relationships were checked against all 20 original payloads:

- `+0x00`: `9c ba 7b 28`; `+0x04`: 32-bit value 9. These identify the observed profile.
- `+0x08`: string-pool start, also the package-name string offset. `+0x0c`: link-array start.
- `+0x10`: BE16 total record count. `+0x12`: BE16 non-self reference count; total is reference count plus one.
- `+0x14`: BE32 tag-table count (3–5 observed). `+0x18`: BE32 tag-table offset.
- Records begin at `0x1c`, stride `0x18`. Record 0 names the containing global stream and has zero in every field except its name offset. The reference records immediately follow it; their first words are unique and increasing in this corpus, but the hash/identifier algorithm is unverified.
- Record `+0x00`: opaque word; `+0x04`: offset to a NUL-terminated ASCII name in the string pool. `+0x08` has observed byte pattern `01 00 NN 00`, where `NN` matches the referenced `.str` entry count. `+0x0c` is zero. `+0x10` points to a BE32 word in the link array; that word points to a record boundary. `+0x14` points to the stream's three-byte-per-entry block metadata, or is zero for empty streams.
- The link array starts exactly at `0x1c + record_count * 0x18`, and occupies `reference_count * 4` bytes. Shared link-array pointers occur and are allowed. Every stored link word targets a valid record boundary.
- The concatenated block metadata follows that array. Each three-byte item is BE16 units followed by a one-byte tag-table index. **`units * 256` equals the referenced SToc row's `word_12`**, and indexing the tag table reproduces that row's `tag_hex`. This establishes the relationship without claiming what SToc `word_12` means at runtime. Nonempty metadata spans are consecutive in reference-record order; zero-entry records use a zero metadata pointer.
- Zero padding aligns the following BE32 tag table to four bytes. A single zero byte follows that table, then the string pool, with exactly `record_count` nonempty NUL-terminated names through payload EOF. Every record's name offset must point to a distinct string start, not into the middle of a name.

The JSON embeds each parsed manifest under `chunk.stream_toc`. `resolve_stream_tocs(files)` resolves names by appending `.str` relative to the containing stream's directory and matching the already inventoried paths case-insensitively. It does not follow paths from binary data through the filesystem. Absolute names, drive prefixes, empty/dot/parent components, ambiguous case-folded inventory paths, missing targets, mismatched entry counts/tags/size words, and cyclic record links are rejected.

For `frontend/frontend_global.str`, the 306-byte payload contains four records. Its self name resolves to that file. The `frontend` record resolves to `frontend/frontend.str` (32 entries) and links to the self record. `text\E172A05C` and `text\F588F94A` resolve to the two frontend text streams (one entry each) and link to the `frontend` record. The manifest locations are record table `0x1c`, link array `0x7c`, block metadata `0x88`, tag table `0xf0`, string pool `0xfd`.

All record-link graphs terminate at their self/global record, with maximum observed depth four. **Interpreting those links as parent/loading dependencies is a hypothesis consistent with the hierarchy; runtime ordering, reference counting and handler ABI remain unverified.** The tool reports `linked_record_index` and `linked_resolved_path`, rather than executing a presumed loading order.

The next small format milestone is a bounded header/descriptor parser for **one `EARS_ITXD` payload**, using the extracted frontend dictionary below, and cross-checking its texture descriptors against additional originals. Native texture layout, material/shader binding, mesh payload semantics, and runtime integration remain separate work.

## Deterministic named-resource extraction

`tools/extract_resource.py` reuses the SToc, RefPack, and resource descriptor parsers. The caller explicitly supplies the original `.str`, zero-based entry index, exact case-sensitive resource name, and output file. It writes the **declared resource payload only**; it strips the outer SToc storage, RefPack encoding, chunk envelope, resource descriptor and alignment. A missing or ambiguous resource name is an error.

Examples from the workspace root:

```powershell
New-Item -ItemType Directory -Force build/extracted | Out-Null
python -B tools/extract_resource.py --input 'Simpsons Game, The (USA)/frontend/frontend_global.str' --entry 0 --name StreamTOC --output build/extracted/frontend_global.StreamTOC.bin
python -B tools/extract_resource.py --input 'Simpsons Game, The (USA)/frontend/frontend_global.str' --entry 1 --name frontend_global.itxd --output build/extracted/frontend_global.itxd
```

The first example exercises a raw SToc entry and produces exactly **306 bytes**. The second exercises RefPack and produces exactly **458,752 bytes** of original `EARS_ITXD` payload. The extractor's JSON stdout records source and payload provenance; it writes no sidecar manifest. Extracting StreamTOC validates its internal structure; inventory-wide resolution is performed by the inspector's full report pass.

Verified output SHA-256 values:

- `build/extracted/frontend_global.StreamTOC.bin`: `732db087168709e3275b9ce8a0b7714ddb51c4380b371759cd68df76de6ac54a`.
- `build/extracted/frontend_global.itxd`: `3e4909a83d0dc05698c43567aea90ae0838b5c21b03ecfc326d30431ad4c033f`.

Both come from original `frontend/frontend_global.str`, SHA-256 `ae967348ebe5a0ed022c13cc372a3e53831a86f26804c76159ac1c8203b6388a`. Repeat extraction preserves the output bytes and modification time and emits identical JSON. Nine negative selection/overwrite checks reject source/tool destinations, a differing existing output, invalid entry indices, missing/case-mismatched names and duplicate name matches; the source, tools, and existing extracted files retain their pre-test hashes and modification times.

The default asset root is the original game directory beside the tools directory. `--asset-root` supports a separately located source tree. The output's parent directory must already exist. Output must be outside that asset root, including after path resolution; source and tool overwrites are rejected. Existing matching output is idempotent, while a differing existing output is rejected. Output filenames come from the caller rather than untrusted embedded names. No original file is changed.

## Validation scope

The corpus scan checks outer SToc tables, RefPack streams, chunk/resource envelopes, StreamTOC layouts/links and every referenced SToc table, EAAC indices/block/layer spans, movie chunk framing and count agreement, readable Lua, and optional PE text boundaries. Development checks exercise synthetic RefPack literals and short/medium/long overlapping copies, truncations at every byte of those small streams, malformed table/chunk/descriptor offsets and sizes, invalid backreferences, media truncations, and PE header/section bounds. Movie mutations cover conflicting codec tags, invalid rates, audio after `SCEl`, and empty video packets. An additional **326 StreamTOC cases** cover every truncation of the 306-byte payload plus malformed counts/offsets/tag indices, unsafe names, cycles, absent targets and contradictory referenced SToc metadata. Checks run from stdin with `python -B`, so no test fixtures or bytecode files are written.

Original GPU microcode, shader completeness/compilability, texture encodings and tiling, resource handler ABI, Lua execution, physics semantics, and opaque codec bitstreams remain outside this validation. External format implementations establish vocabulary and candidate layouts; the counts, offsets, hashes, and accepted structural relationships above come from reading the original or explicitly supplied derived bytes.
