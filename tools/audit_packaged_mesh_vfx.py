#!/usr/bin/env python3
"""Read-only packaged mesh/VFX fields pinned to original stream consumers.

This is an offline field inventory, never native setup/draw/lifetime evidence.
Unknown records and per-record parse failures remain visible independently.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import mmap
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
BASE = 0x82000000
IMAGE_SHA = '6df9123532f4f7f8f00ac30642849058725562f43339461561522c5d442083a0'
SPANS = {
    '826F2710': (0x826F2710, 0x826F27EC, '59c86c71e2689cfafe41d0937c4e966a0a285d33e0098584be2a00a25113785f'),
    '8270F108': (0x8270F108, 0x8270F3C4, '695ed2c78d2d64d7b3af6d65b947cfd75f37590cbe82a7b01a408070f9967fbc'),
    '823F7DD0': (0x823F7DD0, 0x823F7ED0, '1d08828a11657fc4bf1f8161e0c1d59f6b50598ea86bafc0f6c0ffa103aa984d'),
    '823CE380': (0x823CE380, 0x823CE770, '7ce1226d42f54ecdec897205f6a43a9e9bf5db746bf2c47d7946a6aeaeaf7604'),
    '823CC388': (0x823CC388, 0x823CC514, '152e79225efc12a7a9a7bf53d7e254ea306564d05b81749817198cb8c1e74d35'),
    '823D0980': (0x823D0980, 0x823D0CC0, 'e58e24f546acb1da0f2b9b267ae3fcd256d7e6ba305bd0b9331c20265d0fbc35'),
    '8282F970': (0x8282F970, 0x8282FA10, '3b4819abbab23a867478eb3c8244f48c15efeed72d8af0895c6196ea1f3ff91b'),
    '8282F618': (0x8282F618, 0x8282F788, '107bb606355759871f00919e7a5f60fe5a571af0a42cd00f3047500f43886c6b'),
    '82831280': (0x82831280, 0x828312CC, '3e4f115c6d0a9a1e5c8ac9e90d6613224cdb3dc62450dcce4bba89545c6075db'),
    '828311D0': (0x828311D0, 0x82831280, 'db1df2e73ff2f26b9527d09e46816c44f90d0480d5f937ed64ba2406162c27ee'),
    '826FED80': (0x826FED80, 0x826FEDE0, 'dd226602f8c495a4a7cb270709477ca0cc57f71aafc12cd21279078c48f823bd'),
    '8273B760': (0x8273B760, 0x8273B824, '376e4a99d88573e18fc66f2f5611e4f11545f5109a391ba0d38e5feca9656df5'),
    '82701BD8': (0x82701BD8, 0x82701E30, 'e016b3bcb52c5f7a0ba66ed7321a9df0746ac7297a1c18b31d063ce27192b520'),
    '82701220': (0x82701220, 0x82701444, 'c96ef65f8380fac1ee40fabd2a984168b0aaa632b920b9d0cb499e946730fa8e'),
    '827277F0': (0x827277F0, 0x82727AF8, '41fddc97b0b94d0531fa90d5eb7b59f5e8a41765a171bdd85f392dde45e78107'),
    '82C71F90': (0x82C71F90, 0x82C72004, '832356a114c18d171e8aa28e8a18aa1aa0b87067533f0b5249e3f1ca229cc211'),
    '82750580': (0x82750580, 0x827505D4, 'e4dff6f7de7c933d3b536afdc49879109bf2dc998d6f0e48b5dfea79fb6ec409'),
    '82758D70': (0x82758D70, 0x82758F4C, '8ea0bc59c6875fc76118643a40d53caa796316540f482a4e3e1433f81916478d'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def span(data, at, length):
    require(at >= 0 and length >= 0 and at + length <= len(data),
            f'Unreadable offline span offset={at} bytes={length} owner_bytes={len(data)}')
    return data[at:at + length]


def word(data, at, endian='>'):
    return struct.unpack(endian + 'I', span(data, at, 4))[0]


def verify(image, check_hash=True):
    if check_hash:
        require(sha(image) == IMAGE_SHA, 'Original flat image hash differs')
    for name, (start, end, expected) in SPANS.items():
        require(sha(span(image, start - BASE, end - start)) == expected,
                f'Original packaged consumer span changed: {name}')


def chunk_rows(data, lo=0, hi=None):
    """Original823F7DD0 reads/swaps each LE12 header; no version whitelist."""
    hi = len(data) if hi is None else hi
    require(0 <= lo <= hi <= len(data), 'Chunk owner range differs')
    at = lo
    while at < hi:
        require(hi - at >= 12, f'Truncated chunk header at{at}')
        kind, size, version = struct.unpack('<3I', span(data, at, 12))
        end = at + 12 + size
        require(end <= hi, f'Chunk body exceeds owner at{at}')
        yield dict(offset=at, body=at + 12, end=end, kind=kind, bytes=size, version=f'{version:08X}')
        at = end


def children(data, c):
    return list(chunk_rows(data, c['body'], c['end']))


def native_pool(body):
    # Reader8282F618 swaps only its LE12 prefix; opaque pools stay BE.
    alignment, first_bytes, second_bytes = struct.unpack('<3I', span(body, 0, 12))
    first = span(body, 12, first_bytes)
    second = span(body, 12 + first_bytes, second_bytes)
    require(12 + first_bytes + second_bytes == len(body), 'Native pool owner tail is unqualified')
    require(span(first, 0, 8) == bytes.fromhex('BFBFBFBF01000000'), 'Unqualified native pool visitor magic')
    relocation_count, record_count = word(first, 20), word(first, 24)
    table_end = 28 + 8 * relocation_count
    span(first, 28, 8 * relocation_count + 12 * record_count)
    relocations = {}
    for at in range(28, table_end, 8):
        field, selector = word(first, at), word(first, at + 4)
        require(field not in relocations, 'Duplicate relocation target is outside offline qualification')
        target = word(first, field)
        pool = 1 if selector else 0  # Original adds second base for any nonzero selector.
        owner = second if pool else first
        require(target <= len(owner), 'Relocation relative value exceeds selected owner')
        relocations[field] = (pool, target)
    records = []
    for at in range(table_end, table_end + 12 * record_count, 12):
        kind, size, offset = word(first, at), word(first, at + 4), word(first, at + 8)
        span(first, offset, size)
        records.append(dict(type=f'{kind:08X}', bytes=size, offset=offset))
    return dict(alignment=alignment, first_bytes=first_bytes, second_bytes=second_bytes,
                relocation_count=relocation_count, record_count=record_count), (first, second), relocations, records


def resolve(pools, relocations, field, size):
    require(field in relocations, f'Pointer field{field:X} has no original relocation entry')
    pool, at = relocations[field]
    return pool, at, span(pools[pool], at, size)


def mesh_record(pools, relocations, record):
    first = pools[0]
    at = record['offset']
    span(first, at, 56)
    if word(first, at) != 0x30002:
        return dict(status='unqualified_metadata_version', metadata_word=f'{word(first, at):08X}',
                    stride=None, declaration=None, submeshes=None, shader_selection=None)
    pool, geometry, data = resolve(pools, relocations, at + 12, 32)
    require(pool == 0, 'Geometry metadata resides in unqualified second owner')
    vb, stride, count, _, _, ib, format_word, _ = struct.unpack('>8I', data)
    dpool, declaration, declarations = resolve(pools, relocations, geometry + 12, count * 12)
    rows = []
    for offset in range(0, count * 12, 12):
        stream, byte_offset, typ, method, usage, index, opaque = struct.unpack_from('>HHIBBBB', declarations, offset)
        rows.append(dict(stream=stream, offset=byte_offset, type=f'{typ:08X}', method=method,
                         usage=usage, index=index, opaque=opaque))
    vpool, vertex, _ = resolve(pools, relocations, geometry + 16, vb)
    ipool, indices, index_data = resolve(pools, relocations, geometry + 28, ib)
    submesh_count = word(first, at + 16)
    spool, submesh, sdata = resolve(pools, relocations, at + 20, submesh_count * 36)
    draws = []
    for offset in range(0, submesh_count * 36, 36):
        draw = dict(material_index=word(sdata, offset), material_selector=f'{word(sdata, offset+4):08X}',
            primitive=word(sdata, offset + 12), base_vertex=struct.unpack_from('>i', sdata, offset + 16)[0],
            start_index=word(sdata, offset + 20), index_count=word(sdata, offset + 24),
            opaque_words=[f'{word(sdata, offset+x):08X}' for x in (8, 28, 32)], selected_r16_words=None)
        if format_word == 1:
            selected = span(index_data, 2 * draw['start_index'], 2 * draw['index_count'])
            words = [v[0] for v in struct.iter_unpack('>H', selected)]
            other = [v for v in words if v != 0xFFFF]
            draw['selected_r16_words'] = dict(bytes_sha256=sha(selected), count=len(words),
                literal_FFFF_words=words.count(0xFFFF), minimum=min(words, default=None), maximum=max(words, default=None),
                non_FFFF_minimum=min(other, default=None), non_FFFF_maximum=max(other, default=None),
                restart_semantics='Unqualified by this inventory; literal words only')
        draws.append(draw)
    return dict(status='source_pinned_fields', metadata_offset=at, geometry_offset=geometry,
        vertex_bytes=vb, vertex_pool=vpool, vertex_offset=vertex, stride=stride,
        declaration_count=count, declaration_pool=dpool, declaration_offset=declaration,
        declaration=rows, index_bytes=ib, index_format_word=format_word, index_pool=ipool,
        index_offset=indices, submesh_count=submesh_count, submesh_pool=spool,
        submesh_offset=submesh, submeshes=draws, shader_selection=None,
        unverified=['Native family/material/pass selection', 'Consumed fetch validity',
                    'Original resource create/use/release', 'Runtime packaged identity association'])


def material_fields(body):
    # Reader827277F0 accepts LE16 versions1/2; byte7 bit0 carries a24-byte tail.
    version, flags = struct.unpack('<HH', span(body, 0, 4))
    if version not in (1, 2):
        return dict(status='unqualified_material_version', version=version, shader_selection=None)
    size = 16 if version == 1 else 36
    span(body, 0, size + (24 if span(body, 7, 1)[0] & 1 else 0))
    selector = word(body, 12, '<')
    return dict(status='source_pinned_fields', version=version, flags=flags,
        byte2=body[4], byte3=body[5], serialized_byte6=body[6], serialized_flags_byte7=body[7],
        field4=word(body, 8, '<'), pipeline_selector=f'{selector:08X}', shader_selection=None,
        unverified=['Virtual material continuation', 'Texture/material to effect family association'])


def parse_mesh(data):
    result = dict(status='source_pinned_fields', geometries=[], geometry_lists=[], clump_structs=[],
                  materials=[], unknown_chunks=[], failures=[])

    def visit(lo, hi, parent):
        for c in chunk_rows(data, lo, hi):
            tag = c['kind']
            # Only structurally source-established containers are traversed.
            if tag in (16, 26, 15, 8, 7, 6, 20, 3):
                visit(c['body'], c['end'], tag)
            elif parent == 16 and tag == 1:
                fields = struct.unpack('<3I', span(data, c['body'], 12))
                result['clump_structs'].append(dict(chunk_offset=c['offset'], atomic_count=fields[0],
                                                   light_count=fields[1], camera_count=fields[2]))
            elif parent == 26 and tag == 1:
                result['geometry_lists'].append(dict(chunk_offset=c['offset'], count=word(data, c['body'], '<')))
            elif parent == 15 and tag == 1:
                fields = struct.unpack('<4I', span(data, c['body'], 16))
                result['geometries'].append(dict(chunk_offset=c['offset'], serialized_flags=f'{fields[0]:08X}',
                    triangle_count=fields[1], vertex_count=fields[2], morph_count=fields[3], native_records=[]))
            elif parent == 3 and tag == 0xEA33:
                item = dict(chunk_offset=c['offset'])
                try:
                    header, pools, relocations, records = native_pool(span(data, c['body'], c['bytes']))
                    item.update(header, records=[])
                    for record in records:
                        r = dict(record)
                        try:
                            r['fields'] = mesh_record(pools, relocations, record) if record['type'] == '3C43A23D' else None
                        except ValueError as exc:
                            r.update(status='offline_qualification_failure', reason=str(exc), fields=None)
                            result['failures'].append(dict(chunk_offset=c['offset'], record=record, reason=str(exc)))
                        item['records'].append(r)
                except ValueError as exc:
                    item.update(status='offline_qualification_failure', reason=str(exc))
                    result['failures'].append(dict(chunk_offset=c['offset'], reason=str(exc)))
                if result['geometries']:
                    result['geometries'][-1]['native_records'].append(item)
                else:
                    result['failures'].append(dict(chunk_offset=c['offset'], reason='Native pool has no preceding geometry struct'))
            elif parent == 3 and tag == 0xEA13:
                try:
                    fields = material_fields(span(data, c['body'], c['bytes']))
                except ValueError as exc:
                    fields = dict(status='offline_qualification_failure', reason=str(exc))
                    result['failures'].append(dict(chunk_offset=c['offset'], reason=str(exc)))
                result['materials'].append(dict(chunk_offset=c['offset'], **fields))
            elif tag not in (1, 2):
                result['unknown_chunks'].append(dict(chunk_offset=c['offset'], type=f'{tag:08X}', bytes=c['bytes'],
                    fields=None, status='unqualified_offline_domain'))
    visit(0, len(data), None)
    require(result['geometry_lists'], 'No source-established geometry list was found')
    require(sum(g['count'] for g in result['geometry_lists']) == len(result['geometries']), 'Geometry list count differs from parsed structures')
    # Original823CC388 accepts zero and allocates no geometry table; empty
    # clumps are real stock resources, not malformed synthetic geometry.
    result['empty_geometry_list'] = not result['geometries']
    return result


def parse_vfx(data):
    span(data, 0, 48)
    count = data[28]  # Original82758D70 loops unsigned byte count with28-byte rows.
    span(data, 48, count * 28)
    rows, failures = [], []
    for index in range(count):
        at = 48 + index * 28
        relative, word4, flags = struct.unpack('>3I', span(data, at, 12))
        row = dict(index=index, relative_reference=relative, raw_word4=f'{word4:08X}', flags=f'{flags:08X}',
                   module_key=None, module_revision=None, referenced_flags=None,
                   material=None, stride=None, shader_selection=None)
        try:
            key, revision, rflags = struct.unpack('>3I', span(data, relative, 12))
            row.update(module_key=f'{key:08X}', module_revision=f'{revision:08X}', referenced_flags=f'{rflags:08X}',
                       status='source_pinned_reference_fields')
        except ValueError as exc:
            row.update(status='offline_qualification_failure', reason=str(exc))
            failures.append(dict(row=index, relative_reference=relative, reason=str(exc)))
        rows.append(row)
    return dict(status='source_pinned_fields', prepublication_word16=f'{word(data,16):08X}',
        flags=f'{word(data,20):08X}', row_count=count, rows=rows, failures=failures,
        unverified=['Module registry lookup/revision match', 'Emitter/particle/material layouts beyond referenced prefix',
                    'Shader/geometry selection', 'Original resource create/use/release', 'Runtime packaged identity association'])


def audit(assets, asset_root):
    from inspect_assets import decode_entry
    rows, archive_failures = [], []
    cache = {}
    counts = Counter()
    for f in assets['files']:
        selected = [(e, [c for c in e.get('chunks', []) if c.get('type_name') in ('EARS_MESH', 'VFX')])
                    for e in f.get('inspection', {}).get('entries', [])]
        selected = [(e, cs) for e, cs in selected if cs]
        if not selected:
            continue
        try:
            path = (asset_root / f['path']).resolve()
            require(path.is_relative_to(asset_root), 'Archive path escapes asset root')
            with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
                require(sha(raw) == f['sha256'], 'Original archive hash differs')
                for e, cs in selected:
                    try:
                        key = (e['encoding'], e['decoded_size'], sha(span(raw, e['file_offset'], e['stored_size'])))
                        decoded = None
                        if key not in cache:
                            decoded, _ = decode_entry(raw, e)
                            # Cache actual content identities, never the first
                            # occurrence's catalog labels. A malformed label
                            # must not poison a later valid original occurrence.
                            cache[key] = dict(decoded_sha256=sha(decoded), parsed={})
                        cached = cache[key]
                        require(cached['decoded_sha256'] == e['decoded_sha256'],
                                'Original decoded entry hash differs')
                        for c in cs:
                            token = (c['payload_decoded_offset'], c['payload_size'], c['type_name'])
                            if token not in cached['parsed']:
                                if decoded is None:
                                    decoded, _ = decode_entry(raw, e)
                                    require(sha(decoded) == cached['decoded_sha256'],
                                            'Repeated original entry content changed')
                                payload_sha = None
                                try:
                                    payload = span(decoded, token[0], token[1])
                                    payload_sha = sha(payload)
                                    fields = parse_mesh(payload) if c['type_name'] == 'EARS_MESH' else parse_vfx(payload)
                                except (ValueError, struct.error) as exc:
                                    fields = dict(status='offline_qualification_failure', reason=str(exc),
                                                  stride=None, material=None, flags=None, shader_selection=None)
                                cached['parsed'][token] = dict(payload_sha256=payload_sha, parameters=fields)
                            content = cached['parsed'][token]
                            fields = content['parameters']
                            if content['payload_sha256'] is not None and content['payload_sha256'] != c['payload_sha256']:
                                fields = dict(status='offline_qualification_failure', reason='Original named payload hash differs',
                                              stride=None, material=None, flags=None, shader_selection=None)
                            row = dict(kind='geometry' if c['type_name'] == 'EARS_MESH' else 'vfx',
                                source=f['path'], archive_sha256=f['sha256'], entry=e['index'],
                                decoded_sha256=e['decoded_sha256'], payload_decoded_offset=c['payload_decoded_offset'],
                                name=c['name'], payload_sha256=c['payload_sha256'], payload_bytes=c['payload_size'],
                                authored_source=c.get('source_path'), parameters=fields,
                                native_setup_tested=False, native_use_tested=False, native_release_tested=False,
                                encountered_runtime=False, encountered_gameplay=False)
                            rows.append(row)
                            counts[row['kind']] += 1
                    except (ValueError, struct.error) as exc:
                        archive_failures.append(dict(source=f['path'], entry=e['index'], reason=str(exc),
                                                     hidden_named_occurrences=len(cs)))
        except (OSError, ValueError) as exc:
            archive_failures.append(dict(source=f['path'], reason=str(exc), hidden_named_occurrences=sum(len(cs) for _, cs in selected)))
    summary = dict(counts, unique_decoded_entries=len(cache), archive_failures=len(archive_failures),
                   qualification_failures=sum(r['parameters']['status'] == 'offline_qualification_failure' or
                                              bool(r['parameters'].get('failures')) for r in rows))
    field_strides, primitives, vfx_counts, module_keys, material_selectors = Counter(), Counter(), Counter(), Counter(), Counter()
    native_fields, empty_clumps, ffff_rows = 0, 0, 0
    for row in rows:
        p = row['parameters']
        if row['kind'] == 'geometry':
            empty_clumps += bool(p.get('empty_geometry_list'))
            for g in p.get('geometries', []):
                for n in g['native_records']:
                    for record in n.get('records', []):
                        fields = record.get('fields')
                        if fields and fields.get('status') == 'source_pinned_fields':
                            native_fields += 1
                            field_strides[fields['stride']] += 1
                            primitives.update(s['primitive'] for s in fields['submeshes'])
                            ffff_rows += sum(bool(s.get('selected_r16_words', {}).get('literal_FFFF_words'))
                                             for s in fields['submeshes'] if s.get('selected_r16_words'))
            material_selectors.update(m['pipeline_selector'] for m in p.get('materials', []) if 'pipeline_selector' in m)
        else:
            if 'row_count' in p:
                vfx_counts[p['row_count']] += 1
                module_keys.update(r['module_key'] for r in p['rows'] if r['module_key'] is not None)
    summary.update(native_geometry_records=native_fields, empty_mesh_clumps=empty_clumps,
        selected_r16_rows_with_literal_FFFF=ffff_rows, strides=dict(sorted(field_strides.items())),
        primitives=dict(sorted(primitives.items())), vfx_row_counts=dict(sorted(vfx_counts.items())),
        vfx_module_keys=dict(sorted(module_keys.items())), material_selectors=dict(sorted(material_selectors.items())))
    return rows, archive_failures, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets', type=Path, default=ROOT / 'analysis/assets.json')
    parser.add_argument('--asset-root', type=Path, default=ROOT / 'Simpsons Game, The (USA)')
    parser.add_argument('--image', type=Path, default=ROOT / 'analysis/simpsons.pe')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output, asset_root = args.output.resolve(), args.asset_root.resolve()
    require(not output.is_relative_to(asset_root), 'Output must remain outside original assets')
    require(output not in (args.assets.resolve(), args.image.resolve(), Path(__file__).resolve()), 'Output overwrites audit input')
    verify(args.image.read_bytes())
    raw = args.assets.read_bytes()
    rows, failures, summary = audit(json.loads(raw), asset_root)
    report = dict(schema_version=1, scope='source_pinned_offline_packaged_fields',
        image_sha256=IMAGE_SHA, catalog_sha256=sha(raw), tool_sha256=sha(Path(__file__).read_bytes()),
        producer_spans=[dict(function=name, start=f'{start:08X}', end=f'{end:08X}', sha256=digest)
                        for name, (start, end, digest) in SPANS.items()],
        original_paths=dict(mesh='826F2710/8270F108/823CE380/823CC388/823D0980/8282F970/8282F618/82831280/828311D0/826FED80/8273B760',
                            vfx='82C71F90/82750580/82758D70'),
        summary=summary, archive_failures=failures, rows=rows,
        coverage_policy='Offline parsed fields grant no native implementation, setup, draw, release, or gameplay credit; unknown combinations remain explicit.')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, separators=(',', ':'), sort_keys=True) + '\n', encoding='utf-8')
    print(json.dumps(summary, sort_keys=True))
    return 1 if failures or summary['qualification_failures'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
