"""Verify the exact original BC3 resource reached by native boot 164."""
from pathlib import Path
import hashlib
import json
import struct
from extract_resource import select_payload
from decode_itxd import untile_blocks

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/itxd-bc3'


def main():
    root = ROOT / 'Simpsons Game, The (USA)'
    data, provenance = select_payload(root / 'frontend/frontend.str', 25,
                                      'frontend_split24.itxd', root)
    digest = lambda value: hashlib.sha256(value).hexdigest()
    if digest(data) != '0babfa2b129e9c00b5bf28f90db2763e2b5252d611d1d398b80fec37b9283938':
        raise ValueError('Original shared-library dictionary identity changed')
    record = 0xA28
    metadata = data[record:record + 0x100]
    word = lambda offset: struct.unpack_from('>I', metadata, offset)[0]
    words = tuple(word(offset) for offset in range(0xE8, 0x100, 4))
    if (metadata[0x10:0x50].split(b'\0')[0] != b'8_SharedLibrary'
            or words != (0x84000002, 0x54, 0x1FE1FF, 0xD10, 0, 0x200)
            or (word(0x84), word(0x88), word(0xC4), word(0xB0)) != (512, 256, 0x1A200154, 0)
            or (word(0xC0), word(0xBC)) != (757760, 131072)):
        raise ValueError('Original shared-library BC3 record changed')
    start, size = word(0xC0), word(0xBC)
    if start + size > len(data):
        raise ValueError('Original shared-library allocation exceeds dictionary')
    tiled = data[start:start + size]
    linear, addressing = untile_blocks(tiled, 128, 64, 128, 16, '8in16')
    if addressing['addressed_blocks'] != 8192 or addressing['maximum_addressed_end'] != size:
        raise ValueError('Original shared-library block mapping changed')
    outputs = {'8_SharedLibrary.tiled': tiled, '8_SharedLibrary.bc3': linear,
               '8_SharedLibrary.metadata': metadata}
    evidence = dict(provenance=provenance, record_offset=record,
                    descriptor_words=list(words), width=512, height=256,
                    format='BC3_UNORM', addressing=addressing,
                    outputs={name: digest(value) for name, value in outputs.items()})
    outputs['manifest.json'] = (json.dumps(evidence, indent=2) + '\n').encode('utf-8')
    OUT.mkdir(parents=True, exist_ok=True)
    for name, value in outputs.items():
        path = OUT / name
        if path.exists():
            if path.read_bytes() != value:
                raise ValueError(f'Existing original BC3 fixture differs: {name}')
        else:
            with path.open('xb') as stream:
                stream.write(value)
    print('PASS: original 8_SharedLibrary BC3; 8192 exact blocks, pinned dictionary and metadata')


if __name__ == '__main__':
    main()
