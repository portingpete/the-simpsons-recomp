# Original shadows camera selection and border

Boot198 completed the Land of Chocolate opening movie and rejected the first
depth/stencil clear in original82707220, return8270725C. The camera was a genuine
shadows camera, absent from the loading/viewport-only selection policy.

The original image is analysis/simpsons.pe,15466496 bytes,SHA256
6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0.
The existing shadow camera lifecycle and texture contracts remain the source
for allocation/destruction. This change enables selection, clear and CPU passes;
it does not qualify shadow geometry, shaders, depth copies or complete teardown.

## Ownership and unchanged original execution

At actual constructor827064C0's first texture request, LR82706610, both
82704C68 camera constructors have returned. The original stores have published
the cameras in O+5B4/+5B8. The native shadow texture owner captures their camera,
frame, color/depth rasters, opaque native surface IDs and weak resource leases.
Selection requires that same constructor association and both resource
generations, all three live shadow texture records, original typed vtable
8214E518, orthographic camera header/callbacks, sole root-frame attachment, and
the original1024x1024 raster pair. The driver independently validates its raster
registry, plugin metadata, native attachments and binding caches. Equal-sized
real cameras from unrelated constructors are not admitted.

Retiring the shadow textures revokes this association. Existing normal shadows
cleanup still leaves its four raster records and CPU extensions, as previously
documented; this change does not disguise that separate cleanup gap.

Original82707220 chooses O+5B4 for selector0, O+5B8 otherwise. Its clear call at
82707258 passes camera,82D6C09C,selector6. The retained mask table maps6 to30:
depth and stencil only. The existing actual native full-resource clear preserves
color, uses depth0 and the original stencil byte. Camera begin/end, frame and
matrix work stay in original AOT functions. Scissor-enabled clears remain
unqualified; this pass issues its clear before enabling the border.

## Scissor and raw bias requests

Application selector29 maps to SDK scalarC8. Original8243D0E8 stores the enable
word and tail-calls8243C430. The state service now retains Boolean0/1. Existing
screen/movie draws continue to reject enabled scissor.

At827072A4 the original stack rectangle is{1,1,1023,1023}, r3 is the existing
native manager context. The new byte-pinned call-site adapter validates the
real E0-byte frame, saved state-call LR82707264/82707280, original selector and
constructor owner, manager/context, selected camera/attachments and enabled
state. It replaces just that call and continues at827072A8. The global SDK
8243C430 remains guarded before its device+3160 load.

In the SDK's enabled path the rectangle intersects the full1024 viewport;
the result is unchanged,[1,1023) in both axes. NativeBackend sets that actual
D3D11 rectangle and reads it back. Rectangle ownership is distinct from a
draw's rasterizer-state enable, which remains unqualified for shadow geometry.
No viewport depth sorting, clear or draw is implicit in this operation.

The first test exposed a wrong check against lastFunction=82707220: the retained
state-call epilogue leaves lastFunction82A3C418. The corrected adapter checks
the genuine return LR and frame instead. No original return/stack data changed.

Application selectors2A/2B carry raw IEEE-754 depth/slope bias requests. The
state owner now accepts finite values and retains their exact bits. Original
shadow defaults include BA83126F and B9D1B717/BB449BA6. SDK8243AAA0 scales the
slope by16;8243AB68 consumes constant offset directly. The September27 immediate
effect audit corrected the previous reversed labels by tracing SDK2A50..2A5C
through upload8244C1B0 to GPU2380..2383. No native rasterizer equivalence
is claimed: every existing screen/movie drawing profile still requires zero
bias. Non-finite requests reject before state mutation.

## Validation and next boundary

OriginalShadowCameraPass uses actual25-effect registration and finalization,
then both real cameras. Four begin/end cycles, full and depth/stencil-only
clears, actual pixel readback, unrelated-camera/altered-publication/raster
rejection and original texture-owner retirement pass. Both genuine82707220
selector branches issue the native border, retain their original bias values,
and reach the still-guarded shadows FX activation at826B5FC0, LR8270614C,
source820C0550. The fixture has no shadow casters and does not represent a
complete game frame. Its intentionally interrupted CPU pass is ended through
the original camera API; captured state is restored only for test cleanup.

build/shadow-parent-tests.log passes3/3 tests in0.95s. Native rectangle
set/query and invalid-rectangle atomicity passed on WARP and hardware;
build/shadow-border-hardware-tests.log reports158 checks. The complete build
passed120/120 tests in230.09s, recorded in
build/shadow-camera-integration-build.log. Boot199 is running that binary.

## Boot199 and the world matrix setter

Boot199's movie finished and the live character shadow cameraE2CA7550 cleared
and set its border. Real casters then reached827055E0 before the empty fixture's
FX activation. That wrapper obtains the object's original frame matrix through
823F2540, copies its twelve coordinate words to its own A0-byte stack frame,
and appends homogeneous lanes0,0,0,1. At8270568C it calls82704600 with native
FX id00500005, handle O+660 and matrix SP+50. Original82704614 read id+10C,
causing the observed unmapped00500111 failure. No first3D/menu frame appeared.

The new82704600 entry adapter admits only this caller, LR82705690, its actual
frame and reflected typed shadows owner. Original g_World is handle000C0004,
descriptor{004007B0,00100005}, four rows/four columns at private byte80.
The original VMX merge sequence transposes the four source rows. Its vsel mask
for four columns,82000EC0..CF, is all ones, so it changes no components. The
adapter performs this exact bit transpose into native-owned private parameter
storage and sets original leaf2's dirty bit20. All other parameter words, the
shared pool and original input assembly remain unchanged. No SDK-shaped object,
shader bind, upload, draw or invented success result is supplied.

The extended original camera fixture invokes827055E0 twice on a genuinely
constructed frame-bearing object, using distinct positive and negative matrix
components. It checks all1116 private words, the complete modification mask,
shared metadata/values, source preservation and nonvolatile ABI. Another valid
matrix handle and a foreign caller reject without changing stored parameters.
build/shadow-world-tests.log passes OriginalShadowCameraPass and
OriginalEffectFinalizers,2/2 in1.36s. Full build is running in
build/shadow-world-integration-build.log. No Boot200 has been launched yet.
The complete build passed120/120 tests in230.75s. Boot200 is now running
the verified matrix fix for live validation.

Boot200 subsequently completed the movie and passed both the live shadow border
and g_World setter. It stopped at the next unqualified effect technique,
before shader binding. The rejection now prints the original request registers
without dereferencing them or changing its acceptance policy. Boot201 lost the
actual audio device on SavedGames; Boot202 and the targeted regression now fail
at real XAudio2 mastering-voice creation with80070490. The diagnostic-only
change builds, but its targeted runtime validation is pending restoration of a
real audio endpoint. The previous complete120-test result remains the baseline.
