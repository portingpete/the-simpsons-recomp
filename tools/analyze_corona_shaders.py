"""Offline evidence for the original corona visibility producer and consumer.

Only these three complete records are admitted. The native HLSL is a static
transcription, not a runtime shader decoder. Numeric fixtures are native-only.
"""
from pathlib import Path
import hashlib
import struct
import json
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge

ROOT=Path(__file__).resolve().parents[1]
PROFILES={
    'VS':(0x82153278,484,'248aa2ab41c6d88687f7cf937dd7f318974c70a4d31320ae06a66c1ebf790672'),
    'PS':(0x82153460,652,'5b8b4c65b746d1a3e00d89074bb1b805f7f698ebf6df5464deb81fd53d80c05c'),
    'SPRITE':(0x821536F0,500,'7f58913b69b8a16abf6ef477ef374170c900b430c0b4aea82cb3472abd77e2e8'),
}
DECLARATION=(0,0x002C23A5,0,8,0x001A23A6,0x00050000,
             24,0x002A23B9,0x00050100,0x00FF0000,0xFFFFFFFF,0)
CF=((0x3006,0x1200),(0x1F0009,0x7000),(0x1009,0x1200),
    (0x1E0007,0x7000),(0x9600A,0x1200),(0x2010,0x1200),
    (0x1E0004,0x8400),(0x1012,0x1200),(0x1F0002,0x8400),
    (0,0xC400),(0x2013,0x2200),(0,0))

def inspect(image):
    need=screen.require
    need(struct.unpack_from('>12I',image,0x82153E0C-screen.BASE)==DECLARATION,'Corona declaration changed')
    records={}
    for name,(va,n,digest) in PROFILES.items():
        record=image[va-screen.BASE:va-screen.BASE+n]
        need(hashlib.sha256(record).hexdigest()==digest,'Corona '+name+' bytes changed')
        records[name]=record
    ps=records['PS'];header=struct.unpack_from('>9I',ps)
    need(struct.unpack_from('>3I',ps,0x108)==(0x23980002,17,17),'Corona loop defaults changed')
    need(struct.unpack_from('>4f',ps,0x144+48)==(-1,0,.125,1),'Corona literal constants changed')
    code=ps[header[1]+64:];control=[]
    for i in range(6):
        a,b,c=screen.words(code,12*i)
        control.extend(((a,b&65535),((b>>16|(c<<16))&0xFFFFFFFF,c>>16)))
    need(tuple(control)==CF,'Corona nested loop schedule changed')
    # Both loops have seventeen iterations. Neither uses aL, predicates or
    # relative constants; r1.y/r1.w advance explicitly by literal 1/8.
    rows={i:edge.alu(*screen.words(code,12*i)) for i in range(6,21) if i!=10}
    need(rows[14]['scalar_opcode']==rows[18]['scalar_opcode']==45,'Corona grid increment changed')
    need(rows[20]['vector_mask']==15 and rows[20]['scalar_mask']==8 and rows[20]['export'],
         'Corona output literal alpha changed')
    fetch=screen.decode_fetch(screen.words(code,120))
    need(fetch['fetch_constant_index']==0 and fetch['destination_swizzle']==[7,7,7,0]
         and fetch['source_components'][:2]==[0,1] and fetch['computed_lod'],'Corona depth sample changed')
    return {'records':{k:{'address':hex(v[0]),'bytes':v[1],'sha256':v[2]} for k,v in PROFILES.items()},
            'loops':[17,17],'declaration_stride':36,'depth_sampler':0,'query_sampler':1}

if __name__=='__main__':print(json.dumps(inspect((ROOT/'analysis/simpsons.pe').read_bytes())))
