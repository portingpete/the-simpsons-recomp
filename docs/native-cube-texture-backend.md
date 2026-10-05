# Native reflection cube storage

The D3D11 backend now owns six-face RGB10A2 cube textures for the two original
reflection profiles,16 and256 texels per face. It allocates one mip with
`R10G10B10A2_UNORM`, DEFAULT usage, shader-resource/render-target bindings and
the TEXTURECUBE resource flag. The sampled view explicitly covers that cube's
one mip. Creation supplies no initial data and performs no implicit clear.

`writeCubeRows` accepts tightly packed, full-width rows of one face. It validates
the owner thread, immediate context, device identity, actual resource descriptor,
sampled-view resource and descriptor, face/range and exact byte count before
submission. Its UpdateSubresource box selects only those rows. The input buffer
may be changed or released on return; GPU completion is not implied. Single-face
staging readback normalizes the actual mapped row pitch without changing bindings.

These choices follow the native API contracts for
[UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource),
[texture descriptors](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_texture2d_desc)
and the [cube resource flag](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag).
Only the immediate context is used, avoiding the documented deferred-context
destination-box source-offset caveat.

The WARP and hardware regressions each pass618 checks. They seed all faces with
asymmetric packed test values before any pixel read. Then they apply the original
face order0,1,4,5,2,3 and the qualified clear coverage: top8 rows for16, all256
rows for256. Complete face comparisons verify untouched lower rows and other
faces after every write. Further updates cover the last row, interior rows and
the first row. Caller buffers are overwritten/freed before readback. Invalid
dimensions, row/face ranges, lengths, devices, threads, metadata and mismatched
views reject without changing the seeded resources. Same-resource array views
cannot substitute for cube views. Resource ownership survives the backend
wrapper and expires with the last shared owner.

This establishes native storage and byte-preserving partial uploads. It does
not establish original face orientation, shader sampling, forced sampled alpha
one, expanded filtering precision, original rendered pixels or gameplay.
Original constructor8273C2B8 now executes with native context/resource ownership
and its original lock/clear/unlock loop. Paired resource cleanup is tested;
see `native-reflection-texture-lifecycle.md` for the284-check integration and its
remaining direct camera attachment/global teardown limits. Original size16/256
camera/raster construction and isolated full cleanup are separately tested;
see `native-reflection-camera-profile.md`.

Source: `renderer/cube_texture.cpp`, declarations in `renderer/native_backend.h`,
regression `tests/test_cube_texture.cpp`. Run CTest `NativeCubeTextureWARP` and
`NativeCubeTextureHardware`. Focused evidence is retained under
`build/reflection-cubemap/gpu-focused159-tests.log`. Original CPU geometry and
the partial-clear derivation are documented in `native-reflection-cubemap-layout.md`.
