"""Prepare six pinned original packet streams for native source tests; no decode.

Only copies EA layer payloads and restores the previously qualified FF padding.
Derived packets remain under build/xma-source/fixtures, separate from originals
and the other test targets. No stock CLI, output device or EOF operation runs.
"""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import tempfile

from inspect_assets import inspect_snu, inspect_mus
from prepare_audio_fixtures import CASES as MONO
from probe_xma_multilayer import CASES as LAYERS, split_blocks, packets_for

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/xma-source/fixtures'
HASHES = {
    'stereo-layer0.packets': '15b2401b70ad812144a9cc856a0018e6498fb61e32a41a178b6c1c3c837a8dca',
    'six-mus52-layer0.packets': '9bbda700f82471b36310d3994942091a82b0ae9923e872f93435738cb0861c4b',
    'six-mus52-layer1.packets': 'ce0778603fc90ceabb60faec1f16ac3ac9f65d2b5667b66cfdedfc0e79a7fd65',
    'six-mus52-layer2.packets': '414858dac1137e78c8c731fdf3c0cece7ba9f91a792c63c1f0579b7c5ccfe431',
}
def need(value, message):
    if not value: raise ValueError(message)
def sha(data): return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT/'Simpsons Game, The (USA)')
    parser.add_argument('--test', type=Path)
    args = parser.parse_args()
    originals = args.root.resolve(strict=True)
    need(OUT.resolve() == ROOT.resolve()/'build/xma-source/fixtures', 'Fixture output redirected')
    need(not OUT.resolve().is_relative_to(originals), 'Fixture output overlaps originals')
    OUT.mkdir(parents=True, exist_ok=True)
    reports = []
    def read(relative, expected):
        source = (originals/relative).resolve(strict=True)
        need(source.is_relative_to(originals), 'Source escapes original directory')
        data = source.read_bytes()
        need(sha(data) == expected, f'Original source identity changed: {relative}')
        return data
    def save(name, data, expected, metadata):
        need(name == Path(name).name and name.endswith('.packets'), 'Unsafe fixture name')
        target = OUT/name
        need(target.resolve().parent == OUT.resolve(), 'Fixture output escaped directory')
        need(0 < len(data) <= 262144 and len(data) % 2048 == 0, 'Fixture packet extent changed')
        need(sha(data) == expected, f'Adapted packet identity changed: {name}')
        if not target.exists() or sha(target.read_bytes()) != expected:
            with tempfile.NamedTemporaryFile(dir=OUT, prefix='packet-', suffix='.tmp', delete=False) as temp:
                temp.write(data)
                temporary = Path(temp.name)
            try: temporary.replace(target)
            finally:
                if temporary.exists(): temporary.unlink()
        reports.append({'file': name, 'sha256': expected, 'packets': len(data)//2048, **metadata})
    for label, relative, source_hash, expected, declared in MONO:
        data = read(relative, source_hash)
        info = inspect_snu(data); header, audio = info['header'], info['audio']
        need(header['channels'] == 1 and header['sample_rate'] == 48000 and
             not header['loop'] and header['samples'] == declared, 'Mono profile changed')
        start = audio['audio_offset']; end = start+audio['audio_size']
        packets = bytearray(); spans = []
        while start < end:
            size = int.from_bytes(data[start:start+4], 'big') & 0xffffff
            need(size >= 12 and start+size <= end, 'Invalid mono block extent')
            layer = start+8; length = int.from_bytes(data[layer:layer+4], 'big') >> 2
            need(length >= 8 and layer+length <= start+size, 'Invalid mono layer extent')
            payload = data[layer+4:layer+length]
            need(payload[:4] == b'\x08\0\0\0', 'Unqualified mono packet prefix')
            padding = (-len(payload)) % 2048
            spans.append({'source_offset': layer+4, 'source_bytes': len(payload),
                          'packet_offset': len(packets), 'restored_ff_bytes': padding})
            packets += payload+b'\xff'*padding
            start += size
        need(start == end, 'Mono source extent not exhausted')
        save(label+'.packets', bytes(packets), expected,
             {'source': relative, 'source_sha256': source_hash, 'spans': spans})
    for case in LAYERS:
        if case['label'] not in ('stereo', 'six-mus52'): continue
        data = read(case['path'], case['sha256'])
        info = inspect_mus(data)['streams'][case['mus_index']] if 'mus_index' in case else inspect_snu(data)
        header, audio = info['header'], info['audio']
        need(all(header[k] == case[k] for k in ('channels','samples','loop')) and header['sample_rate'] == 48000,
             'Multilayer profile changed')
        blocks = split_blocks(data, audio['audio_offset'], audio['audio_offset']+audio['audio_size'], header['channels'])
        need(sum(block['samples'] for block in blocks) == header['samples'], 'Multilayer sample extent changed')
        for layer in range(header['channels']//2):
            name = f"{case['label']}-layer{layer}.packets"
            packets, spans = packets_for(data, blocks, layer)
            save(name, packets, HASHES[name], {'source': case['path'], 'source_sha256': case['sha256'], 'spans': spans})
    need(len(reports) == 6, 'Missing source fixture streams')
    (OUT/'manifest.json').write_text(json.dumps(reports, indent=2)+'\n', encoding="utf-8")
    print('Prepared six hash-pinned original layer streams; packet copies only, no decoder or original writes', flush=True)
    if args.test:
        return subprocess.run([str(args.test.resolve(strict=True)), str(OUT), str(OUT)], timeout=45).returncode
    return 0

if __name__ == '__main__': sys.exit(main())
