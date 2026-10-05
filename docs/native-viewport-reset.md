# Reset from private camera targets to default targets

The original mode-zero binding reset823EFDA0 now supports the three established
private viewport cameras. It restores the original default color/depth targets
and applies the conditional viewport/clipping change. Original camera ownership
and the CPU state rebuild remain intact. Startup subsequently reaches the
original textured loading quad82756480, caller82862884, selector0. This quad's
expanded additive blending remains guarded; no original screen is yet verified.

## Original transition

823EFDA0 requests color-zero from82D0CB00 through823EDB68 and depth from
82D0CAFC. The color helper calls the SDK binding only when cachedCF5C differs.
Its changed-target branch8243D230 reaches8243D198, which resets the requested
viewport from82069FA4 and requested scissor from82069FBC. The original requests
are(0,0,65535,65535,depth0..1) and(0,0,65535,65535).

8243D0F8 passes those viewport values to8243CE80. The latter limits dimensions
to the actual bound target, giving1280x720 with depth0..1 for the default pair.
It also calls8243C430: effective clipping uses the viewport bounds with scissor
disabled, or intersects those bounds with the requested rectangle if enabled.
Thus effective clipping here is(0,0,1280,720), while the requested scissor words
remain65535. These are distinct values; the native backend uses effective bounds.

A cache-hit color-zero bind does not perform this viewport reset. The forced
depth bind only has an analogous viewport effect if color-zero is null, which
is outside this qualified pair. Repeated resets therefore preserve the current
viewport; they do not always restore reversed depth or always force forward depth.

The original reset changes graphics bindings without ending the CPU camera pass.
Its target-cache stores occur before the original82400D50 rebuild. The existing
null texture helpers, scalar/sampler conversions, dirty queues and CPU stage
callbacks continue AOT. SDK device/header/command code is never executed.

## Native implementation

Before changing CPU state, the driver validates its selected native pair against
actual D3D attachments. It accepts the camera's private pair or an already
restored default pair. Expanded target interpretation on a changed binding
remains explicitly unsupported. The default-pair cache-hit behavior is retained.

CF58..CF6B are now part of the rollback snapshot. Original CPU target caches
change before the original rebuild; a pre-commit failure restores them with the
other cache windows and effective state. GPU publication restores default
attachments and, only for a changed color-zero target, full viewport/depth0..1
and effective clipping. Post-publication failures stop the driver.

The native camera record retains its original raster associations separately
from its actual bound identities. Reset changes the latter to defaults without
releasing the original active camera. Original camera reselection restores its
private pair and logical reversed depth1..0. Raster destruction still rejects
active CPU ownership. An ended private camera cannot enter the separately
qualified loading-camera presentation path merely because defaults are bound.

The backend queries actual viewport/scissor and resource bindings after mutation.
It retains other shader/resource slots, samplers, constants, buffers, fixed
states and UAV counters. No clear, copy, draw or presentation is added.
[Microsoft viewport/scissor binding](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetscissorrects).

## Verification

OriginalViewportReset passes1,427 checks: all three cameras, two camera cycles,
13 successful resets including repeated default-target cache hits, independent
pixel preservation, camera reselection, matrix copies, ABI, original reset and
deletion, expanded-target rejection and private-presentation rejection.

Three injected failures each execute two real original CPU stage callbacks after
default target-cache publication. Tests verify restored CPU ranges, scalar and
sampler state, private bindings and a successful retry. No production failure
injection or dispatch replacement is shipped.

Native graphics tests verify WARP and hardware viewport/clipping, retained state,
pixels and UAV counters. An initial new test mistakenly captured an output UAV
before its own setup cleared it; restoring that fixture binding before the
tested reset fixed the expectation. The native reset already preserved it.
The existing original driver suite still passes29,977 checks.

Full build170, actual muted boot121 and byte evidence are frozen under
`build/viewport-reset`. The 82756480 draw and its assets/state remain the next
task. General geometry, effects, depth rounding, gameplay and saves are unverified.
