#!/usr/bin/env python3
"""Export exact original draw/CPU lifetime receipts from independent CTests.

These fixtures exercise inherited blend, signed base offsets, and tangent
payloads. They do not identify a packaged geometry resource. Backend buffer
destruction is a separate scope from original asset retirement.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit
from export_geometry_draw_evidence import ctest_commands


@dataclass(frozen=True)
class Spec:
    source: str
    fixture: str
    executable: str
    arguments: tuple[str, ...]
    marker: str
    required: tuple[tuple[str, str], ...]
    native_pass: str
    dependencies: tuple[str, ...] = ()
    selected_technique: str | None = None


COMMON = (('create', 'passed'), ('use', 'passed'), ('declaration_release', 'passed'),
          ('fx_release', 'passed'), ('cpu_cache_release', 'passed'), ('stale_use', 'passed'),
          ('backend_mesh_cache', 'owner_resident'), ('full_gpu_retirement', 'unproven'))
BLEND = (('blend', '07060706'), ('enable', '1'), ('expanded', '0'),
         ('original_alpha_to_opaque', 'passed'))
SPECS: dict[str, Spec] = {}
for family, source in (('base', '0x82006348'), ('textured', '0x8200FB98'), ('dual', '0x8201CD48')):
    SPECS[f'OriginalSkin{family}InheritedPass'] = Spec(source, 'tests/test_skin_pass.cpp', 'SkinPassTests',
        ('{dictionary}', family, 'inherited'), 'AUDIT_SKIN_BLEND_LIFECYCLE', BLEND,
        f'PASS original skin {family}:')
for mode in ('positive', 'negative', 'selected'):
    SPECS[f'OriginalSkyBase_{mode}'] = Spec('0x82036448', 'tests/test_sky_base_pass.cpp',
        'OriginalSkyBasePassTests', (mode,), 'AUDIT_GEOMETRY_LIFECYCLE',
        (('stride', '28'), ('delta', '0'), ('base_case', mode)), f'PASS original sky base offset {mode}:',
        ('tests/test_sky_pass.cpp',))
SPECS['OriginalShadowTangentPass'] = Spec('0x820C0550', 'tests/test_shadow_camera_pass.cpp',
    'ShadowCameraPassTests', ('tangent',), 'AUDIT_SHADOW_TANGENT_LIFECYCLE',
    (('stride', '40'), ('tangent', '002A2187'), ('original_static_entry', '82707678'),
     ('malformed_scope', 'existing_original_queue_range')), 'PASS original shadows camera passes:',
    selected_technique='0x0007FFFC')
for family, source in (('base', '0x8200CCB8'), ('textured', '0x820168F8'), ('dual', '0x8202AD78'),
                       ('gloss', '0x82019988'), ('multitone', '0x820547E8'), ('normalmap', '0x82057E08')):
    SPECS[f'OriginalRigidInherited_{family}'] = Spec(source, 'tests/test_rigid_inherited_pass.cpp',
        'OriginalRigidInheritedPassTests', ('{dictionary}', family), 'AUDIT_RIGID_BLEND_LIFECYCLE',
        BLEND, f'PASS original rigid inherited blend {family}:',
        ('tests/test_rigid_dual_pass.cpp', 'tests/effect_fallback_blend_helpers.h'))
SPECS['OriginalSkyInheritedPass'] = Spec('0x82036448', 'tests/test_sky_inherited_pass.cpp',
    'OriginalSkyInheritedPassTests', (), 'AUDIT_SKY_BLEND_LIFECYCLE', BLEND,
    'PASS original sky inherited blend:', ('tests/test_sky_pass.cpp', 'tests/effect_fallback_blend_helpers.h'))
SPECS['OriginalSkyStripBoundaryPass'] = Spec('0x82036448', 'tests/test_sky_strip_boundary_pass.cpp',
    'OriginalSkyStripBoundaryPassTests', (), 'AUDIT_GEOMETRY_STRIP_BOUNDARY',
    (('original_setup', '8273B760'), ('original_draw', '82701220'), ('sdk_draw', '8244D360'),
     ('vertices', '5'), ('index_bytes', '131076'), ('selected_start', '1'), ('selected_count', '65536'),
     ('reset_position', '65530'), ('requested_cull', '2_6'), ('actual_opaque_cull', '0_2'),
     ('actual_alpha_cull', '0_2'), ('original_material_cull', 'passed'), ('original_packet_alpha', 'cleared'),
     ('sdk_packets', '65534_4'), ('sdk_advance', '65532'),
     ('independent_winding_pixels', 'passed'), ('original_packet_pixels', 'passed'),
     ('vertex_owner_release', 'passed'), ('index_owner_release', 'passed'), ('malformed', 'passed')),
    'PASS original sky strip boundary:', ('tests/test_sky_pass.cpp',))

MONO_SPECS = {}
MONO_RUNTIME = ('runtime/engine_recording.cpp', 'runtime/engine_recording.h', 'config/simpsons.toml', 'renderer/mono_draw.hlsl',
                'renderer/mono_mesh.cpp', 'renderer/mono_mesh.h',
                'renderer/screen_pipeline.cpp', 'renderer/native_backend.h')
for mode, technique, pass_handle, vertex, pixel in (
        ('opaque', '0x0003FFFC', '0003FFFE', '82120C04', '82122BD4'),
        ('alpha', '0x0007FFFC', '0007FFFE', '82121BE8', '82122D38')):
    MONO_SPECS[f'OriginalMonoImmediate_{mode}'] = Spec('0x8211F480', 'tests/test_mono_immediate_pass.cpp',
        'MonoImmediatePassTests', (mode,), 'AUDIT_MONO_IMMEDIATE_LIFECYCLE',
        (('technique', technique[2:]), ('pass', pass_handle), ('vertex', vertex), ('pixel', pixel),
         ('original_wrapper', '8273B4D0'), ('original_immediate', '827400F8'), ('malformed', 'passed')),
        f'PASS original mono immediate {mode}:', ('tests/test_mono_pass.cpp', *MONO_RUNTIME), technique)

MONO_RECORDING_SPECS = {}
for mode, flag2 in (('default', '0'), ('bit2', '1')):
    MONO_RECORDING_SPECS[f'OriginalMonoRecording_{mode}'] = Spec('0x8211F480',
        'tests/test_mono_recording_frontier.cpp', 'MonoRecordingFrontierTests', (mode,),
        'AUDIT_MONO_RECORDING_LIFECYCLE',
        (('technique', '0003FFFC'), ('pass', '0003FFFE'), ('vertex', '82120C04'), ('pixel', '82122BD4'),
         ('original_wrapper', '8273B4D0'), ('original_recording', '82740420'), ('flag2', flag2),
         ('original_context_setter', '826FF6D8'), ('context_store', '826FF6F0'), ('idle_publication', 'passed'),
         ('replay', 'passed'), ('inherited_matrix', 'passed'), ('payload_release', 'passed'),
         ('recording_context_release', 'passed'), ('malformed', 'passed')),
        f'PASS original mono recording {mode}:', ('tests/test_mono_pass.cpp', *MONO_RUNTIME), '0x0003FFFC')


def parse_marker(text, name):
    lines = [line for line in text.splitlines() if line.startswith(name + ' ')]
    if len(lines) != 1:
        return None
    words = lines[0].split()[1:]
    if any(not re.fullmatch(r'[a-z][a-z0-9_]*=[A-Za-z0-9_]+', word) for word in words):
        return None
    pairs = [word.split('=', 1) for word in words]
    if len({key for key, _ in pairs}) != len(pairs):
        return None
    return dict(pairs)


def export(matrix, junit, config, root=ROOT, include_mono=False, include_mono_recording=False):
    specs = dict(SPECS)
    if include_mono:
        specs.update(MONO_SPECS)
    if include_mono_recording:
        specs.update(MONO_RECORDING_SPECS)
    testcases = {}
    for node in ET.fromstring(junit.read_bytes()).iter('testcase'):
        name = node.get('name')
        if name in specs:
            audit.require(name not in testcases, 'Duplicate native CTest testcase')
            testcases[name] = node
    commands = ctest_commands(config.read_text(encoding='utf-8'))
    proof = dict(path=junit.relative_to(root).as_posix(), sha256=audit.sha(junit.read_bytes()))
    cases, synthetic, gaps = [], [], []
    references = {}
    for test, spec in specs.items():
        node = testcases.get(test)
        if node is None or any(node.find(tag) is not None for tag in ('failure', 'error', 'skipped')):
            gaps.append(dict(test=test, status='not_passing'));continue
        output = '\n'.join(node.itertext()).replace('\r\n', '\n')
        fields = parse_marker(output, spec.marker)
        expected = dict(COMMON + spec.required + (('source', spec.source[2:]),))
        if fields is None or any(fields.get(key) != value for key, value in expected.items()):
            gaps.append(dict(test=test, status='missing_or_mismatched_lifecycle_marker'));continue
        if spec.native_pass not in output or any(line.startswith('FAIL ') for line in output.splitlines()):
            gaps.append(dict(test=test, status='missing_native_pass'));continue
        command = commands.get(test, [])
        if len(command) != 2 + len(spec.arguments) or Path(command[0]).stem != spec.executable or any(
                expected != '{dictionary}' and actual != expected
                for actual, expected in zip(command[2:], spec.arguments)):
            gaps.append(dict(test=test, status='command_mismatch'));continue
        paths = [Path(command[0]), Path(command[1]), root / spec.fixture, config,
                 root / 'tests/effect_catalog_lifecycle_helpers.h', root / 'tests/effect_draw_cleanup_helpers.h']
        paths += [root / dependency for dependency in spec.dependencies]
        paths += [Path(value) for value, expected in zip(command[2:], spec.arguments) if expected == '{dictionary}']
        # Include production selectors and native shader profile tables. This
        # rejects a receipt if a newer source edit has not been rebuilt.
        paths += [root / value for value in ('runtime/engine_effects.cpp', 'runtime/engine_driver.cpp',
                                             'runtime/rigid_profile.h', 'runtime/skin_profile.h')]
        exe = paths[0]
        audit.require(all(path.is_relative_to(root) for path in paths), 'Native inputs leave the workspace')
        audit.require(all(path.stat().st_mtime_ns <= exe.stat().st_mtime_ns for path in paths[2:] if path != config),
                      'Native source/helper changed since this executable was built')
        audit.require(all(path.stat().st_mtime_ns <= junit.stat().st_mtime_ns for path in paths),
                      'Native input/config changed since the test report')
        inputs = []
        for path in paths:
            if path not in references:
                references[path] = dict(path=path.relative_to(root).as_posix(), sha256=audit.sha(path.read_bytes()))
            inputs.append(references[path])
        selected = [row for row in matrix['rows'] if row['kind'] == 'effect_pass' and row['source'] == spec.source and
                    (spec.selected_technique is None or row['technique_handle'] == spec.selected_technique)]
        audit.require(len(selected) == (1 if spec.selected_technique else 2), 'Original draw pass association differs')
        if spec.marker in ('AUDIT_RIGID_BLEND_LIFECYCLE', 'AUDIT_SKY_BLEND_LIFECYCLE'):
            pairs = {(fields.get(prefix + '_vertex'), fields.get(prefix + '_pixel')) for prefix in ('opaque', 'alpha')}
            audit.require(pairs == {(row['vertex'][2:], row['pixel'][2:]) for row in selected},
                          'Native marker shader pairs do not match the exact catalog passes')
        if spec.selected_technique:
            row = selected[0]
            if spec.source == '0x820C0550':
                audit.require((row['vertex'], row['pixel']) == ('0x820C2FA0', '0x00000000'),
                              'Tangent fixture may credit only the original static depth pair')
            else:
                audit.require((row['vertex'][2:], row['pixel'][2:], row['pass_handle'][2:]) ==
                              (fields['vertex'], fields['pixel'], fields['pass']), 'Mono selected pair differs')
        phases = ['create', 'use', 'release'] + (['malformed'] if fields.get('malformed') == 'passed' else [])
        qualification = ('Whole original draw, pixel/ABI checks, original declaration/FX/CPU-cache retirement and stale-use rejection; '
                         'backend mesh upload caches remain owner-resident, full GPU retirement unproven')
        if spec.marker == 'AUDIT_MONO_RECORDING_LIFECYCLE':
            qualification = ('Whole original public dispatcher recording/default-or-bit2 selection, cached replay and inherited-matrix pixels; '
                             'genuine original idle context publication, registered plugin payload retirement, recording-context/declaration/FX/CPU-cache release and malformed/stale rejection; '
                             'backend mesh upload caches remain owner-resident, full GPU retirement unproven')
        for row in selected:
            cases.append(dict(catalog_id=row['id'], result='passed', scope='draw', phases=phases, proof=proof,
                test=test, inputs=inputs, owner='original_fx_cpu_record', qualification=qualification))
        geometry_id = audit.identity('geometry_regression', test, spec.source)
        synthetic.append(dict(id=geometry_id, kind='geometry_regression', synthetic=True, asset_group=None,
            name=test, effect_source=spec.source, parameters=fields,
            qualification='Synthetic original-valid producer case; no packaged geometry identity or gameplay encounter'))
        cases.append(dict(catalog_id=geometry_id, result='passed', scope='draw', phases=phases, proof=proof,
            test=test, inputs=inputs, owner='declaration', qualification=qualification))
    return dict(schema=1, complete=not gaps, cases=cases, synthetic_rows=synthetic, gaps=gaps,
        limits=['Original FX/CPU/declaration retirement and backend destructor scopes remain distinct.',
                'A native marker and passing independent case are required; source alone grants no test credit.',
                'No packaged mesh/VFX identity or gameplay action is inferred from these synthetic inputs.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--matrix', type=Path, required=True)
    parser.add_argument('--junit', type=Path, required=True)
    parser.add_argument('--ctest-config', type=Path, default=ROOT / 'build/native/CTestTestfile.cmake')
    parser.add_argument('--include-mono', action='store_true')
    parser.add_argument('--include-mono-recording', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    matrix, reference = audit.load(args.matrix.resolve())
    report = export(matrix, args.junit.resolve(), args.ctest_config.resolve(), include_mono=args.include_mono,
                    include_mono_recording=args.include_mono_recording)
    report['matrix'] = reference
    output = args.output.resolve()
    audit.require(output.is_relative_to(ROOT / 'build') and output not in
                  (args.matrix.resolve(), args.junit.resolve(), args.ctest_config.resolve()), 'Invalid evidence output')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(complete=report['complete'], synthetic_cases=len(report['synthetic_rows']), gaps=report['gaps'])))
    if not report['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
