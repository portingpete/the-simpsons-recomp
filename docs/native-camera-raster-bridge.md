# Native camera raster bridge

Build062 / boot035 create the original startup color type 2 and shared depth
type 1, both 1280x720. They retain original wrapper allocations, plugin callbacks,
CPU format normalization and original pool list nodes. No draw or camera-begin
callback is enabled. The executable next rejects loading texture flags 384.

`runtime/engine_rasters.cpp` owns bounded native associations backed by
the existing driver targets. Original type 2 leaves extension +0 zero; type 1
publishes the checked driver depth identity and consumes the original shared
flag. Destroy does not rearm that flag or release the shared driver backing.
Other types, private depth, resized/child rasters and bound-stage destruction
remain explicit unsupported paths. No console GPU objects or commands exist.

The byte evidence is in native-camera-rasters.md and its frozen analyzer. Live
validation checks the dynamic plugin offset and full 3C-byte doubly linked
registry, owner and tail. Raster records remember their allocation extent.
A new full-context hook at original wrapper 82407DC0 validates before plugin
destruction, then retains every original instruction and allocator free. Native
platform callbacks at 823F7070/823F62A0 preserve the original Boolean ABI.

Host allocations and checked memory precede original mutations. A failed
original list allocation restores this transaction's exact raster fields and
shared flag, preserving the original format helper's independent scratch writes.
A cleanup failure is explicitly terminal, not reported as successful rollback.
Driver teardown rejects live associations. Fatal process cleanup releases host
backing and reports incomplete original application cleanup.

All 22 CTest suites pass. The actual original driver fixture has 277 checks,
including original camera/frame/raster destruction, additional original wrapper
allocation/free, exact untouched bytes, all eight stage guards before plugin
callbacks, failed CPU list allocation rollback, corrupt registry rejection,
dynamic extension relocation, stale destruction and early driver stop.
The fixture changes only its own mapped memory for fault injection and initial
shared-depth test conditions. It does not claim application restart or shutdown.

The actual muted executable was run separately in boot034 and boot035. Both
created the original two rasters and stopped explicitly at flags 384 during
loading artwork setup. No original game pixels, menus or gameplay are verified.
