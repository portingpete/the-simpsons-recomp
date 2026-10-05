"""Line-level self-time hot spots from a SIMPSONS_STACK_SAMPLE capture (see tools/stack_profile.py).

python -B tools/stack_lines.py build/perf240/s5.txt [--top 40] [--within FUNC_SUBSTR] [--skip-guest]
Symbolizes each sample's leaf PC with inline expansion (needs the PDB of the exact exe in the capture)
and ranks source lines by self samples; with --depth 2 also prints the inline caller line.
"""
import argparse, collections, subprocess
from pathlib import Path

SYMBOLIZER = r"C:\Program Files\LLVM\bin\llvm-symbolizer.exe"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture', type=Path)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--within')
    ap.add_argument('--tid', type=int)
    ap.add_argument('--skip-guest', action='store_true', help='ignore leaves inside generated __imp__sub_ code')
    ap.add_argument('--depth', type=int, default=1, help='inline frames to show (1 = innermost only)')
    args = ap.parse_args()
    modules, samples = {}, []
    for line in args.capture.read_text().splitlines():
        parts = line.split()
        if parts[0] == 'M': modules[int(parts[1])] = ' '.join(parts[2:])
        elif parts[0] == 'S':
            samples.append((int(parts[1]), [tuple(int(x, 16) if i else int(x) for i, x in enumerate(f.split(':'))) for f in parts[2:]]))
    exe = next(i for i, m in modules.items() if m.lower().endswith('simpsonsnative.exe'))
    per = collections.Counter(t for t, _ in samples)
    tid = args.tid or per.most_common(1)[0][0]
    mine = [fr for t, fr in samples if t == tid]
    wanted = sorted({rva for fr in mine for m, rva in fr if m == exe})
    proc = subprocess.run([SYMBOLIZER, '--obj=' + modules[exe], '--relative-address', '--inlines', '--functions=short'],
                          input='\n'.join(hex(r) for r in wanted), text=True, capture_output=True)
    blocks = proc.stdout.strip().split('\n\n')
    table = {}
    for rva, block in zip(wanted, blocks):
        lines = block.split('\n')
        frames = list(zip(lines[0::2], lines[1::2]))  # (function, file:line:col)
        table[rva] = frames
    total = len(mine)
    cnt = collections.Counter()
    for fr in mine:
        m, rva = fr[0]
        if args.within and not any(args.within in (table.get(r, [('', '')])[-1][0]) or any(args.within in f for f, _ in table.get(r, [])) for mm, r in fr if mm == exe):
            continue
        if m != exe:
            cnt[f'[{Path(modules.get(m, "?")).name}]'] += 1
            continue
        frames = table.get(rva, [])
        if not frames: cnt[hex(rva)] += 1; continue
        if args.skip_guest and frames[-1][0].startswith('__imp__sub_'): continue
        key = ' <- '.join(f'{f} {Path(l).name.rsplit(":", 1)[0]}' for f, l in frames[:args.depth])
        cnt[key] += 1
    print(f'thread {tid}: {total} samples')
    for k, c in cnt.most_common(args.top):
        print(f'{c / total * 100:5.2f}% {k}')


main()
