# Native Vulkan renderer

The game can render through Vulkan instead of Direct3D 11. Both renderers draw
the same frames from the same native renderer code; D3D11 remains the default.

## Choosing the renderer

Open **Options → Video**, go to the **Performance** page and change
**Renderer** to Vulkan or D3D11. Accept saves the choice; it applies on the next
launch, like the other `(restart)` rows. Version 9 video preferences
(`.video.cfg`) store it as their last field; version 8 files load with D3D11.

The renderer used for a launch is decided in this order:

1. `--renderer vulkan` or `--renderer d3d11` on the `SimpsonsNative` command line;
2. the `SIMPSONS_RENDERER=vulkan|d3d11` environment variable (tests and tools);
3. the saved Video menu choice.

The log names the selection and its source, for example
`[BOOT] renderer=Vulkan (video settings)`, followed by the selected adapter:
`[NATIVE GRAPHICS] Vulkan NVIDIA GeForce RTX 5080 api=1.4.351 driver=NVIDIA 617.14`.

## Requirements

A Vulkan 1.3 adapter with dynamic rendering, synchronization2, extended dynamic
state, timeline semaphores, push descriptors, `VK_EXT_robustness2` null
descriptors, `VK_EXT_depth_clip_enable`, depth clamp, independent blending,
sample-rate shading and BC texture compression. Discrete GPUs are preferred;
`SIMPSONS_VULKAN_DEVICE=<name substring>` picks a specific adapter. Optional
features are used when present: `VK_EXT_present_timing` (display statistics),
`VK_EXT_shader_stencil_export`, custom border colours and pipeline statistics.
Without stencil export the renderer reports `PSSpecifiedStencilRefSupported`
as false and uses its existing stencil-bit fallback, exactly as on D3D11
hardware without that feature.

There is no software (WARP) Vulkan device: test probes that ask for WARP get
the selected Vulkan adapter, and the log says so.

## Design

`renderer/vulkan` implements the part of the D3D11/DXGI interface that
`NativeBackend` uses — device, immediate and deferred contexts, command lists,
buffers, 2D textures, views, state objects, input layouts, queries and
predicates, context-state objects, and the flip-model swap chain — natively on
Vulkan. The renderer's pipelines, recording payloads and tests therefore run
unchanged on either API. The D3D11 contracts they rely on � binding hazards,
`Get*` round trips, `ClearState`, `SwapDeviceContextState`, deferred command
lists with and without state restore, `WRITE_DISCARD` renaming, staging reads,
occlusion and timestamp queries � are reproduced, and the test suite exercises
them on both APIs.

Key mappings:

| D3D11 | Vulkan |
| --- | --- |
| Draw-time state | One pipeline per shader pair, layout, blend, topology class and attachment formats (textures are single-sample), keyed by shader code and layout elements rather than object identity; everything else is dynamic state, re-emitted only when it changes |
| Shader resources | One push-descriptor set per draw; vertex bindings at their register numbers, pixel bindings offset by 256 (b# → #, s# → 16+#, t# → 32+#) |
| `DYNAMIC` and constant buffers | Host-visible slots renamed on `WRITE_DISCARD` or whole updates; earlier draws keep their storage |
| Resource state | Every image stays in `GENERAL`; one full memory barrier separates rendering, copy and present phases |
| `SV_VertexID` / `SV_InstanceID` | Base vertex and instance folded into vertex-buffer offsets, so shaders see D3D's values |
| Viewport | Negative-height viewport, giving D3D's y direction and winding |
| Present | Backbuffer copied (or blitted) into the acquired swap-chain image; FIFO with VSync, IMMEDIATE (tearing) or MAILBOX without; the Frame queue setting bounds GPU frames in flight |
| Frame statistics | `VK_EXT_present_timing` stage times, converted to `QueryPerformanceCounter` ticks (directly, or through calibrated timestamps for swap-chain-local clocks) |

Device-child objects' COM methods make no virtual calls beyond their
interface's slots, so hooks and test observers that clone a vtable to its SDK
slot count keep working.

Skin, mono, shadow and sky meshes create their own input layout and adapter
shader objects. Pipelines were keyed by those objects, so every new mesh
created new pipelines during play: 291 in the first 150 s of a first-mission
wander route. They are now keyed by the shader's SPIR-V twin (parsed once per
DXBC) and an interned copy of the layout's elements: 70 on the same route. Each
came from the driver cache (0.1-0.3 ms on average); the 4-8 ms stalls were the cache
file rewrites (below), which ran inside pipeline creation on the render thread.

Compiled pipelines persist across launches in `vulkan-pipeline-cache.bin`
beside the executable. The file is rewritten when the pipeline count doubles,
on a below-normal worker thread (pipeline caches are internally synchronized,
so rendering keeps creating pipelines meanwhile), and once more, atomically,
when the device closes. A cache
from another GPU, driver or cache UUID is ignored. `SIMPSONS_VULKAN_PIPELINE_CACHE=0`
disables it; another value names the file. Character detail draws its smoothed
meshes as R32 triangle lists, which the index-buffer path already supports.

## Shaders

Every HLSL shader is still compiled by FXC. `tools/vulkan_shader_compile.py`
wraps FXC (CMake routes all shader rules through it) and compiles the same
source and entry point again with glslang's HLSL front end into SPIR-V. It then:

- marks floating-point arithmetic `NoContraction`, matching FXC's `/Gis`;
- maps the two inputs glslang lacks (`SV_StencilRef`, render-target sample
  positions) onto SPIR-V built-ins through marker semantics;
- cross-checks the SPIR-V against FXC's DXBC reflection: every resource
  binding, every constant-buffer member offset, vertex input signatures and
  render-target outputs must agree, or the build fails;
- appends the SPIR-V to the generated FXC header, registered under the DXBC
  checksum, so `CreateVertexShader`/`CreatePixelShader` on the Vulkan device find
  it from the bytecode they are given.

Pixel-shader inputs are relinked to the vertex shader's output locations by
semantic when a pipeline is built, following D3D's linkage rules.

glslang is used because the Windows SDK's DXC has no SPIR-V code generator.
CMake finds `glslangValidator` in `$VULKAN_SDK/Bin` or the Android SDK emulator
(`%LOCALAPPDATA%/Android/Sdk/emulator/lib64/vulkan`), or takes
`-DSIMPSONS_GLSLANG=<path>`. Configure with `-DSIMPSONS_VULKAN=OFF` to build
without the Vulkan renderer; `--renderer vulkan` then fails with a clear
message, and the Video menu keeps D3D11.

Geometry shaders are not compiled to SPIR-V: the game uses none, and the test
fixtures that use them observe D3D11 stream output.

## Third-party code

`third_party/vulkan` vendors Vulkan-Headers, volk (loader) and Vulkan Memory
Allocator, each with its license; `third_party/vulkan/vulkan_origin.json`
records the upstream commits and file hashes. Nothing is downloaded at build
or run time.

## Not implemented

These are outside what the game uses. Each fails loudly instead of rendering
something different:

- geometry, hull, domain and compute shaders (their bindings are retained as
  state, as D3D11 does with no shader bound);
- stream output: targets and `SOGetTargets` round-trip, but a draw with a
  target bound throws;
- predicated rendering: predicates and their values round-trip, but
  predicated draws, clears and copies throw;
- the D3D11 debug layer and `SIMPSONS_D3D_CALL_STATS` vtable statistics, which
  are D3D11 diagnostics.

## Diagnostics

| Variable | Effect |
| --- | --- |
| `SIMPSONS_VULKAN_VALIDATION=1` | Enables `VK_LAYER_KHRONOS_validation` when it is installed |
| `SIMPSONS_VULKAN_DEVICE=<text>` | Uses the first suitable adapter whose name contains the text |
| `SIMPSONS_VULKAN_TRACE=1` | Logs each draw's state and constant-buffer contents (fixture diagnosis; very slow) |
| `SIMPSONS_VULKAN_PIPELINE_LOG=1` | Logs each created pipeline: running count, creation time, whether the driver cache supplied it (pipeline creation feedback), and its shaders |

Pipeline creation is reported to the stall profiler.

## Tests

`VulkanDeviceConformance` (`tests/test_vulkan_device.cpp`) checks the device
directly: depth and stencil, indexed strips with cut indices, negative base
vertices, deferred command lists, renamed buffers, constant-buffer offsets,
binding hazards, sampling and real game shaders. The rest of the suite runs
on Vulkan with `SIMPSONS_RENDERER=vulkan`:

```powershell
$env:SIMPSONS_RENDERER='vulkan'; ctest -j 6
```

On 2026-10-14 all 567 tests passed with each renderer (RTX 5080, driver
617.14). Probes that create their own devices create one of the selected API;
D3D11 debug-layer probes and the geometry-shader stream-output observer in
`NativeRigidMesh` run on D3D11 only.

## Measurements

Land of Chocolate first-mission start view, 1280 × 720, uncapped, background
benchmark window (`tools/benchmark_first_mission.py`, 60 s, window from 20 s),
RTX 5080, 2026-10-14. Final build, four runs alternating the renderers, all
with peak system load below 26%:

| Renderer | Average FPS | 1% low FPS | Main-thread CPU per frame |
| --- | --- | --- | --- |
| D3D11 | 153.3, 150.9 | 121.9, 124.4 | 6.08, 6.17 ms |
| Vulkan | 139.1, 133.2 | 117.6, 104.2 | 6.82, 7.04 ms |

Single runs vary by several percent with other desktop load (an earlier
Vulkan run of the same path reached 144.2 FPS with 124.2 FPS 1% lows), so
compare alternating runs rather than runs from different times.

Presented frames captured at the same moments were 99.6% bit-identical to
D3D11; the remaining pixels were Homer's idle animation and the animated burp
meter, which differ by the same amount between two D3D11 frames.

The game is CPU-bound here. Vulkan's lower average comes from recording Vulkan
commands on the game thread, where NVIDIA's D3D11 driver moves that work to its
own thread. Moving command recording to a worker thread is the next step for
Vulkan throughput.
The Im2D path's packed-target copies (a renderer design shared by both APIs)
end and restart Vulkan rendering scopes and are the largest remaining
driver cost.

Display statistics could not be observed from the background benchmark window
with either renderer (DWM reports none for a covered window), so the
present-timing conversion was verified only up to successful calibration.
Presents request display timing only while the game reads frame statistics
(the refresh-locked limiter or a pacing trace): the driver's timing query costs
about 1.5% of an uncapped frame. Results drain whenever statistics are read,
and in Present once 16 requests are outstanding. A frame is presented without
a timing request while 32 are outstanding (the driver would otherwise reject
the present), and a queue that stays full because frames never reach the
display is reset by recreating the swap chain, after 2 s and then at doubling
intervals up to 64 s.

## Live check

On 2026-10-14 a private-store run (game-local controller commands to the
disabled background window; no desktop input) opened **Video**, moved to the
Performance page, changed **Renderer** to Vulkan and accepted: the preferences
were saved as version 9 with every other field unchanged. Relaunching with
that profile logged `[BOOT] renderer=Vulkan (video settings)`, rendered the
first-mission scene and shut down normally.
