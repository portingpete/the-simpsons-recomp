#!/usr/bin/env python3
"""Persist restrictive-check candidates separately from original producer proof.

This lexical census is intentionally conservative. A found numeric check or
rejection string is a review candidate, not proof that valid input is rejected.
Balanced C++ check calls retain their complete original expression; other
keyword hits retain the exact original line. Documented producer findings and
unresolved catalog populations are separate, explicitly qualified records.
"""
from __future__ import annotations
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_mission_asset_support as audit

KEYWORDS = re.compile(r'unsupported|unqualified|whitelist|allowlist|fixed[ _-]?(?:size|length)|\w*stride\w*|\w*flags?\w*', re.I)
TOKENS = re.compile(r'R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
CALLS = re.compile(r'\b(?:need|require|supported|assert|PPC_RECOMP_FAILURE)\s*\(')
NUMBER_CHECK = re.compile(r'(?:==|!=|<=|>=|<|>)\s*(?:0x[0-9a-f]+|\d+)[ul]*\b|\b(?:0x[0-9a-f]+|\d+)[ul]*\s*(?:==|!=|<=|>=|<|>)', re.I)
EXTENSIONS = {'.cpp', '.h', '.hpp', '.c', '.cc', '.inl', '.hlsl', '.hlsli', '.glsl'}
SOURCE_FOLDERS = ('runtime', 'renderer', 'audio', 'common', 'app')
DISPOSITIONS = {
    'proven_valid_producer_rejection': 'A pinned original producer establishes an input rejected by the former or current native path; implementation and lifecycle receipts are separate.',
    'proven_producer_cap': 'An explicit original producer encoding, allocation or literal establishes a bound only for the named scope.',
    'unqualified_offline_domain': 'Packaged identity or SDK capacity does not establish the offline serializer or consumed semantics.',
    'untested_create_use_release_path': 'The complete original creation, actual use and owner retirement path requires independent native receipts.',
    'mechanical_candidate_untriaged': 'A lexical guard/line has no individual original-producer validity conclusion from this scan.',
}


def mask_cpp(text):
    return TOKENS.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text)


def categories(expression):
    result = []
    lowered = expression.lower()
    if re.search(r'unsupported|unqualified|whitelist|allowlist', lowered):
        result.append('explicit_support_rejection')
    if re.search(r'\w*stride\w*', lowered):
        result.append('geometry_stride')
    if re.search(r'\w*flags?\w*|\bbit[12]\b', lowered):
        result.append('flags_or_state')
    numeric = NUMBER_CHECK.search(mask_cpp(expression))
    if numeric and re.search(r'\w*(?:size|length|bytes|count|capacity|ring|extent|width|height|level|pitch|bones)\w*', lowered):
        result.append('fixed_size_or_numeric_bound')
    if re.search(r'\w*(?:shader|effect|pixel|vertex)\w*', lowered) and (numeric or 'unqualified' in lowered or 'unsupported' in lowered):
        result.append('shader_or_vertex_profile')
    if numeric and re.search(r'basevertex|vertices|indices', lowered):
        result.append('geometry_index_range')
    return result


def domain(path):
    name = str(path).lower()
    if 'audio' in name or 'exm0' in name:
        return 'audio'
    if any(word in name for word in ('vertices', 'mesh', 'rigid_profile', 'skin_profile', 'declaration')):
        return 'geometry'
    if any(word in name for word in ('texture', 'itxd')):
        return 'texture'
    if any(word in name for word in ('effect', 'shader', 'particle', 'screen')):
        return 'effects_shaders'
    return 'platform_state_other'


def scan_file(path, root=ROOT):
    raw = path.read_bytes();text = raw.decode('utf-8')
    relative = path.relative_to(root).as_posix()
    masked = mask_cpp(text)
    newlines = [m.start() for m in re.finditer('\n', text)]
    line = lambda offset: bisect_right(newlines, offset) + 1
    guards, spans = [], []
    for match in CALLS.finditer(masked):
        depth, end = 1, match.end()
        while depth and end < len(masked):
            depth += (masked[end] == '(') - (masked[end] == ')');end += 1
        if depth:
            continue
        expression = text[match.start():end]
        tags = categories(expression)
        if not tags:
            continue
        identifier = audit.identity('source_guard', relative, match.start(), expression)
        guards.append(dict(id=identifier, status='mechanical_candidate', file=relative,
            line=line(match.start()), end_line=line(end-1), domain=domain(relative), categories=tags,
            expression=expression, expression_kind='balanced_check_call',
            producer_validity='unestablished_by_this_scan', disposition='mechanical_candidate_untriaged'))
        spans.append((match.start(), end, identifier))
    hits, position = [], 0
    for number, original in enumerate(text.splitlines(keepends=True), 1):
        terms = [m[0] for m in KEYWORDS.finditer(original)]
        if terms:
            enclosing = next((ident for start, end, ident in spans if start < position + len(original) and end > position), None)
            hits.append(dict(id=audit.identity('source_line', relative, number, original.strip()),
                status='mechanical_candidate', file=relative, line=number, domain=domain(relative),
                terms=terms, expression=original.rstrip('\r\n'), expression_kind='original_source_line',
                related_guard=enclosing, producer_validity='unestablished_by_this_scan',
                disposition='mechanical_candidate_untriaged'))
        position += len(original)
    return dict(path=relative, sha256=audit.sha(raw), bytes=len(raw)), guards, hits


def document_reference(path, needle, root=ROOT):
    source = root / path;raw = source.read_bytes();lines = raw.decode('utf-8').splitlines()
    found = [i for i, line in enumerate(lines, 1) if needle in line]
    audit.require(len(found) == 1, 'Producer document anchor changed: ' + path + ' ' + needle)
    return dict(path=path, line=found[0], sha256=audit.sha(raw), authority='documented_original_producer')


def census_sources(root=ROOT):
    manifest, guards, lines = [], [], []
    for folder in SOURCE_FOLDERS:
        for path in sorted((root / folder).rglob('*')):
            if path.is_file() and path.suffix.lower() in EXTENSIONS:
                reference, checks, hits = scan_file(path, root)
                manifest.append(reference);guards += checks;lines += hits
    return manifest, guards, lines


def run(root=ROOT, effect_report=None, producer_report=None, texture_producer_report=None, packaged_report=None):
    manifest, guards, lines = census_sources(root)
    refs = {
        'audio_ring': document_reference('docs/audio-restriction-audit.md', 'structural ring bounds', root),
        'audio_seek': document_reference('docs/audio-restriction-audit.md', 'One substantive production restriction remains:', root),
        'geometry_stride': document_reference('docs/restrictive-geometry-audit.md', 'aligned strides that round-trip', root),
        'geometry_base': document_reference('docs/restrictive-geometry-audit.md', 'All six mesh backends', root),
        'producer_fixed_sizes': document_reference('docs/restrictive-geometry-audit.md', 'Other reviewed fixed sizes', root),
        'catalog': document_reference('docs/catalog-support-audit.md', '| Registered effects |', root),
        'pipeline_capability': document_reference('docs/restrictive-pipeline-capability-audit.md', 'Bit16 of', root),
        'copied_itxd_lifetime': document_reference('docs/restrictive-copied-itxd-range-audit.md', 'Generic loader', root),
    }
    findings = [
        dict(id='audio_ring', status='repaired_documented_producer_bound', domain='audio',
            valid_bound='0..0x7FFFFFF0 bytes, aligned16, with exact original single-precision arithmetic witness; no original64-live-group quota',
            original_calls=['82330540', '82330614', '82330620', '82330628', '8238C8A4'], reference=refs['audio_ring']),
        dict(id='geometry_stride', status='repaired_documented_producer_bound', domain='geometry',
            valid_bound='4..1020 nonzero DWORD-aligned byte stride; unique consumed semantic/index fields must fit their record',
            original_calls=['8243C5D8', '8243C6A8', '8243C6B4', '8245EE64', '8245EE94'], reference=refs['geometry_stride']),
        dict(id='producer_fixed_sizes', status='retained_documented_original_profile', domain='effects_shaders',
            valid_bound='Reflection literal256/16; shadow surfaces literal1024 and border literal32 at their established producers',
            original_calls=['826FF140', '826FF18C', '827065F0', '82706600', '82706664'], reference=refs['producer_fixed_sizes']),
        dict(id='pipeline_capability_argument', status='source_repaired_original_producer_contract', domain='platform_state_other',
            valid_bound='Whole82416C58 producer supplies r6=0 when capsbit16 is set, else2; SDK82441A08 never reads/retains that argument. Exact size1FFFE/type1/flags8 and cap correlation remain required.',
            original_calls=['82416C58', '82416C80', '82416C88', '82416C8C', '82441A08', '82416BC8'],
            reference=refs['pipeline_capability']),
    ]
    queue = [
        dict(id='audio_seek', status='remaining_documented_original_path', domain='audio',
            problem='Zero-only seek/config fields reject the original nonzero command-metadata path',
            next_case='Census original command metadata; run original seek/decode/stop/restart/release and malformed metadata cases',
            original_calls=['82341F78', '82342058', '82342748', '8234EB50'], reference=refs['audio_seek']),
        dict(id='geometry_base', status='source_repaired_original_producer_contract', domain='geometry',
            problem='Former baseVertex0/65535 vertex caps confused the independently transported signed submesh+10 base with original index-count splitting; source now validates selected effective indices',
            next_case='Require fresh original positive/negative/selected-range create/use/release and malformed native receipts; source/document presence alone grants no tested coverage',
            original_calls=['82706444', '8244D374', '8244D5EC'], reference=refs['geometry_base']),
        dict(id='geometry_vfx_catalog', status='unresolved_inventory_semantics', domain='geometry',
            problem='5533 packaged mesh and8770 VFX occurrences require exact serialized declaration/stride/material/emitter associations and native lifetimes; optional source-pinned field inventory does not qualify full shader/emitter semantics',
            next_case='Inventory exact original relocated fields, qualify material/module continuations, attach exact packaged ownership identity and run independent original create/use/release before claiming mission combinations', reference=refs['catalog']),
        dict(id='itxd_borrowed_mode_and_auxiliary', status='source_branch_known_caller_owner_unqualified', domain='texture',
            problem='Generic826F24D8 admits any nonzero low-byte mode with in-place relocation and no-op final destructor; normal named caller uses0. Auxiliary+B0 is relocated and copied destructor imposes no zero-only rule.',
            valid_bound='Generic mode low8=1..255 is source-established; backing owner, real caller and auxiliary serialized consumer domain remain unqualified.',
            next_case='Locate an actual caller/backing owner and auxiliary serializer/consumer; independently execute original publication/use/owner-release before widening native qualification.',
            original_calls=['826F24D8', '826F24EC', '826F26B0', '826F8AE8', '82736D50', '82736DF0'],
            reference=refs['copied_itxd_lifetime']),
    ]
    authority = dict(source_files=manifest, source_folders=list(SOURCE_FOLDERS), related_documents=refs)
    if packaged_report:
        fields, reference = audit.load(packaged_report)
        audit.require(fields.get('scope') == 'source_pinned_offline_packaged_fields', 'Packaged field scope differs')
        audit.require(fields['catalog_sha256'] == audit.sha((root/'analysis/assets.json').read_bytes()), 'Packaged field census identity differs')
        audit.require(fields['tool_sha256'] == audit.sha((root/'tools/audit_packaged_mesh_vfx.py').read_bytes()), 'Packaged field parser changed since inventory')
        audit.require(fields['image_sha256'] == audit.sha((root/'analysis/simpsons.pe').read_bytes()), 'Packaged field original image identity differs')
        authority['packaged_fields_report'] = reference
        item = next(item for item in queue if item['id'] == 'geometry_vfx_catalog')
        item['source_fields_summary'] = fields['summary']
        item['source_fields_remaining_unknown'] = ['Material continuation to native FX/pass', 'VFX module-specific layouts',
                                                  'Consumed declaration/fetch validity', 'Actual runtime payload identity',
                                                  'Independent original native create/use/release for each packaged combination']
        item['source_field_evidence_reference'] = reference
    if producer_report:
        producers, reference = audit.load(producer_report)
        authority['shader_producer_report'] = reference
        engine = producers['authority']['engine']
        audit.require(audit.sha((root / engine['path']).read_bytes()) == engine['sha256'],
                      'Shader producer report uses stale native admission source')
        for finding in producers['findings']:
            if finding['status'] in ('original_valid_unported_public_path', 'native_source_continuation_pending_test'):
                queue.append(dict(id=finding['id'], status=finding['status'], domain='effects_shaders',
                    problem=finding['native_rejection']['expression'], original_source=finding['source'],
                    valid_bound=finding['valid_flag'], selections=finding['selections'],
                    original_callers=finding['original_callers'], current_route=finding['current_route'],
                    next_case=finding['next_case'], reference=reference))
        if producers.get('texture_scope') and not texture_producer_report:
            queue.append(dict(id='texture_profile_producer_bounds', status='producer_bound_unestablished', domain='texture',
                problem='All shipped dictionary metadata was admitted, but qualified decoder format/dimension/mip profiles have no complete original producer bound in this audit',
                shipped_scope=producers['texture_scope'],
                next_case='Trace original native descriptor producer and reader; reproduce newly valid formats or dimensions through original copy/create/use/release, independently retaining malformed bounds',
                reference=reference))
    if texture_producer_report:
        textures,reference=audit.load(texture_producer_report)
        authority['texture_producer_report']=reference
        for source in textures['authority']['sources']:
            audit.require(audit.sha((root/source['path']).read_bytes())==source['sha256'],
                          'Texture producer report uses stale native admission source')
        findings.append(dict(id='texture_sdk_caps',status='established_separate_original_sdk_scope',domain='texture',
            valid_bound='Ordinary compatible type3 2D texture requirements normalize dimensions1..8192; native copied ITXD serialization validity remains separate',
            established_scopes=textures['established_scopes'],reference=reference))
        for item in textures['actionable_queue']:
            queue.append(dict(item,domain='texture',reference=reference))
    if effect_report:
        effects, reference = audit.load(effect_report)
        missing = [dict(effect=p['effect'], source=p['source'], vertex=p['vertex'], pixel=p['pixel'],
                        technique_handle=p['technique_handle'], pass_handle=p['pass_handle'],
                        missing_native_artifacts=p['missing_native_artifacts']) for p in effects['passes'] if p['missing_native_artifacts']]
        authority['effect_report'] = reference
        queue.append(dict(id='shader_artifact_catalog', status='unresolved_catalog_support', domain='effects_shaders',
            problem=f'{len(missing)} original registered passes lack required native shader artifacts', combinations=missing,
            next_case='Select each original catalog pass independently; implement its real input/state contract and pixel-checked create/use/release regressions', reference=refs['catalog']))
    for item in queue:
        if item['id'] in ('geometry_base', 'mono_public_alpha'):
            item['dispositions'] = ['proven_valid_producer_rejection', 'untested_create_use_release_path']
            item['rejection_state'] = 'source_repaired_or_unported_as_recorded_above; require_current_receipts'
        elif item['id'] == 'geometry_vfx_catalog':
            item['dispositions'] = ['unqualified_offline_domain', 'untested_create_use_release_path']
        elif item['domain'] == 'texture':
            item['dispositions'] = ['unqualified_offline_domain']
        else:
            item['dispositions'] = ['untested_create_use_release_path']
        if item['id'] == 'audio_seek':
            item['authored_nonzero_input'] = 'unestablished; configured selector0/start0 fixtures do not qualify this input'
        item['related_candidate_ids'] = [g['id'] for g in guards if g['domain'] == item['domain']]
        item['qualification'] = 'Related candidates are lexical associations, not individual producer-validity conclusions'
    for item in findings:
        item['dispositions'] = ['proven_producer_cap']
        if item['id'] in ('audio_ring', 'geometry_stride'):
            item['dispositions'].insert(0, 'proven_valid_producer_rejection')
            item['rejection_state'] = 'source_repaired; independent current lifecycle receipts remain separate'
        if item['id'] == 'pipeline_capability_argument':
            item['dispositions'] = ['proven_valid_producer_rejection']
            item['rejection_state'] = 'source_repaired; independent original init/use/readback/cleanup receipts are reported separately; this source census grants no execution credit'
    grouped = {}
    for key in sorted({g['domain'] for g in guards} | {h['domain'] for h in lines}):
        grouped[key] = dict(guard_candidates=sum(g['domain'] == key for g in guards),
            keyword_lines=sum(h['domain'] == key for h in lines),
            files=sorted({g['file'] for g in guards + lines if g['domain'] == key}))
    return dict(schema=1, authority=authority,
        limits=['Lexical matches are review candidates; original validity and malfunction are not inferred.',
                'Balanced expressions recover named check calls, not a complete C++ control-flow graph.',
                'Documented producer findings qualify only their explicit scopes; unrelated checks remain unestablished.',
                'Static shader artifacts and opaque packaged populations do not prove runtime encounters or complete GPU lifetimes.'],
        disposition_definitions=DISPOSITIONS,
        summary=dict(source_files=len(manifest), guard_candidates=len(guards), keyword_lines=len(lines),
                     guard_dispositions=dict(Counter(g['disposition'] for g in guards)),
                     documented_dispositions=dict(Counter(d for item in findings + queue for d in item['dispositions'])),
                     categories=dict(sorted(Counter(tag for g in guards for tag in g['categories']).items()))),
        groups=grouped, guard_candidates=guards, keyword_lines=lines,
        documented_producer_findings=findings, actionable_queue=queue)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--effect-report', type=Path)
    parser.add_argument('--producer-report', type=Path)
    parser.add_argument('--texture-producer-report', type=Path)
    parser.add_argument('--packaged-report', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/restrictive-check-audit/source-census.json')
    args = parser.parse_args()
    report = run(effect_report=args.effect_report.resolve() if args.effect_report else None,
                 producer_report=args.producer_report.resolve() if args.producer_report else None,
                 texture_producer_report=args.texture_producer_report.resolve() if args.texture_producer_report else None,
                 packaged_report=args.packaged_report.resolve() if args.packaged_report else None)
    output = args.output.resolve()
    audit.require(output.is_relative_to(ROOT / 'build') and
                  output not in [p.resolve() for p in (args.effect_report,args.producer_report,args.texture_producer_report,args.packaged_report) if p],
                  'Source census must remain in build and must not replace its input')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report['summary'], sort_keys=True))
    print('Report:', output)


if __name__ == '__main__':
    main()
