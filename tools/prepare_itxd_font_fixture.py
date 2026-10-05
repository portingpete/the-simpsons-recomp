"""Create or verify exact font BC2 fixtures from the player's original STR."""
from pathlib import Path
import hashlib,json,struct
from extract_resource import select_payload
from inspect_itxd import descriptor
from decode_itxd import untile_blocks

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/itxd-font'

def main():
    root=ROOT/'Simpsons Game, The (USA)'
    data,provenance=select_payload(root/'frontend/frontend.str',17,'frontend_split16.itxd',root)
    digest=lambda b:hashlib.sha256(b).hexdigest()
    if digest(data)!='ed6c2788d661f59236637f290dd52b210dd32c368b0fa6f7739af5df35c0fad4':
        raise ValueError('Original font dictionary identity changed')
    word=lambda at:struct.unpack_from('>I',data,at)[0]
    record=0x930
    if data[record+8:record+72].split(b'\0')[0]!=b'HighlanderStdBold6060b':
        raise ValueError('Original font record name changed')
    words=tuple(word(record+i) for i in range(0xE0,0xF8,4))
    if words!=(0x88000002,0x53,0x3FE3FF,0xD10,0,0x200):raise ValueError('Original font descriptor changed')
    description=descriptor(words)
    start,size=word(record+0xB8),word(record+0xB4)
    if start!=561152 or size!=524288 or start+size!=len(data):raise ValueError('Original font payload bounds changed')
    tiled=data[start:start+size]
    linear,addressing=untile_blocks(tiled,256,128,256,16,'8in16')
    if addressing['addressed_blocks']!=32768 or addressing['maximum_addressed_end']!=size:
        raise ValueError('Original font block mapping changed')
    outputs={'HighlanderStdBold6060b.tiled':tiled,'HighlanderStdBold6060b.bc2':linear,
             'HighlanderStdBold6060b.metadata':data[record-8:record-8+0x100]}
    evidence=dict(provenance=provenance,descriptor=description,addressing=addressing,
                  outputs={n:digest(b) for n,b in outputs.items()})
    outputs['manifest.json']=(json.dumps(evidence,indent=2)+'\n').encode('utf-8')
    OUT.mkdir(parents=True,exist_ok=True)
    for name,payload in outputs.items():
        path=OUT/name
        if path.exists():
            if path.read_bytes()!=payload:raise ValueError(f'Existing original font fixture differs: {name}')
        else:
            with path.open('xb') as f:f.write(payload)
    print('PASS: original ITXD font fixtures; 32768 addressed BC2 blocks, exact source identity')

if __name__=='__main__':main()
