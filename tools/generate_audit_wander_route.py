"""Generate the deterministic extended gameplay route for the restrictive-check audit.

The 20-action opening route (SMOKE in audit_stage_routes.py) only reaches the first seconds of a stage. This route
keeps the same bounded controller vocabulary but runs much longer: eight stick directions of varying length,
attacks, jumps, the secondary ability, interact, the special trigger, character switches and A confirmations to clear
prompts. The sequence is seeded and written once; replaying the JSON is repeatable. Nothing here knows a level layout, so
command delivery proves neither a hit, a pickup, a destruction nor a defeat. Those need their own receipts.
START and BACK are never issued.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import random

ROOT = Path(__file__).resolve().parents[1]
STICKS = [(0, 32767), (0, -32768), (-32768, 0), (32767, 0), (23170, 23170), (-23170, 23170), (23170, -23170), (-23170, -23170)]
STICK_NAMES = ['fwd', 'back', 'left', 'right', 'fwd-right', 'fwd-left', 'back-right', 'back-left']
A, B, X, Y, SWITCH = 0x1000, 0x2000, 0x4000, 0x8000, 0x0200


def build(seed, segments):
    rng = random.Random(seed)
    route = [dict(name='opening-confirm-' + str(index), command='A') for index in range(4)]
    for segment in range(segments):
        direction = rng.randrange(len(STICKS))
        lx, ly = STICKS[direction]
        move_ms = rng.choice((900, 1200, 1500, 1800, 2000))
        route.append(dict(name=f's{segment:02d}-walk-{STICK_NAMES[direction]}', buttons=0, lx=lx, ly=ly, ms=move_ms, rt=0, wait_after_ms=300))
        route.append(dict(name=f's{segment:02d}-walk-attack', buttons=X, lx=lx, ly=ly, ms=rng.choice((500, 800, 1100)), rt=0, wait_after_ms=300))
        route.append(dict(name=f's{segment:02d}-attack', buttons=X, lx=0, ly=0, ms=450, rt=0, wait_after_ms=300))
        route.append(dict(name=f's{segment:02d}-jump', buttons=A, lx=lx, ly=ly, ms=rng.choice((350, 600)), rt=0, wait_after_ms=500))
        if segment % 2 == 0:
            route.append(dict(name=f's{segment:02d}-ability', buttons=B, lx=0, ly=0, ms=rng.choice((600, 900)), rt=0, wait_after_ms=500))
        if segment % 3 == 1:
            route.append(dict(name=f's{segment:02d}-interact', buttons=Y, lx=0, ly=0, ms=350, rt=0, wait_after_ms=700))
        if segment % 5 == 2:
            route.append(dict(name=f's{segment:02d}-special', buttons=0, lx=0, ly=0, ms=350, rt=255, wait_after_ms=700))
        if segment % 4 == 3:
            route.append(dict(name=f's{segment:02d}-switch', buttons=SWITCH, lx=0, ly=0, ms=350, rt=0, wait_after_ms=800))
        route.append(dict(name=f's{segment:02d}-confirm', command='A'))
    return route


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', type=int, default=20261003)
    parser.add_argument('--segments', type=int, default=16)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Preserve the existing route file; choose a new name')
    route = build(args.seed, args.segments)
    args.output.write_text(json.dumps(route, indent=1) + '\n', encoding='utf-8')
    print(json.dumps(dict(path=str(args.output), actions=len(route), seed=args.seed, segments=args.segments)))


if __name__ == '__main__':
    main()
