# Original Im2D depth integration

Build186 implements the depth operation reached in actual154. The original UI
requests depth writing on and depth rejection off. The resulting committed SDK
state is depthEnable1, depthWrite1, compare7 (ALWAYS). Disabling the operation
would omit original behavior. The mode-zero camera retains endpoints1,0; the
Im2D screen override changes only its flag and dimensions.

`runtime/engine_driver.cpp` retains all three effective depth fields in the
owned Im2DDraw packet. It accepts only logical depth ranges0,1 and1,0, setting
reverseDepth for the latter. Enabled depth requires the actual selected depth
attachment and original working format1A220197. The common screen-state gate
checks the other state policy independently. Stencil and unqualified cull/state
still fail explicitly. Original scalar queues and cache values remain intact.

The native viewport is0,1. The shipped vertex expressions still preserve the
input clip Z and use W1. The pixel shader alone applies1-Z when requested, then
converts the result to positive20e4 with the audited reference nearest-even
rule and exports its exact float32 decoding through SV_Depth. There is no
vertex Z flip or second reversal. This output participates in real native
depth comparisons/writes against the retained attachment. Alpha discard gates
both color and depth, and stencil remains unchanged.

Original format23 selects the BE16 table entry1120 and D24FS8; plain D32 values
would introduce excess precision. The reached vertex Z3EFF7CEE reverses to
3F004189 and quantizes to codeE00831, decoded float3F004188. Both examined
reference rounding candidates agree on this reached constant. General console
rounding and varying-Z interpolation remain independently unverified. The
native renderer uses a declared reference conversion, not a console-parity
claim. Full evidence is in `native-im2d-depth-contract.md` and
`build/im2d-depth/{evidence.json,verification.json}`:2,202 original words,
23 spans,78 literal pins,6 data pins,4 SDK table rows and4 shipped shader strings.

The caller evidence in `build/im2d-upload/ui-depth186-evidence.json` contains
707 words across three original UI functions,31 literal pins,82 effective
scalar fields,20 stage-zero sampler fields,29 original stack frames and the
four original28-byte vertices captured at actual154's failure. It connects
827F5B08/827F5B20's RenderWare8/6 pair, the original quad producer and draw
callback827F5724. It does not treat the native test fixture as live gameplay.

The driver integration test replays those exact vertices through original
82409308, including native upload, original setup, queue commit, screen override
reset and return epilogue. It invokes the real RenderWare setters and checks
enable1/write1/ALWAYS, original cache/queue state, unchanged source words,
logical camera range, resource counts and submission counters. Covered pixels
must be black RGB10A2 C0000000 with exact depth3F004188 and stencil6D.

The captured bottomY is719.61865234375. The original VS subtracts0.5, leaving
the last native sample row at719.5 uncovered. The fixture preserves that
geometry: rows0..718 receive the draw; row719 retains white and cleared depth0.
This corrects the initial full-screen fixture assumption without modifying the
captured vertices or renderer. It is a native rasterization result; independent
console snapping and coverage remain unverified.

WARP and hardware each pass337,420 native backend checks, including all32
depth-state combinations in both mappings, quantized comparison ties, denormal
and normal boundaries, alpha/cull/color masks, ordered overlapping triangles,
ownership rejection and depth-off preservation. Both use the D3D11 debug layer.
Windows canonicalizes disabled depth descriptor fields (observed ALL/LESS for
supplied ZERO/NEVER); only active descriptor fields are required to match, while
the full readback matrix checks the inactive behavior. Microsoft's
[depth-stencil guidance](https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-depth-stencil)
defines disabled depth's output-merger role; descriptor canonicalization here
is a local experiment, not a claim sourced from that page.

The original-driver suite passes122,091 checks, including67 Im2D checks. The
three focused suites (native Im2D, original driver, original movie integration)
pass in7.57s. Logs are `build/im2d-upload/focused186-depth-final-tests.log`,
`focused186-depth-final-detail.log`, and `im2d-depth186-hardware.log`. Broader
build and executable results are recorded separately in STATUS and the frozen
build186 summary after validation completes.
