# Native owned raster binding

The original RenderWare selector1 dispatcher and helper82401940 now accept an
owned copied ITXD raster, in addition to existing native raster owners and null.
The CPU dispatcher, texture-alpha cache, deferred3C/60 writes, dirty queue,
stage cache and Boolean result execute as original AOT code. The bounded source
pins are in `build/im2d-upload/rw-texture187-evidence.json`; copied allocation,
index and release ownership is specified in `native-itxd-integration.md`.

Before the first original cache write, the bridge validates the input and
displaced owners, plugin bounds, Boolean alpha flags, relevant queue membership
and capacity, writable state and stack. Unsupported auxiliary textures reject.
A nonnull cache hit requires the matching actual native PS view: a CPU cache
value alone cannot manufacture a successful graphics bind.

The checked824408E0 adapter permits the original nonnull caller82401AA0. It
requires the original zero console-device field, stage0..7, exact64-bit mask and
already-published raster cache. The identity must equal the owned raster's X+0.
For copied ITXD this is still the original embedded CPU header. It is never
interpreted as a native GPU ID. The typed registry supplies the actual texture.

`NativeBackend::bindEngineTexture` validates the device, resource and complete
SRV interval, then sets exactly one PS slot. The accepted backing has only
SHADER_RESOURCE bind flags, so the setter cannot silently remove an output
alias. The postcondition is queried from the actual context. Samplers, other
slots, targets, contents, draw counts and presentation counts survive. Native
publication failure is terminal; no successful CPU rollback is claimed.

Im2D resolves the stage-zero raster through this shared ownership lookup. Its
shader, sampling, alpha/blend and depth contracts remain separately checked.
The copied font profile uploads one linear BC2 level after the independently
tested original storage conversion. Native mip coverage is0..0; original
sampler requests remain retained and are not overwritten by binding.

The source fixtures exercise original dispatcher/helper calls for all eight
stages, alpha/vertex-alpha/retained-alpha combinations, cache hits, unchanged
references, absent-native-view rejection and queue-full rejection. Native
context snapshots exercise single-slot binding, repeated calls, foreign
devices/threads, invalid metadata and preserved resource contents. Execution
results: build187 passed all96 suites in163.30 seconds; the original driver
fixture passed123,175 checks. Native binding/resource checks also passed on
hardware. After the BC2 filter-alpha oracle correction, the focused Im2D test
passed359,956 checks on each of WARP and hardware. Production shaders did not
change for that correction. The actual game outcome is recorded separately.
