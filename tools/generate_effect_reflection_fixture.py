"""Serialize the independent original-byte proof as a CPU test fixture only."""
from pathlib import Path
import argparse
import json
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'build/effect-reflection-all49/evidence.json'
OUTPUT=ROOT/'build/effect-reflection-all49/fixture.bin'


def render(report,catalogs):
    rows=[r for c in catalogs for r in c['rows']]
    out=bytearray(b'FXRF')
    def word(n):out.extend(struct.pack('>I',n&0xFFFFFFFF))
    def quad(n):out.extend(struct.pack('>Q',n))
    def text(s):
        b=s.encode('ascii');word(len(b));out.extend(b)
    def binding(words,elements=None):
        for n in words:word(n)
        word(0xFFFFFFFF if elements is None else elements)
    word(1);word(47)
    for r,q,p,pred in zip(rows,report['parameters'],report['passes'],report['predicates']):
        if not q['gateway_row']:continue
        assert q['name']==r['name']==p['name']
        word(int(r['blob_va'],16));word(r['bytes']);word(pred['helper_r6'])
        word(q['gateway_T2C']);word(len(q['parameters']))
        for param in q['parameters']:
            text(param['descriptor']['name'])
            d=param['descriptor'];binding(param['parameter_row_words'],d['elements'] if d['kind']==2 else None)
            for n in param['reflection_row_words'][:6]:word(n)
        word(len(q['gateway_T30']))
        for cls in q['gateway_T30']:
            for n in cls[:6]:word(n)
        lights=q['lighting'][0]['blocks'] if q['lighting'] else []
        word(len(lights));active=flags=0
        for block in lights:
            for b in block['rows_in_storage_order']:binding(b)
            uses=[b[1] for b in block['rows_in_storage_order']]
            if any(u&0x55 for u in uses):active+=1;flags|=2
            elif any(u&0xAA for u in uses):active+=1;flags|=1
        word(active);word(flags)
        word(len(p['numeric_output_projection']))
        for projection in p['numeric_output_projection']:
            raw=bytes.fromhex(projection['record_hex'])
            word(int.from_bytes(raw[:4],'big'));word(int.from_bytes(raw[4:8],'big'))
            for m in projection['masks']:quad(int(m,16))
            word(len(projection['mutable_clear_calls']))
            for c in projection['mutable_clear_calls']:
                word(c['namespace']);word(c['first_leaf']);word(c['leaf_count'])
    return bytes(out)


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    subprocess.run([sys.executable,'-B',str(ROOT/'build/effect-reflection-all49/verify.py')],cwd=ROOT,check=True, timeout=60)
    report=json.loads(SOURCE.read_text(encoding='utf-8'))
    catalogs=[json.loads((ROOT/'analysis'/n).read_text(encoding='utf-8')) for n in
              ('native-effect-catalog.json','native-post-effect-catalog.json')]
    data=render(report,catalogs)
    if args.check:
        if OUTPUT.read_bytes()!=data:raise ValueError('stale reflection fixture')
    elif not OUTPUT.exists() or OUTPUT.read_bytes()!=data:OUTPUT.write_bytes(data)
    print('PASS independent reflection test fixture:',len(data),'bytes,47 profiles')


if __name__=='__main__':main()
