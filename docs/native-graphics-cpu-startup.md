# Original graphics CPU startup

The first two objects in `826B0DF8` remain original AOT code. The native
preflight validates the actual caller and live backend identity, then preserves
all allocations, CPU arrays, matrix operations, constructors and publications.
The build142 guard was **82701B70**, before the 25-effect registration loop.
The later native first-effect owner preserves this CPU construction and advances
to the second effect; see `native-first-effect-owner.md`. No draw is claimed.

**Integrated build142:** all47 suites pass in63.22s. Actual muted boot085 reaches
the new guard at82701B70, caller826B0E94, with M=E1A9C600, O=E1A9C910,
child=E1A9C998, context00900001 and first-effect output zero.
Logs: `build/native-graphics-cpu-142.log`, `build/boot-085.log`.
Executable SHA256
`caa574b0421a9bcb7d88a930a1c2fa6924981053a92a5f46d52886a9f3a7c827`.
This verifies actual parent allocation/publication, without original game pixels.

## Evidence and retained behavior

`tools/analyze_graphics_cpu_startup.py --verify` checks the pinned original PE,
14 bounded bodies and **1,154 instruction words**, constant words and the first
constructor's direct CPU dependency closure. The report is
`analysis/native-graphics-cpu-startup.json`. Parent `826B0DF8` is included as
context; this does not qualify all its subsequent operations. The separate
`docs/native-graphics-startup-services.md` covers the second object and later
resource dependencies, with its reproducible 10-test evidence verifier.

The original caller is `828620D4`. **82D5DA74** is the correct context alias:
`lis 82D6` plus the signed `DA74` displacement. It contains a borrowed, unmapped
native identity. It is not an SDK device pointer; **82D0CAF8 remains zero**.

Original allocation `8269BF70(260,{2,10,0})` supplies the first object M.
`826B6F60` publishes M at `82D08BFC`, installs vtable `820B71DC`, stores the
identity at M+14, and creates two six-entry containers at M+1C/M+34 with scalar
0.25. Each container retains its real C-byte node, 1C-byte entry array and
18-byte parallel array. It initializes six entries; the seventh entry and
parallel array are not assumed zero. No device is dereferenced.

The aggregate at M+50 keeps the original VMX matrix construction, multiplication,
inverse, CPU copies and revision increments. Its six matrices are numerically
identity. Inverse math retains signed zeros; the fixture checks numerical
identity, without normalizing memory or claiming original-console bit equality.
The original constants 0, -1, 1 and 0.1 at M+1F0..218 remain original loads/stores.
The byte at `82D5DB74` conditionally sets `82E2D2D8=1`; both paths are exercised
in the isolated fixture. Getter `827225B0` supplies `82D6D2F8` to M+18.

Original second allocation `8269BF70(70,{2,10,0})` supplies O.
`8271BD10(O,M)` retains its borrowed fields and real 58-byte child. The actual
`8273EB50(child,identity,1)` clears the child, stores identity and flag byte1,
and branches around SDK construction. The parent publishes O at `82D5DA78`.
The original null allocation branches remain intact; the preflight does not
fabricate an allocation result or skip an allocation failure.

Fresh O cleanup `8271CD00` frees that child and sees eight empty resource slots;
`8269BEB0` frees O. M's deleting destructor `826B7600(M,1)` executes original
container cleanup, frees its arrays/node/object and clears `82D08BFC`. This
qualifies the **empty** profile only. Populated effects, changed child flags,
render/reset methods and complete application shutdown remain separate work.

## Executed fixture

`GraphicsCpuStartupTests` uses real original startup through the existing
post-audio diagnostic point `828166FC`, with the initialized original heap and
real native driver. It throws there, then uses a fresh `EngineCpuCalls` from the
saved entry context. It never resumes the exception-unwound startup frame.
No new production observer, allocator or manager implementation was added.

Two paired M/O lifetimes pass **605 checks**: actual original allocations,
CPU defaults, arrays, six matrices, nonvolatile register/frame preservation,
both optional-store branches, untouched storage, child flag, empty destruction,
singleton clear and unchanged native context ownership. Owned M/O test storage
is initially poisoned to expose unwritten bytes. Freed pointers are not read.
This fixture invokes constructors individually; the actual executable boot
separately verifies their parent orchestration/publication.

Two early fixture attempts could not intercept the direct AOT parent call via
dispatch replacement or a weak-symbol override. The final fixture instead uses
the existing real startup observation. A further assertion initially rejected
inverse -0 as unequal to +0; the numerical identity oracle was corrected.
No production matrix code was changed to satisfy that assertion.

Reproduce:

```powershell
python -B tools/analyze_graphics_cpu_startup.py --verify
python -B build/graphics-startup-services/verify.py
python -B build/graphics-cpu-startup/run_probe.py
ctest --test-dir build/native -R OriginalGraphicsCpuStartup --output-on-failure
```

The standalone compiler uses `/fp:strict /W4 /WX` and existing parent libraries,
with unchanged input hashes recorded in `build/graphics-cpu-startup/result.json`.
All launches are muted. Other startup systems use terminal Runtime shutdown;
their cleanup notices do not prove normal application teardown.

## Next native operation

First effect `fourtapblend`, blob `820B8AA0`, is wrapped by `826B4B88`.
Its SDK creation is immediately followed by reflection reads and parameter-cache
construction. Replacing creation with an opaque shader ID would leave invalid
SDK accesses. Recover a native effect owner with the original metadata,
parameter and typed-consumer contract, including paired destruction, before
allowing the registration loop to continue.
