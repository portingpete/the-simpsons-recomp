# Original renderer boundary: startup and loading quads

The verified replacement surface is the **engine's platform driver and resource callbacks**, together with the original screen-quad helper **`0x82756480`**. Boot021 reaches platform renderer start through that driver. Replacing `VdInitializeEngines` with success would leave the console device and its dependent resources without their required implementation.

This report describes the identified US executable. Function names below are behavioral labels, not recovered debugging symbols. **Observed** means recorded by boot021; **static verified** means checked against original instructions and data; **design** means a proposed native interface. No renderer, GPU command processor, runtime hooks, or generator modifications are included.

## Reproduction and provenance

Run from `K:\SimpsonsNativeCopy`:

```powershell
python -B tools/analyze_render_boundary.py --self-test
python -B tools/analyze_render_boundary.py --report analysis/render-boundary.json
```

The analyzer reads the original flat `analysis/simpsons.pe`, generated AOT, `build/boot-021.log`, and the existing offline `build/generator-ninja/SimpsonsDisasm.exe`. It writes only the selected JSON report. It pins the PE size (15,466,496 bytes) and SHA256 `6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`. Addresses map to **VA minus `0x82000000`**, not the PE's raw-file section offsets.

The JSON includes original instruction words and disassembly, per-function byte hashes, exact AOT source locations and instruction hashes, decoded direct branch edges, the boot log hash, and a recovered driver callback map. It compares every selected AOT instruction comment with disassembly of the original bytes; it does not certify the generated C++ semantics. `.pdata` extents are checked independently. The entry's AOT recovery includes an adjacent tail (484 bytes versus its original 448-byte `.pdata` extent); inspection deliberately uses the original 448 bytes. Nearest preceding `.pdata` entries are **not** treated as owners of unrelated leaf functions.

The report is deterministic for unchanged inputs and analyzer. It has no timestamps. Original assets and all previous asset/texture tools remain unchanged.

Frozen verification result: **77 functions, 5,581 original instruction words, 34,176 original `.pdata` records**, all selected AOT instruction comments equal to the original offline disassembly. **18 in-memory checks** passed. Six additional in-memory mutations of the real PE/log were rejected: truncated image, changed instruction byte, duplicate frame depth, changed saved LR, unaligned logged device address, and changed import argument. Two full regenerations were byte-identical: JSON **845,585 bytes**, SHA256 **`0d6ddbc9b1141e240dd8d28fed0dc0cf332a5bb72d3943d99cae4b526f72689f`**. Analyzer SHA256 **`c5024cec39462565a8053ae78eafd03949ce30046f48f10014cc3906c66318c4`**; boot021 log SHA256 **`729cc3fa5c1a17232bc1ba20ec96fd6e1bdada0c860a569180874bf5146d6ea0`**.

`tools/inspect_assets.py`, `tools/inspect_itxd.py`, `tools/decode_itxd.py`, and the extracted original ITXD still match their previously recorded hashes. Validation wrote only `analysis/render-boundary.json`; no original, runtime, generator, or previous tools were changed. Additional screen-shader proof is kept in `docs/screen-material.md`; this frozen JSON deliberately retains the uncertainty known at its snapshot.

## Observed original-entry-to-device-start chain

Boot021 records these functions, outermost first. Addresses after `@` are the original call instructions, not the saved return addresses:

```text
82432280 original entry
  @82432414 -> 823BC8C0
  @823BC98C -> 82861F48 startup and loop orchestration
  @828620A8 -> 82875BF0 engine initialization orchestration
  @82875D08 -> 823EC9E0 engine start
  @823ECA14 -> 823EC490 driver request adapter
  @823EC4C0 -> 823F0630 indirect driver callback, request 2
  @823F0888 -> 823EDF20 platform renderer start
  @823EE03C -> 82452540 console device creation
  @824525DC -> 82466F30 console device initialization
  @82466F84 -> 82466E48 console engines initialization
  @82466E7C -> 82CC2E44 VdInitializeEngines import thunk
```

The last call passes `r3=0x16000000`, `r4=0x82466430`, `r5=0`, `r6=0x8206DA30`, `r7=0x8206DEB0`. The logged PC `0x82466E80` is the continuation after the call. `r31=0xE2C9A380` is the device pointer in this particular run; it is not a stable address to hardcode. The analyzer checks all ten saved-LR call instructions against the original image, including the indirect call, and rejects discontinuous logged backchains.

This proves execution **to initialization failure**. It does not prove successful renderer start, frontend execution, pixels, or presentation.

## Engine driver contract above console D3D

Let `E = BE32[0x82D0CA68]`. `0x823ED028` initially selects static core storage `0x82D0C918`. `0x823ECE30` subsequently allocates the extended engine using its allocator callback, copies the original **0x14C-byte core**, and publishes the allocation through `0x82D0CA68`. The allocation size comes from the plugin registry; 0x14C is not the full extensible object's size.

Verified core fields relevant here:

- `E+0x00`: current camera, set by `0x823F1870` before platform camera begin and cleared by `0x823F1800` after successful end.
- `E+0x10..0x47`: 56-byte platform driver descriptor, copied from `0x82CD1A78` by driver request 4 (`0x823F07B0..07CC`). Its callback is at descriptor `+4`, hence `E+0x14`.
- `E+0x48`: start of 29 four-byte standard callback slots, populated by request 11. The request is made at `0x823ECEDC` with output `E+0x48`, count 29.
- `E+0x108` / `E+0x10C`: allocator/free callbacks used by engine open and failure cleanup. They are retained by the original engine, not fabricated renderer data.
- `E+0x144`: engine lifecycle state. Core init writes 1, successful open writes 2 at `0x823ECFE8`, successful start writes 3 at `0x823ECA68`. Those values have verified local behavior; they are not a complete invented enum.

`0x823EC490` has the original ABI `(r3=descriptor, r4=request, r5=output, r6=input, r7=value)`. At `0x823EC4A0` it reads `[descriptor+4]` and at `0x823EC4C0` calls it as `(r3=request, r4=output, r5=input, r6=value)`. Return `r3` is checked as zero/nonzero.

The original descriptor words at `0x82CD1A78` are:

```text
3F800000 823F0630 3F800000 00000000
824025A8 82401260 82408EB0 824090A8
82409308 82409570 00000000 00000000
00000000 00000000
```

Only the dispatcher slot and its installation are needed to identify the initialization boundary. The other words must not be interpreted as a complete C++ class or host COM vtable.

The dispatcher `0x823F0630` bounds the request to 0..22 and uses 23 original bytes at `0x82062AC0` to select `0x823F067C + 4*index`. Verified lifecycle requests are:

- **0:** open, via `0x823EFFD8` at `0x823F07E4`.
- **1:** close, starting at `0x823F07EC`.
- **2:** start, `0x823EDF20` at `0x823F0888`; its result is stored at `0x82D0CB08`.
- **3:** stop, `0x823EE1A8` at `0x823F08A8`.
- **4:** copy the 56-byte driver descriptor to the caller's output and retain the supplied allocator-table pointer at `0x82E3DD64`.
- **11:** populate standard callbacks through `0x823F03A0` at `0x823F08BC`.
- **17:** this original dispatcher returns 1. Engine start issues it after plugin construction and gamma setup; that does not authorize making other requests succeed without work.

After a successful request 2, `0x823EC9E0` **still** calls plugin construction `0x823FB328` at `0x823ECA2C`, gamma-related `0x82406560` at `0x823ECA40`, request 17, and finally writes lifecycle state 3. Plugin-construction failure calls request 3. Hooking the entire engine-start routine and simply returning 1 would skip these responsibilities.

## Exact platform start/stop requirements

`0x823EDF20` consumes global state; it does not consume incoming argument registers. Its verified extent is `[0x823EDF20,0x823EE1A8)`, 0x288 bytes, SHA256 `575ebe22b19549ad84801306e347ca37feaf58da2b2c2f026f4fe54bd41cd1d6`. It returns 0 on negative device-creation HRESULT and otherwise proceeds through the following original side effects before returning 1:

1. Clear **0x7C bytes at `0x82E3DCE0`** (presentation block), load width/height from `0x82E3DF84/0x82E3DF88`, and write them at block `+0/+4`. Fixed format words are `+08=0x182801B6`, `+28=0x1A220197`, `+40=0x28280136`. Words `+34/+38/+3C` are 1. The `+60..+78` region is video-mode dependent; this is a partial proven block, not a fully named presentation schema.
2. At **`0x823EE03C`**, call `0x82452540(0,1,0,1,0x82E3DCE0,&0x82D0CAF8)` in r3..r8.
3. Create color surface at `0x823EE070 -> 0x82440698` and publish it at **`0x82D0CB00`** (`0x823EE084`). Create depth surface at `0x823EE0B0` and publish it at **`0x82D0CAFC`** (`0x823EE0BC`). Bind target 0 through `0x823EDB68` and depth through `0x8243D598`.
4. Create four textures through `0x82440578`, at callsites `0x823EE104/12C/154/17C`, publishing them at **`0x82D0CF84/CF90/CF8C/CF88`** respectively. These calls use target dimensions and the presentation format words; their later whole-game purposes are not named here.
5. Call `0x823EDD38` at `0x823EE184`, which clears binding caches at `0x82D0CF58` and `0x82D0CF5C..68`. Thus final zero caches do not mean no targets were bound.
6. Initialize further resource/geometry/state services with `0x823F4780`, `0x823F69E0`, `0x82409A90`, `0x824008E0`, `0x823FCF60` at `0x823EE188..198`.

The SDK device is **not** the small engine descriptor. `0x82452540` requests **0x5700 bytes, 0x80 alignment**, clears the caller's output at `0x82452568`, calls aligned allocator `0x82451FB0`, initializes with `0x82466F30`, and only publishes the result at **`0x824525E8`**. The allocator clears the device and retains its original allocation at `device-4`. Therefore the observed path predicts `[0x82D0CAF8]==0` at the boot021 GPU-init failure, despite r31 pointing to allocated storage. This is a static prediction, not a logged memory read.

Original code directly reads/writes SDK internals: target pointers at `device+0x3090+4*n`, depth at `+0x30A0`, target/depth descriptors at `+0x2884/+0x2888`, depth request/control at `+0x2E5C/+0x2934`, stage sampler words around `+0x480`, dirty masks, shader/declaration state and color constants. Initialization `0x824008E0` itself writes sampler words (`0x82400BA0..BB4`). **No small sufficient fake device field set has been established.** A native driver needs native handling for retained calls that otherwise dereference this object; the plan is not to reproduce its command backend.

Stop **`0x823EE1A8`**, extent 0x290 bytes, releases renderer resources and calls `0x824520F0(device)` at `0x823EE420`, then clears `0x82D0CAF8` at `0x823EE42C`. Native start/stop must be paired, with defined partial-start failure cleanup. Original stop must not receive host-native handles in fields it interprets as SDK objects.

## Standard callback addresses and targets

`0x823F03A0` constructs 27 literal `(slot,address)` pairs and fills a 29-slot vector. All slots are first set to `0x823EE438` (returns 0); slots 0 and 22 remain that default. The analyzer reconstructs the literal pairs with a bounded, rejecting reader for the exact `addi/addis/stw/cmpwi` sequence, rather than accepting a handwritten table.

The most useful boundaries for the native implementation are:

- Slot **1**, `E+0x4C`: `0x823F00C0`, platform camera begin.
- Slot **2**, `E+0x50`: `0x8240AC78`, pixel conversion, **not raster creation**.
- Slot **3**, `E+0x54`: `0x8240B058`, pixel conversion, **not raster destruction**.
- Slot **4**, `E+0x58`: `0x823F7070`, raster creation, called by `0x82408130` at `0x824081BC`.
- Slot **5**, `E+0x5C`: `0x823F62A0`, raster destruction, called by `0x82407DC0` at `0x82407E00`.
- Slot **10**, `E+0x70`: `0x823EE7F0`, platform camera end. It clears the platform's current-camera storage `0x82E3DD60` and its local active flag.
- Slot **20**, `E+0x98`: `0x823EE820`, raster presentation path. `0x823F1BD0` obtains `camera+0x60`; `0x82408030` calls this engine callback.
- Slot **21**, `E+0x9C`: `0x823EE940`, clear path candidate; exact clear ABI must be retained from its original callers.
- Slot **26**, `E+0xB0`: `0x8240A278`, native texture stream reader, called at `0x823FF8F0`.

The JSON preserves every address, its engine offset, and the exact original instructions that stored its index and target. A recovered address alone does not prove all argument fields of that callback.

Raster creation wrapper **`0x82408130`** takes `r3=width,r4=height,r5=depth-like value,r6=flags`. It allocates through `E+0x120`, writes raster parent `+0=self`, dimensions `+0x0C/+0x10`, depth-like value `+0x14`, zero offsets `+0x1C/+0x1E`, and cleared pointer/flag members, then calls slot 4 with **`r3=0,r4=raster,r5=flags`**. A false result returns the object to the original allocator; success runs raster plugin initialization and returns the raster. Platform callback `0x823F7070` reads dynamic plugin offset `[0x82E3DC94]`, initializes extension fields, and creates or associates resources according to type/flags. Camera type 2 and shared depth type 1 have special handling; not every raster owns its underlying target.

Raster destruction wrapper **`0x82407DC0(r3=raster)`** runs plugin destruction, calls slot 5 with **`r3=0,r4=raster,r5=0`**, and returns the raster object to its allocator. `0x823F62A0` removes matching texture bindings among eight stages, distinguishes parent/subrasters, and avoids releasing the shared default depth surface as a privately owned allocation. These distinctions matter to a native resource registry.

## Camera, targets, and quad lifetime

`0x827142D8` constructs the camera and two rasters. It stores the first `0x82408130` result at camera `+0x60` and the second at `+0x64`. The calls use original width/height inputs. The first uses a flags/type argument calculated as 2 or 5; the second uses 1. The full raster format enum is not inferred from those numbers alone.

`0x823F1DB0` installs camera begin callback `0x823F1870` at `+0x18`, end callback `0x823F1800` at `+0x1C`. Public dispatchers `0x823F1A18` and `0x823F1A08` tail-call those fields. The loading draw path checks begin's nonzero return before drawing and calls end afterwards.

Platform begin `0x823F00C0` takes the camera in **r4**, retains it at `0x82E3DD60`, computes matrices, and calls target selection `0x823EE6C8`. Target selection follows camera `+0x60/+0x64`, raster parent `+0`, and plugin offset `[0x82E3DC94]`. It chooses either default target globals or raster plugin handles according to the original raster-type test. It constructs viewport values from raster signed offsets `+0x1C/+0x1E`, dimensions `+0x0C/+0x10`, and float depth endpoints **1 then 0**, then calls `0x8243D0F8`. Native code must account for this reversed endpoint order instead of assuming the usual 0..1 ordering.

The quad helper itself does not choose a render target. A replacement must consume the camera/target selected by the surrounding begin/end scope. Its input color is copied before submission; it does not acquire texture ownership. The original transient vertices are allocated and finalized within the call. A deferred native queue therefore needs its own snapshot and resource-lifetime policy, rather than retaining pointers to the caller's stack or the original transient allocation.

## Small frontend path, independently tied to its name

Startup `0x82861F48` constructs the 32-byte frontend-loop object at SP+0xD0 using `0x8285F640` (`0x828621EC`). It obtains the key for the original string **`FrontendMainLoop` at `0x82001F10`** and registers the object at `0x82862210`. The object's vtable is `0x8215F698`, with tick at `+0x0C = 0x8285FDA0`. The separate object constructed by `0x82860058` has a different vtable; it must not be substituted for this identification.

Manager `0x82860C08` calls the current object's vtable `+0x0C`. The frontend tick calls `0x82862D50`, which conditionally gets the active camera and calls `0x828625A0` at `0x82862E40`. That helper:

1. Checks the camera and invokes begin (`0x828625C8 -> 0x823F1A18`).
2. Selects engine viewport scale (`0x828625D8 -> 0x82752090`).
3. Reads width/height from `BE32[camera+0x60] + 0x0C/+0x10`.
4. Calls `0x82756480` at **`0x8286266C`** for a black full-target rectangle if requested, with selector **3**, no texture, UVs 0,0,1,1, and color 0,0,0,1.
5. Runs five animated-element iterations and conditionally calls the same helper at **`0x82862880`**, with selector **0**, white RGB plus computed alpha, and texture `frame1` or `frame2`.
6. Ends the camera through `0x823F1A08`.

This is a static, complete narrow draw-request path. Boot021 has not reached it.

## Exact screen quad input and geometry

At **`0x82756480`**, original ABI:

```text
r3       camera pointer
f1..f4   x0, y0, x1, y1 (rectangle endpoints, NOT width and height)
f5..f8   u0, v0, u1, v1
r8       pointer to four big-endian float32 color components
r9       blend selector used by this helper
r10      texture wrapper pointer, or zero for flat color
```

The helper reads alpha at `r8+0x0C` and skips drawing if it is less than the original float `0x3C010204` at `0x821DD350` (float32 approximation of 1/127). Raster offsets are signed 16-bit values. With engine scales `sx=[0x82DFEB3C]`, `sy=[0x82DFEB40]`, its original arithmetic is:

```text
X0 = 2 * ((rasterOffsetX + x0) * sx) - 1
X1 = 2 * ((rasterOffsetX + x1) * sx) - 1
Y0 = 1 - 2 * ((rasterOffsetY + y0) * sy)
Y1 = 1 - 2 * ((rasterOffsetY + y1) * sy)
```

`0x82751510` copies those scales from the selected engine viewport record. A native bridge should read or reconstruct that selection, not blindly replace them by the supplied rectangle's reciprocal dimensions. PPC single-precision and fused-operation rounding also constrain any later pixel-identity claim.

Vertices are TL, TR, BL, BR. Textured vertices contain `float X,Y,U,V`, stride **16**, totaling 64 bytes. Flat vertices contain `float X,Y`, stride **8**, totaling 32 bytes. `0x827566E8` or `0x827567E8` calls `0x8244C450(device, primitive=6, vertexCount=4, stride)`. Vertex writes finish at `0x82756808`; `0x82756810` calls `0x8244C8F0` to finalize. These lower routines enter console submission machinery. The suitable engine replacement is the helper entry, not those lower submission routines.

## Material, texture, sampler, and render state

The textured branch binds handles `[0x82CF2340]` and `[0x82CF2334]`; the flat branch binds `[0x82CF231C]` and `[0x82CF2310]`. Original records associate these with Screen_Xenon VSTextured/PSTextured and VSFlat/PSFlat respectively. The helper copies the color into device `+0x1780..0x178C`. Original reflection calls the color parameter `screen_color`. Exact shader output Z/W and final arithmetic remain separate proof requirements; these names alone are not replacement shader source.

Texture resolution is explicit at `0x82756600..661C`: `raster = BE32[r10]`, `nativeTexture = BE32[raster + BE32[0x82E3DC94]]`, then slot 0 bind via `0x824408E0`. This dynamic plugin offset must not be replaced by an assumed fixed raster member. The helper selects linear min/mag through `0x8243BA40/0x8243BBD0`, clears U/V address fields to repeat/repeat, and leaves some other sampler fields inherited. The wrapper's packed sampler-like word is not consumed to configure this draw's U/V/min/mag choices.

Calls at `0x82756554/60/6C/78/84` request depth-test off, culling off, alpha-test on, alpha comparison GREATER, and alpha reference 1/255. The blend words passed to `0x8243CB80(device,0,word)` are:

```text
selector 0 : 00010106  source-alpha additive RGB
selector 1 : 00010706  source-alpha-over RGB
selector 2 : 00010186  reverse-subtract RGB with source-alpha factor
selector 3 : 00010001  replacement RGB
```

All four words select source-alpha replacement for the alpha channel. The loading animation explicitly sets selector **0**; the background explicitly sets **3**. Raw words and original setter instructions are authoritative; register-field interpretations are corroborated against read-only reference declarations, not a copied backend.

Cleanup calls `0x8243A010(0)`, alpha-test off, and depth-test on. It is not a save/restore of previous render state. Non-3 selectors also bracket the draw with `0x8243B3B0(1)` then `(0)`. That original setter toggles target format **2↔10 and 3↔12**, using request `device+0x2EF4`, surface `+0x1C`, and mirror `device+0x2884`. Read-only `K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h` identifies the alternate values as expanded blending formats. Exact precision/parity is still unresolved. Depth writes, stencil, color masks, remaining sampler fields, and the caller's target state are not all specified by this helper.

Reference declarations used only for interpreting state values: `xenos.h` lines 104 (repeat), 130 (linear), 298 (target formats), 677 (comparison), 700 (blend factors), and `registers.h` lines 779 (blend fields) and 798 (depth fields), under the same read-only `include/rex/graphics` directory. These declarations corroborate fields manipulated by the original setters; no reference command processing code is shipped.

## Loading texture ownership

`0x82862A28` opens a stream over embedded original bytes at **`0x8215F820`, length `0x20150`**. The pair `{pointer,length}` is at SP+0x50; the open call at `0x82862A68` is `0x823F9598(r3=3,r4=1,r5=&pair)`. It searches type 22 using `0x823F7F58`, reads a dictionary with `0x823FF7A8` at `0x82862A9C`, and closes the temporary stream with `0x823F94A0`.

The dictionary lives at `[0x82E071EC]`. `0x823FE0A8` looks up original strings `frame1` (`0x8217F9A8`) and `frame2` (`0x8217F9A0`) at `0x82862ABC/AD0`, storing results at `[0x82E071E4]` / `[0x82E071E8]`. Cleanup `0x82862B18` calls dictionary release `0x823FEC80` at `0x82862B60`, then clears all three pointers. These textures are distinct from the previously decoded `frontend_global.itxd` dictionary. Extraction/decoding of this embedded artwork belongs to the parent's separate loading-art work.

The exact embedded span SHA256 is **`74ddb211e9ebf1025dd39aa5632ce2cfbabdb67fffa1eecfd6be5ca4a5d364dd`**. The native stream reader callback has **`r3=stream,r4=&texture_output,r5=chunk_length`**, returns Boolean, and stores a valid texture at the output only on success (`0x8240A8C8`). The callee overwrites incoming r5 immediately; its own chunk searches/read-length checks govern the parse. Dictionary loading checks both Boolean success and nonzero texture, then inserts through `0x823FE020` at `0x823FF924`.

`0x823FE0A8` performs case-insensitive lookup without incrementing the texture count. These frame pointers are **borrowed**. Dictionary destruction `0x823FEC80` iterates textures (`0x823FDD58`), invokes `0x823FDEC8`, decrements texture `+0x54`, and calls final destruction `0x823FD900` unless the result remains positive. Final destruction runs extension cleanup, unlinks the texture, releases `[texture+0]` via `0x82407DC0` at `0x823FD960`, and returns the texture object to its pool. A queued native draw cannot assume a borrowed texture survives dictionary release.

## Code proof: serialized loading pixels are linear source blocks

The parent decoded the two original loading textures as 256×256, one level, platform 9, scalar format `0x1A200154` (BC3), flags 9, using serialized linear blocks and 8-in-16 byte swapping. The original loader independently corroborates the **linear source layout**:

- `0x8240A2C4` reads a 72-byte native header and checks platform 9; `0x8240A2E8` reads a further 16 bytes containing format, dimensions, depth-like byte, level count, type and flags. Its word/halfword loads are big-endian.
- For each level, `0x8240A6DC` reads four bytes; `0x8240A6E8..70C` explicitly reverses the size-word bytes. This corroborates the mixed-endian level framing rather than assuming all native-header fields have one byte order.
- `0x8240A758` derives compressed block rows from level height divided by four, clamped to one. `0x8240A764` computes source pitch as serialized size divided by those rows. `0x8240A77C` reads the payload into a temporary buffer. For 65,536 bytes and 256 pixels high, this is **64 block rows and 1,024 bytes per row**.
- `0x8240A7D8` calls layout adapter `0x82534228`, passing the temporary buffer in r10 and pitch in caller SP+0x54, distinct from the destination returned by raster lock `0x82408208`.
- `0x82534228` converts pixel dimensions to format block dimensions, then calls **`0x82533970` at `0x82534474`**. That routine saves the source pointer separately, multiplies source row by pitch at `0x82533ACC`, and adds source column times bytes-per-block at `0x82533BBC..BC4`. Its destination uses separate tiled address arithmetic before the memcpy at `0x82533C44`. The first and tail spans use the same linear source relationship (`0x82533B14`, `0x82533C8C`).

Thus the original engine uploads **linear serialized source rows into a different native destination layout**. The tiled native format is not evidence that stream payload bytes were already tiled. This code proof is limited to serialization/layout; endian treatment of compressed block contents remains supported by the parent's decoder evidence and format declarations. No swizzler or pixel decoder is added by this report. The parent's corrected artwork identification is Itchy/Scratchy; this report relies on addresses/bytes rather than a visual character label.

## Implementation target and limits

**Design:** implement a bounded native platform-driver service for the observed start path and loading camera/quad/resource lifecycle. Keep original engine allocation, plugin setup, request results, callback installation, and teardown meaningful. Replace the platform-dependent bodies only when their native target/resource services exist. Do not expose a fake console D3D object at `[0x82D0CAF8]` to continuing original device setters.

The first useful rendering milestone is the original loading background and animated `frame1/frame2` quads, driven by the original frontend loop and this exact ABI. It requires target creation/selection, texture creation/destruction, original pixels, verified screen material behavior, and the proven sampler/blend states. Unsupported callbacks/formats should remain explicit failures. A whole-game material, scene, or shader mapping is not claimed.

`0x82740CE8` remains definitively an allocator: original instructions pop a free-list record, decrement a count, invoke initializer `0x82740C28`, and return the record. The known camera hooks `0x823F1AC0`, `0x823F1C98`, and `0x8273B518` are not evidence that this allocator submits a draw.
