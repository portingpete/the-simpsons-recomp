#!/usr/bin/env python3
"""Export passing independent original setup results without claiming draws.

Requires one captured per-process log per catalog row. A passing marker, exact
original source/name/technique count, and setup phases must agree. Failed and
missing rows are preserved as gaps; one failure never hides later rows.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit

MARKER = re.compile(r'^AUDIT_EFFECT_SETUP row=(\d+) source=([0-9A-F]{8}) name=(\S+) techniques=(\d+) bindings=(\d+) '
                    r'create=passed metadata_use=passed release=passed malformed=passed draw=unproven '
                    r'shadow_camera_rasters_retained=(\d+)$', re.MULTILINE)
PASS = re.compile(r'^PASS independent original effect setup: row=(\d+) checks=(\d+);', re.MULTILINE)


def export(matrix, directory, root=ROOT):
    cases, gaps = [], []
    runner_path = directory / 'runner.json'
    runner = json.loads(runner_path.read_text(encoding='utf-8'))
    audit.require(runner.get('schema') == 1, 'Unsupported setup runner schema')
    results = {case['row']: case for case in runner['cases']}
    audit.require(len(results) == len(runner['cases']), 'Duplicate independent runner outcome')
    inputs = [runner[field] for field in ('fixture', 'image', 'source')] + runner.get('source_inputs', [])
    for reference in inputs:
        path = (root / reference['path']).resolve()
        audit.require(path.is_relative_to(root.resolve()), 'Setup input must remain in the audit workspace')
        audit.require(audit.sha(path.read_bytes()) == reference['sha256'], 'Setup runner input changed: ' + reference['path'])
    inputs.append(dict(path=runner_path.resolve().relative_to(root.resolve()).as_posix(),
                       sha256=audit.sha(runner_path.read_bytes())))
    by_row = {}
    for row in matrix['rows']:
        if row['kind'] == 'effect_pass':
            by_row.setdefault(row['row'] + (25 if row['table'] else 0), []).append(row)
    audit.require(set(by_row) == set(range(49)), 'Support matrix must contain all49 original rows')
    for index, passes in sorted(by_row.items()):
        path = directory / f'row-{index:02d}.log'
        if not path.exists():
            gaps.append(dict(row=index, status='missing', path=str(path)))
            continue
        raw = path.read_bytes();text = raw.decode('utf-8', errors='replace').replace('\r\n', '\n')
        result = results.get(index)
        if (not result or result['returncode'] != 0 or result['log_sha256'] != audit.sha(raw) or
            (root / result['log']).resolve() != path.resolve() or text.count('RUNNER_EXIT_CODE=0') != 1):
            gaps.append(dict(row=index, status='runner_not_passing', path=str(path), sha256=audit.sha(raw)))
            continue
        markers, passed = list(MARKER.finditer(text)), list(PASS.finditer(text))
        if len(markers) != 1 or len(passed) != 1 or 'FAIL independent original FX setup:' in text:
            gaps.append(dict(row=index, status='not_passing', path=str(path), sha256=audit.sha(raw)))
            continue
        marker = markers[0]
        if (int(marker[1]) != index or int(passed[0][1]) != index or int(passed[0][2]) <= 0 or
            '0x' + marker[2] != passes[0]['source'] or marker[3] != passes[0]['effect'] or
            int(marker[4]) != len(passes) or int(marker[6]) != (4 if index == 4 else 0)):
            gaps.append(dict(row=index, status='identity_mismatch', path=str(path), sha256=audit.sha(raw)))
            continue
        resolved = path.resolve()
        audit.require(resolved.is_relative_to(root.resolve()), 'Setup proof logs must remain in the audit workspace')
        proof = dict(path=resolved.relative_to(root).as_posix(), sha256=audit.sha(raw))
        for row in passes:
            cases.append(dict(catalog_id=row['id'], result='passed', scope='setup',
                phases=['create', 'query', 'metadata_use', 'release', 'malformed'],
                proof=proof, inputs=inputs, row=index, source=row['source'], technique_handle=row['technique_handle'],
                pass_handle=row['pass_handle'], original_calls=['827019E8', '823C7B20', '823C7CA0', '82701118'],
                qualification='Fresh-process canonical original setup/metadata-use/retirement; no draw or full shadow camera teardown claim'))
    return dict(schema=1, complete=not gaps, rows_passed=49-len(gaps), passes_setup_tested=len(cases),
                runner=dict(path=str(runner_path), sha256=audit.sha(runner_path.read_bytes())),
                cases=cases, gaps=gaps)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', type=Path, required=True)
    parser.add_argument('--logs', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    matrix, reference = audit.load(args.matrix.resolve())
    report = export(matrix, args.logs.resolve())
    report['matrix'] = reference
    output = args.output.resolve()
    audit.require(output.is_relative_to(ROOT / 'build') and output != args.matrix.resolve(),
                  'Setup receipts must remain in build and must not replace their matrix')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: report[k] for k in ('complete', 'rows_passed', 'passes_setup_tested')}))
    if not report['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
