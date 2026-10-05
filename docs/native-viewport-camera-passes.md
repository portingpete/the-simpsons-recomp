# Native viewport camera passes

The three viewport cameras created by the original manager now select their
own native color/depth attachments. The original loading camera continues to
select the default pair. Target binding, clear operations and cache publication
follow the actual raster roles; original camera begin/end, frame updates,
matrix-pool copies and CPU ownership stores remain AOT code.

Actual boot115 identified cameraE4D41AB0, distinct from loading cameraE2CA7010.
Its stack proves the renderer path `82867D60 ->826B8068 ->826B7E58` and original
camera begin at826B7E6C. The outer renderer collects viewport cameras through
8269CF78, temporarily changes active global82D61D50 and restores it after each
pass. The matching end is826B805C. The shared validation diagnostic now records
unqualified camera identities and the original stack without changing behavior.

Original selection823EE6C8 tests the root color raster's type atR+20.
Type5 takes its native color identity from the raster extension and its depth
identity from the depth raster extension. The other established type2 branch
selects default CB00/CAFC roles. The viewport uses root offsets1C/1E, widthC,
height10 and logical reversed depth1->0. Viewport root offsets are zero:
the right-half camera renders into its own640x720 resource, not at x640 inside
that resource. The manager's screen-space rectangles are used elsewhere.

The native implementation qualifies the live manager at82D08B10, both original
vtables, its four-slot capacity and its three established camera slots. Slot0
is1280x720; slots1/2 are640x720. Their type5 color and type1 depth roots must
retain their original registry/list ownership, metadata, dimensions and live
owned native target identities. Private depth is required and must match the
color extent. Missing/unknown cameras and the fourth slot remain unqualified.

Binding accepts only the previous native selection or an original zero cache
entry, and rejects extra/foreign attachments. D3D11 binding is verified by the
backend's attachment query. Clear uses the selected pair and the existing
verified endpoint RGBA, depth0 and byte stencil contract. It updates no
original begin/end ownership fields. A device submission failure is terminal.
Logical depth1->0 is preserved as metadata; no invalid reversed D3D11 viewport
is issued, and no draw or depth-mapping correctness is claimed here.

The141-check original-pass fixture creates all three cameras through the real
manager factory and slot constructors after actual startup. It seeds distinct
GPU colors/stencils in each private pair and in the defaults. It runs original
begin/end twice for each camera, verifies nonvolatile integer/floating-point
ABI, original view/projection matrix-pool copies and ownership fields, then
reads every pixel to verify separation. It tests malformed ownership and
missing depth before GPU mutation. Active or still-bound target destruction
rejects; switching to the loading camera restores default attachments, after
which original manager reset/deletion releases all private owners and stale
identities reject. Prior allocation/projection/rectangle/lifetime suites remain
separate and unchanged.

Actual startup advances beyond camera selection to the guarded graphics helper
826B08B0, caller826B1068. Its operation, inputs, output resources and lifetime
must be verified before implementing it. No original draw, screen, menu, world,
gameplay, progression or save/load is verified. Shadow/reflection passes,
additional attachments, subrasters, scissor and general clear color conversion
remain outside the qualified camera path.

Original spans, live stack and all181 existing hook ranges are pinned by
`build/camera-selection/evidence.py`. No new hooks were added and original
game/reference files remain unchanged.
