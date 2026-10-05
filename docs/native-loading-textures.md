# Native embedded loading textures

Build066 passes 23 suites. The actual executable in boot038 constructs the
original startup camera, reads both original embedded frame1/frame2 textures,
creates real immutable D3D11 BC3 resources and completes the original loading
dictionary path. It stops explicitly at application scalar-state dispatcher
82723D80 (caller 8272470C, selector 6, value 0, force 1). No frame is rendered.

## Engine boundary and ownership

`runtime/engine_loading_textures.cpp` replaces engine stream callback 8240A278.
Original 823FF7A8 dictionary parsing, extension reads, insertion and outer loader
remain AOT code. The callback preserves original stream find/read helpers,
scratch payload writes, raster and texture constructors, name/mask setters and
sampler 1102. The exact 20150-byte embedded dictionary is hash-checked with its
chunk bounds and extensions before allocation. This bounded profile rejects
other sources, plugin registry shapes, mips and formats explicitly.

`runtime/engine_rasters.cpp` now owns camera/shared-depth associations and this
type-4 texture profile. Flags 384 run original CPU format normalization, create
no surface and allocate no camera list node. Only after a real BC3 upload does
attachment validate the resource's actual device/immutable descriptor, publish
a checked unmapped monotonic identity and retain native ownership. The original
post-unlock dimension/stride/pixel/lock-level fields remain correct. Unsupported
temporary console surface/pitch/pixel slots are unavailable/zero; original lock
and unlock wrappers fail explicitly before SDK consumption. There is no console
texture header, tiling engine, command stream or shader interpreter.

The original texture object starts with refcount 1; the dictionary inserts it
without incrementing, and borrowed name lookups preserve the reference count.
Original EA2F construction and stream reads execute. Destruction retains the
original dictionary, texture, plugin, raster and allocator wrappers. Preflight
checks native ownership and all eight texture stages before the raster plugin
destructors. Bound-stage teardown remains unsupported until native unbinding
and its original CPU state effects are implemented. Queued native work can hold
its own shared texture reference without prolonging the guest wrapper lifetime.

## Verification and limits

The 315-check actual-original driver fixture includes both complete loading
textures, exact GPU block readback, names/samplers/reference counts, raw EA2F
payloads, borrowed lookups, native identity validation and both lock guards.
Original loading dictionary destruction clears its three globals and returns
the native owner to the two camera associations. Separately held native texture
references remain live, then expire on their final release.

Malformed second-texture extension bytes and wrong enclosing length fail before
allocation, output publication or stream movement. A real upload on a foreign
D3D11 device fails attachment after original raster construction; the callback
preserves actual stream advancement and rolls back that raster via original
cleanup. A detached callback result leaves extensions to its caller and releases
through original texture final destruction. CPU constructor interruption and
rollback failure remain explicit terminal states; arbitrary OOM and malformed
future plugins are not claimed failure-complete.

The frozen original-byte specification is native-texture-bridge.md with its
46-function analyzer and four self-tests. Build063 caught a mistyped embedded
hash constant; build064 corrected it against the original flat image. Boot037
exposed another application consumer of the opaque context identity. Build065
added the byte-pinned 82723D80 guard; build066 adds further rollback checks.
Full application shutdown/restart, texture binding, native game draws and general
asset textures remain unverified.
