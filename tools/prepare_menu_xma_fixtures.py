"""Extract original reader-normalized menu or loc music for native admission tests."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from inspect_assets import inspect_mus, inspect_snu
from probe_xma_multilayer import split_blocks

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test', type=Path, required=True)
    parser.add_argument('--bank', choices=('menu','loc','dialogue'), default='menu')
    args = parser.parse_args()
    dialogue=args.bank=='dialogue'
    path='Simpsons Game, The (USA)/audiostreams/hr_xxx_0/d_wchr_xxx_0000b31.exa.snu' if dialogue else f'Simpsons Game, The (USA)/audiostreams/{args.bank}_mus.mus'
    data = (ROOT / path).read_bytes()
    expected = ('202927e7eb530e077db5e1543bfde9d654cc18356964c9a0d67e822c6218aa62' if dialogue else
                'b350a5b1ce17ff5b53a2b0a6c035d9c36eafbf213252dad07b2c387f6c0743fa' if args.bank=='loc' else
                '3154127815784c0de9331e1b69748ff47b2e5e70de012296fedb1aace9a31dda')
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('Original music asset changed')
    directory = ROOT / f'build/{args.bank}-music-fixtures'
    directory.mkdir(parents=True, exist_ok=True)
    if dialogue:
        from qualify_mono_dialogue_xma import PROFILES
        streams=[]
        for profile in PROFILES:
            original=(ROOT/profile['source']).read_bytes()
            if hashlib.sha256(original).hexdigest()!=profile['sha256']:raise ValueError('Original dialogue asset changed')
            streams.append((original,inspect_snu(original)))
    else:streams=[(data,stream) for stream in inspect_mus(data)['streams']]
    output = bytearray(struct.pack('<I', len(streams)))
    for data,stream in streams:
        audio = stream['audio']
        blocks = split_blocks(data, audio['audio_offset'], audio['audio_offset'] + audio['audio_size'], stream['header']['channels'])
        header = stream['header']['header_offset']
        output.extend(data[header:header+8])
        output.extend(struct.pack('<I', len(blocks)))
        for block in blocks:
            raw = bytearray(data[block['offset']:block['offset'] + block['bytes']])
            raw[0] &= 0x7f
            output.extend(struct.pack('<I', len(raw)))
            output.extend(raw)
    path = directory / 'streams.bin'
    path.write_bytes(output)
    return subprocess.run([str(args.test.resolve(strict=True)), str(path),args.bank], timeout=160).returncode


if __name__ == '__main__':
    sys.exit(main())
