# Bounded headless IDA probe

Status: complete and frozen, 2026-09-09. Headless IDAPython and the installed
PPC decompiler work. Four selected functions produced pseudocode; the raw
pseudocode is useful for navigation and control-flow inspection, **not a faithful
guest ABI or replacement C implementation**. No whole-image autoanalysis or
post-start `823EE8F8` investigation was performed.

## Actual result and artifacts

Successful run: `build/ida-probe/ppc64-01/probe.i64`, process exit 0, IDA 9.3,
embedded Python 3.14.4, Hex-Rays PPC 9.3.0.260213. `load_plugin("hexppc")` and
`init_hexrays_plugin()` both succeeded, followed by four successful `decompile`
calls. This verifies operational licensing/decompiler availability in this
installation at test time, not merely the presence of `plugins/hexppc.dll`.
No license file was read, copied, edited or activated.

Evidence is under `analysis/ida/ppc64-01/`:

- `report.json`: API results, exact extents, every instruction word and IDA
  disassembly/reference, direct calls and unresolved `bctrl` sites.
- `823EDF20.c.txt`, `823EE1A8.c.txt`, `823EE6C8.c.txt`, `82756480.c.txt`: raw,
  unedited pseudocode (139/132/41/176 lines respectively).
- Matching `ADDRESS.asm.txt` and `ADDRESS.original.txt`: IDA output and the
  independent existing `SimpsonsDisasm.exe` output from the same original bytes.
- `comparison.json`: **646/646 instruction words match** the pinned image and
  independent decoder; **73/73 direct BL targets match** IDA references. Five
  indirect calls occur in `823EE1A8`. Six mnemonic differences are normal
  `rlwinm`/`extrwi`/`srwi` and `rldicr`/`extldi` aliases, with identical words.
  This comparison verifies instruction/call evidence, not pseudocode semantics.

Only four functions were defined/analyzed; other call targets remain untyped.
The process exited and no probe IDA process remains running.

## Concrete usefulness and ABI limitations

- `823EDF20`: pseudocode retains the startup resource sequence and the six
  direct CPU/mixed-stage calls at `823EE184..198`, in order:
  `823EDD38`, `823F4780`, `823F69E0`, `82409A90`, `824008E0`, `823FCF60`.
  But it invents a varargs function signature, infers some r3 results as floats,
  and propagates results as arguments between untyped no-argument calls. Those
  inferred types/arguments are not evidence for the engine contract.
- `823EE1A8`: output shows the 260-slot cleanup loop, declaration-cache walk
  and final cleanup order. Its first indirect free is printed with no arguments,
  although `823EE1C4` explicitly loads r3 from CB18 before `bctrl` at `823EE1E0`.
  The pool callback at `823EE224` likewise needs the original r3/r4 contract;
  a printed `void (*)(void)` does not establish that contract.
- `823EE6C8`: the color/default-target choice and CF58 depth-cache comparison
  agree with the branch/load/store evidence. However its apparent first call
  result is actually the untouched incoming r3: helper `82A3C3C8..3DC` only
  saves r28..31 and r12 to the stack, then returns. Raw pseudocode prints an
  uninitialized viewport local and omits meaningful stack stores: instructions
  `823EE7AC/B4/BC/C4/D4/DC` populate six words at SP+50/+54/+58/+5C/+60/+64,
  then `823EE7E0` calls `8243D0F8` with their address in r4. Those stores must
  be recovered from instructions, not from this raw pseudocode.
- `82756480`: the textured/untextured branches, shader/state calls and two
  `8244C450` calls are visible. Its inferred `void f()` signature loses the
  original r8/r9/r10 and f5..f8 inputs explicitly copied at `82756494..4B0`,
  and the other incoming register uses. Do not derive the screen ABI or floating
  point rounding guarantees from the inferred locals/signature.

All four functions use Xenon register-save/restore helpers left untyped by this
bounded database. They appear as ordinary calls and `JUMPOUT` epilogues, which
spoils argument, return and stack inference. PPC64 mode also prints guest globals
such as `MEMORY[0xFFFFFFFF82D0CAF8]`; the runtime guest address is the low 32-bit
`82D0CAF8`. Native pointer arithmetic/types in this output do not implement guest
wrapping. No original instructions were patched or helpers replaced by no-ops.

Future focused improvement would need byte-verified save/restore annotations,
explicit guest pointer/callback types and stack structures, then fresh comparison.
That work was not attempted here. The current actionable evidence is the
disassembly, exact call sites, and manually checked control flow above.

## Loading and bounded analysis

Input is the existing derived flat memory image `analysis/simpsons.pe`, SHA256
`6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0`.
Its bytes map at `0x82000000 + file_offset`; this is not the PE disk layout
advertised by its inherited section raw offsets. An ordinary PE load would move
code incorrectly. The successful probe uses a new empty database and `mem2base`,
checks every loaded byte, and defines only four functions using their original
`.pdata` extents: `823EDF20/288`, `823EE1A8/290`, `823EE6C8/124`,
`82756480/3DC` (hex). Image bytes are loaded for data/reference visibility;
only the selected extents become analyzed functions. Automatic discovery,
signature application and recursive callee analysis are disabled.

The flat evidence segment is deliberately writable in IDA metadata, so unknown
mutable globals are not folded to their initial image values. That is conservative
analysis metadata, not guest memory protection or a change to the source image.

The first sandboxed launches failed before IDAPython; the hidden GUI batch probe
reported `Fatal registry error: Access is denied.` It was stopped. The same
headless `idat.exe` launch succeeded after tool approval for normal registry
access. This was an environment permission failure, not evidence of a missing
license. IDA user files were redirected with IDAUSR to `build/ida-probe/user`.

Preserved failed experiments make the limits reproducible: `raw02` had mismatched
32-bit segment/64-bit database flags (decompiler error -25); `raw03` raw-binary
loading initially selected a 64-bit segment and then produced error -15 after
changing only the database flags. Consistent PPC32 (`raw04`) and ILP32
(`ilp32-01`) both hit `INTERR 50735` on `823EDF20`; these do not establish a usable
32-bit guest decompiler model. The final script stops decompilation attempts in
that process after an internal error. Consistent PPC64 mode (`ppc64-01`) succeeds
with the pointer/ABI limitations described above. No installation changes were
made to work around these errors.

## Repeatable commands and local primary documentation

Installed sources used: `C:/Program Files/IDA Professional 9.3/docs/user-guide/`
`configuration/command-line-switches.html`, `general-concepts/environment-variables.html`,
and `python/examples/decompiler/decompile_entry_points.py` under the installation
root. The example explicitly loads `hexppc` before `init_hexrays_plugin()` for an
early `-S` script. `python/ida_ida.py`, `ida_segment.py`, `ida_loader.py` and
`ida_hexrays.py` supplied API signatures. The installed gooMBA plugin loads with
Hex-Rays but its `cfg/goomba.cfg` sets `MBA_RUN_AUTOMATICALLY = NO`; it was not
invoked by this experiment.

Use a fresh tag; the script rejects an existing report or an output database
outside the assigned run directory. Run from PowerShell with normal IDA registry
access (the agent sandbox required tool escalation):

```powershell
$tag = 'probe-next'
$root = 'K:/SimpsonsNativeCopy'
$run = "$root/build/ida-probe/$tag"
New-Item -ItemType Directory -Path $run -Force | Out-Null
$env:IDAUSR = "$root/build/ida-probe/user"
$env:IDADIR = 'C:/Program Files/IDA Professional 9.3'
$env:IDALOG = "$run/ida.log"
$env:PYTHONDONTWRITEBYTECODE = '1'
$args = @('-A', '-a', '-t', '-pppc', "-o$run/probe.i64", "-L$run/ida.log",
          "-S`"$root/tools/ida_probe.py $tag ppc64`"")
$proc = Start-Process "$env:IDADIR/idat.exe" -ArgumentList $args `
  -WorkingDirectory $run -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput "$run/stdout.log" -RedirectStandardError "$run/stderr.log"
$proc.WaitForExit(10000) # false means still running; do not launch a duplicate
# After process completion:
python -B "$root/tools/ida_probe.py" --compare $tag
```

The tested command `python -B tools/ida_probe.py --compare ppc64-01` performs the
independent comparison without IDA, regeneration or guest execution.
`--check-image` validates the exact image and `.pdata` selections without IDA.

ReAgent's current [primary README](https://github.com/Dryxio/reagent/blob/main/README.md)
says the binary-backed workflow uses Ghidra via ghidra-ai-bridge; IDA is mentioned
as a future backend. This experiment tests IDA directly, without ReAgent or
another model CLI/API. No ReAgent installation or model calls were made. Writes
for this experiment were confined to `tools/ida_probe.py`, this document,
`build/ida-probe/` and `analysis/ida/`; parent runtime/driver/config, reference
projects and original assets were unchanged.
