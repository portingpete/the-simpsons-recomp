# Original zero-row rigid regression

The replacement rejected a valid original zero-row loop by requesting a
zero-byte guest mapping. The repair returns before row-span admission for an
empty loop and validates the material collection only when rows are consumed.
Geometry binding, reflection, auxiliary/object cleanup and completion remain.
The first repair passed 18/18 selected tests in Native and Release (15.70 and
14.12 seconds). Its unused-pointer and collection-logging extensions are now
applied and verified: **Native and Release each pass 26/26 selected tests**, in
24.81 and 43.50 seconds. The later [capture repair](rigid-capture-contained-audit.md)
registers the first-capture target and both configurations pass the full
525-test aggregate. The broader audit remains active.

The original static wrapper `8273B4D0` reaches `82701220` through the zero-bone
fallback, with actual caller `827402B0` and full metadata count in `r7`.
At count zero it still binds geometry and closes the object, but skips all
material rows. `82831C10` stores the collection pointer without reading its
target; the actual target lookup belongs to the skipped loop.

The [baseline binding](../build/restrictive-audit/rigid-zero-submeshes-wave-20261003/baseline-binding-v1.json)
records two independently launched opaque/alpha processes. Each reaches its
first zero-row call after 265 checks and fails at `width=0`. A genuine original
pool owns 164 bytes, including one backing row; logical count and transported
`r7` are zero. Each raw file has 43 rows, including the fixture entry,
production entry and matching failure. The diagnostic physical address is the
runtime's normalized alias of the recorded row view. Neither aborted baseline
earns later probe, draw or normal cleanup credit.

Ten independent zero processes now pass: opaque/alpha ordinary empty loops plus
null/unmapped row-pointer and null/unmapped collection-pointer variants (eight
extra processes), each created by the genuine original reader/pool and each with
the unused field left untouched by the original loop. Every case retains the
isolated metadata/ABI count negative (the transported `r7` must still match the
zero metadata), later ordinary opaque/alpha draws, and original
camera/declaration/FX/cache/pool/serialized-source cleanup. They verify no
material callbacks, unchanged complete color/depth and original bind/object-end
traversal. Native completion checks its backend draw count against the zero
cursor. Stale CPU owner queries pass; the backend mesh cache remains resident, so
full GPU retirement is unproven.

Production prevalidation logging now includes the material-collection pointer
**value** in the entry instance, without reading its target. Addresses stay
instance data, outside stable combination keys. The six large-row family cases
retain their 24 malformed controls per configuration; the zero fixtures add the
ABI-count negatives (34 malformed controls per configuration in all). Nonempty
owner bounds, wrapped selectors and material indices still fail. Each
configuration has twelve rendered shader-pass catalog joins. Zero loops begin the
selected pass but add no rendered shader combination or gameplay encounter.

Authoritative receipts (under `build/restrictive-audit/rigid-zero-submeshes-wave-20261003/`):

| Artifact | SHA256 |
| --- | --- |
| `unused-native-v3-receipt.json` | `fa58d834f4e507b2a549e1761428cbf5f660528563024f3ec1096ea88592f742` |
| `unused-native-release-v3-receipt.json` | `30851c248e88226c154d685aa7496662cc1f24f1b76e5229a692fbaa7ac324ae` |
| `independent-unused-v3-native-release-review-audio_checks-v1.json` | `aca8583095a407c6cd6b5e3b2453ad6d946977de3d3c12442c39101026072675` |

Those scopes predate capture registration; the CMake they ran against is preserved
at `build/restrictive-audit/rigid-first-capture-wave-20261003/before-CMakeLists.txt`.
One old Native `CTestTestfile` byte copy is unavailable and the independent review
records that explicitly; actual commands, XML, `LastTest`, raw logs and executable
identities are preserved. The earlier
[repair](../build/restrictive-audit/rigid-zero-submeshes-wave-20261003/repair-native-v2-receipt.json)
and
[Release](../build/restrictive-audit/rigid-zero-submeshes-wave-20261003/repair-native-release-v2-receipt.json)
receipts remain the historical 18-test evidence. The
[catalog review](../build/restrictive-audit/rigid-zero-submeshes-wave-20261003/catalog-coverage-review-catalog-v1.md)
checks complete original effect and shader records. Actual builds complete 13
Native and 14 Release steps; AOT verifies 311 files with zero semantic
diagnostics.

Recorded loops, other geometry domains, per-row diagnostics, opened audio-file/claim
attribution, complete GPU lifetimes and broad mission action/resource coverage
remain open.
