# Original rigid submesh range regression

The native 65,535-row ceiling rejected a valid original 65,536-row input. The
repair checks the complete byte span and observed allocation boundary instead
of imposing another row-count whitelist. **Native and Release each pass 16/16
selected tests**, in 13.25 and 12.87 seconds, including six independently
launched rigid families. The overall restrictive-check audit remains active.

Original zero-bone producer `827400F8` passes the full metadata DWORD count to
`82701220`, with caller `827402B0`. Its unsigned traversal advances 36 bytes per
row. The regression's genuine `8282F618` pool reader produces a 2,359,424-byte
allocation: a 128-byte prefix and 2,359,296 bytes containing 65,536 rows. The
first 65,535 rows use the original flag `0x20` skip branch; the last selects one
existing material. This qualifies the larger row count without expanding the
separate material collection, texture, vertex or bone domains.

The [baseline receipt](../build/restrictive-audit/rigid-submesh65536-wave-20261003/baseline-native-v3-receipt.json)
records the actual 351-check rejection after both one-row reference draws.
It preserves the original caller, count, table pointer, logical owner,
generation, source identity, mission and action. That aborted run has no
completed large draw or normal cleanup credit. Earlier attempts stopped at a
fixture assertion: the original dispatcher clears the alpha eligibility byte
after selecting the alpha pass, so the later raw mesh argument is zero. The
correction is pinned to original instructions; those attempts are not count
rejection evidence.

The repair computes `36ull * count`, checks address representability, looks up
the owner using the starting row address, and validates the full remainder
before reading any row. Existing unobserved/static views retain their mapped
range checks. Two malformed controls exceed the logical owner by 36 bytes:
count 65,537 and a row pointer shifted forward by one row. Readable allocator
padding cannot admit them. Invalid compiled material indices and wrapped
material selectors still reach their own exact guards. Each control restores
the actual PPC context and guest byte views, preserves host state, and is
followed by a genuine valid original continuation.

Each fresh family process constructs real catalogs, textures, row pools and
geometry declarations, invokes the whole original wrapper for opaque and
alpha draws, compares complete pixels with its one-row references, and runs
original camera/declaration/FX/cache/pool/serialized-source cleanup. Stale CPU
owner and query checks pass; they are not retired rigid-handler invocations.

| Family | Original FX source | Tested selected passes |
| --- | --- | --- |
| textured | `820168F8` | opaque and alpha |
| base | `8200CCB8` | opaque and alpha |
| dual | `8202AD78` | opaque and alpha |
| gloss | `82019988` | opaque and alpha |
| multitone | `820547E8` | opaque and alpha |
| normalmap | `82057E08` | opaque and alpha |

The [Native](../build/restrictive-audit/rigid-submesh65536-wave-20261003/repair-native-v2-receipt.json)
and [Release](../build/restrictive-audit/rigid-submesh65536-wave-20261003/repair-native-release-v2-receipt.json)
receipts join all twelve selected shader pairs to the original catalogs and
actual effect-pass observations. Each configuration includes 24 malformed
controls, 60 fixture encounter records and 78 production entry/failure records.
Production observations run before rigid validation; every failure retains
its preceding input's stable group and instance. Alpha pass selection precedes
the isolated controls, while its valid mesh continuation carries the later
action. No telemetry was rewritten to reconcile these boundaries.

The [independent repair review](../build/restrictive-audit/rigid-submesh65536-wave-20261003/independent-repair-native-release-review-audio_checks-v2.json)
rehashes both configurations and verifies complete original effect blobs and
24 distinct shader records directly against the original image. Its source,
command, grouping and release review adds no further native execution.

Each compiled scope binds 1,221 project inputs, five selected executables,
three DLLs and 643 observed object dependency records. Actual initial repair
builds complete 13 steps in each configuration; Native's subsequent family
registration build completes four steps, including three verification steps. AOT regeneration
and verification confirm 311 files with zero semantic diagnostics. There are
507 registered CTests; the full 507-test aggregate was not run. The other ten
selected controls cover existing rigid inheritance, skin large-row cases and
producer-entry logging without broadening their historical domain claims.

The [fresh source census](../build/restrictive-check-audit/rigid-row-expanded-census-v1-20261003.json)
contains 823 mechanical candidates across 313 files and 1,168 keyword lines.
All remain mechanically untriaged; that report does not infer runtime or
gameplay coverage. Fresh admission and producer reports retain 49 effects,
110 passes and 63 passes with missing artifacts.

The subsequent [zero-row regression](rigid-zero-submesh-audit.md) qualifies
ordinary static immediate empty loops and scoped CPU cleanup, and the
[capture repair](rigid-capture-contained-audit.md) qualifies capture-enabled
first uploads (65,536 rows, and a valid 1,864,136-row table above the 64 MiB
diagnostic budget). Recorded large-row lifetimes, rejected-upload capture (no
deterministic original producer), recorded zero loops, concurrent row ownership
and complete GPU retirement remain unproven. Material objects and some
driver/texture owners use the fixture's borrowed scaffolding. Actual authored
visitors and broad mission action/resource lifetimes also remain open (the
opened audio-file/claim relation has since become an observer-only receipt, see
[audit-hot-path-and-file-provenance.md](audit-hot-path-and-file-provenance.md)). These fixtures grant no new
gameplay encounters. The
[row observation addendum](../build/restrictive-audit/rigid-capture-zero-source-plan-20261003/row-prevalidation-addendum-v1.md)
preserves all nine raw words per offending row while keeping unqualified pointer
fields out of stable combination keys; its implementation status is recorded
in the restrictive-check audit.
