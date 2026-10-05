# Embedded original loading artwork

`tools/extract_loading_art.py` reads the exact identified flat PE and extracts
the dictionary at VA `0x8215F820`, length `0x20150`. Its SHA256 is
`74ddb211e9ebf1025dd39aa5632ce2cfbabdb67fffa1eecfd6be5ca4a5d364dd`.
Original loader `0x82862A28` selects this stream and looks up `frame1` and
`frame2`; see the independently recovered renderer boundary report.

Both original textures are 256x256. The outer dictionary/chunk headers are
little-endian. Each 88-byte native header is big-endian, followed by a
**little-endian** level byte count and 65,536 original data bytes. The profile
has platform 9, sampler word `0x1102`, raster-format word `0x300`, native format
`0x1A200154`, depth field 16, one level, type field 4, and flags field 9.
The depth field is preserved as observed metadata; it is not used to infer
uncompressed bits per pixel. Each extension contains an opaque eight-byte
`0xEA2F` payload, preserved without inventing an interpretation.

The data decodes as linear rows of 64x64 BC3/DXT4_5 blocks after adjacent-byte
exchange (`8in16`). The two PNGs coherently show the Itchy/Scratchy mallet
animation. They were both inspected at their original resolution. These are
decoded original resources, **not captures of the running game**.

The native format word's low-six-bit format 20, endian 1, and RGBA selectors
agree with the scalar/descriptor relationships established in the frontend
ITXD investigation and the reference `xenos.h` format enum. A tiled interpretation
of the embedded stream produces scrambled artwork. The native format describes
runtime storage, while these serialized bytes are linear. The original loader
corroborates this: `8240A6E8..70C` reverses the level-size word, `8240A758..764`
derives 64 compressed block rows and a 1,024-byte source pitch, and `8240A77C`
reads into a temporary buffer. The upload at `8240A7D8` passes that buffer and
pitch to `82534228`, which calls `82533970`. Source addresses there are
`source + row*pitch + column*bytesPerBlock` (`82533ACC`, `82533BBC..BC4`);
the destination uses separate tiled address arithmetic. See
`docs/render-boundary.md` for the complete byte-checked chain. This proves the
serialized layout; the block byte swap remains supported by the format and
decoder evidence. Do not generalize it to arbitrary native dictionaries. The
earlier ITXD sample really does use tiled storage.

The first failed tiled hypothesis and explicit unswapped/linear diagnostic
candidates are retained in `build/loading-art-candidates`. Only
`build/loading-art` contains the corrected stream, PNGs and manifest.

BC3 alpha structure follows Microsoft's
[compressed alpha description](https://learn.microsoft.com/en-us/windows/win32/direct3d9/textures-with-alpha-channels).
RGB565 expansion and integer interpolation use the already tested frontend
decoder. Alpha interpolation deliberately uses integer division without bias,
matching the readable reference conversion in
`K:\Simpsons\RexGlueCurrent\src\graphics\shaders\bytecode\d3d12_5_1\texture_load_dxt5_rgba8_cs.h`
(five/seven-way division near lines 228–257), rather than the rounding bias in
Microsoft's illustrative pseudocode. Exact console filtering/framebuffer
rounding and premultiplication are unverified. Stored RGB/A are preserved; no
gamma or alpha-association transform is applied.

From the workspace root:

```powershell
python -B tools/extract_loading_art.py --verify
python -B -m unittest discover -s tests -p test_loading_art.py -v
```

Omit `--verify` only when `build/loading-art` does not exist. The tool refuses
to overwrite an existing output directory and checks the complete PE identity.
Five tests cover both alpha modes, implicit extremes, transparent RGB, color
endpoint order, independent linear-block/endian fixtures, mixed-endian sizes,
chunk bounds, exact identity and deterministic artifact reproduction. All pass.
Original data, the derived PE, and the previous texture tools remain unchanged.

The native C++ equivalent in `renderer/native_texture_stream.cpp` now accepts
the same bounded struct profile, preserves header metadata, validates mixed
endianness/extent/dimensions, and converts the linear block bytes for a native
BC3 resource. It rejects other formats/mips/flags explicitly. Chunk traversal,
original texture/raster object construction and extension ownership remain the
engine bridge's responsibility. `OriginalNativeTextureStream` reads both actual
original structs from the verified derived image, uploads them through D3D11,
and checks exact compressed-byte readback, plus malformed-input rejection.
This does not yet replace the original stream callback in a running game.
