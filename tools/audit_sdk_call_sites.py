"""Static audit of console-SDK call sites that no native bridge replaces.

The native port keeps the console device global null, so any original call into
the SDK library (82438000..82462000) that is not replaced by a midasm hook fails
when reached. Starting from root functions, this follows direct `bl` calls
(not through call sites a hook replaces, and not into functions a return hook
replaces) and lists every reachable function that still has unreplaced SDK
calls. It proactively finds dormant rendering passes that would fail the first
time gameplay activates them.

Limits: direct calls only (vtables and function pointers are not followed);
reachability is static, not proof that gameplay reaches a site. A few SDK
entry hooks accept specific callers natively; those sites are listed in
ENTRY_DISPATCHED so they are not reported.

    python -B tools/audit_sdk_call_sites.py [root ...]
"""
from __future__ import annotations

import argparse
import bisect
import json
import re
import struct
from collections import defaultdict, deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK_LO, SDK_HI = 0x82438000, 0x82462000
# Post phases (82751778), post depth copy (82751700) and the screen-effect
# command dispatcher with its immediate callers.
DEFAULT_ROOTS = (0x82751778, 0x82751700, 0x8276E1E8, 0x826C2260, 0x826C23E8)
# Sites whose SDK entry hook qualifies the caller natively:
# 82455570 from 827724DC (trail resolve), 824408E0 from 82771B40 (Ball texture)
# and 82771DB0 (its null-texture reset).
ENTRY_DISPATCHED = {0x827724DC, 0x82771B40, 0x82771DB0}


def load():
    meta = json.loads((ROOT / 'analysis/executable.json').read_text(encoding='utf-8'))
    image = (ROOT / 'analysis/simpsons.pe').read_bytes()
    functions = json.loads((ROOT / 'analysis/ida/default-xex-full-20260919-side/functions.json').read_text(encoding='utf-8'))
    toml = (ROOT / 'config/simpsons.toml').read_text(encoding='utf-8')
    return meta['image_base'], image, functions, toml


def audit(roots):
    base, image, functions, toml = load()
    chunks = sorted((int(c0, 16), int(c1, 16), int(f['address'], 16)) for f in functions for c0, c1 in f['chunks'])
    covered, replaced, returns_at = set(), set(), []
    for block in toml.split('[[midasm_hook]]')[1:]:
        address = re.search(r'(?m)^address = (0x[0-9A-Fa-f]+)', block)
        if not address:
            continue
        a = int(address[1], 16)
        jumps = [int(m[1], 16) for m in re.finditer(r'(?m)^jump_address = (0x[0-9A-Fa-f]+)', block)]
        on_true = re.search(r'jump_address_on_true = (0x[0-9A-Fa-f]+)', block)
        on_false = re.search(r'jump_address_on_false = (0x[0-9A-Fa-f]+)', block)
        if on_true and on_false:
            jumps.append(min(int(on_true[1], 16), int(on_false[1], 16)))
        for jump in jumps:
            covered.update(range(a, max(jump, a + 4), 4))
        if re.search(r'(?m)^return = true', block):
            replaced.add(a)
            returns_at.append(a)
    for a in returns_at:
        for c0, c1, _ in chunks:
            if c0 <= a < c1:
                covered.update(range(a, c1, 4))
    covered.update(ENTRY_DISPATCHED)
    calls, sdk_sites = defaultdict(set), defaultdict(list)
    for c0, c1, owner in chunks:
        for pc in range(c0, c1, 4):
            word = struct.unpack_from('>I', image, pc - base)[0]
            if (word >> 26) != 18 or (word & 3) != 1:
                continue
            displacement = word & 0x03FFFFFC
            if displacement & 0x02000000:
                displacement -= 0x04000000
            target = (pc + displacement) & 0xFFFFFFFF
            if pc not in covered:
                calls[owner].add(target)
            if SDK_LO <= target < SDK_HI and not (SDK_LO <= owner < SDK_HI):
                sdk_sites[owner].append((pc, target, pc in covered))
    seen = {root: None for root in roots}
    queue = deque(roots)
    while queue:
        function = queue.popleft()
        if function in replaced:
            continue
        for target in calls.get(function, ()):
            if not (SDK_LO <= target < SDK_HI) and target not in seen:
                seen[target] = function
                queue.append(target)
    report = []
    for function in sorted(seen):
        sites = sdk_sites.get(function, [])
        open_sites = [(pc, target) for pc, target, done in sites if not done]
        if open_sites:
            path, walk = [], function
            while walk is not None:
                path.append('%08X' % walk)
                walk = seen[walk]
            report.append({'function': '%08X' % function, 'open': len(open_sites), 'sites': len(sites),
                           'path': '<-'.join(path), 'calls': ['%08X@%08X' % (t, pc) for pc, t in open_sites]})
    return len(seen), report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('roots', nargs='*', type=lambda v: int(v, 16))
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    reachable, report = audit(tuple(args.roots) or DEFAULT_ROOTS)
    if args.json:
        print(json.dumps({'reachable_functions': reachable, 'unbridged': report}, indent=2))
        return 0
    for row in report:
        print('%s open=%d/%d path=%s' % (row['function'], row['open'], row['sites'], row['path']))
        print('    ' + ' '.join(row['calls']))
    print('reachable functions: %d; functions with unbridged SDK calls: %d' % (reachable, len(report)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
