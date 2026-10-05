# Rigid first-capture repair and contained capture diagnostics

Two independently evidenced stages. Both Native and Release pass **525/525**
registered tests in full serial aggregates (419.23 and 467.49 seconds), the first
complete aggregates since the capture target was registered. The broader
restrictive-check audit remains active; this adds no gameplay encounter and no
complete GPU-retirement credit.

## Stage 1: the 65,535-row capture whitelist

The original static wrapper `8273B4D0` passes the full metadata DWORD count to
`82701220`, which iterates 36-byte rows. A genuine 65,536-row pool (`8282F618`
reader, `82831280` relocation; 2,359,296 row bytes in a 2,359,424-byte owner)
rendered correctly, but the capture diagnostic re-imposed a 16-bit row cap and
aborted the draw: **“First rigid capture submesh extent changed”** after 352
checks, in two fresh processes (opaque/alpha). Their five precall expectation
snapshots and 43 raw rows are preserved in
`build/restrictive-audit/rigid-first-capture-wave-20261003/baseline-binding-v1.json`.

The repair (reviewed candidate, applied byte-for-byte, receipt
`capture-extent-application-v1.json`) makes `rigidSubmeshExtent` return the
already-checked byte count and reuses it for both capture paths: complete
overflow, logical-owner and mapped-span checks, no row-count whitelist.

| Configuration | Receipt SHA256 | Checks (opaque/alpha) |
| --- | --- | --- |
| Native | `f7cd6a67…54f1d0` | 3,425 / 3,429 |
| Release | `ef0dcd8e…187050` | 3,425 / 3,429 |

Each receipt re-derives from raw files: all five captured files equal their
precall snapshots; the 2,359,296-byte table is 65,535 skipped rows (`7,0,0,6,0,0,4,0,0`)
plus the selected final row (`2,0,0,6,0,0,4,0,0`); draw JSON identifies that
row; first-call prevalidation tuple and expectation hashes equal the failing
baseline.

## Stage 2: diagnostics must never decide a primary outcome

Stage 1 still threw if a valid table exceeded the 64 MiB diagnostic copy budget
or if any capture file could not be written, which aborted real rendering and
(for draw metadata) attributed the failure to an unrelated latest encounter.

Implemented in `runtime/engine_effects.cpp` and `runtime/resource_audit.h`:

- `containedRigidCapture` is `noexcept`; allocation, path, I/O, formatting and
  reporting failures are contained. Host CSR and `LastError` are restored; no
  PPC/TLS state is touched. In the rejected-upload `catch(Graphics::Error&)` the
  original bare `throw;` stays primary. **No deterministic original producer of a
  rejected upload exists, so that path is source-reviewed only.**
- Over budget (`36*rows > 64 MiB`) is `skipped`/`budget_exceeded` before any
  allocation or file open; rendering is unaffected.
- Separate attempted and completed latches (`rigidFirstMeshAttempted`,
  `rigidCaptureDrawAttempted`): one attempt per fresh latch, so a later smaller
  table never silently replaces the first, and a failed attempt is never retried.
- `ResourceAudit::diagnostic` appends a non-attributing `diagnostic` row
  (kind `rigid_capture`) without touching the latest primary encounter, scene
  owner or dedup set. Finite phase/status/reason_code/capture_step; addresses,
  owner generation and output directory live only in `instance`.

Tests (fresh process each, all with a genuine original reader pool):

| Case | Real condition | Verified |
| --- | --- | --- |
| `OriginalRigidFirstCapture_*` | 65,536 rows (unchanged success) | files, draw JSON, two `complete` diagnostics |
| `OriginalRigidCaptureBudgetSkip_*` | **1,864,136 rows = 64 MiB + 32 bytes** | valid input rendered; `skipped`; no files; no retry |
| `OriginalRigidCaptureRawIoFailure_*` | directory occupies `rigid-first-indices.bin` | geometry/vertices closed, rest absent; draw proceeds; no retry |
| `OriginalRigidCaptureDrawIoFailure_*` | directory occupies `rigid-first-draw.json` | raw group complete; `failed`; draw proceeds; no retry |
| `ResourceAuditReceipts` | host regression | diagnostics never become the latest primary encounter, scene owner or dedup entry; identical diagnostics are not deduplicated |

**Genuine baseline** (stage-1 source, new tests; preserved XML
`…capture-contained-baseline-native-20261002-v2-tests.xml`): 6/6 fail. Budget
rejects the valid 1,864,136-row table at 345 checks (“Rigid submesh diagnostic
exceeds 64 MiB capture budget”); raw/draw I/O fail at 354 checks. The draw-I/O
failure was recorded against a `texture_binding` encounter. An earlier baseline
attempt (v1) ran the stage-2 binary because a restored source kept its old
timestamp and Ninja did not rebuild; it is preserved but **invalid and
uncredited** (`stage2-baseline-attempt-v1-INVALID.md`).

| Configuration | Receipt SHA256 | Aggregate |
| --- | --- | --- |
| Native | `28815e70…074868` | 525/525, 419.23 s |
| Release | `fdef2fa9…a77c4e` | 525/525, 467.49 s |

Scopes: Native `125330a7…bf1dbc5`, Release `f0d01d26…f4901c`; each binds 1,223
inputs, 645 compiled-object dependency records, three DLLs and every executable
(223 Native / 215 Release identities, which grant no execution credit to
unselected binaries). AOT verifies 311 files with zero semantic diagnostics.
Final identities: `engine_effects.cpp` `aa4c4564…8b8a2f`, `resource_audit.h`
`ba26ab26…db351`, first-capture test `116a8414…a0b15`.

The lexical census is now 821 mechanical candidates (from 823): two
`<=65535` guards were removed and the throwing 64 MiB guard became a
non-throwing skip. Remaining limits: the over-budget input is a synthetic
original-reader table, not a stock asset; fixtures borrow material/driver
scaffolding; backend mesh cache stays resident (full GPU retirement unproven);
row epoch/pinning and concurrent free are unqualified.
