# Checked native engine entry hooks

The offline generator accepts `ctx` and `base` arguments in `midasm_hook`.
Declarations use `PPCContext&` and `uint8_t*`; calls pass the original objects.
Full-context hooks reject localized register configurations and skipped LR/MSR
state. Duplicate hook addresses and empty arguments fail configuration loading.
Following a hook, the emitter invalidates its host FP-mode assumption so that
the next original floating-point operation establishes the required mode.

`config/simpsons.toml` installs the first such hook at the verified original
platform-driver dispatcher `823F0630`, before its prologue. The original six
entry words are checked during generation through `evidence_hex`, in addition
to the complete image hash. Generated declarations and calls are produced by
the generator; generated C++ is never manually patched.

`runtime/engine_driver.cpp` observes actual requests and the live engine plugin
registry. Boot023 verified its initial observation-only version against the
unchanged GPU failure. Request 2 now enters native resource allocation and
window attachment, then fails explicitly before publishing driver readiness.
Other requests still continue the original CPU dispatcher. No console device
object is substituted, and no original engine plugin constructor is skipped.

Boot024 created the eleven required backing resources at the game's 1280x720
dimensions and attached the swap chain on the hardware adapter (feature level
11.1). The console device pointer and started flag remained zero, lifecycle
remained 2, and native ownership unwound at the explicit incomplete-service
failure. No draw/presentation or original game frame occurred.

Boot025 additionally calls the original CPU binding-pool, pipeline-cache
and raster-pool helpers through a checked ABI caller frame. It keeps original
memory/allocator effects while isolating call-clobbered registers in a temporary
context. The completed stages unwind before the still-incomplete mixed native
state/resource initialization is reported. Both original pools were observed
nonzero and then released by their original CPU teardown paths; the run stops
explicitly before mixed scratch-resource initialization at `82409A90`.
Unexpected original-helper failures
remain terminal diagnostics; reusable driver restart is not yet implemented.

Boot026 additionally retains the original scratch initializer `82409A90` and
cleanup `82408E30`. Only the index allocation/release callsites and declaration
create/release engine services are native hooks. Real buffer ownership and the
complete original 12-byte declaration records are preserved; both original
guest fields return to zero on cleanup. The next explicit failure is mixed
render-state initialization `824008E0`. No driver-ready result is returned.

Boot027 replaces that mixed initializer and its commit/sampler engine services,
preserving the CPU caches and original pipeline records. Boot028 adds the native
dynamic-buffer owner, whose four real pools and linked records are allocated and
released through original CPU functions. All completed stages unwind before the
explicit stop at persistent driver ownership/target publication. Separate shader
creation/release entry hooks now pass original-ABI resource tests; actual plugin
construction remains unreached. See native-state-bridge.md and
native-material-bridge.md for contracts and limits.

Boot030/031 give the native resource services a persistent driver lifetime and
allow the original plugin/gamma/start walk to finish. Request 3 pairs with the
original destructor walk and request 8 queries real native ownership. Fourteen
additional original-byte-checked guards reject unported post-start integration,
camera/raster and immediate-render callbacks before SDK access. Two original
start/stop/close cycles and partial-start rollback pass the actual AOT lifecycle
fixture. See native-driver-lifecycle.md for evidence and remaining limits.

Nine generator tests include compiled full-context hooks, retained scalar and
vector mutations, conditional fallthrough/return, after-instruction ordering,
propagated failures and actual parser rejection of all unsafe localization
configurations. The registry observer is additionally checked by running the
original executable, not merely a test fixture.

Boot032/033 add original binding reset, checked native context identity and
native submission registration while retaining both original CPU allocations.
Boot034/035 create the original camera/shared-depth rasters through native
platform callbacks and original CPU allocation/plugin/list services. Build062
passes 22 suites. Camera begin, target/viewport integration, texture loading and
draws remain unimplemented. See native-camera-raster-bridge.md.
