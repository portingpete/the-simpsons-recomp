"""Static original-byte field/effect proof for simpsons_edgeAA.

No instruction execution or runtime translation. The checked native shader is
a fixed transcription; the independent pixel fixture uses color-domain math.
"""
from pathlib import Path
import argparse,hashlib,json,struct
import analyze_edge_shaders as edge
import analyze_screen_shaders as screen
import analyze_fourtap_shaders as four
ROOT=Path(__file__).resolve().parents[1]
REPORT=ROOT/'analysis/native-edgeaa-shaders.json'
need=screen.require
PARSER=Path('K:/Simpsons/RexGlueCurrent/src/graphics/pipeline/shader/translator.cpp')
PARSER_SHA='448576e802b52019481f58168bc82f6192b426493870491a4db7e394410aac10'
PROFILES=[('VS',0x820347B0,320,224,3,'f9c7b041b09d31e0ba7e11cb4c432ae679f2facf730840f9e8e35325eb9456fe'),
          ('PS',0x82034900,2688,1224,12,'66a97e173078b799410a9a063daca781c426efbc2c0ac8dd9e5b509f5167196f')]
CF={'VS':[(0x30052003,0x1200),(0,0xC200),(0x1005,0x1200),(0,0xC400),(0x1006,0x2200),(0,0)],
    'PS':[(0x9600C,0x1200),(0x6012,0x1000),(0x6018,0x5600),(0x601E,0x1200),
          (0x906024,0x1200),(0x540602A,0x1200),(0x26030,0x1000),(0x4006036,0x5600),
          (0x95603C,0x5600),(0x6042,0x1000),(0x6048,0x1000),(0x504E,0x1000),
          (0x10000F,0x7000),(0x245053,0x5600),(0x30000D,0x8000),(0x6058,0x5600),
          (0x100013,0x7000),(0x24605E,0x5600),(0x300011,0x8000),(0x6064,0x5600),
          (0x606A,0x1000),(0x5070,0x1000),(0,0xC400),(0x4075,0x2200)]}
SPECIAL={19:('sub',27,0,0,2,True),24:('mul',254,0,2,1,False),
         26:('sub',254,3,0,2,False),30:('sub',254,3,0,2,False),
         36:('mul',26,0,0,2,False),41:('mul',21,0,1,1,False),
         50:('sub',254,3,1,0,False),67:('mul',20,0,0,2,False),
         85:('add',254,3,1,0,False),99:('add',254,3,0,2,False)}

def scalar_fields(a,b,c):
    d=edge.alu(a,b,c);op=d['scalar_opcode'];s=d['sources'][2]
    if 42<=op<=47:
        # The split encoding's src3_sel and middle swizzle bits encode the
        # TEMP INDEX, not an absolute/relative source flag or four-lane swizzle.
        return {'operation':{42:'mul',44:'add',46:'sub'}[op&~1],
            'constant':c&255,'constant_component':(((b&255)>>6)+3)&3,
            'temporary':(op&1)|(((c>>29)&1)<<1)|(b&60),'temporary_component':b&3,
            'negate_both':bool(b&(1<<24)),'absolute_both':d['absolute_constants']}
    return {'operation_code':op,'source':s,'scalar_component':s['components'][3],
            'second_component_for_two_component_ops':s['components'][0],
            'updates_previous_scalar':op!=50}

def schedule(code,name,pairs):
    cf=[];slots=[]
    for pair in range(pairs):
        a,b,c=screen.words(code,pair*12)
        cf.extend([(a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)])
    need(cf==CF[name],'Changed edgeAA control flow')
    for i,(lo,hi) in enumerate(cf):
        if hi>>12 in (1,2,5):
            at,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(0<count<=6 and seq>>(count*2)==0,'Malformed edgeAA issue extent')
            slots.extend((at+j,bool(seq&(1<<(2*j))),bool(seq&(2<<(2*j))),i) for j in range(count))
    need([s[0] for s in slots]==list(range(pairs,len(code)//12-1)),'Unaccounted edgeAA issue slots')
    need(screen.words(code,len(code)-12)==(0x4E4A0001 if name=='VS' else 0x4E4A0000,0x832F20EB,0x9AA0E809),
         'Changed edgeAA shader trailer')
    return cf,slots

def inspect(image):
    need(hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,'Changed original image')
    for p,d in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA),(PARSER,PARSER_SHA)):
        need(hashlib.sha256(p.read_bytes()).hexdigest()==d,'Changed field reference')
    shaders=[]
    for name,va,total,offset,pairs,digest in PROFILES:
        record=image[va-screen.BASE:va-screen.BASE+total]
        need(hashlib.sha256(record).hexdigest()==digest,'Changed edgeAA shader record')
        code=record[offset:];cf,slots=schedule(code,name,pairs);instructions=[]
        for i,fetch,serial,block in slots:
            a,b,c=screen.words(code,i*12)
            if fetch:
                fields=screen.decode_fetch((a,b&0x7FFFFFFF,c&0x7FFFFFFF))
                fields.update(predicated=bool(b>>31),predicate_condition=bool(c>>31))
            else:
                fields=edge.alu(a,b,c);fields['scalar_operands']=scalar_fields(a,b,c)
                if name=='PS' and i in SPECIAL:
                    s=fields['scalar_operands'];actual=tuple(s[k] for k in ('operation','constant','constant_component','temporary','temporary_component','negate_both'))
                    need(actual==SPECIAL[i] and not s['absolute_both'],'Changed special scalar encoding')
            instructions.append({'slot':i,'control_flow':block,'serialize':serial,
                'words':[f'{v:08X}' for v in (a,b,c)],'fields':fields})
        shaders.append({'stage':name,'address':f'{va:08X}','bytes':total,'sha256':digest,
            'executable_offset':offset,'executable_bytes':len(code),'executable_sha256':hashlib.sha256(code).hexdigest(),
            'control_flow':[[f'{lo:08X}',f'{hi:04X}'] for lo,hi in cf],'instructions':instructions})
    body=image[0x34014:0x34014+9268]
    need(hashlib.sha256(body).hexdigest()=='d8e15a8b641d4a0765209c671ff643ccc69683effe013f414712f1f537d53075','Changed edgeAA effect body')
    bindings=[]
    expected={9:(0x2C0012,0,0,0),10:(0x300014,0,0x400000,0),12:(0x380018,0,50,0),
        13:(0x3C001A,0,49,0),14:(0x40001C,0,48,0),23:(0x64002E,0,0x800000,0),
        24:(0x680030,0,0xC00000,0),25:(0x6C0032,0,0x1000000,0)}
    expected.update({i:(0x44001E+(i-15)*0x40002,0,20+i-15,0) for i in range(15,23)})
    for i in range(26):
        row=struct.unpack_from('>4I',body,8512+i*16)
        need(row==expected.get(i,(0,0,0,0)),'Changed edgeAA parameter binding')
        if any(row):bindings.append([f'{v:08X}' for v in row])
    tail=struct.unpack_from('>4I',body,8992)
    need(tail==(10,0,1,0),'Changed authored integer constant vector')
    ps=image[0x34900:0x34900+2688]
    need(struct.unpack_from('>4I',ps,0x2C8)==tail,'Effect/shader integer defaults differ')
    literals=struct.unpack_from('>32I',ps,1096)
    need(literals==(0,)*12+(0x3CB851EC,0x3E570A3D,0x3C800000,0,0,0x3F733333,0x3D800000,0x3F000000,
        0xBF000000,0x3E800000,0x3DAE147B,0x3D4CCCCD,0x42000000,0x40A00000,0x3D000000,0x3F800000,
        0x3EEB851F,0x3F666666,0x3F19999A,0x3E000000),'Changed edgeAA literal prefix')
    return {'effect':'simpsons_edgeAA / 82034008','image_sha256':screen.IMAGE_SHA256,
        'references':{'ucode':four.UCODE_SHA,'xenos':four.XENOS_SHA,'scalar_operand_parser_only':PARSER_SHA},
        'shaders':shaders,'bindings':bindings,'literal_c248_255':[f'{v:08X}' for v in literals],
        'loops':{'hardware_integer':16,'authored_vector':list(tail),'body_cf':[13,17],'exit_cf':[15,19],
            'predicated_break_enabled':True,'break_predicate_condition':False,'addressed_operands':False,'native_bound':10,
            'qualification':'Default vector only; changing integer constant application remains unqualified.'},
        'math':{'initial':'Decode base B/A flags, rim and cast-shadow factors, direct/palette base RGB.',
            'depth':'Point sample depth at center and four BlurWidth-offset neighbors; fade=saturate(max*DepthFadeControl).',
            'no_edge':'No outline attenuation; alpha factor1.',
            'low_depth':'Attenuate RGBA by 1-0.5*EdgeColorScale*colorTexture*fade.',
            'filtered':'Two ten-sample line sums divided by NSamples, multiplied by0.5*EdgeColorScale*fade.',
            'solid_line':'Blend base RGB toward1-lineRGB, alpha factor1-fade*lineAlpha.',
            'final':'Multiply RGB and alpha by rim-shadow and cast-shadow factors, then add rim-light to all four lanes.'},
        'limits':['Finite native point sampling, single mip, original NSamples=10.',
            'Native RGB10A2 samples recover round(sample*1023/3) codes before canonical normalization; this avoids device-specific palette-index changes.',
            'KernelWidth is unused by this shader; it remains original CPU data.',
            'No runtime admission, frame, presentation or main-menu completion follows from shader evidence.',
            'Console reciprocal/fused arithmetic, filtering, rasterization and output rounding parity remain unproven.']}

def self_test(image):
    checks=0
    def reject(f):
        nonlocal checks
        try:f()
        except ValueError:checks+=1;return
        raise ValueError('Changed edgeAA evidence accepted')
    for name,va,total,offset,pairs,_ in PROFILES:
        code=image[va-screen.BASE+offset:va-screen.BASE+total]
        for at in range(12*pairs):
            changed=bytearray(code);changed[at]^=1;reject(lambda:schedule(changed,name,pairs))
        changed=bytearray(code);changed[-1]^=1;reject(lambda:schedule(changed,name,pairs))
    for at in (0x34014,0x347B0,0x34900,0x34900+1096,0x34014+8512,0x34014+8992):
        changed=bytearray(image);changed[at]^=1;reject(lambda:inspect(changed))
    return checks

def main():
    p=argparse.ArgumentParser();p.add_argument('--write',action='store_true');p.add_argument('--verify',action='store_true');p.add_argument('--self-test',action='store_true');args=p.parse_args()
    need(not(args.write and args.verify),'Choose write or verify')
    image=(ROOT/'analysis/simpsons.pe').read_bytes();result=inspect(image)
    if args.self_test:print(f'PASS {self_test(image)} edgeAA identity/control-flow mutation checks')
    if args.write:REPORT.write_text(json.dumps(result,indent=2)+'\n', encoding="utf-8")
    elif args.verify:need(json.loads(REPORT.read_text(encoding="utf-8"))==result,'Saved edgeAA evidence differs')
    else:print(json.dumps(result,indent=2))
    print('PASS exact edgeAA records, 113 issue slots, five texture roles, two loops, split scalar operands')
if __name__=='__main__':main()
