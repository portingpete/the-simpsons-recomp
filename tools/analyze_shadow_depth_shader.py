"""Offline qualification of original RenderShadowDepth VS820C2FA0.

Static field inspection only. No shader execution, runtime translator or GPU
backend is imported. Native arithmetic and mesh binding require separate tests.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four

ROOT=Path(__file__).resolve().parents[1]
REPORT=ROOT/'analysis/native-shadow-depth-shader.json'
need=screen.require
VA=0x820C2FA0
RECORD_SHA='b22ecbda409e80ba9bbc5a8da237c8894c2f09c79b93646868e925c70ef058be'
CODE_SHA='b3072e2f1850e46eb535cc075d96ee712aa53b5e46c43ee4692a61ba3fbfef16'
CF=[(0xF2555007,0x1000),(0x4006,0xB000),(0x600C,0x1200),(0x6012,0x1200),
    (0x6018,0x1200),(0x201E,0x1200),(0,0xC200),(0x6020,0x1200),
    (0x6026,0x1200),(0x602C,0x1200),(0x6032,0x1200),(0,0xC400),
    (0x2038,0x2200),(0,0)]
LITERALS=(0,0,0,0,0,0,0,0,0,0x3F800000,0x40400000,0x447A0000,0x42700000,0,0,0)

def schedule(code):
    fields=[]
    for i in range(7):
        a,b,c=screen.words(code,i*12)
        fields.extend(((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)))
    need(fields==CF,'Original shadows control flow changed')
    slots=[]
    for lo,hi in fields:
        if hi>>12 in (1,2):
            start,count,sequence=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(0<count<=6 and sequence>>(count*2)==0,'Invalid shadows exec span')
            slots.extend((start+i,bool(sequence&(1<<(2*i))),bool(sequence&(2<<(2*i)))) for i in range(count))
    need([i for i,_,_ in slots]==list(range(7,58)),'Incomplete shadows schedule')
    need([i for i,f,_ in slots if f]==[7,8,9,10],'Changed original fetch slots')
    need(screen.words(code,58*12)==(0x4E4A000A,0x01EA67DC,0xD36BD9F6),'Changed shadows trailer')
    return slots

def inspect(image):
    need(hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,'Original image changed')
    for path,digest in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA)):
        need(hashlib.sha256(path.read_bytes()).hexdigest()==digest,'Static field reference changed')
    record=image[VA-screen.BASE:VA-screen.BASE+4396]
    need(hashlib.sha256(record).hexdigest()==RECORD_SHA,'Original shadows shader changed')
    code=record[3688:4396]
    need(hashlib.sha256(code).hexdigest()==CODE_SHA,'Original shadows code changed')
    need(struct.unpack_from('>16I',record,3624)==LITERALS,'Original shadows literals changed')
    rows=[]
    for slot,fetch,serial in schedule(code):
        words=screen.words(code,slot*12)
        fields=screen.decode_fetch(words) if fetch else edge.alu(*words)
        if not fetch:
            need(not fields['predicated'] and not fields['vector_clamp'] and not fields['scalar_clamp'],
                 'Unexpected instruction predicate/clamp')
            if fields['scalar_opcode']==46:
                # Binary scalar operands use W of the constant and X of a
                # separately encoded temporary; both have the negate modifier.
                swizzle=words[1]&255
                fields['binary_scalar']={'constant':words[2]&255,'constant_component':((swizzle>>6)+3)&3,
                    'temporary':((words[0]>>26)&1)|(((words[2]>>29)&1)<<1)|(swizzle&0x3C),
                    'temporary_component':swizzle&3,'negate_both':bool(words[1]&(1<<24))}
                need(fields['binary_scalar']=={'constant':255,'constant_component':0,'temporary':0,
                    'temporary_component':3,'negate_both':True},'Changed SUB_CONST operands')
        rows.append({'slot':slot,'va':f'{VA+3688+slot*12:08X}','words':[f'{x:08X}' for x in words],
                     'serialized':serial,'fields':fields})
    return {'image_sha256':screen.IMAGE_SHA256,'record_va':f'{VA:08X}','record_bytes':4396,
        'record_sha256':RECORD_SHA,'code_offset':3688,'code_bytes':708,'code_sha256':CODE_SHA,
        'literal_c252_to_c255':[f'{x:08X}' for x in LITERALS],
        'control_flow':[[f'{a:08X}',f'{b:04X}'] for a,b in CF],
        'flow':'Slot11 sets p0=(c40.x!=0). CF1 jumps to CF6 when p0 is false, skipping slots12..31. No loop.',
        'dataflow':{'7..10':'Four vertex fetch payloads; original declaration patch/format mapping remains separate.',
            '12..31':'Four-weight bone matrix blend (W,Z,Y,X accumulation), then transform the source position.',
            '32..42':'Retain Y if Y>=1000, otherwise cap Y at60. Finite homogeneous position W=1.',
            '32..54':'Compose g_ViewProjection(c0..3) with g_World(c12..15), then transform the adjusted position.',
            '55..57':'Export clip position, source UV.xy and the same clip position as interpolator1.'},
        'qualified_domain':'Finite decoded attributes/constants; integer bone indices0..63; actual native VS stream-output tests required.',
        'remaining':'Original mesh/declaration association, constant uploads, native viewport/depth adaptation, shadow draw and full menu remain unqualified.',
        'instructions':rows}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');parser.add_argument('--verify',action='store_true')
    parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    need(not(args.write and args.verify),'Choose write or verify')
    image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.self_test:
        original=image[VA-screen.BASE+3688:VA-screen.BASE+4396]
        for offset in (*range(84),*range(696,708)):
            changed=bytearray(original);changed[offset]^=1
            try:schedule(changed)
            except ValueError:continue
            raise ValueError('Changed shadows schedule/trailer accepted')
        print('PASS 96 original control-flow/trailer mutation checks')
    if args.write:REPORT.write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    elif args.verify:need(json.loads(REPORT.read_text(encoding="utf-8"))==report,'Saved shadows evidence differs')
    else:print(json.dumps(report,indent=2))
    print('PASS exact RenderShadowDepth record, 51 static slots, conditional skinning and original literals')
if __name__=='__main__':main()
