# Native memory service contract

The runtime reserves an internal 4 GiB address interval and commits actual native
Windows pages for verified game memory. It links no runtime instruction decoder
or device command processor. Invalid/unimplemented MMIO fails explicitly.

`MmQueryStatistics` uses the 104-byte big-endian layout corroborated by original
`0x82B75C48` (size check and reads of total/available/reserved memory) and the
structure in the read-only reference's `xboxkrnl_memory.cpp`. The reference's
guessed counter values are not copied. This port uses an enforced 512 MiB native
compatibility budget, with actual committed image, bootstrap, stack, virtual and
physical backing charged once. Uncommitted reservations consume virtual address
space, not this budget. Internal AOT function tables are host implementation
metadata and do not consume title memory. There is no separate guest system
process, pool or file cache until those services create actual allocations.

These counters describe this port; they are not a measurement of the original
retail console's kernel overhead or allocation placement. They may influence
the engine's adaptive memory partitioning. Boot log 007 demonstrates that the
original engine uses this query to size a `0x1E2B0000` physical allocation.

`MmAllocatePhysicalMemoryEx` implements a single backing allocator with shared
CPU aliases. Evidence for address windows and the 4 KiB aperture's `+0x1000`
offset is the reference's `src/system/xmemory.cpp` (`PhysicalHeap::GetPhysicalAddress`
and heap initialization). Physical minima/maxima and alignment constrain the
backing offset; all page-size apertures allocate from one set of live extents.
CPU accesses through any valid alias return the same checked native pointer.
Native CPU memory is coherent. Cache-policy flags are retained as allocation
metadata for the future engine renderer; this does not implement any console
GPU allocation, packet processing, or completion service.

Tests cover endian layout, reserve versus commit, repeated commit contents,
budget exhaustion rollback, physical alias sharing, physical bounds/alignment,
read-only protection, and free restoring both access protection and the budget.
Remaining work includes virtual free/protect/query,
native pool services, and renderer resource ownership. Unsupported contracts
remain explicit failures when reached.

Boot 019 reaches `MmSetAddressProtect(0xE2CA0000,0x2000,0x404)` inside the
engine allocator `0x8268E138`. The requested bits mean read/write plus write
combine, verified against original instructions and `system/xtypes.h`.
The physical implementation retains independent per-page access/cache flags
for A/C/E apertures. Rounding follows the selected aperture (64 KiB / 16 MiB /
4 KiB), and extents must stay inside a live allocation. Every translated access
checks the requested guest address before resolving its shared bytes. Internal
host RAM uses the union of permitted alias accesses, updated by VirtualProtect
when that union changes, so protecting one alias cannot break a writable sibling.
Failed native protection batches roll back before metadata is published.
No-access pages remain queryable through metadata. Console cache-policy bits are metadata on
coherent native CPU RAM, not simulated GPU cache operations. Tests cover a
two-byte straddle, independent alias state/coherence, all three page granularities,
host readonly union, no-access, invalid-range atomicity, restoration, and unchanged allocation accounting. Virtual/image
protection and unimplemented flags still fail explicitly.

This corrects the shared-permission defect found in review of checkpoint 020.
Reference `PhysicalHeap::Protect` updates the selected aperture and raw physical
view, not sibling apertures; QueryProtect uses that aperture's metadata. The
native compatibility allocation still exposes its owned bytes through valid
CPU aliases from allocation time. Complete initial sibling mapping behavior on
retail hardware has not been established by this reference comparison.

The guest handle table owns native Windows semaphore handles. A lookup retains
the host object throughout a wait even if another guest thread closes the table
entry. Semaphore creation/release delegates to native NT services so initial,
maximum, previous counts and failure statuses are real. Unnamed objects are
implemented; named attributes and guest APC enqueue/delivery services currently
fail explicitly. Alertable waits/delays work with the currently empty guest APC
queue and use real native alertable waits. An unknown host APC result fails
explicitly instead of being reported as a delivered guest callback.
The native wait preserves the guest's absolute/relative timeout representation
and zero-time poll behavior, as documented by Microsoft for
[NtWaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntwaitforsingleobject).
Count consumption/release semantics also match Microsoft's
[semaphore example](https://learn.microsoft.com/en-us/windows/win32/sync/using-semaphore-objects).

Native synchronization/notification timers use Windows NT timer objects. Relative
and absolute due times, periods, waits and cancellation state come from those
objects. Guest callback/APC bridging is still unsupported and fails explicitly.

The 48-byte `XGetVideoMode` layout comes from the published
[XTLOnPC compatibility header](https://raw.githubusercontent.com/CodeAsm/ffplay360/master/Common/XTLOnPC.h)
and the original function `0x8270CD10`, which reads `VideoStandard` at +24 and
refresh at +20 to select PAL timing. Five trailing words are reserved in that
API header; this port does not copy the reference runtime's extra interpretations
of them. Native window client dimensions are queried after creation. Progressive
60 Hz presentation and US/NTSC-M compatibility are configured policies, not
claims about the host monitor or measured rendered frames. The first captured
window is blank; no native renderer is linked yet.
