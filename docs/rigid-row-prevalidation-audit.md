# Rigid per-row nine-word prevalidation receipts

Before this change, a malformed row inside a nonempty rigid table (an out-of-range
compiled material index or a wrapped Rw selector) was rejected with the *entry*
receipt attached: the audit showed only the table count and pointer, not the
offending row. The native rigid entry now copies the row's nine raw words into the
audit **before** any offset, header or collection read. Both configurations pass
the full serial aggregate of **525/525** (Native 428.27 s, Release 421.99 s).

## Observer

`runtime/engine_effects.cpp`, immediate (`82701220`) and recording (`82701448`)
entry, immediately after the complete nine-word row copy from the already checked
`36 * count` span. Kind `effect_producer_row`, boundary `rigid_row_prevalidation`,
original caller `827402B0` / `82740624`. It validates nothing, reads no header, offset
table or material target, restores host CSR/`LastError`, contains every
formatting/audit failure, and returns immediately when no audit file is active.

| Stable group (`parameters`) | Instance only |
| --- | --- |
| source, requested technique, metadata count, bones, collection count, entry argument `variant`, VFX flag | row ordinal / byte offset / address |
| word0 Rw selector, word1 compiled index, word3 primitive, word4 signed base, word5 start, word6 count | all nine raw words (hex) |
| word7 authored group count (separately labelled), `header_state=not-read-before-admission` | table, collection, metadata, raw r3/r4/r5/r7 |
| ownership: unvalidated, runtime/thread/cpu match, field roles (`consumed=w0,w1,w3,w4,w5,w6 authored=w7 unclassified=w2,w8`) | logical owner base/extent/generation |

Word2 and word8 have no source-established consumed role in this route and are kept
as unclassified raw context, never as pointers or flags. `variant` is the raw entry
argument, which is zero for every call because the original dispatcher clears the
alpha eligibility byte before the mesh call; the alpha pass is identified by the
separate `effect_pass` receipts.

Rows with identical consumed fields share one stable group, so the observer fires
only when that group changes (a 1,864,136-row table costs two receipts). The row
checks that follow depend only on those fields, so a failing row always differs
from its predecessor and its snapshot is the latest attempted row. Limit: rows
differing only in word2/word8 are not re-observed.

Span failures (count 65,537, shifted view) occur before any valid row copy and
**keep entry attribution**; only the compiled-index and selector controls now fail
with row attribution.

## Evidence

The updated tests (6 rigid 65,536-row families, 8 capture processes, 10 zero-table
processes; shared checks in `tests/rigid_row_audit_checks.h`) were built against
the preserved stage-2 source and **all 24 fail** (preserved XML, SHA256
`c9602bad…c10d`): the family/capture cases at “Production rigid entry receipts
missing, repeated or unexpected” because the row controls still bound to entry
receipts, and the zero cases at “Row prevalidation receipts missing”. After the
change all 24 pass. The zero fixtures additionally prove an empty loop emits no
row receipt while the later ordinary one-row calls do.

| Configuration | Receipt SHA256 | Scope SHA256 |
| --- | --- | --- |
| Native | `17fc985c339d0608b5726e824a3fa9f81a044c9a669de536c27b4194febeb059` | `30738fd2…adc7a` |
| Release | `dbf08e00e083f8407594e274f671bea50753010d42957e3b39add0187044717e` | `b80bcec6…c4a3` |

The binder is independent of the tests' helper: it joins every row receipt to its
own entry receipt (same metadata/object/typed owner, count and *logical row owner*),
rebuilds the nine words from the authored recurrence, checks the group excludes
ordinal/address/words/generation, checks a changed compiled index or selector
differs from the valid final row in exactly that one stable field, and checks the
two span controls have no row receipts. Final identities: `engine_effects.cpp`
`3dab6237…16fb8`, `rigid_row_audit_checks.h` `e1595256…c94aa`. Baseline raw
receipts show no `effect_producer_row` and four entry-kind control failures.

Limits: rigid immediate/recording entry only (skin, sky, mono and other families
have no row-level receipts); synthetic authored fixture tables with borrowed
material/driver scaffolding; header skip flags are intentionally unread at the
observer; no gameplay encounter and no complete GPU retirement. The lexical census
stays at 821 mechanical candidates.
