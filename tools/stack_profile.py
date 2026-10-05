"""Symbolize and summarize a SIMPSONS_STACK_SAMPLE capture (see app/stack_sampler.h).

python -B tools/stack_profile.py build/perf240/s1.txt [--top 40] [--tid N] [--children FUNC_SUBSTR]
Prints per-thread sample counts, self time, and inclusive time per function.
"""
import argparse, collections, subprocess, sys
from pathlib import Path

SYMBOLIZER = r"C:\Program Files\LLVM\bin\llvm-symbolizer.exe"

def short(name):
    name = name.split('(')[0] if not name.startswith('`') else name
    return name[:100]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture', type=Path)
    ap.add_argument('--top', type=int, default=40)
    ap.add_argument('--tid', type=int)
    ap.add_argument('--callers', help='show the caller chain (2 levels) of the first frame containing this text')
    ap.add_argument('--leaf', help='only samples whose leaf frame contains this text; shows 3 frames above it')
    ap.add_argument('--within', help='only samples whose stack contains this text')
    ap.add_argument('--children', help='show callees (one level) of frames containing this text')
    args = ap.parse_args()
    modules, samples = {}, []
    for line in args.capture.read_text().splitlines():
        parts = line.split()
        if parts[0] == 'M': modules[int(parts[1])] = ' '.join(parts[2:])
        elif parts[0] == 'S': samples.append((int(parts[1]), [tuple(int(x, 16) if i else int(x) for i, x in enumerate(f.split(':'))) for f in parts[2:]]))
    exe = next(i for i, m in modules.items() if m.lower().endswith('simpsonsnative.exe'))
    wanted = sorted({rva for _, fr in samples for m, rva in fr if m == exe})
    cache = {}
    proc = subprocess.run([SYMBOLIZER, '--obj=' + modules[exe], '--relative-address', '--no-inlines'],
                          input='\n'.join(hex(r) for r in wanted), text=True, capture_output=True)
    blocks = proc.stdout.strip().split('\n\n')
    for rva, block in zip(wanted, blocks):
        cache[rva] = block.split('\n')[0]
    def name(m, rva):
        if m == exe: return short(cache.get(rva, hex(rva)))
        return Path(modules.get(m, '?')).name if m >= 0 else '?'
    per_thread = collections.Counter(t for t, _ in samples)
    print('samples per thread:', dict(per_thread))
    for tid, _ in per_thread.most_common():
        if args.tid and tid != args.tid: continue
        mine = [fr for t, fr in samples if t == tid]
        if args.within: mine = [fr for fr in mine if any(args.within in name(m, r) for m, r in fr)]
        if args.leaf:
            lc = collections.Counter()
            for fr in mine:
                names = [name(m, r) for m, r in fr]
                if args.leaf in names[0]: lc[' <- '.join(names[:4])] += 1
            print(f'-- leaf {args.leaf} ({len(mine)} samples)')
            for n, c in lc.most_common(args.top): print(f'{c/len(mine)*100:5.1f}% {n}')
            continue
        total = len(mine)
        selfc, incl, child, callers = (collections.Counter() for _ in range(4))
        for fr in mine:
            names = [name(m, r) for m, r in fr]
            selfc[names[0]] += 1
            for n in set(names): incl[n] += 1
            if args.callers:
                for i, n in enumerate(names):
                    if args.callers in n: callers[' <- '.join(names[i + 1:i + 4])] += 1; break
            if args.children:
                for i, n in enumerate(names):
                    if args.children in n and i: child[names[i - 1]] += 1; break
        if args.callers:
            print(f'-- callers of {args.callers} (thread {tid}, {total} samples)')
            for n, c in callers.most_common(15): print(f'{c/total*100:5.1f}% {n}')
            continue
        if args.children:
            print(f'-- callees of {args.children} (thread {tid}, {total} samples)')
            for n, c in child.most_common(args.top): print(f'{c/total*100:5.1f}% {n}')
            continue
        print(f'=== thread {tid}: {total} samples ===')
        print('-- self')
        for n, c in selfc.most_common(args.top): print(f'{c/total*100:5.1f}% {n}')
        print('-- inclusive')
        for n, c in incl.most_common(args.top): print(f'{c/total*100:5.1f}% {n}')
main()
