# Rendering bug audit after the Ball Homer J freeze

The first repair in [ball-homer-j-freeze.md](ball-homer-j-freeze.md) covered the
Ball sprite and five-stage distortion graph. This audit followed the same
failure classes through the neighboring native rendering paths.

The original mesh declaration cache creates CPU SDK objects in the game's
allocator. Its release enters the shared graphics-resource hook, which used to
interpret an original mapped declaration pointer as a native shadow-texture ID.
The hook now recognizes only the declaration owned by the current original
vector iteration and lets the original atomic reference decrement run. The
subsequent type-five destructor uses the game's original allocator free and
epilogue. A second guarded continuation skips only a console device import
that the CPU-only destructor never uses. The original lifecycle fixture creates
primary and alternate declarations, checks invalid-owner rejection, and follows
their actual cleanup and reconstruction.

Ball, immediate, particle, post-filter, shadow and other mesh draws, plus
screen, edge and corona draws, now check the full D3D11 output-UAV slot range
supported by the active feature level before submitting. This prevents a
retained high-slot UAV from silently changing an effect draw. Target queries
retain their read-only meaning; the draw entrypoints perform the check.

Trail and billboard batches use the direct packed blend equation even when the
retained scalar blend flag is zero. Their original immediate texture setup
selects point mip filtering. Original-code bridge checks render two additive
quads with the scalar flag zero; a separate GPU test distinguishes point from
linear mip selection using different colors in each mip level.

The Ball sprite and distortion rectangles each reserve a 4096-byte guest CPU
staging range. Both ranges are now retired during normal driver cleanup and
terminal driver destruction. The original-code screen bridge identifies both
allocations and checks retirement before the runtime releases its address
space. The distortion viewport texture slots are also checked across retirement
and recreation, including the shared slot-seven backup owner.

The final native executables were rebuilt after AOT verification of all 311
generated files with zero semantic diagnostics. All 22 focused CTest cases
pass, including hardware and WARP GPU checks, original mesh cleanup, viewport
and ITXD lifecycles, and the original Ball screen bridge. The build log, test
log, and executable hashes are under `build/ball-homer-freeze`.

A final bounded playback reached scene 5620, continued through recorded input
after scene 5258, and produced no failure marker. Its artifacts are in
`build/ball-homer-freeze/20260924-023920-448154Z`. The replay did not enter
the Ball Homer effect. The controlled original-code fixtures establish the
repaired path directly; they do not establish pixel parity on a physical
console or a successful replay of the exact manual J scene.
