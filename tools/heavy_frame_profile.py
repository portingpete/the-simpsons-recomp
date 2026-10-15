"""Attribute stack samples to heavy frames of a pacing trace.

python -B tools/heavy_frame_profile.py <run folder> <stack capture> [--threshold-ms 9.5] [--top 30]

The run folder holds pacing.csv (SIMPSONS_PACING_TRACE) and game.log, whose
"[PACING TRACE] origin_timebase=" line anchors trace time. The stack capture is a
SIMPSONS_STACK_SAMPLE file of the main thread (its T records carry QPC stamps).
Each sample is assigned to the frame whose update+render span (gameplay clock
sample to present arrival) contains it. The report lists, per function, inclusive
samples per heavy frame minus samples per normal frame: what heavy frames add.
Sampling suspends the thread about once per millisecond, so frames run a little
slower than in an unsampled run.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import csv
import re
import subprocess
from pathlib import Path

SYMBOLIZER = r"C:\Program Files\LLVM\bin\llvm-symbolizer.exe"


def load_capture(path: Path):
    modules, samples, stamps, frequency = {}, [], {}, None
    for line in path.read_text().splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'M':
            modules[int(parts[1])] = ' '.join(parts[2:])
        elif parts[0] == 'Q':
            frequency = int(re.search(r"frequency=(\d+)", line).group(1))
        elif parts[0] == 'T':
            fields = dict(item.split('=') for item in parts[1:])
            stamps[int(fields['sample'])] = int(fields['begin_qpc'])
        elif parts[0] == 'S':
            frames = [tuple(int(x, 16) if i else int(x) for i, x in enumerate(f.split(':'))) for f in parts[2:]]
            samples.append((int(parts[1]), frames))
    return modules, samples, stamps, frequency


def symbolizer(modules, samples):
    exe = next(i for i, m in modules.items() if m.lower().endswith('simpsonsnative.exe'))
    wanted = sorted({rva for _, frames in samples for m, rva in frames if m == exe})
    process = subprocess.run([SYMBOLIZER, '--obj=' + modules[exe], '--relative-address', '--no-inlines'],
                             input='\n'.join(hex(r) for r in wanted), text=True, capture_output=True)
    names = {rva: block.split('\n')[0].split('(')[0][:100]
             for rva, block in zip(wanted, process.stdout.strip().split('\n\n'))}

    def name(module, rva):
        if module == exe:
            return names.get(rva, hex(rva))
        return Path(modules.get(module, '?')).name
    return name


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('run', type=Path)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--threshold-ms', type=float, default=9.5)
    parser.add_argument('--top', type=int, default=30)
    parser.add_argument('--gap-ms', type=float, default=0,
                        help='Also print the stacks sampled in gaps longer than this between a present '
                             'returning and the next gameplay clock sample')
    args = parser.parse_args()
    log = (args.run / 'game.log').read_text(encoding='utf-8', errors='replace')
    origin = int(re.search(r"\[PACING TRACE\] origin_timebase=(\d+)", log).group(1))
    rows = [r for r in csv.DictReader(open(args.run / 'pacing.csv')) if r.get('stat_sync_us') not in (None, '')]
    spans = sorted((float(r['sample_us']), float(r['arrival_us'])) for r in rows
                   if float(r['sample_us']) >= 0 and float(r['arrival_us']) > float(r['sample_us']))
    starts = [s for s, _ in spans]
    modules, samples, stamps, frequency = load_capture(args.capture)
    name = symbolizer(modules, samples)
    heavy, normal = collections.Counter(), collections.Counter()
    heavy_frames, normal_frames = set(), set()
    for index, (tid, frames) in enumerate(samples):
        if index not in stamps:
            continue
        at = (stamps[index] * 50_000_000 // frequency - origin) / 50.0  # trace microseconds
        position = bisect.bisect_right(starts, at) - 1
        if position < 0 or at > spans[position][1]:
            continue
        work_ms = (spans[position][1] - spans[position][0]) / 1000.0
        functions = {name(m, rva) for m, rva in frames}
        if work_ms > args.threshold_ms:
            heavy.update(functions)
            heavy_frames.add(position)
        else:
            normal.update(functions)
            normal_frames.add(position)
    if args.gap_ms:
        ordered = sorted((float(r['present_end_us']), float(r['sample_us'])) for r in rows if float(r['present_end_us']) >= 0)
        gaps = [(current[0], following[1]) for current, following in zip(ordered, ordered[1:])
                if following[1] - current[0] > args.gap_ms * 1000]
        timed = sorted(((stamps[i] * 50_000_000 // frequency - origin) / 50.0, i) for i in stamps)
        for begin, end in gaps:
            print(f"gap {begin / 1e6:.4f}s .. {end / 1e6:.4f}s ({(end - begin) / 1000:.2f} ms)")
            first = bisect.bisect_left(timed, (begin, -1))
            for at, index in timed[first:bisect.bisect_right(timed, (end, len(samples)))]:
                frames = samples[index][1]
                print(f"  {(at - begin) / 1000:6.2f} ms: " + " <- ".join(name(m, rva) for m, rva in frames[:14]))
    print(f"heavy frames (> {args.threshold_ms} ms work) with samples: {len(heavy_frames)}; normal: {len(normal_frames)}")
    if not heavy_frames or not normal_frames:
        return 0
    excess = {f: heavy[f] / len(heavy_frames) - normal[f] / len(normal_frames) for f in set(heavy) | set(normal)}
    for function, value in sorted(excess.items(), key=lambda item: -item[1])[:args.top]:
        print(f"{value:7.2f} samples/frame extra  heavy {heavy[function] / len(heavy_frames):6.2f}  "
              f"normal {normal[function] / len(normal_frames):6.2f}  {function}")
    heavy_list = sorted(heavy_frames)
    print("heavy frame times (s):", [round(spans[i][0] / 1e6, 2) for i in heavy_list[:40]])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
