# Original viewport camera ownership

The native raster service now supports the original viewport manager's 1280×720
and 640×720 offscreen cameras. This qualifies allocation, original CPU metadata,
projection and paired resource ownership. It does not qualify drawing, initial
pixels, original screens or gameplay.

## Original request and implementation

Actual muted boot108 stopped at raster callback823F7070 with flags5 and a
1280×720 request. A separate run against that same runtime recorded the original
stack: public allocator82408130 → helper827142D8 → slot constructor8269D608 →
startup823B6598. Its viewport manager was E1AB46B8 with four available slots.

Startup823B6598 uses the original three-row table82CD12CC: full viewport,
left half and right half. Slot constructor8269D608 obtains the original source
camera through826B7B50/global82D61D50, computes pixel rectangles using original
single-precision arithmetic and truncation, and calls827142D8 with private color
enabled. The helper allocates one color raster with flags5 and one depth raster
with flags1, creates an identity frame, attaches the camera, sets perspective
projection, and allocates21 original CPU state rows.

Native EngineRasters previously assumed every private camera was square. It now
reads both original dimensions and admits only the previously verified square
profiles and these two rectangular profiles. Both dimensions are passed to
native RGB10A2 color and native depth/stencil allocation. Unknown dimensions
still fail explicitly. Original raster allocation, format normalization,
reset-list insertion/removal and frame/state management remain compiled original
CPU code. The original private-depth placement helper823ED930 is still executed:
1280×720 produces0x2D0;640×720 produces0x168. These scalars remain console
metadata and are never treated as native allocation addresses.

The original near/far constants are1 and5000. Projection helper82A3C710 takes
the stored double0.5235987901687622 and produces bits3FE279A74FFE43EB; its
single-precision result is3F13CD3A. Original code divides that horizontal window
by the single-precision width/height ratio to obtain the vertical window.
It retains zero view offsets and computes reciprocal windows in823F1C98.
No host trigonometric approximation replaces these instructions.

## Paired original lifetime

Factory8269D788 allocates a320-byte manager and calls8269CC50. The constructor
owns four CPU camera-controller objects, two message subscriptions and four
camera slots. The new fixture calls the real factory and original slot
constructor for all three startup table rows; it supplies no replacement
allocator, fake owner or patched dispatch table.

Reset8269C438 calls82714220 for each occupied camera and auxiliary slot, then
clears each retired slot. Cleanup82714220 detaches/deletes the root frame,
destroys both rasters, deletes CPU state rows and destroys the camera.
The native raster callbacks release their owned surfaces and remove their
original reset-list nodes. Finally, the fixture exercises virtual deleting
entry8269CC28 →8269D7B8 →8269CEB0, including the original controller-array and
subscription cleanup.

The viewport regression covers three concurrent cameras, two construction/reset
cycles, actual original camera allocation reuse, distinct native storage,
expired backing and stale-ID rejection. It checks original pixel, normalized
and clip-space rectangles, camera/frame/state metadata, raster list order,
native copy/map extents, nonvolatile integer/floating-point ABI preservation,
and preservation of the source camera, sampling stages and target attachments.
An empty repeated reset must leave all manager bytes unchanged. Initial GPU
contents are unspecified; the test does not compare them to invented pixels.

The existing driver contract fixture now rejects eight unqualified sizes for
both private color and private depth, checking that failed requests leave bytes,
native ownership and the reset list unchanged. Its previous1280×720 rejection
expectation became obsolete when this verified viewport profile was added.

## Reproduction and remaining work

Build with `.\tools\build.ps1 -Jobs 8`. Run just the new regression with
`ctest --test-dir build/native -R OriginalViewportCameraLifecycle --output-on-failure`.
All launches are muted. Evidence is in `build/rectangular-camera/evidence.json`
and `original-chain.txt`; rerun `python -B build/rectangular-camera/evidence.py`
to verify27 spans/1467 original instruction words and all181 hook byte ranges.

No new hooks or rendering paths are introduced. Viewport frame children and
parented-frame cleanup remain outside this fixture. The earlier shadow and
reflection owners use different teardown paths; their documented cleanup gaps
are unchanged. The current actual startup boundary and checkpoint are recorded
in local development logs.
