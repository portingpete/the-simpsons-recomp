# Original single-level BC3 resources

Actual164 stopped at the original copied `8_SharedLibrary` resource, generation125,
rasterE1ACBE70, format1A200154, extent512x256 and auxiliary0. The failure was a
format guard, before the established180-second run limit. The preceding
copyright frame is original text, not the main menu.

## Exact original resource

The resource is record0xA28 in `frontend_split24.itxd`, STRentry25 in the player's
read-only `frontend/frontend.str`. The dictionary has1019904 bytes and SHA256
`0babfa2b129e9c00b5bf28f90db2763e2b5252d611d1d398b80fec37b9283938`.
The record's payload starts at757760 and contains131072 bytes. Its stored,
pre-relocation descriptor is:

```
84000002 00000054 001FE1FF 00000D10 00000000 00000200
```

`tools/prepare_itxd_bc3_fixture.py` verifies the dictionary identity, record name,
dimensions, packed format, auxiliary field, descriptor and allocation bounds.
It independently prepares all8192 linear16-byte blocks using the existing
Python reference tiling/address helper, without calling the production C++
decoder. Existing fixture files must match byte-for-byte. The output manifest
in `build/itxd-bc3` records extraction provenance and these SHA256 values:

| Fixture | SHA256 |
| --- | --- |
| Tiled storage | `67ead1514b88522ffb46679c3057cf1b3ea37dfded064b8b6c80b2ddd7ebfb92` |
| Linear BC3 | `7f173a72d49f05a8a19e0acb87d055673423d19edc5c281df686fab61626bfc8` |
| Original metadata | `1c9afcad8549d0aa8c2d6830ee99fea68f239cb8f4a51e748e7ed7e62051cadb` |

## Implementation and boundaries

`decodeITXDBC3Base` shares the bounded16-byte block untile/address permutation
with BC2, while each public decoder requires its own exact descriptor format
word:54 for BC3,53 for BC2. Original descriptor, dimensions, power-of-two pitch,
single-level/base-zero profile, endian permutation and exact allocation checks
remain enforced. Neither BC1 nor authored mip chains become qualified.

The ITXD owner still verifies publication, unchanged original metadata,
generation and GPU submission ownership before lazily uploading. Packed
format1A200154 selects immutable nativeBC3_UNORM;1A200153 retains BC2_UNORM.
Auxiliary must remain zero. GPU texture creation does not alter original
metadata/pixels or substitute original allocation, relocation, cache or frees.

Im2D permits the same checked native BC3 owner with exactly one level. The
original qualified `texture2D * diffuse` shader, alpha comparisons, blend
equations and depth semantics are unchanged. There is no CPU color expansion,
alpha reassociation, interpreter, GPU command emulation or menu substitution.

## Verification coverage

`OriginalITXDSharedLibraryBlocks` compares all original linear block bytes,
checks a single stored byte-lane mutation, and rejects format confusion,
descriptor mutations, addresses, mips, incorrect pitch/dimensions and short or
oversized payloads. The existing full BC2 atlas test is retained.

`OriginalITXDBC3Lifecycle` uses the same original startup/load/copy/relocation
and group lifetime harness as the font resource, stopping at the actual
`8_SharedLibrary` load. It checks original pixels and metadata, thread ownership,
lazy upload identity, native readback, original cache sharing, primary-owner
promotion, original frees, stale generations and reloads. The font lifecycle
continues to run separately. Altered original format metadata is rejected.

Native Im2D fixtures use literal BC3 bytes and separately authored expected
palettes. Both alpha modes exercise all eight selectors. Ascending RGB
endpoints retain four colors. Tests cover tint, replace/SRC_ALPHA blending,
all eight alpha comparisons, exact depth/stencil and uncovered margins.
The same endpoint-alpha quadrant fixture exercises both BC2 and BC3 point and
linear filtering, separated alpha thresholds, exact point-alpha equality,
released caller references and immutable source storage. Foreign ownership,
spoofed format/width, unsupported samplers, BC1 and compressed mips still reject.

The [D3D11.3 specification, sections19.5.2 and19.5.8](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
allows error below `1/255 + .03 * endpointRange` for derived BC3 components;
reference zero and one must remain exact. The literal fixture endpoints span
one, so the tests propagate `173/5100` through positive tint, blending and
packing instead of demanding device-specific interpolation. Alpha-test
references are separated from every interpolated value by more than this
bound. Equality uses exact point-sampled alpha1 times diffuse1/2.

The build191 hardware run passed493617 Im2D checks with the D3D11 debug layer.
Full-suite and actual-game results are recorded separately after their runs
settle. Native readbacks alone do not establish accepted desktop presentation
or physical-console precision parity.
