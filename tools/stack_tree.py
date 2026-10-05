"""Top-down call tree for a SIMPSONS_STACK_SAMPLE capture (see tools/stack_profile.py).

python -B tools/stack_tree.py build/perf240/s5.txt [--min 1.5] [--depth 14] [--root FUNC_SUBSTR] [--collapse PREFIX,...]
Prints inclusive percentages of the main thread's samples, children sorted by weight,
pruned below --min percent. --root restarts the tree at the first frame containing FUNC_SUBSTR.
"""
import argparse, collections, subprocess
from pathlib import Path

SYMBOLIZER = r"C:\Program Files\LLVM\bin\llvm-symbolizer.exe"


def short(name):
    name = name.split('(')[0] if not name.startswith('`') else name
    return name[:90]


def load(capture):
    modules, samples = {}, []
    for line in capture.read_text().splitlines():
        parts = line.split()
        if parts[0] == 'M': modules[int(parts[1])] = ' '.join(parts[2:])
        elif parts[0] == 'S':
            samples.append((int(parts[1]), [tuple(int(x, 16) if i else int(x) for i, x in enumerate(f.split(':'))) for f in parts[2:]]))
    exe = next(i for i, m in modules.items() if m.lower().endswith('simpsonsnative.exe'))
    wanted = sorted({rva for _, fr in samples for m, rva in fr if m == exe})
    proc = subprocess.run([SYMBOLIZER, '--obj=' + modules[exe], '--relative-address', '--no-inlines'],
                          input='\n'.join(hex(r) for r in wanted), text=True, capture_output=True)
    cache = {rva: block.split('\n')[0] for rva, block in zip(wanted, proc.stdout.strip().split('\n\n'))}

    def name(m, rva):
        if m == exe: return short(cache.get(rva, hex(rva)))
        return Path(modules.get(m, '?')).name if m >= 0 else '?'
    return [(t, [name(m, r) for m, r in fr][::-1]) for t, fr in samples]  # outermost first


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture', type=Path)
    ap.add_argument('--min', type=float, default=1.5)
    ap.add_argument('--depth', type=int, default=14)
    ap.add_argument('--root')
    ap.add_argument('--tid', type=int)
    args = ap.parse_args()
    samples = load(args.capture)
    per = collections.Counter(t for t, _ in samples)
    tid = args.tid or per.most_common(1)[0][0]
    stacks = [fr for t, fr in samples if t == tid]
    if args.root:
        cut = []
        for fr in stacks:
            for i, n in enumerate(fr):
                if args.root in n:
                    cut.append(fr[i:])
                    break
        stacks = cut
    total = len(stacks)
    print(f'thread {tid}: {total} samples')
    root = {'n': 0, 'c': {}}
    for fr in stacks:
        node = root
        for n in fr[:args.depth]:
            node['n'] += 1
            node = node['c'].setdefault(n, {'n': 0, 'c': {}})
        node['n'] += 1

    def show(node, name, depth):
        pct = node['n'] / total * 100
        if pct < args.min: return
        print(f'{"  " * depth}{pct:5.1f}% {name}')
        for n, ch in sorted(node['c'].items(), key=lambda kv: -kv[1]['n']):
            show(ch, n, depth + 1)
    for n, ch in sorted(root['c'].items(), key=lambda kv: -kv[1]['n']):
        show(ch, n, 0)


main()
