# Original quad declaration ownership

The quad typed constructor `826B6B68` allocates a0x118-byte original object,
runs base constructor `826B4F60`, writes vtable `820B7170`, then creates two
declarations. Its SDK factory is a CPU metadata builder, not an input-layout
or draw service. The native adapter owns those exact declarations using the
existing `DeclarationRegistry`; no SDK declaration header is fabricated.

The call at `826B6BCC` takes the24 bytes at `820B71C4`, stores the result at
O+A8 and describes one stream0 float2 position at offset0. The call at
`826B6BE0` takes the36 bytes at `820B71A0`, stores O+AC and describes float2
position at0 and float2 UV at8. Each includes its entire12-byte terminator.
Their SHA256 values are respectively
`29f7798c5da22917cf343d338cc4ae42b9c136ae61e081240976ba422b22dc6f`
and `0433df1ef161551bfd46cb5edf8e33da819182bb79db90bd1ee610ffe757f473`.
The minimum byte extents are8 and16; these values alone do not prove draw
stride, native shader compatibility, coordinate conversion or input layout.

Build151's whole-factory hook also intercepted an earlier unrelated startup
caller, causing eight original-lifecycle regressions. Build152 narrows the
adapter to the two BL instructions above. Before those calls LR is respectively
`826B6BB8` and `826B6BD0`; the adapter preserves the skipped BL effect by
setting LR to `826B6BD0` and `826B6BE4`. The original next instructions publish
the fields. Other users of the original CPU declaration builder remain intact.
Build152 passes all57 suites, including18,441 checks in the first25-effect
two-cycle fixture. Actual muted boot094 also creates both declarations and
continues through the first25 registrations before the second catalog's
unqualified source82006348. No declaration binding or quad draw is claimed.

The owner validates its runtime/thread/context, original typed vtable, source
bytes, source/call pairing, creation order and original field publication.
It allocates distinct opaque identities from the driver's existing monotonically
allocated target namespace. Original destructor `826B4CD8` releases O+A8 at
`826B4D0C` and O+AC at `826B4D20`; the native resource-release dispatcher
routes only known declaration IDs to this owner. Return0 represents final
release of the established single logical reference. The original caller
clears the fields. Unknown IDs fail; none is allowed into SDK resource code.

The registry may cache immutable CPU record bytes after logical release, as
documented in the existing declaration registry contract. A later creation
gets a fresh generation and opaque identity. Cached record storage is not a
live declaration reference. Driver stop requires all logical owners released;
terminal cleanup reports incomplete original ownership when necessary.

The complete first25-effect lifecycle fixture checks two native declaration
owners, exact owned original bytes, element counts/extents, fresh IDs across
cycles and rejection after the original paired cleanup. It also checks the
separate three shadow textures and actual border pixels. Shader binding,
quad drawing and complete shadow-camera teardown remain unqualified.

Independent source evidence contains seven original functions and331 decoded
instruction words, each matched to the pinned flat image and original `.pdata`
extent: constructor, deleting destructor, destructor, SDK declaration create
and initialization, generic release and destruction. See
[original-evidence.json](../build/quad-declarations/original-evidence.json)
and [original.txt](../build/quad-declarations/original.txt). Generated code was
navigation only. The original/reference files were unchanged.
