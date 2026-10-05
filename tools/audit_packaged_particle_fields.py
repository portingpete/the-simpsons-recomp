#!/usr/bin/env python3
"""Read-only .prt definition fields from the source-qualified VFX module path.

These are authored/requested counts. Pool availability and owner construction
can reduce or reject activation; no native create/use/release is credited.
"""
import argparse
from collections import Counter
import json
import mmap
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import audit_packaged_mesh_vfx as fields
from inspect_assets import decode_entry

SPANS = {
    '82758D70': (0x82758F4C, '8ea0bc59c6875fc76118643a40d53caa796316540f482a4e3e1433f81916478d'),
    '8275CE78': (0x8275CEC8, '6a4ef4cd360e018a66d8a2a05c03c8d7495fefe0eb6f08a89cf4de3abd129441'),
    '8275EE68': (0x8275EEAC, '05550b710511fd918b6505d99215cdb64afac9c0bb3eb59e83eae764c2cccfe5'),
    '8275DFF0': (0x8275E0AC, '1a1422639c55f149df5dd3aab112ff2c3e76eac942b9194ec1e4fbe10b88150d'),
    '82762980': (0x82762F04, '6607af1c51a6d358322b62dbd776c243a1776e3fded648138f0d1155a2161e02'),
    '82762F08': (0x82763090, 'aa73249442daf247c3d2c0fff43551ad89faa58a55f373aac8e44052d9a31f2b'),
    '8275DEC8': (0x8275DFE4, 'bb5a82c4646a21aeccad19cd042c9fd07721916e4189e3a0a56e6cd0b8e2db77'),
    '8275E290': (0x8275E2E0, '1845bb08816d673f9c52b22ad0e42a4dd00c9d752aef4fce98857920920a8c1f'),
    '8275E2E0': (0x8275E320, '419c4efa295d419e18dbcf89ae2d27c0b950c89b30e8008649ff15221f6404a5'),
    '827527A0': (0x827527A4, '05ff762975c2c6f685446e67ed3e774822014c8005f03cf286b404085493f8a1'),
    '8270CB10': (0x8270CB74, '3c82a510a696f01131f8aac2b85dfd0f52816d162d9b467d9adf97a3908515cb'),
    '827527B0': (0x827527B8, '03f4f95fd457c776fba85c4dec52c0809298af46acd5cd90ad041962b2ca9d7a'),
    '827526F0': (0x8275279C, '6b021e336e9ca01b1dd485ac09cadd720284b51184f855ea2bfa080e578bcd09'),
    '8270CA78': (0x8270CB00, 'cb23567e2f5bd301bcc009beaec16b090db08ba66715dd9c8e8c455825d93ace'),
    '82763218': (0x82763238, '5ce8a6d34959c966804e893bd227705c4207a95569370db4f740c0bb0336c29a'),
    '82760830': (0x82760988, '99776111da2872b2bcf4af16d64a1c1edc573dd78cfb575d003ac96c0241cee6'),
    '8275CED0': (0x8275CEF0, '80bb83e355a4ac7e1a5cc03709eab4b168a73008b2793bf359a508d6499a930b'),
    '82760EB0': (0x82760F00, '004aaf3bf3fe09907febec15b6ce873ada456ffeba8c5e3d05df061221d39247'),
    '82760728': (0x82760818, '4ef2d25c27cd5dd65651fd7c448e5d73d8b464260fd3582a08ab254396cf1ae3'),
    '8275F090': (0x8275F164, '073f806b37a64db4816f0619675e22c98982cba8893a861d1b34c26eb66106ed'),
    '827527A8': (0x827527AC, '6ea3a5c268fd31965257ec5e9ae68eafc572ee0859a297d0849fcc961f72ea57'),
    '8270CB78': (0x8270CBE0, '90fbc0a9cd7f732c07326c93af0f4069b189e0d89fde714acdaf463f1b18bd8e'),
    '82772CA8': (0x827736C4, 'd641f45b4ec79f483c8d03fb16c3d647af014c7605cf486a995f2ee0d133a4d8'),
}


def verify(image, full_hash=True):
    if full_hash:
        fields.require(fields.sha(image) == fields.IMAGE_SHA, 'Original particle image differs')
    for start, (end, digest) in SPANS.items():
        fields.require(fields.sha(fields.span(image, int(start, 16) - fields.BASE, end - int(start, 16))) == digest,
                       'Original particle producer span differs: ' + start)
    table = struct.unpack('>13I', fields.span(image, 0x82152FBC - fields.BASE, 52))
    fields.require(table == (0x8275EF00, 0x8275CEC8, 0x8275DFF0, 0x82763218, 0x8275CED0,
                            0x8275EE68, 0x8275CB00, 0x8275CB08, 0x8275E290, 0x8275E2E0,
                            0x8275CB20, 0x8275CB28, 0x8275CB30), 'Original .prt module table differs')


def particle_fields(data):
    prefix = fields.parse_vfx(data)
    modules = []
    for row in prefix['rows']:
        if row['module_key'] != '2E707274':
            continue
        result = dict(row=row['index'], relative_reference=row['relative_reference'],
                      revision=row['module_revision'], row_flags=row['flags'],
                      referenced_flags=row['referenced_flags'], native_lifecycle_tested=False)
        try:
            fields.require(row['module_revision'] == '0000000F', 'Unqualified .prt module revision')
            # 82758DE8 passes reference+12. 8275EE68 relocates block+4/+8
            # relative to that block, not relative to the outer VFX owner.
            block = row['relative_reference'] + 12
            raw_flags, definition_delta, parameter_delta = struct.unpack('>3I', fields.span(data, block, 12))
            definition, parameters = block + definition_delta, block + parameter_delta
            d = fields.span(data, definition, 0x108)
            fields.span(data, parameters, 0)  # Address only; full parameter extent remains unproved.
            authored = struct.unpack_from('>h', d, 0xDA)[0]
            normalized = max(authored, 1)  # Literal signed clamp in8275EE68.
            requested_blocks = (normalized + 7) // 8
            result.update(status='source_pinned_fields', block_offset=block, block_flags=f'{raw_flags:08X}',
                definition_offset=definition, parameter_offset=parameters,
                definition_consumed_prefix_sha256=fields.sha(d), authored_count_signed=authored,
                loader_normalized_requested_count=normalized, requested_blocks_of8=requested_blocks,
                requested_rounded_capacity=requested_blocks * 8, type100=d[0x100], flags104=d[0x104],
                mode40=d[0x40], alpha_reference106=d[0x106],
                flagsD0_serialized=f'{fields.word(d,0xD0):08X}',
                flagsD0_after_original_loader=f'{fields.word(d,0xD0)&~0x1000:08X}',
                flagsD4=f'{fields.word(d,0xD4):08X}',
                exceeds_native4096_requested_domain=requested_blocks * 8 > 4096,
                shader_family_candidate='type5' if d[0x100] == 5 else 'ordinary' if d[0x100] in (0, 3) else None,
                live_capacity=None, shader_selection=None, full_parameters=None,
                qualification='Authored/requested source fields only; actual pool/embedded owner activation, live count and texture/shader lifetime unproved')
        except (ValueError, struct.error) as error:
            result.update(status='offline_qualification_failure', reason=str(error), live_capacity=None,
                          shader_selection=None, full_parameters=None)
        modules.append(result)
    return dict(modules=modules, prefix_failures=prefix['failures'])


def named_fields(raw, entry, chunk, cached, decoded):
    token = (chunk['payload_decoded_offset'], chunk['payload_size'])
    if token not in cached['chunks']:
        if decoded is None:
            decoded, _ = decode_entry(raw, entry)
        payload = fields.span(decoded, *token)
        try:
            parsed = particle_fields(payload)
        except (ValueError, struct.error) as error:
            parsed = dict(modules=[], prefix_failures=[dict(reason=str(error))])
        cached['chunks'][token] = (fields.sha(payload), parsed)
    digest, parsed = cached['chunks'][token]
    fields.require(digest == chunk['payload_sha256'], 'Particle named payload hash differs')
    return digest, parsed, decoded


def inventory(assets, root):
    rows, failures, cache = [], [], {}
    for archive in assets['files']:
        selected = [(e, [c for c in e.get('chunks', []) if c.get('type_name') == 'VFX'])
                    for e in archive.get('inspection', {}).get('entries', [])]
        selected = [(e, cs) for e, cs in selected if cs]
        if not selected:
            continue
        try:
            path = (root / archive['path']).resolve()
            fields.require(path.is_relative_to(root), 'Particle archive path escapes original root')
            with path.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as raw:
                fields.require(fields.sha(raw) == archive['sha256'], 'Original particle archive hash differs')
                for e, chunks in selected:
                    try:
                        key = (e['encoding'], e['decoded_size'], fields.sha(fields.span(raw,e['file_offset'],e['stored_size'])))
                        decoded = None
                        if key not in cache:
                            decoded, _ = decode_entry(raw,e)
                            cache[key] = dict(decoded_sha256=fields.sha(decoded), chunks={})
                        cached = cache[key]
                        fields.require(cached['decoded_sha256'] == e['decoded_sha256'], 'Particle entry hash differs')
                        for c in chunks:
                            try:
                                digest, parsed, decoded = named_fields(raw,e,c,cached,decoded)
                            except (ValueError,struct.error) as error:
                                failures.append(dict(source=archive['path'],entry=e['index'],name=c['name'],
                                    payload_decoded_offset=c['payload_decoded_offset'],payload_sha256=c['payload_sha256'],reason=str(error)))
                                continue
                            if parsed['modules'] or parsed['prefix_failures']:
                                rows.append(dict(source=archive['path'],archive_sha256=archive['sha256'],entry=e['index'],
                                    payload_decoded_offset=c['payload_decoded_offset'],name=c['name'],payload_sha256=digest,
                                    payload_bytes=c['payload_size'],parameters=parsed,
                                    native_setup_tested=False,native_use_tested=False,native_release_tested=False,
                                    encountered_runtime=False,encountered_gameplay=False))
                    except (ValueError,struct.error) as error:
                        failures.append(dict(source=archive['path'],entry=e['index'],reason=str(error),names=[c['name'] for c in chunks]))
        except (OSError,ValueError) as error:
            failures.append(dict(source=archive['path'],reason=str(error)))
    modules = [m for r in rows for m in r['parameters']['modules']]
    valid = [m for m in modules if m['status']=='source_pinned_fields']
    summary = dict(named_vfx_with_particle_fields=len(rows),particle_module_occurrences=len(modules),
        unique_decoded_entries=len(cache),archive_or_entry_failures=len(failures),
        prefix_qualification_failures=sum(len(r['parameters']['prefix_failures']) for r in rows),
        module_qualification_failures=len(modules)-len(valid),
        authored_counts=dict(sorted(Counter(m['authored_count_signed'] for m in valid).items())),
        requested_rounded_capacities=dict(sorted(Counter(m['requested_rounded_capacity'] for m in valid).items())),
        requested_over_native4096=sum(m['exceeds_native4096_requested_domain'] for m in valid),
        type100=dict(sorted(Counter(m['type100'] for m in valid).items())),
        flag104_profiles=dict(sorted(Counter(m['flags104'] for m in valid).items())),
        modes40=dict(sorted(Counter(m['mode40'] for m in valid).items())))
    return rows, failures, summary


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,default=ROOT/'analysis/assets.json')
    parser.add_argument('--asset-root',type=Path,default=ROOT/'Simpsons Game, The (USA)')
    parser.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    parser.add_argument('--packaged-report',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();verify(args.image.read_bytes())
    raw=args.assets.read_bytes();previous=json.loads(args.packaged_report.read_bytes())
    fields.require(previous['scope']=='source_pinned_offline_packaged_fields' and
        previous['catalog_sha256']==fields.sha(raw) and previous['image_sha256']==fields.IMAGE_SHA and
        previous['tool_sha256']==fields.sha(Path(fields.__file__).read_bytes()), 'Stale or conflicting packaged field authority')
    root=args.asset_root.resolve();output=args.output.resolve()
    fields.require(not output.is_relative_to(root) and output.is_relative_to(ROOT/'build') and
        output not in (args.assets.resolve(),args.image.resolve(),args.packaged_report.resolve()), 'Invalid particle report output')
    rows,failures,summary=inventory(json.loads(raw),root)
    report=dict(schema_version=1,scope='source_pinned_offline_prt_requested_fields',summary=summary,rows=rows,
        archive_failures=failures,image_sha256=fields.IMAGE_SHA,catalog_sha256=fields.sha(raw),
        tool_sha256=fields.sha(Path(__file__).read_bytes()),reader_sha256=fields.sha(Path(fields.__file__).read_bytes()),
        packaged_report=dict(path=str(args.packaged_report),sha256=fields.sha(args.packaged_report.read_bytes())),
        native_guard=dict(path='runtime/engine_particles.cpp',sha256=fields.sha((ROOT/'runtime/engine_particles.cpp').read_bytes()),
            expression='count && count<=capacity && capacity<=4096 && emitter_F6<capacity && r25==count',
            disposition='Requested-count candidate; complete original activation/live use/release and valid range still untested'),
        producer_spans=[dict(function=name,end=f'{end:08X}',sha256=digest) for name,(end,digest) in SPANS.items()],
        limits=['Original constructor can receive fewer blocks from the shared pool, so authored/requested capacity is not actual live capacity.',
                'Signed authored count and rounded field representability alone do not qualify the full original live count range.',
                'Module key/revision and observed fields do not prove texture, parameter, shader state or resource lifetime.',
                'No native or gameplay credit is granted; stable full packaged v2 report is unchanged.'])
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,separators=(',',':'))+'\n',encoding='utf-8')
    print(json.dumps(summary,sort_keys=True))
    return int(bool(failures or summary['prefix_qualification_failures'] or summary['module_qualification_failures']))


if __name__=='__main__':raise SystemExit(main())
