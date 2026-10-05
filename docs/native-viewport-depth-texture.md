# Original full-size viewport depth texture

Boot197 completed the edge, AA and edgeAA passes, then rejected resolve82455570
from82751754. Original caller82751700 requests a depth copy with selector0x14,
destination from82DFEA20+148, null rectangle/point/level/slice and auxiliary
arguments, zero unused clear values. The low three selector bits choose depth;
0x10 selects the single sample. Neither0x100 nor0x200 clear-enable bit is set.
The existing native depth-copy backend already preserves the selected source
and all depth/stencil bits with a real copy and completion lease.

## Original texture ownership

Constructor82751118 builds ten original CPU texture headers per viewport, using
the52-byte row spacing and156-byte slot spacing at82DFE360. Its100-byte viewport
rows live at82DFEF00. Original profile8 is the full-size depth texture;
row0+30 publishes header82DFE840. Other rows publish82DFE874/82DFE8A8 and share
some original placement; their sampling/copy/alias semantics remain unported.
The native bridge admits row0 only.

The original constructor completes all header construction, placement, real
allocator calls and publication before the observation at827514F4. No original
header is fabricated or replaced. The verified row0 header words are:

```
00000003 00000001 00000000 00000000 00000000 FFFF0000 FFFF0000
8A000002 <original-address>|00000097 0059E4FF 00000D11 00000000 00000200
```

This is1280x720, tiled pitch1280, format23/endian8-in-32, floating depth with
stencil. Native storage is the existing R32G8X24_TYPELESS depth/stencil resource
with D32_FLOAT_S8X24_UINT attachment and separate sample views. Original20e4
rasterization/rounding parity is not inferred from this storage choice.

The original allocation root is82DFEA20+124; row0+0C is its construction end,
0xEA6000 bytes later. In the controlled original-loader run these were
E2FEB000/E3E91000, with depth payloadE3AF7000. The original allocator obtains
this range inside its larger physical pool; equality with the pool allocation
base would incorrectly reject it. Constructor and row-retirement observations
establish the native ownership lifetime. Pool containment is an additional
backing check, not ownership inferred from readable memory. The original
published root/end/header and all13 header words must remain unchanged.

Selector82751510 copies row0 dimensions to82DFEB38/3A and its texture8 header
to82DFEB68. The copy bridge checks these publications, the exact82751700
frame/arguments, selected full-size native camera/attachments and actual D3D
depth target. It performs one full native depth/stencil copy to a distinct
owned destination. No SDK resolve code or packets execute. The unrelated
global SDK resolve entry remains guarded. The original following calls to
8276C930 and82754288 remain in place.

Retirement observation82751004 runs at the start of each original row cleanup.
The preserve-row0 branch never retires its depth texture; full cleanup retires
it before original headers and row fields are cleared. Pending native copies
retain backing until GPU completion. Recreating the same original header
address creates a new native resource lifetime.

## Verification

`build/viewport-depth-probe.log` captures the actual three original headers and
placement after normal startup and original palette-loader setup. The initial
pool-base check failed; the corrected containing-pool check and constructor
lifetime pass in `build/viewport-depth-pool-tests.log`: OriginalViewportSurfaces
4.60s, OriginalEdgeAAParameters6.79s. Tests cover preserve/full cleanup,
recreation, altered and unsupported headers, and two complete original effect
sequences followed by original82751700. Destination depth/stencil bytes equal
the source; source and color are unchanged; caller ABI and copy counts match.
The existing NativeViewportCopy tests independently cover nonuniform floating
depth/stencil values on hardware and software. Added negative caller/clear/
split-screen checks are included in the full build at
`build/viewport-depth-integration-build.log`:119/119 PASS227.59s.

These fixtures do not establish a visible opening game frame or main menu.
