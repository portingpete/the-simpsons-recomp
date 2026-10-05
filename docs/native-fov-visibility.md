# FOV and original scene visibility

The selected native FOV now applies before the original camera frustum rebuild
at `823F1790`, as well as before camera begin at `823F1870`. Objects admitted by
the wider render projection are therefore considered by the earlier scene
visibility pass.

Original helper `827258B8` calls `823F17F8` at `827258D8`. That wrapper tail-calls
`823F1790`, which rebuilds the original perspective planes and corners. The helper
then calls `826AE9D0` at `827258E8`. This copies the camera's six planes into the
scene's visibility record at scene `+6384`; original object tests subsequently
consume that record. Applying FOV only at camera begin changed the later render
projection after this visibility record had already been built from stock FOV.

The additional entry hook calls the same camera qualification and source-tracking
code as the existing begin hook. It accepts only the main full-size perspective
scene camera with the registered callback profile and qualified raster pair.
Private reflection/shadow cameras, half-width cameras and square UI windows keep
their own projection. The original view-window setter `823F1C98` retains authored
zoom, recalculates reciprocals and marks the frame dirty; the original frustum
body still calculates every plane, corner and bounding box. Culling stays enabled,
including near/far rejection.

The setter does not invoke the frustum callback recursively. Its dirty-frame helper
`823F2460` inserts the root frame only when its dirty bits are clear. When the new
hook runs within the original `8240D5C0` dirty-list callback, that frame is already
dirty and is not inserted again. The original synchronization loop retires it.

`OriginalUltrawideCamera` now additionally exercises the generated hook with a real
renderer owner supplying frozen aspect. Its hidden background window receives no
input. No game loop or GPU draw runs. The fixture calls original `827258B8` and the
original six-plane AABB classifier `8270E490` against the copied visibility record.
It first runs the original reciprocal-square-root table initializer `823EBB20`,
with a bounded allocator supplying storage, and checks `823EBF68(4) == 0.5`.
This preserves the original normalized side-plane math rather than supplying
test-generated planes.
It checks stock rejection of objects beyond all four side/top edges, admission at
80/110-degree FOV before camera begin, repeated sync without accumulated zoom,
exact Original restoration, and continued rejection outside the selected view and
near/far depth range. It also checks that a pending render-resolution preference
does not change the live aspect, dirty-list callback retirement and hook ABI.

```powershell
cmake --build build/native --target UltrawideCameraTests --parallel 8
ctest --test-dir build/native -R '^OriginalUltrawideCamera$' --output-on-failure
```

Source recovery copies are under
`build/mouse-fov-fix-20261004/before-fov`. Native and release focused tests pass.
The private live mouse test selects and saves FOV110, resizes the client window,
and resumes the wider Chocolate Land scene after two Pause entries. Completed
scene captures were viewed; primary saves and preferences remain unchanged.
See the [final verification receipt](../build/mouse-fov-fix-20261004/final-verification.json).
Full mission traversal remains separate from these original CPU visibility fixtures.
