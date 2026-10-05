#!/usr/bin/env python3
"""Export native CTest padded geometry evidence without packaged asset claims.

Original padded skin/sky cases create, draw and retire declaration owners and
check output pixels. An explicit native marker is required to prove FX/CPU
cache retirement. Geometry cases are synthetic original-valid inputs,
independent of the stock mesh resource population.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit

SPECS = {
    'OriginalSkinbasePaddedPass': ('0x82006348', 'base', 80, 16, 'tests/test_skin_pass.cpp', 'SkinPassTests', True),
    'OriginalSkintexturedPaddedPass': ('0x8200FB98', 'textured', 80, 16, 'tests/test_skin_pass.cpp', 'SkinPassTests', True),
    'OriginalSkindualPaddedPass': ('0x8201CD48', 'dual', 80, 16, 'tests/test_skin_pass.cpp', 'SkinPassTests', True),
    'OriginalSkyPaddedPass': ('0x82036448', 'sky', 64, 36, 'tests/test_sky_pass.cpp', 'OriginalSkyPassTests', True),
}

LIFECYCLE = re.compile(r'^AUDIT_GEOMETRY_LIFECYCLE source=([0-9A-F]{8}) stride=(\d+) delta=(\d+) '
    r'create=passed use=passed declaration_release=passed fx_release=passed cpu_cache_release=passed '
    r'malformed=passed malformed_scope=(\S+) stale_use=passed '
    r'backend_mesh_cache=owner_resident full_gpu_retirement=unproven$', re.MULTILINE)


def ctest_commands(text):
    result = {}
    for line in text.splitlines():
        m = re.fullmatch(r'add_test\(\[=\[([^]]+)\]=\] (.*)\)', line)
        if m:
            result[m[1]] = re.findall(r'"([^"]*)"', m[2])
    return result


def export(matrix, junit, config, root=ROOT):
    tree = ET.fromstring(junit.read_bytes())
    testcases = {}
    for node in tree.iter('testcase'):
        name = node.get('name')
        if name in SPECS:
            audit.require(name not in testcases, 'Duplicate native CTest testcase')
            testcases[name] = node
    commands = ctest_commands(config.read_text(encoding='utf-8'))
    proof = dict(path=junit.resolve().relative_to(root).as_posix(), sha256=audit.sha(junit.read_bytes()))
    cases, synthetic_rows, gaps = [], [], []
    for test, (source, family, stride, relocated, fixture, executable, negatives) in SPECS.items():
        node = testcases.get(test)
        if node is None or any(node.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
            gaps.append(dict(test=test, status='not_passing'));continue
        text = '\n'.join(node.itertext()).replace('\r\n', '\n')
        marker = f'PASS original skin {family}:' if family != 'sky' else 'PASS original sky:'
        if marker not in text or ('FAIL original skin' if family != 'sky' else 'FAIL original sky:') in text:
            gaps.append(dict(test=test, status='missing_native_pass'));continue
        command = commands.get(test, [])
        if len(command) != (3 if family == 'sky' else 5) or Path(command[0]).stem != executable or command[-1] != 'padded':
            gaps.append(dict(test=test, status='command_mismatch'));continue
        image = Path(command[1]);exe = Path(command[0]);source_path = root / fixture
        audit.require(exe.is_relative_to(root) and image.is_relative_to(root), 'Native test inputs must remain in the workspace')
        audit.require(source_path.stat().st_mtime_ns <= exe.stat().st_mtime_ns <= junit.stat().st_mtime_ns,
                      'Native fixture changed or executable was rebuilt since the test report')
        paths = [exe, image, source_path, config, root / 'tests/effect_catalog_lifecycle_helpers.h']
        audit.require(paths[-1].stat().st_mtime_ns <= exe.stat().st_mtime_ns,
                      'Native lifecycle helper changed since this executable was built')
        if family != 'sky':
            dictionary = Path(command[2])
            audit.require(dictionary.is_relative_to(root) and command[-2] == family, 'Skin input/family command differs')
            paths.append(dictionary)
        audit.require(all(p.stat().st_mtime_ns <= junit.stat().st_mtime_ns for p in paths),
                      'Native input changed since the test report')
        inputs = [dict(path=p.resolve().relative_to(root).as_posix(), sha256=audit.sha(p.read_bytes())) for p in paths]
        markers = list(LIFECYCLE.finditer(text))
        expected_scope = 'original_bones_streams_screen_cache' if family != 'sky' else 'original_screen_cache'
        if len(markers) != 1 or markers[0][1] != source[2:] or int(markers[0][2]) != stride or \
           int(markers[0][3]) != relocated or markers[0][4] != expected_scope:
            gaps.append(dict(test=test, status='missing_or_mismatched_lifecycle_marker'));continue
        selected = [r for r in matrix['rows'] if r['kind'] == 'effect_pass' and r['source'] == source]
        audit.require(len(selected) == 2, 'Native padded fixture must select both exact original passes')
        for row in selected:
            cases.append(dict(catalog_id=row['id'], result='passed', scope='draw', phases=['create', 'use', 'release', 'malformed'],
                proof=proof, test=test, inputs=inputs,
                owner='original_fx_cpu_record', original_calls=['827019E8', '8273B4D0', '82701118', '826B7600'],
                qualification='Original FX registration, opaque/alpha pixel-checked draws, FX/cache retirement and stale-use rejection; backend mesh upload caches remain owner-resident, full GPU retirement unproven'))
        geometry_id = audit.identity('geometry_regression', source, stride, relocated, test)
        synthetic_rows.append(dict(id=geometry_id, kind='geometry_regression', synthetic=True,
            asset_group=None, name=test, effect_source=source,
            parameters=dict(stride=stride, attribute_offset_delta=relocated, original_valid=True,
                            declaration_rows_reordered=family=='sky'),
            qualification='Synthesized original-valid declaration/vertex input; no packaged mesh identity or gameplay encounter'))
        phases = ['create', 'use', 'release'] + (['malformed'] if negatives else [])
        cases.append(dict(catalog_id=geometry_id, result='passed', scope='draw', phases=phases,
            proof=proof, test=test, inputs=inputs, owner='declaration',
            original_calls=['82701BD8', '8273B4D0', '82700A78'],
            qualification='Original CPU declaration construction, pixel-checked use, original release and retired allocator generation rejection; backend mesh upload caches remain owner-resident, full GPU retirement unproven'))
    return dict(schema=1, complete=not gaps, cases=cases, synthetic_rows=synthetic_rows, gaps=gaps,
                limits=['Lifecycle evidence covers original declaration/FX/CPU cache owners; full GPU upload-cache retirement remains unproven.',
                        'Current fixture/executable/input identities are pinned; file times reject source changes or rebuilds since this CTest report.',
                        'Synthetic declarations are not linked to packaged assets.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', type=Path, required=True)
    parser.add_argument('--junit', type=Path, required=True)
    parser.add_argument('--ctest-config', type=Path, default=ROOT/'build/native/CTestTestfile.cmake')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    matrix, reference = audit.load(args.matrix.resolve())
    report = export(matrix, args.junit.resolve(), args.ctest_config.resolve())
    report['matrix'] = reference
    output = args.output.resolve()
    audit.require(output.is_relative_to(ROOT/'build') and output not in
                  (args.junit.resolve(), args.matrix.resolve(), args.ctest_config.resolve()), 'Invalid native evidence output')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(complete=report['complete'], synthetic_cases=len(report['synthetic_rows']), gaps=report['gaps'])))
    if not report['complete']:
        raise SystemExit(1)


if __name__=='__main__':
    main()
