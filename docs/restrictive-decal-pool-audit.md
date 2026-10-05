# Original decal pool bounds

The native list scanner's 65,536-node cutoff has no verified valid rejection
in the original singleton gameplay producer. Its real owner contains 800 quad
nodes and 200 cached-mesh nodes. The original list traversal itself has no
count cutoff. These are separate statements: the pool proof bounds this
producer, not every list constructible with the generic insertion helpers.

Run `python -B tools/analyze_decal_pool_contract.py --output
build/restrictive-check-audit/decal-pool-contract-20261002.json`. The independent
source report checks the entire original image identity, 13 complete byte
spans, 52 annotated instruction words, allocation/slot/free-chain/destructor
arithmetic, and rejects all 3,200 independent one-byte mutations. It contains
no native rendering or executed owner-retirement evidence.

Startup `827502A0..B8` requests `F2560` bytes with alignment 16 through
`8274BB10`, then calls constructor `82767340`. The constructor publishes the
singleton at `82DFE344`. Its quad initialization loop `827674B8..D8` visits
800 slots at `owner+250+i*80`; the cached loop `827674DC..FC` visits 200 slots
at `owner+19260+i*1160`. The last cached slot ends exactly at `owner+F2560`.
The original free-chain construction contains 160 groups of five quad nodes
and 40 groups of five cached nodes, matching those initialized slots.

Acquisition `82767708` and `827677D8` pops the respective free head. When it
is empty, the original routine searches the manager's existing groups,
unlinks a reusable node, clears its material owner and returns that same
pointer. It returns null when no node qualifies. These complete pinned
functions contain no pool allocation or growth path. Gameplay `82769490`
selects these acquisitions, initializes the result and inserts it into the
intrusive list at `827695B4..D8`. The draw entry `82753508` selects the same
singleton; `82764C20` traverses its groups and `827640A0` traverses each
group's two node lists.

Group cleanup `82764C78` and expiry `827678B0` return those same nodes to the
singleton free chains. Pool destructor `82764B58` visits all 200 cached and
800 quad slots and clears the singleton. Deleting destructor `82767668`
calls that destructor and, when flag bit 0 is set, calls paired owner free
`8274BB70`. The source call sequence does not prove executed allocator or
GPU retirement.

Generic group initializer `82764A40` and insertion helpers `827649E0` /
`82764A10` impose no node-count condition. Their broader caller-owned
allocation and complete lifetime beyond the singleton pool remain
unqualified. No production guard change or new native fixture is included.
The next meaningful case is genuine owner construction, all 800/200
acquisitions, original insertion/count/use of invisible children, paired
group return/reacquisition and deleting destruction, with independent cycle,
duplicate-owner, pointer, callback and material-mismatch rejection cases.
