#!/usr/bin/env python3
"""Run all49 original FX setup fixtures in separate bounded native processes.

Each process gets one row. Failures/timeouts are recorded and later rows still
run. This launches the test fixture only, never a gameplay stage or game exe.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--image', type=Path, default=ROOT / 'analysis/simpsons.pe')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/restrictive-check-audit/effect-setup')
    parser.add_argument('--timeout', type=float, default=60)
    args = parser.parse_args()
    fixture, image, output = args.fixture.resolve(), args.image.resolve(), args.output.resolve()
    audit.require(fixture.stem == 'IndependentEffectSetupTests' and fixture.suffix.lower() == '.exe',
                  'Runner accepts the independent native setup test fixture only')
    audit.require(output.is_relative_to(ROOT / 'build') and args.timeout > 0, 'Invalid fixture output or timeout')
    fixture_ref = dict(path=str(fixture), sha256=audit.sha(fixture.read_bytes()))
    image_ref = dict(path=str(image), sha256=audit.sha(image.read_bytes()))
    source_ref = dict(path='tests/test_independent_effect_setup.cpp',
                      sha256=audit.sha((ROOT / 'tests/test_independent_effect_setup.cpp').read_bytes()))
    source_inputs = [dict(path='tests/effect_catalog_lifecycle_helpers.h',
                         sha256=audit.sha((ROOT / 'tests/effect_catalog_lifecycle_helpers.h').read_bytes()))]
    output.mkdir(parents=True, exist_ok=True)
    cases = []
    for row in range(49):
        start = time.monotonic()
        try:
            result = subprocess.run([str(fixture), str(image), str(row)], cwd=ROOT,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout)
            raw, code = result.stdout, result.returncode
        except subprocess.TimeoutExpired as error:
            raw, code = error.stdout or b'', 'timeout'
        raw += f'\nRUNNER_EXIT_CODE={code}\n'.encode()
        path = output / f'row-{row:02d}.log';path.write_bytes(raw)
        cases.append(dict(row=row, returncode=code, elapsed_seconds=time.monotonic()-start,
                          log=path.relative_to(ROOT).as_posix(), log_sha256=audit.sha(raw)))
        print(f'row={row:02d} returncode={code} seconds={cases[-1]["elapsed_seconds"]:.2f}', flush=True)
        # Persist after every case so an interruption cannot erase earlier
        # independent outcomes or make the remaining rows look successful.
        report = dict(schema=1, fixture=fixture_ref, image=image_ref, source=source_ref, source_inputs=source_inputs,
                      complete=len(cases)==49, cases=cases)
        (output / 'runner.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    if any(c['returncode'] != 0 for c in cases):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
