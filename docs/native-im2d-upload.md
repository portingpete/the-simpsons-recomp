# Original Im2D vertex upload

The original triangle-strip path82409308 now executes its CPU allocation and
copy prefix. Its native boundary owns real upload staging and the selected
D3D11 vertex buffer. The next function82408CC0 remains explicitly guarded
before fixed-function shader preparation or drawing. There is no draw return,
final cursor increment, frame or menu claim from this milestone.

The selected scope has3..9362 vertices,28 bytes each, an active zero-offset
camera raster and four native0x40000-byte dynamic buffers created by the
existing startup owner. The entire original allocator823FC848 runs as AOT:
stride alignment, current-slot selection, ascending search of other buffers,
ring wrap/discard, and all original cursor/output publications. Resource list,
record and identity ownership are checked before entering it. Single-buffer
capability, other primitives, subrasters and oversized growth still reject.

Two original lock callsites return real CPU-visible physical staging. The
original allocator adds its selected offset; the original memcpy then copies
the caller's bytes. The original Im2D unlock callsite uploads exactly that
range through UpdateSubresource, which captures caller memory before return.
The staging allocation is then freed. Partial updates preserve earlier native
buffer contents without relying on guest fences or D3D11 NO_OVERWRITE behavior.
An interrupted upload retains its explicit ownership and terminal destruction
attempts to release staging while reporting incomplete original work.

Uploaded bytes retain the original big-endian XYZRHW/color/UV layout. This is
storage, not a qualified D3D11 input layout: native vertex fetch/endian conversion
and fixed-function shader calculations still require their own implementation.
The original fixed-function route is not the previously ported Screen_Xenon
material shader.82408CC0 binds the Im2D declaration, clears explicit VS/PS,
sets stage/scalar state and commits.823F4B60 then requests pixel shader8240F028
and vertex shader82410A68, and submits the draw. Those later effects remain
unimplemented and cannot be omitted because the first vertices are degenerate.

The real AOT regression fixture checks every modulo28 alignment residue on all
four buffers, ring exhaustion, reuse, the maximum vertex count, exact native GPU
readback bytes and unchanged neighboring ranges. It also checks staging release,
original publications, rejected malformed requests/owners, unchanged state,
draw/presentation counters and color/depth pixels. It deliberately catches the
actual guard at82408CC0; it does not replace that function or invent its result.
Original full draw epilogue behavior remains beyond the guard.

Evidence: `build/im2d-upload/evidence.json` and `original-chain.txt` pin426
original words,11 reviewed calls and all189 hook ranges. Actual muted boot132
uploads112 bytes into original slot3/nativebuffer00D00004, then fails at82408CC0
with caller8240950C. Full build and final actual-run results are recorded in the
checkpoint summary. Original game and reference files remain unchanged.
