#!/usr/bin/env python3
"""Join every shipped asset group to conservative native support evidence.

Inventory is independent of execution. Global FX registrations have no proved
mission association. A source-only encounter never proves a technique/pass,
packaged mesh, emitter, or texture identity. Explicit test receipts must cover
create/use/release before this report calls a combination lifecycle-tested.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
KINDS = {'VFX': 'vfx', 'EARS_MESH': 'geometry', 'EARS_ITXD': 'texture_dictionary',
         'SBK': 'sound_bank', 'AMX': 'sound_bank'}


def require(ok, why):
    if not ok:
        raise ValueError(why)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    raw = path.read_bytes()
    return json.loads(raw), dict(path=path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path),
                                sha256=sha(raw))


def asset_group(source):
    """Archive directory is provenance, not proof of a runtime mission."""
    return source.replace('\\', '/').split('/')[0]


def identity(kind, *parts):
    return kind + ':' + sha(json.dumps(parts, separators=(',', ':'), ensure_ascii=True).encode())


def coverage(status='unknown', detail=None):
    return dict(implemented=status, implementation_detail=detail, tested=False, setup_tested=False,
                draw_tested=False, lifecycle_tested=False, use_kind=None, malformed_inputs_rejected=False,
                encountered_runtime=False, encountered_gameplay=False,
                encounter_sequences=[], test_evidence=[])


def resource_inventory(assets):
    resources, archives = [], []
    for f in assets['files']:
        if f['extension'] == '.str':
            archives.append(dict(path=f['path'], asset_group=asset_group(f['path']),
                                 sha256=f['sha256'], bytes=f['size']))
        for e in f.get('inspection', {}).get('entries', []):
            for c in e.get('chunks', []):
                kind = KINDS.get(c.get('type_name'))
                if kind is None:
                    continue
                resources.append(dict(id=identity(kind, f['path'], e['index'], c['payload_decoded_offset'], c['name'], c['payload_sha256']),
                    kind=kind, source=f['path'], asset_group=asset_group(f['path']), entry=e['index'],
                    name=c['name'], payload_sha256=c['payload_sha256'], payload_bytes=c['payload_size'],
                    payload_decoded_offset=c['payload_decoded_offset'],
                    authored_source=c.get('source_path'), parameters='opaque', coverage=coverage()))
    return resources, archives


def join_packaged_fields(resources, report, catalog_sha):
    """Exact archived occurrence joins source fields without granting coverage."""
    require(report.get('schema_version') == 1 and
            report.get('scope') == 'source_pinned_offline_packaged_fields',
            'Unsupported packaged field report scope')
    require(report.get('catalog_sha256') == catalog_sha, 'Packaged field audit uses a different asset census')
    require(report.get('image_sha256') == sha((ROOT/'analysis/simpsons.pe').read_bytes()),
            'Packaged field audit uses a different original image')
    require(report.get('tool_sha256') == sha((ROOT/'tools/audit_packaged_mesh_vfx.py').read_bytes()),
            'Packaged field parser changed since source inventory')
    by_key = {(r['kind'], r['source'], r['entry'], r['payload_decoded_offset'], r['name'], r['payload_sha256']): r
              for r in resources if r['kind'] in ('geometry', 'vfx')}
    seen = set()
    for field_row in report['rows']:
        key = tuple(field_row[k] for k in ('kind', 'source', 'entry', 'payload_decoded_offset', 'name', 'payload_sha256'))
        require(key in by_key and key not in seen, 'Packaged field occurrence has a missing or duplicate identity')
        resource = by_key[key]
        require(field_row['payload_bytes'] == resource['payload_bytes'], 'Packaged field byte extent differs')
        require(not any(field_row.get(k, False) for k in ('native_setup_tested', 'native_use_tested', 'native_release_tested',
                                                        'encountered_runtime', 'encountered_gameplay')),
                'Offline field report carries unqualified native coverage')
        resource['parameters'] = field_row['parameters']
        resource['source_fields_status'] = field_row['parameters']['status']
        seen.add(key)
    return dict(joined=len(seen), catalog_occurrences=len(by_key),
                remaining_without_fields=len(by_key) - len(seen),
                archive_failures=report.get('archive_failures', []),
                scope='Source fields only; native support and all coverage unchanged')


def effect_inventory(report):
    rows = []
    for p in report['passes']:
        selected = bool(p['runtime_selection']) and not p['missing_native_artifacts']
        row = dict(p)
        row.update(id=identity('effect_pass', p['source'], p['technique_handle'], p['pass_handle']),
                   kind='effect_pass', asset_group=None,
                   coverage=coverage('declared_selection' if selected else
                       'missing_artifacts' if p['missing_native_artifacts'] else 'artifact_only',
                       p['runtime_selection']))
        # Older free-form native logs identify shader pairs only. Their first
        # line is retained as legacy reachability, never converted to gameplay
        # or complete create/use/release evidence.
        row['legacy_pair_observation'] = row.pop('observed')
        rows.append(row)
    return rows


def direct_particle_inventory(report):
    rows = []
    for selection in report.get('direct_particles', {}).get('matrix', []):
        row = dict(selection)
        row.update(id=identity('particle_sdk_pass', row['vertex'], row['pixel']),
            kind='particle_sdk_pass', source=None, asset_group=None,
            producer_calls=['82772D94', '82772F94'],
            coverage=coverage('declared_selection', 'Original direct particle SDK selection; distinct from registered particles FX and opaque emitters'))
        row['legacy_pair_observation'] = row.pop('observed', None)
        rows.append(row)
    return rows


def texture_inventory(report, resources):
    dictionaries = {(r['source'], r['entry'], r['name'], r['payload_sha256']): r
                    for r in resources if r['kind'] == 'texture_dictionary'}
    for d in report['dictionaries']:
        key = (d['source'], d['entry'], d['name'], d['original_payload_sha256'])
        require(key in dictionaries, 'Texture dictionary does not reconcile to original asset census')
        dictionaries[key]['parameters'] = dict(textures=d['textures'])
        dictionaries[key]['coverage'] = coverage('metadata_parsed' if d['textures'] is not None else 'metadata_rejected')
    rows = []
    for t in report['textures']:
        row = dict(t)
        row.update(id=identity('texture', t['source'], t['entry'], t['dictionary'], t['record_offset'], t['metadata_sha256']),
                   kind='texture', asset_group=asset_group(t['source']),
                   coverage=coverage('metadata_admitted' if t['admission'] == 'admitted_metadata' else 'metadata_rejected'))
        rows.append(row)
    return rows


def sound_inventory(streamed, resident, amx):
    """Each cue retains its original offset/owner; no mission inferred for SNU."""
    import audit_audio_runtime_coverage as audio
    rows, validation = [], {}
    for key, value, owners in (('resident', resident, 'banks'), ('amx', amx, 'payloads')):
        validation[key] = audio.resident_report(value, owners)
        for owner in value[owners]:
            occurrences = owner.get('occurrences', [owner])
            for occurrence in occurrences:
                source, entry = occurrence['container_path'], occurrence['entry_index']
                for cue in owner['cues']:
                    row = dict(id=identity('sound_cue', key, source, entry, owner['payload_sha256'], cue['header_offset']),
                        kind='sound_cue', catalog=key, source=source, asset_group=asset_group(source), entry=entry,
                        bank=owner['name'], payload_sha256=owner['payload_sha256'], header_offset=cue['header_offset'],
                        header_hex=cue['header_hex'].lower(),
                        channels=cue['channels'], playback_rate=cue['playback_rate'], loop=cue['loop'],
                        loop_start_sample=cue['loop_start_sample'], frames=cue['frames'],
                        block_sha256=[b['sha256'] for b in cue['blocks']],
                        codec_rates=sorted({b['codec_rate'] for b in cue['blocks']}),
                        coverage=coverage('metadata_admitted'))
                    violations = [v for v in validation[key]['violations'] if
                        v.get('source') in (source, owner['name'], owner['payload_sha256']) and
                        ('cue' not in v or v['cue'] == cue['header_offset'])]
                    if violations:
                        row['coverage'] = coverage('metadata_rejected', violations)
                    rows.append(row)
    for s in streamed['stream_inventory']:
        rows.append(dict(s, id=identity('sound_stream', s['path'], s['ordinal'], s['header_offset']),
                         kind='sound_stream', source=s['path'], asset_group=None,
                         coverage=coverage('cataloged', 'Native format/storage audit is a separate report; live scheduling unproved')))
    return rows, validation


def reconcile_sound_catalogs(assets, resources, streamed, resident, amx):
    files = {f['path']: f for f in assets['files']}
    banks = {(r['source'], r['entry'], r['name'], r['payload_sha256']): r
             for r in resources if r['kind'] == 'sound_bank'}
    for owner in resident['banks'] + amx['payloads']:
        for occurrence in owner.get('occurrences', [owner]):
            source = occurrence['container_path']
            require(source in files and files[source]['sha256'] == occurrence['container_sha256'],
                    'Sound catalog source does not reconcile to original asset census')
            key = (source, occurrence['entry_index'], owner['name'], owner['payload_sha256'])
            require(key in banks, 'Sound catalog owner does not reconcile to original resource census')
            banks[key]['parameters'] = dict(cues=len(owner['cues']))
            banks[key]['coverage'] = coverage('metadata_parsed')
    for source in streamed['source_provenance']:
        require(source['path'] in files and files[source['path']]['sha256'] == source['source_sha256'],
                'Stream catalog source does not reconcile to original asset census')


def parse_encounters(text):
    events, errors = [], []
    required = ('kind', 'asset', 'caller', 'parameters', 'ownership', 'mission', 'last_action', 'sequence')
    for line, raw in enumerate(text.splitlines(), 1):
        if not raw.strip():
            continue
        try:
            e = json.loads(raw)
            require(isinstance(e, dict), 'Encounter must be a JSON object')
            require(e.get('schema') == 1 and e.get('event') in ('encounter', 'failure', 'release', 'shutdown', 'lifecycle'), 'Unsupported encounter schema/event')
            require(all(k in e for k in required), 'Incomplete encounter provenance')
            require(all(isinstance(e[k], str) for k in ('kind', 'asset', 'mission', 'last_action')),
                    'Invalid encounter identity/context type')
            require(isinstance(e['caller'], str) or (type(e['caller']) is int and 0 <= e['caller'] <= 0xffffffff),
                    'Invalid original caller type/range')
            require(isinstance(e['parameters'], (dict, str)) and isinstance(e['ownership'], (dict, str)),
                    'Invalid encounter parameter/ownership type')
            require(type(e['sequence']) is int and e['sequence'] >= 0, 'Invalid encounter sequence')
            e['log_line'] = line
            events.append(e)
        except (ValueError, TypeError) as error:
            errors.append(dict(line=line, error=str(error), raw=raw))
    return events, errors


def key_values(value):
    if isinstance(value, dict):
        return value
    if not isinstance(value, str):
        return {}
    return dict(re.findall(r'\b([A-Za-z_][A-Za-z_0-9]*)=([^\s;]+)', value))


def event_parameters(event):
    return key_values(event['parameters'])


def hexadecimal(value):
    if isinstance(value, int):
        return f'0x{value:08X}'
    if isinstance(value, str) and re.fullmatch(r'(?:0x)?[0-9a-fA-F]{8}', value):
        return f'0x{int(value, 16):08X}'
    return None


def original_handle(value):
    if type(value) is int and 0 <= value <= 0xffffffff:
        return value
    if isinstance(value, str):
        if re.fullmatch(r'(?:0x)?[0-9a-fA-F]{8}', value):
            return int(value, 16)
        if value.isdecimal():
            return int(value)
    return None


def annotate_mission_owners(events):
    """Raw mission labels survive; only a live qualified owner grants gameplay."""
    logs = defaultdict(list)
    for event in events:
        logs[event.get('log_path')].append(event)
    for items in logs.values():
        has_boundaries = any(e['event'] == 'lifecycle' and e['kind'] == 'map-lifetime' for e in items)
        state = dict(scope='awaiting_map_owner' if has_boundaries else 'legacy_label_only', request=None, active=None)
        for e in sorted(items, key=lambda item: (item['sequence'], item['log_line'])):
            p, o, i = event_parameters(e), key_values(e['ownership']), key_values(e.get('instance'))
            if e['event'] == 'lifecycle' and e['kind'] == 'map-lifetime':
                phase = p.get('phase')
                if phase == 'map-load-request':
                    qualified = hexadecimal(e['caller']) == '0x828998C4' and p.get('operation') == '0' and \
                        p.get('mapType') == '1' and hexadecimal(p.get('managerType')) == '0x821822E8' and o.get('snapshot') == 'readable' and \
                        o.get('qualification') == 'map-load-request' and all(o.get(k) == 'true' for k in
                            ('managerMatchesGlobal', 'recordArgumentsMatch', 'registeredCallbackMatches', 'originalFieldsUnchanged'))
                    manager, package = hexadecimal(i.get('manager')), hexadecimal(i.get('requestedPackage'))
                    qualified &= manager not in (None, '0x00000000') and package not in (None, '0x00000000') and \
                        hexadecimal(i.get('globalManager')) == manager and hexadecimal(i.get('dispatchManager')) == manager and \
                        e['asset'] != 'unknown' and e['asset'].split('/')[0] == e['mission']
                    state = dict(scope='map_request' if qualified else 'unqualified_map_request', active=None,
                        request=dict(asset=e['asset'], manager=manager, package=package, sequence=e['sequence']) if qualified else None)
                elif phase == 'map-ready':
                    owner, manager, package = hexadecimal(i.get('owner')), hexadecimal(i.get('manager')), hexadecimal(i.get('package'))
                    generation = i.get('ownerGeneration')
                    qualified = hexadecimal(e['caller']) == '0x823BB5D8' and o.get('snapshot') == 'readable' and \
                        o.get('qualification') == 'ready' and o.get('ready') == '1' and all(o.get(k) == 'true' for k in
                            ('ownerMatchesGlobal', 'currentPackageIsMap')) and owner not in (None, '0x00000000') and \
                        hexadecimal(i.get('globalOwner')) == owner and manager not in (None, '0x00000000') and \
                        package not in (None, '0x00000000') and hexadecimal(i.get('map')) == package and \
                        isinstance(generation, str) and generation.isdecimal() and int(generation) > 0 and e['asset'] != 'unknown' and \
                        e['asset'].split('/')[0] == e['mission']
                    request = state['request']
                    if request:
                        qualified &= (e['asset'], manager, package) == (request['asset'], request['manager'], request['package'])
                    state = dict(scope='active_map_owner' if qualified else 'unqualified_map_ready', request=request,
                        active=dict(asset=e['asset'], mission=e['mission'], owner=owner, generation=generation,
                            ready_sequence=e['sequence']) if qualified else None)
                elif phase == 'cleanup-complete':
                    active = state['active']
                    matched = bool(active) and hexadecimal(e['caller']) == '0x823BBCE0' and \
                        o.get('cachedReady') == 'true' and o.get('globalOwnerCleared') == 'true' and \
                        hexadecimal(i.get('globalOwner')) == '0x00000000' and \
                        (hexadecimal(i.get('owner')), i.get('ownerGeneration')) == (active['owner'], active['generation'])
                    state = dict(scope='post_retirement_unqualified' if matched else 'unqualified_owner_retirement',
                                 request=None, active=None)
            active = state['active']
            qualified = bool(active) and e['mission'] == active['mission'] and e['mission'].lower() not in \
                ('', 'unknown', 'unattributed', 'fixture', 'frontend')
            e['mission_attribution'] = dict(scope=state['scope'], qualified_gameplay=qualified,
                active_owner=active, requested_map=state['request'],
                qualification='Raw mission text is retained; requests/legacy labels/post-retirement resources do not prove gameplay owner lifetime')


def join_encounters(rows, events):
    annotate_mission_owners(events)
    by_id = {r['id']: r for r in rows}
    effects = defaultdict(list)
    particle_pairs = defaultdict(list)
    textures = defaultdict(list)
    texture_profiles = defaultdict(list)
    payloads = defaultdict(list)
    resident_cues, amx_cues, stream_headers = defaultdict(list), defaultdict(list), defaultdict(list)
    for r in rows:
        if r['kind'] == 'effect_pass':
            effects[r['source']].append(r)
        if r['kind'] == 'particle_sdk_pass':
            particle_pairs[(r['vertex'], r['pixel'])].append(r)
        if r['kind'] == 'texture':
            textures[r['metadata_sha256']].append(r)
            descriptor = r.get('descriptor')
            if isinstance(descriptor, list) and len(descriptor) == 6:
                texture_profiles[(r['name'], r['storage_bytes'], tuple(hexadecimal(v) for v in descriptor))].append(r)
        if r.get('payload_sha256'):
            payloads[(r['kind'], r['payload_sha256'])].append(r)
        if r['kind'] == 'sound_cue' and r.get('catalog') == 'resident':
            resident_cues[(r['source'], r['bank'], r['header_offset'], r['header_hex'])].append(r)
        if r['kind'] == 'sound_cue' and r.get('catalog') == 'amx':
            amx_cues[(r['bank'], r['header_offset'], r['header_hex'])].append(r)
        if r['kind'] == 'sound_stream':
            stream_headers[r['header_hex'].lower()].append(r)
    unlinked, source_encounters, audio_encounters, profile_encounters, failures = [], [], [], [], defaultdict(list)
    for e in events:
        if e['event'] in ('shutdown', 'lifecycle'):
            # Retain terminal context and owner-route observations, including
            # map ready/retire and movie input-ready/decoder-stop requests.
            # These boundaries do not prove asset use or completed retirement.
            unlinked.append(e)
            continue
        p, matches, audio_encounter = event_parameters(e), [], None
        if e['asset'] in by_id and by_id[e['asset']]['kind'] == e['kind']:
            matches = [by_id[e['asset']]]
        elif e['kind'] in ('effect_pass', 'geometry_use', 'screen_replacement'):
            source = hexadecimal(p.get('source'))
            if not source:
                match = re.fullmatch(r'source:(?:0x)?([0-9a-fA-F]{8})', e['asset'])
                source = hexadecimal(match[1]) if match else None
            vertex, pixel = hexadecimal(p.get('vertex', p.get('vs'))), hexadecimal(p.get('pixel', p.get('ps')))
            if source and vertex is not None and pixel is not None:
                matches = [r for r in effects[source] if r['vertex'] == vertex and r['pixel'] == pixel]
                # The same shader pair can occur in multiple techniques. Without
                # handles it proves this pair's reachability, not each pass.
                technique = p.get('technique_handle', p.get('technique'))
                passed = p.get('pass_handle', p.get('pass'))
                if len(matches) > 1 or technique is not None or passed is not None:
                    matches = [r for r in matches if original_handle(r['technique_handle']) == original_handle(technique)
                               and original_handle(r['pass_handle']) == original_handle(passed)]
            elif source in effects and e['event'] == 'encounter':
                source_encounters.append(dict(event=e, source=source, matched_passes=False))
        elif e['kind'] == 'particle_sdk_pass':
            vertex, pixel = hexadecimal(p.get('vertex', p.get('vs'))), hexadecimal(p.get('pixel', p.get('ps')))
            if vertex is not None and pixel is not None:
                matches = particle_pairs[(vertex, pixel)]
        elif e['kind'] in ('texture', 'texture_binding') and isinstance(p.get('metadata_sha256'), str):
            matches = textures[p['metadata_sha256']]
        elif e['kind'] in ('texture', 'texture_binding'):
            descriptor = p.get('descriptor')
            if isinstance(descriptor, str):
                descriptor = descriptor.split(',')
            count = p.get('payload_bytes')
            if isinstance(descriptor, (list, tuple)) and len(descriptor) == 6 and \
               (type(count) is int or isinstance(count, str) and count.isdecimal()):
                values = tuple(hexadecimal(v) for v in descriptor)
                if all(v is not None for v in values) and e['event'] == 'encounter':
                    candidates = texture_profiles[(e['asset'], int(count), values)]
                    profile_encounters.append(dict(event=e, candidate_catalog_ids=[r['id'] for r in candidates],
                        packaged_occurrence_proved=False,
                        qualification='Cached name/bytes/descriptor profile; complete metadata/payload identity and packaged occurrence are unproved'))
        elif e['kind'] == 'audio-source':
            ownership = key_values(e['ownership'])
            registered = str(ownership.get('registeredMember')) == '1' and str(ownership.get('constructed')) == '1'
            resident = re.fullmatch(r'EAAC:([0-9a-fA-F]{16}) bank=(.+?) name=(.+?) headerOffset=(\d+)', e['asset'])
            ambient = re.fullmatch(r'EAAC:([0-9a-fA-F]{16}) amx=(.+?) headerOffset=(\d+)', e['asset'])
            stream = re.fullmatch(r'EAAC:([0-9a-fA-F]{16})', e['asset'])
            catalog = None
            if registered and resident and str(ownership.get('residentBank')) == '1':
                catalog = 'resident'
                matches = resident_cues[(resident[2], resident[3], int(resident[4]), resident[1].lower())]
            elif registered and ambient and str(ownership.get('amxOwner')) == '1':
                catalog = 'amx'
                matches = amx_cues[(ambient[2], int(ambient[3]), ambient[1].lower())]
            elif registered and stream and str(ownership.get('encodedStream')) == '1':
                catalog = 'streamed_header_candidates'
                candidates = stream_headers[stream[1].lower()]
                # Even one header candidate is not proof of a streamed file's
                # complete encoded chain or original catalog owner.
                audio_encounters.append(dict(event=e, catalog=catalog,
                    candidate_catalog_ids=[r['id'] for r in candidates], packaged_occurrence_proved=False))
            if isinstance(p.get('payload_sha256'), str):
                matches = [r for r in matches if r['payload_sha256'] == p['payload_sha256']]
            if catalog in ('resident', 'amx'):
                audio_encounter = dict(event=e, catalog=catalog,
                    candidate_catalog_ids=[r['id'] for r in matches], packaged_occurrence_proved=False)
                audio_encounters.append(audio_encounter)
        elif isinstance(p.get('payload_sha256'), str):
            matches = payloads[(e['kind'], p['payload_sha256'])]
        # Every supplied provenance field must agree, including when a hash
        # has only one candidate. Mission names never resolve shared payloads.
        for parameter, field in (('archive', 'source'), ('entry', 'entry'),
                                 ('payload_decoded_offset', 'payload_decoded_offset')):
            if parameter in p:
                matches = [r for r in matches if field in r and str(r[field]) == str(p[parameter])]
        if audio_encounter is not None:
            audio_encounter['resolved_catalog_ids'] = [r['id'] for r in matches]
            audio_encounter['packaged_occurrence_proved'] = len(matches) == 1
        if len(matches) == 1:
            state = matches[0]['coverage']
            if e['event'] == 'encounter':
                state['encountered_runtime'] = True
                state['encountered_gameplay'] |= e['mission_attribution']['qualified_gameplay']
                state['encounter_sequences'].append(dict(sequence=e['sequence'], mission=e['mission'], log_line=e['log_line'],
                                                        log_path=e.get('log_path'), mission_attribution=e['mission_attribution']))
            e['catalog_id'] = matches[0]['id']
        else:
            unlinked.append(e)
        if e['event'] == 'failure':
            # Exact prevalidation signature, including caller/mission/action/
            # ownership, is stable across repeated events and excludes sequence.
            signature = {k: e[k] for k in ('kind', 'asset', 'caller', 'parameters', 'ownership', 'mission', 'last_action')}
            failures[sha(json.dumps(signature, sort_keys=True, separators=(',', ':')).encode())].append(e)
    return dict(events=events, unlinked=unlinked, encountered_sources=source_encounters,
                encountered_audio_cues=audio_encounters,
                encountered_texture_profiles=profile_encounters,
        failure_groups={key: dict(count=len(value), first=value[0], sequences=[e['sequence'] for e in value],
                                 reasons=dict(Counter(e.get('reason', 'unreported') for e in value)),
                                 attribution='Retained per-thread resource context; asset admission causality unproved')
                                for key, value in sorted(failures.items())})


def join_test_evidence(rows, receipts, root=ROOT):
    by_id = {r['id']: r for r in rows}
    for e in receipts:
        require(e.get('result') == 'passed' and e.get('catalog_id') in by_id, 'Unknown or nonpassing test receipt')
        proof = e['proof']
        path = (root / proof['path']).resolve()
        require(path.is_relative_to(root.resolve()), 'Test proof must be inside the audit workspace')
        require(sha(path.read_bytes()) == proof['sha256'], 'Test proof identity changed')
        for reference in e.get('inputs', []):
            input_path = (root / reference['path']).resolve()
            require(input_path.is_relative_to(root.resolve()), 'Test input must be inside the audit workspace')
            require(sha(input_path.read_bytes()) == reference['sha256'], 'Test input identity changed')
        phases = set(e.get('phases', []))
        require(phases <= {'create', 'query', 'metadata_use', 'use', 'release', 'malformed'}, 'Unknown test phase')
        scope = e.get('scope', 'draw')
        require(scope in ('setup', 'draw', 'lifecycle'), 'Unknown test scope')
        use_kind = e.get('use_kind', 'rendering' if scope == 'draw' else None)
        if scope == 'lifecycle':
            require(use_kind == 'pcm_mixer_service' and by_id[e['catalog_id']]['kind'] == 'sound_stream',
                    'Unqualified nonrendering lifecycle use kind')
        elif scope == 'draw':
            require(use_kind == 'rendering', 'Draw receipt requires rendering use kind')
        state = by_id[e['catalog_id']]['coverage']
        if 'use' in phases and scope in ('draw', 'lifecycle'):
            require(state.get('use_kind') in (None, use_kind), 'Conflicting tested use kinds')
            state['use_kind'] = use_kind
        state['tested'] = True
        state['setup_tested'] |= scope == 'setup' and {'create', 'query', 'metadata_use', 'release'} <= phases
        state['draw_tested'] |= scope == 'draw' and 'use' in phases
        state['lifecycle_tested'] |= scope in ('draw', 'lifecycle') and {'create', 'use', 'release'} <= phases
        state['malformed_inputs_rejected'] |= 'malformed' in phases
        state['test_evidence'].append(e)


def add_synthetic_rows(rows, regressions):
    identities = {r['id'] for r in rows}
    for regression in regressions:
        require(regression.get('kind') == 'geometry_regression' and regression.get('synthetic') is True
                and regression.get('asset_group') is None, 'Unsupported synthetic regression identity')
        require(regression['id'] not in identities, 'Duplicate synthetic regression identity')
        identities.add(regression['id'])
        rows.append(dict(regression, coverage=coverage('declared_input_profile')))


def summarize(rows, archives):
    groups = defaultdict(list)
    for r in rows:
        groups[r['asset_group'] or 'unattributed_global'].append(r)
    result = {}
    for group, items in sorted(groups.items()):
        result[group] = dict(archives=sum(a['asset_group'] == group for a in archives),
            kinds=dict(sorted(Counter(r['kind'] for r in items).items())),
            implemented=dict(sorted(Counter(r['coverage']['implemented'] for r in items).items())),
            tested=sum(r['coverage']['tested'] for r in items),
            setup_tested=sum(r['coverage']['setup_tested'] for r in items),
            draw_tested=sum(r['coverage']['draw_tested'] for r in items),
            lifecycle_tested=sum(r['coverage']['lifecycle_tested'] for r in items),
            malformed_inputs_rejected=sum(r['coverage']['malformed_inputs_rejected'] for r in items),
            encountered_runtime=sum(r['coverage']['encountered_runtime'] for r in items),
            encountered_gameplay=sum(r['coverage']['encountered_gameplay'] for r in items))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--effect-report', type=Path, required=True)
    parser.add_argument('--texture-report', type=Path, required=True)
    parser.add_argument('--packaged-fields', type=Path)
    parser.add_argument('--encounter-log', type=Path, action='append', default=[])
    parser.add_argument('--test-evidence', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, default=ROOT / 'build/restrictive-check-audit/mission-support.json')
    args = parser.parse_args()
    assets, assets_ref = load(ROOT / 'analysis/assets.json')
    effects, effects_ref = load(args.effect_report.resolve())
    textures, textures_ref = load(args.texture_report.resolve())
    require(textures['authority']['inventory_sha256'] == assets_ref['sha256'], 'Texture audit uses a different asset census')
    for source in effects.get('inputs', []) + effects.get('catalogs', []):
        require(sha((ROOT / source['path']).read_bytes()) == source['sha256'], 'Effect audit source changed: ' + source['path'])
    require(sha((ROOT / 'renderer/itxd_blocks.cpp').read_bytes()) == textures['authority']['decoder_sha256'], 'Texture decoder changed since audit')
    require(sha((ROOT / 'runtime/engine_itxd_textures.cpp').read_bytes()) == textures['authority']['owner_sha256'], 'Texture owner changed since audit')
    streamed, streamed_ref = load(ROOT / 'analysis/audio_catalog_summary.json')
    resident, resident_ref = load(ROOT / 'analysis/resident_audio_catalog.json')
    amx, amx_ref = load(ROOT / 'analysis/amx_audio_catalog.json')
    resources, archives = resource_inventory(assets)
    packaged_ref, packaged_summary = None, None
    if args.packaged_fields:
        packaged, packaged_ref = load(args.packaged_fields.resolve())
        packaged_summary = join_packaged_fields(resources, packaged, assets_ref['sha256'])
    reconcile_sound_catalogs(assets, resources, streamed, resident, amx)
    rows = resources + effect_inventory(effects) + direct_particle_inventory(effects) + texture_inventory(textures, resources)
    sounds, sound_validation = sound_inventory(streamed, resident, amx)
    rows += sounds
    require(len({r['id'] for r in rows}) == len(rows), 'Duplicate catalog occurrence identity')
    events, errors, logs = [], [], []
    for path in args.encounter_log:
        raw = path.read_bytes()
        parsed, rejected = parse_encounters(raw.decode('utf-8', errors='replace'))
        label = str(path)
        for event in parsed:
            event['log_path'] = label
        events += parsed
        errors += [dict(log_path=label, **e) for e in rejected]
        logs.append(dict(path=label, sha256=sha(raw)))
    encounters = join_encounters(rows, events)
    receipts, evidence_refs = [], []
    for path in args.test_evidence:
        evidence, ref = load(path.resolve())
        require(evidence.get('schema') == 1, 'Unsupported test evidence schema')
        add_synthetic_rows(rows, evidence.get('synthetic_rows', []))
        receipts += evidence['cases']; evidence_refs.append(ref)
    join_test_evidence(rows, receipts)
    report = dict(schema=1, scope='Complete original resource/cue occurrences; support, lifecycle tests and gameplay encounters remain independent',
        authority=dict(assets=assets_ref, effects=effects_ref, textures=textures_ref, streamed=streamed_ref,
                       resident=resident_ref, amx=amx_ref, packaged_fields=packaged_ref,
                       encounter_logs=logs, test_evidence=evidence_refs),
        limits=['Asset groups are original archive directories, not inferred runtime mission assignments.',
                'Global shader pairs are not mapped to packaged meshes or VFX emitters.',
                'Metadata admission is distinct from native create/use/release and malformed-input regression.',
                'No native path executes in this tool; independent original setup tests run separately.',
                'Failure receipts may retain shutdown/window-close context; their asset admission causality is unproved.',
                'Raw mission labels are retained; gameplay attribution requires a qualified map-ready owner/generation and ends at cleanup. Legacy labels and pre-ready requests grant no gameplay credit.',
                'Optional packaged field report pins original loader spans and payload hashes; it grants no native support or lifecycle credit.'],
        complete_texture_metadata=textures.get('complete', not textures.get('failures')),
        summary=dict(archives=len(archives), asset_groups=len({a['asset_group'] for a in archives}),
                     records=len(rows), original_records=sum(not r.get('synthetic') for r in rows),
                     synthetic_records=sum(bool(r.get('synthetic')) for r in rows),
                     kinds=dict(sorted(Counter(r['kind'] for r in rows).items())),
                     tested=sum(r['coverage']['tested'] for r in rows),
                     setup_tested=sum(r['coverage']['setup_tested'] for r in rows),
                     draw_tested=sum(r['coverage']['draw_tested'] for r in rows),
                     lifecycle_tested=sum(r['coverage']['lifecycle_tested'] for r in rows),
                     malformed_inputs_rejected=sum(r['coverage']['malformed_inputs_rejected'] for r in rows),
                     encountered_runtime=sum(r['coverage']['encountered_runtime'] for r in rows),
                     encountered_gameplay=sum(r['coverage']['encountered_gameplay'] for r in rows),
                     encountered_effect_sources=len({e['source'] for e in encounters['encountered_sources']}),
                     telemetry_events=len(events), unlinked_events=len(encounters['unlinked']),
                     failure_groups=len(encounters['failure_groups']),
                     rejected_telemetry_lines=len(errors)),
        asset_groups=summarize(rows, archives), archives=archives, rows=rows,
        sound_validation=sound_validation, packaged_fields=packaged_summary, texture_failures=textures.get('failures', []),
        encounters=encounters, telemetry_errors=errors)
    output = args.output.resolve()
    original = (ROOT / 'Simpsons Game, The (USA)').resolve()
    require(not output.is_relative_to(original), 'Report must remain outside original game assets')
    protected = {Path(__file__).resolve(), ROOT / 'analysis/assets.json', ROOT / 'analysis/audio_catalog_summary.json',
                 ROOT / 'analysis/resident_audio_catalog.json', ROOT / 'analysis/amx_audio_catalog.json',
                 args.effect_report.resolve(), args.texture_report.resolve()}
    protected.update(p.resolve() for p in args.encounter_log + args.test_evidence)
    if args.packaged_fields:
        protected.add(args.packaged_fields.resolve())
    require(output not in protected,
            'Report must not overwrite an audit input')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, separators=(',', ':')) + '\n', encoding='utf-8')
    print(json.dumps(report['summary'], sort_keys=True))
    print('Report:', output)
    if errors or not report['complete_texture_metadata']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
