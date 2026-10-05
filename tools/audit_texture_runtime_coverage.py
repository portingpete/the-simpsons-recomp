#!/usr/bin/env python3
"""Audit shipped ITXD metadata against current native decoder admission.

Only compressed prefixes through the dictionary metadata are expanded. The
complete STR hashes are checked against analysis/assets.json, but pixel bytes
are neither expanded nor decoded. Admission is a source-level inventory, not a
native decoder test or a claim that every texture has been drawn successfully.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import mmap
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def digest(data):
    return hashlib.sha256(data).hexdigest()


class PrefixReader:
    """Bounded incremental observed RefPack 10FB reader, stopping after prefix."""
    def __init__(self, data, expected):
        require(data[:2] == b'\x10\xfb', 'Unexpected RefPack signature')
        require(int.from_bytes(data[2:5], 'big') == expected, 'RefPack size mismatch')
        self.data, self.expected, self.pos, self.out = data, expected, 5, bytearray()

    def take(self, count):
        require(self.pos + count <= len(self.data), 'Truncated RefPack command')
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value

    def prefix(self, end):
        require(0 < end <= self.expected, 'Prefix exceeds original entry')
        while len(self.out) < end:
            command = self.take(1)[0]
            count = distance = 0
            stop = command >= 0xfc
            if command < 0x80:
                a = self.take(1)[0]
                literal = command & 3
                count = ((command >> 2) & 7) + 3
                distance = ((command & 0x60) << 3) + a + 1
            elif command < 0xc0:
                a, b = self.take(2)
                literal = a >> 6
                count = (command & 63) + 4
                distance = ((a & 63) << 8) + b + 1
            elif command < 0xe0:
                a, b, c = self.take(3)
                literal = command & 3
                count = ((command & 12) << 6) + c + 5
                distance = ((command & 16) << 12) + (a << 8) + b + 1
            else:
                literal = command & 3 if stop else ((command & 31) + 1) * 4
            require(len(self.out) + literal + count <= self.expected, 'RefPack output overflow')
            self.out.extend(self.take(literal))
            if count:
                require(distance <= len(self.out), 'RefPack backreference before output')
                seed = self.out[-distance:][:min(count, distance)]
                self.out.extend((seed * ((count + len(seed) - 1) // len(seed)))[:count])
            require(not stop or len(self.out) == self.expected, 'Premature RefPack stop')
        return self.out[:end]


def word(data, at):
    require(0 <= at <= len(data) - 4, 'Metadata word exceeds expanded prefix')
    return struct.unpack_from('>I', data, at)[0]


def original_records(raw, entry, chunk):
    start, length = entry['file_offset'], entry['stored_size']
    reader = PrefixReader(raw[start:start + length], entry['decoded_size'])
    begin = chunk['payload_decoded_offset']
    prefix = reader.prefix(begin + 48)
    require(prefix[begin:begin + 32].hex() == chunk['payload_header_hex'], 'Cached ITXD header changed')
    count_a, count_b = struct.unpack_from('>HH', prefix, begin + 4)
    sentinel = 8 * (count_a + count_b + 1) + 8
    require(sentinel < 4096, 'Unexpected dictionary header size')
    prefix = reader.prefix(begin + sentinel + 8)
    first, last = word(prefix, begin + sentinel), word(prefix, begin + sentinel + 4)
    require(first >= sentinel + 8 and last >= first, 'Unsupported empty/invalid texture list')
    end = last + 248
    require(end <= chunk['payload_size'] and end < 4 * 1024 * 1024,
            'Dictionary metadata exceeds payload or audit budget')
    prefix = reader.prefix(begin + end)
    metadata = prefix[begin:]
    records, visited, link = [], set(), first
    while link != sentinel:
        require(link not in visited and first <= link <= last and link + 248 <= len(metadata),
                'Texture list cycle or out-of-bounds record')
        visited.add(link)
        offset = link - 8
        m = metadata[offset:offset + 256]
        name_bytes = m[16:80].split(b'\0', 1)[0]
        require(name_bytes and all(32 <= c <= 126 for c in name_bytes), 'Invalid texture name')
        w, h = word(m, 0x84), word(m, 0x88)
        require(word(m, 0) == offset + 0x78 and word(m, 0x78) == word(m, 0)
                and word(m, 0xac) == offset + 0xcc, 'Original copied raster/header layout differs')
        storage, size = word(m, 0xc0), word(m, 0xbc)
        require(size and storage + size <= chunk['payload_size'], 'Pixel extent outside dictionary')
        d = struct.unpack_from('>6I', m, 0xe8)
        records.append(dict(name=name_bytes.decode('ascii'), record_offset=offset,
                            metadata_sha256=digest(m), width=w, height=h,
                            format=f'{word(m, 0xc4):08X}', auxiliary=word(m, 0xb0),
                            storage_offset=storage, storage_bytes=size,
                            descriptor=[f'{v:08X}' for v in d],
                            mip_max_level=(d[4] >> 6) & 15, pitch=((d[0] >> 22) & 511) * 32))
        link = word(metadata, link)
    require(last in visited, 'Last texture list node was not reached')
    return records, len(reader.out), reader.pos


def admission(r, before_small_rgba=False):
    """Translate current explicit C++ guards for metadata coverage only."""
    w, h, n = r['width'], r['height'], r['storage_bytes']
    d = tuple(int(v, 16) for v in r['descriptor'])
    fmt = int(r['format'], 16)
    if r['auxiliary']:
        return 'nonzero_auxiliary'
    if fmt not in (0x28000102, 0x18280186, 0x1a200152, 0x1a200153, 0x1a200154):
        return 'unqualified_format'
    power = lambda v: v > 0 and not v & (v - 1)
    minimum, maximum = (64, 2048) if fmt == 0x28000102 else ((32, 1024) if fmt == 0x18280186 else (4, 2048))
    small_base = not before_small_rgba and fmt == 0x18280186 and w == h == 16 and d[4] == 0
    if not ((small_base or (minimum <= w <= maximum and minimum <= h <= maximum)) and power(w) and power(h)):
        return 'unqualified_dimensions'
    level = (d[4] >> 6) & 15
    lw, lh = w.bit_length() - 1, h.bit_length() - 1
    packed = min(lw, lh) - 4
    if fmt == 0x28000102:
        expected = (0x80000002 | ((w // 32) << 22), 2, ((h - 1) << 13) | (w - 1),
                    0x1400, level << 6, ((w * h) | 0xa00) if level else 0x200)
        if d != expected or level > max(lw, lh):
            return 'unqualified_luminance_descriptor'
        total = w * h if not level else sum(
            (max(32, w >> i) * max(32, h >> i) + 4095) & ~4095
            for i in range(min(level, packed) + 1))
    elif fmt == 0x18280186:
        if r['name'] in ('simpsons_palette', 'dual_simpsons_palette') and (w != 64 or h != 64 or level):
            return 'unqualified_named_palette_profile'
        expected = (0x80000002 | ((max(32, w) // 32) << 22), 0x86, ((h - 1) << 13) | (w - 1),
                    0xc14, level << 6, ((w * h * 4) | 0xa00) if level else 0x200)
        if d != expected or level > packed:
            return 'unqualified_rgba_descriptor_or_shared_tail'
        total = sum(max(32, w >> i) * max(32, h >> i) * 4 for i in range(level + 1))
    else:
        if ((d[0] & 0x803fffff) != 0x80000002 or d[1] != (fmt & 255)
                or d[2] != ((h - 1) << 13 | (w - 1)) or d[3] != 0xd10
                or d[4] != level << 6 or level > max(lw, lh)
                or (level and packed <= 0)
                or ((d[5] & 0xfff) != 0xa00 if level else d[5] != 0x200)):
            return 'unqualified_bc_descriptor'
        pitch = r['pitch']
        if pitch < w or pitch % 128 or pitch > 2048:
            return 'unqualified_bc_pitch'
        sizes = []
        for i in range(min(level, packed) + 1 if level else 1):
            wi, hi = (w >> i) if i else pitch, max(1, h >> i)
            columns = (((wi + 3) // 4) + 31) & ~31
            rows = (((hi + 3) // 4) + 31) & ~31
            sizes.append((columns * rows * (8 if fmt & 255 == 0x52 else 16) + 4095) & ~4095)
        if level and (d[5] & 0xfffff000) != sizes[0]:
            return 'unqualified_bc_mip_address'
        total = sum(sizes)
    return 'admitted_metadata' if n == total else 'unqualified_allocation_extent'


def fixtures(records):
    actual = defaultdict(list)
    for r in records:
        actual[r['metadata_sha256']].append(r)
    result = []
    for path in sorted((ROOT / 'build').glob('itxd*/*.metadata')):
        data = path.read_bytes()
        if len(data) != 256:
            continue
        matches = actual.get(digest(data), [])
        result.append(dict(path=path.relative_to(ROOT).as_posix(), sha256=digest(data),
                           original_occurrences=len(matches), name=matches[0]['name'] if matches else None,
                           format=matches[0]['format'] if matches else None,
                           dimensions=[matches[0]['width'], matches[0]['height']] if matches else None,
                           mip_max_level=matches[0]['mip_max_level'] if matches else None))
    return result


def fixture_corpus(records):
    """Reconcile original case manifests with audited source metadata identities."""
    original, synthetic = [], []
    for rel in ('build/itxd-luminance/family-manifest.json', 'build/itxd-mips/manifest.json',
                'build/itxd-luminance-mip/manifest.json'):
        path = ROOT / rel
        if not path.exists():
            continue
        value = json.loads(path.read_text())
        cases = value if isinstance(value, list) else value['cases']
        for case in cases:
            item = dict(manifest=rel, name=case['name'], width=case['width'], height=case['height'],
                        descriptor=[f'{v:08X}' if isinstance(v, int) else v for v in case['descriptor']],
                        levels=len(case.get('level_sha256', [])) or 1)
            if case.get('synthetic'):
                synthetic.append(item)
                continue
            p = case['provenance']
            matches = [r for r in records if r['source'] == p['source']['path']
                       and r['entry'] == p['entry']['index'] and r['dictionary'] == p['resource']['name']
                       and r['name'] == case['name'] and r['descriptor'] == item['descriptor']]
            require(len(matches) == 1, 'Original family fixture metadata did not reconcile: ' + case['name'])
            if case.get('metadata_sha256'):
                require(matches[0]['metadata_sha256'] == case['metadata_sha256'], 'Family metadata hash differs')
            item.update(metadata_sha256=matches[0]['metadata_sha256'], format=matches[0]['format'])
            original.append(item)
    for binary, manifest in (('candy.bin', 'manifest.json'), ('tonal.bin', 'tonal-manifest.json'),
                             ('buildings.bin', 'buildings-manifest.json')):
        folder = ROOT / 'build/itxd-rgba'
        if not (folder / binary).exists() or not (folder / manifest).exists():
            continue
        data = (folder / binary).read_bytes()
        m = json.loads((folder / manifest).read_text())
        d = struct.unpack_from('<6I', data)
        descriptor = [f'{v:08X}' for v in d]
        length = sum(level[1] for level in m['layout'])
        require(digest(data[24:24 + length]) == m['storage_sha256'], 'RGBA fixture storage changed')
        provenance = m['provenance'] if isinstance(m['provenance'], list) else [m['provenance']]
        for p in provenance:
            matches = [r for r in records if r['source'] == p['source']['path']
                       and r['entry'] == p['entry']['index'] and r['dictionary'] == p['resource']['name']
                       and r['descriptor'] == descriptor and r['storage_bytes'] == length]
            require(len(matches) == 1, 'Original RGBA fixture metadata did not reconcile: ' + binary)
            r = matches[0]
            original.append(dict(manifest='build/itxd-rgba/' + manifest, name=r['name'],
                                 width=r['width'], height=r['height'], format=r['format'],
                                 descriptor=descriptor, levels=len(m['level_sha256']),
                                 metadata_sha256=r['metadata_sha256']))
    return dict(original_family_cases=original, synthetic_family_cases=synthetic,
                original_family_case_count=len(original), synthetic_family_case_count=len(synthetic),
                original_family_levels=sum(c['levels'] for c in original),
                synthetic_family_levels=sum(c['levels'] for c in synthetic))


def dictionary_cases(raw, entries, source, cache, before_small_rgba=False):
    """Audit each original dictionary independently, retaining failed identities.

    A rejected dictionary is never put in the successful payload cache. Later
    occurrences and unrelated entries still execute their own original parser.
    This is metadata parsing only, not the runtime create/use/release path.
    """
    records, dictionaries, failures = [], [], []
    expanded = consumed = 0
    for entry, chunk in entries:
        identity = dict(source=source, entry=entry['index'], name=chunk['name'],
                        original_payload_sha256=chunk['payload_sha256'])
        key = chunk['payload_sha256']
        try:
            if key not in cache:
                parsed = original_records(raw, entry, chunk)
                cache[key] = parsed
                expanded += parsed[1]
                consumed += parsed[2]
            parsed, _, _ = cache[key]
            dictionaries.append(dict(identity, textures=len(parsed), status='parsed'))
            for texture in parsed:
                r = dict(texture, source=source, entry=entry['index'], dictionary=chunk['name'])
                r['admission'] = admission(r, before_small_rgba)
                records.append(r)
        except (ValueError, KeyError, struct.error) as error:
            failure = dict(identity, status='rejected_metadata', error=str(error),
                           error_type=type(error).__name__)
            failures.append(failure)
            dictionaries.append(dict(failure, textures=None))
    return records, dictionaries, failures, expanded, consumed


def run(output, before_small_rgba=False):
    inventory_path = ROOT / 'analysis/assets.json'
    asset_root = (ROOT / 'Simpsons Game, The (USA)').resolve()
    require(output != asset_root and asset_root not in output.parents,
            'Audit report must remain outside the original asset tree')
    require(output != inventory_path.resolve() and output != Path(__file__).resolve(),
            'Audit report must not overwrite its source inventory or tool')
    inventory = json.loads(inventory_path.read_text())
    records, files, dictionaries, failures = [], [], [], []
    expanded = consumed = declared = 0
    cache = {}
    for item in inventory['files']:
        entries = [(e, c) for e in item.get('inspection', {}).get('entries', [])
                   for c in e.get('chunks', []) if c.get('type_name') == 'EARS_ITXD']
        if not entries:
            continue
        path = ROOT / 'Simpsons Game, The (USA)' / item['path']
        declared += sum(chunk['payload_size'] for _, chunk in entries)
        try:
            with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as raw:
                require(len(raw) == item['size'] and digest(raw) == item['sha256'], 'Original STR identity changed: ' + item['path'])
                files.append(dict(path=item['path'], size=item['size'], sha256=item['sha256']))
                result = dictionary_cases(raw, entries, item['path'], cache, before_small_rgba)
                records.extend(result[0]); dictionaries.extend(result[1]); failures.extend(result[2])
                expanded += result[3]; consumed += result[4]
        except (OSError, ValueError) as error:
            for entry, chunk in entries:
                failure = dict(source=item['path'], entry=entry['index'], name=chunk['name'],
                               original_payload_sha256=chunk['payload_sha256'], status='rejected_source',
                               error=str(error), error_type=type(error).__name__)
                failures.append(failure); dictionaries.append(dict(failure, textures=None))
    groups = defaultdict(list)
    for r in records:
        if r['admission'] != 'admitted_metadata':
            groups[r['admission']].append(r)
    report = dict(schema='texture_runtime_metadata_coverage_v1', complete=not failures,
                  authority=dict(inventory_sha256=digest(inventory_path.read_bytes()),
                                 source_files=files,
                                 decoder_sha256=digest((ROOT / ('build/texture-runtime-audit/before/itxd_blocks.cpp'
                                                              if before_small_rgba else 'renderer/itxd_blocks.cpp')).read_bytes()),
                                 admission_baseline='before_small_rgba' if before_small_rgba else 'current',
                                 owner_sha256=digest((ROOT / 'runtime/engine_itxd_textures.cpp').read_bytes())),
                  limits=['Metadata admission translated from current C++ guards; native decoder not run.',
                          'Packaged STR SHA256 checked; cached payload SHA256 labels not recomputed from full decoded pixels.',
                          'Prefix stops after metadata; original pixels and GPU output not checked here.',
                          'Fixture presence and original metadata identity do not imply a test was run or every asset drawn.',
                          'Asset occurrence does not establish reachability in any particular mission.'],
                  summary=dict(source_files=len(files), dictionaries=len(dictionaries),
                               unique_dictionaries=len(cache), rejected_dictionaries=len(failures), texture_occurrences=len(records),
                               unique_metadata_records=len({r['metadata_sha256'] for r in records}),
                               packaged_source_bytes=sum(f['size'] for f in files),
                               declared_texture_dictionary_bytes=declared,
                               expanded_prefix_bytes=expanded, consumed_compressed_prefix_bytes=consumed,
                               format_counts=dict(sorted(Counter(r['format'] for r in records).items())),
                               dimensions=dict(sorted(Counter(f"{r['width']}x{r['height']}" for r in records).items())),
                               mip_max_counts=dict(sorted(Counter(str(r['mip_max_level']) for r in records).items())),
                               admission_counts=dict(sorted(Counter(r['admission'] for r in records).items()))),
                  rejection_groups={k: dict(count=len(v), unique_metadata=len({r['metadata_sha256'] for r in v}),
                                            representatives=v[:12]) for k, v in sorted(groups.items())},
                  original_fixture_metadata=fixtures(records), fixture_corpus=fixture_corpus(records),
                  failures=failures, dictionaries=dictionaries, textures=records)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(report['summary'], indent=2, sort_keys=True))
    print('Report:', output)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/texture-runtime-audit/report.json')
    parser.add_argument('--before-small-rgba', action='store_true',
                        help='Reproduce pre-fix admission using the saved original decoder source identity.')
    args = parser.parse_args()
    result = run(args.output.resolve(), args.before_small_rgba)
    if not result['complete']:
        raise SystemExit(1)
