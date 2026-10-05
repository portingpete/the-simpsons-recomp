"""Offline proof of the exact original zprepass VS, constants and CPU boundaries.

No arguments prints deterministic JSON. --verify suppresses JSON; --self-test
adds record, control-flow, mapping and HLSL mutation checks. No output files,
builds, GPU commands or instruction execution. Native GPU tests are separate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

sys.dont_write_bytecode = True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four

ROOT = Path(__file__).resolve().parents[1]
need = screen.require
FX_VA, FX_BYTES = 0x821490E0, 0x5330
FX_SHA = '5468a2f63b578bf497431aeede9522730b6a46b10aa65f1e77970289ce264c6b'
RECORDS = (
    (0x8214A8A4, '529fd099ed29b895d820c0e5d652d7579ad84d344eacff052b76ca4b4375ca6b', 0x4E4A0001),
    (0x8214B9B8, 'acad3597471f274125ba3337dca966d4b14105b7a05594794ad94c09158a275b', 0x4E4A0000),
)
EXEC_SHA = 'b05ca8f1b48c659d0fefa13ecd414f4c36538e922edac30100c68ac3f5df4c1b'
SOURCE_SHA = '8e4dc752754b38095e9665966d09eb8d70e62171d022919b82f332014b73eaae'
LITERALS = (0,0,0,0,0,0,0,0,0,0,0,0,0,0x3F800000,0x3F000000,0x40400000)
FETCH_METADATA = (0x00100007,0x00001008,0x00002009,0x0001000A,0x0002000B,
                  0x0003000C,0x0004000D,0x0005000E,0x0036000F)
CF = ((0xF5556007,0x1203),(0x7095400D,0x1200),(0xA,0xB000),(0x2011,0x1000),
      (0x4006,0xB000),(0x6013,0x1200),(0x6019,0x1200),(0x601F,0x1200),
      (0x6025,0x1200),(0x202B,0x1200),(0,0xC200),(0x502D,0x1200),
      (0,0xC400),(7,0x2200))
# Every executable bit is pinned independently of the decoded annotations.
RAW = bytes.fromhex('''
F5556007 400D1203 12007095 0000000A 2011B000 10000000
00004006 6013B000 12000000 00006019 601F1200 12000000
00006025 202B1200 12000000 00000000 502DC200 12000000
00000000 0007C400 22000000 05F85000 00000E88 00000000
05F86000 00000688 00000000 05F84000 00000053 00000000
05F87000 00000E88 00000000 05F83000 00000E88 00000000
05F82000 00000E88 00000000 05F81000 00000E88 00000000
05F88000 00000E88 00000000 05F80000 00000E88 00000000
C8000000 00000000 02000000 C8080000 001BC600 0525FF00
70000000 0000001B E2000000 C8070005 00C06CC0 AB072405
C8070003 00C0B1C0 AB032405 C8070002 00C0C6C0 AB022403
C8070001 00C01BC0 AB012402 C8070001 00C06CC0 AB082501
C8070005 00C0B1C0 AB002501 C80F0000 00ACBEB1 4CFF05FF
C80F0004 00001B00 8104FF00 5C000000 0000006C E2000004
C80F0003 A01BFF00 81063400 C80F0002 A01BFF00 81063500
5C0F0001 A01BFFB1 A1063604 C80F0001 A0C6A7F8 AB063601
C80F0002 A0C6A7F8 AB063502 C80F0003 A0C6A7F8 AB063403
5C000000 000000C6 E2000004 C80F0003 A0B1F89E AB063403
C80F0002 A0B1F89E AB063502 C80F0001 A0B1F89E AB063601
5C000000 0000001B E2000004 C80F0001 A06CDD3E AB063601
C80F0002 A06CDD3E AB063502 C80F0003 A06CDD3E AB063403
C8010005 00E30000 CF030000 C8020005 00E30000 CF020000
C8040005 00E30000 CF010000 C80F0000 00ACBEB1 4CFF05FF
C801803E 003E0000 4F000000 C802803E 003E0000 4F010000
C804803E 003E0000 4F020000 C808803E 003E0000 4F030000
''')
# Original CPU spans; semantic arguments are reported separately below.
CPU_SPANS = (
    (0x823C8EB0,0xB4,'822daab351cc61e31a5cea45128e46b0df49894d21324176568e4e2a8d4b482e'),
    (0x82740680,0x56C,'be51c268553e3962c9c7f32fc53a358053de4b9c77cd9d28aed5b27f4b7463b4'),
    (0x8270BF08,0x388,'0afa63d7ae380b0b10d2d49eb7ff99c7b4b606e8ef9cd4f6394bb9d3cc54861f'),
    (0x8273A6C0,0x150,'e7734db3c6d5afb36b9f5b2cb2a10c7d2c82ac7e93d27d586791c4a2d1578a0d'),
    (0x826FF250,0x274,'c1edaaa679f646b67023d566505ffb327fb7a9194a94ed5b48e572da639c2385'),
    (0x826FF4C8,0x100,'e048ac745d1ae3b5a7aaa6eeeb4c375923ba89d813440b00cb61dcb736a0c51b'),
    (0x82700318,0x180,'3e1efbc035c7eebee005704aabf4a72f22a91c925a0efba848de6bf284c0cc0c'),
    (0x826FD060,0xB80,'177ff96ad58067a64e83dd63131da27a78bb537ca374f48988e7aaec939e6be8'),
    (0x8273FB68,0x18C,'056541d2f7489180c29d4b6967c7fc14215af188507e0ba7cc965ec81a386392'),
)
CALLS = (
    (0x827406C4,0x826B6078,'begin W, Z+AC; TechniqueOpaque'),
    (0x827406D4,0x8270BF08,'object, Z, camera; retain original matrix arithmetic'),
    (0x8270C26C,0x82704600,'identity, Z+C0, helper SP+90; shared combined matrix'),
    (0x827407C0,0x8273FB68,'identity, Z+C4, dispatcher SP+60, count2'),
    (0x827407D0,0x823C8EB0,'W, Z+B8, 1; skinned Boolean'),
    (0x827407D8,0x826B3980,'W; first skinned commit before bone update'),
    (0x82740814,0x823C8EB0,'W, Z+B8, 0; static Boolean'),
    (0x8274081C,0x826B3980,'W; static commit'),
    (0x826FF340,0x8243C5C0,'ordinary stream0: context,0,G+38,0,G+4,1'),
    (0x826FF498,0x82445798,'context, declaration cache+4 (ordinary branch)'),
    (0x826FF4A4,0x8243C768,'context,G+58'),
    (0x826FF584,0x8244D360,'static draw: context,submesh+C/+10/+14/+18'),
    (0x827003A0,0x826FD060,'identity,Z+BC,computed bone matrices,count<=64'),
    (0x827003B0,0x823C8EB0,'W,Z+B8,1; after bone setter'),
    (0x827003B8,0x82C1DBA0,'identity; actual post-bone SDK commit'),
    (0x82700470,0x8244D360,'skinned draw: context,submesh+C/+10/+14/+18'),
    (0x826FF5B0,0x8243C5C0,'static cleanup: context,1,0,0,0,1'),
)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def word(data, at):
    need(0 <= at <= len(data)-4, 'Truncated original word')
    return struct.unpack_from('>I',data,at)[0]


def control_flow(code):
    need(len(code)==612,'Original zprepass code extent changed')
    fields=[]
    for i in range(7):
        a,b,c=screen.words(code,i*12)
        fields.extend(((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)))
    need(tuple(fields)==CF,'Original zprepass CF changed')
    slots=[]
    for lo,hi in fields:
        if hi>>12 in (1,2):
            start,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(count<=6 and seq>>(count*2)==0,'Invalid original EXEC extent')
            slots.extend((start+i,bool(seq&(1<<(2*i))),bool(seq&(1<<(2*i+1)))) for i in range(count))
    # CF13 is an EMPTY EXEC_END. It does not re-execute slot7.
    need([i for i,_,_ in slots]==list(range(7,50)),'Original issue schedule changed')
    need([i for i,f,_ in slots if f]==list(range(7,16)),'Original nine FETCH slots changed')
    jumps=[]
    for cf_index in (2,4):
        lo,hi=fields[cf_index]
        jumps.append(dict(target=lo&8191,unconditional=bool(lo&8192),
                          predicated=bool(lo&16384),condition=bool(hi&1024),
                          bool_address=(hi>>2)&255,address_mode=(hi>>11)&1))
    need(jumps==[dict(target=10,unconditional=False,predicated=False,condition=False,bool_address=0,address_mode=0),
                 dict(target=6,unconditional=False,predicated=True,condition=False,bool_address=0,address_mode=0)],
         'Boolean/morph jump contract changed')
    return slots,jumps


def program(code,trailer):
    slots,jumps=control_flow(code)
    need(code[:600]==RAW and digest(code[:600])==EXEC_SHA,'Original executable bytes changed')
    need(screen.words(code,600)==(trailer,0x9A2D5105,0x77EBE6DB),'Original trailer changed')
    rows=[]
    dynamic=set(range(28,34))|set(range(35,38))|set(range(39,42))
    for i,fetch,serial in slots:
        raw=screen.words(code,12*i)
        f=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
        if fetch:
            j=i-7
            need(f['destination_register']==(5,6,4,7,3,2,1,8,0)[j] and
                 f['destination_swizzle']==([0,1,2,3] if j==1 else [3,2,1,0] if j==2 else [0,1,2,7]),
                 'FETCH destination/swizzle changed')
            need(f['source_register']==0 and f['source_component']==0 and f['fetch_constant_index']==95 and
                 f['format_field']==f['stride_dwords']==f['offset_field']==0,'Unqualified FETCH patch fields')
        else:
            for flag in ('vector_clamp','scalar_clamp','absolute_constants','vector_destination_relative',
                         'scalar_destination_relative_or_export_zero','predicated','predicate_condition','constant_1_relative'):
                need(not f[flag],'Unqualified ALU modifier '+flag)
            need(f['constant_address_register_relative']==(i in dynamic) and f['constant_0_relative']==(i in dynamic),
                 'Dynamic bone-address flags changed')
            need(not f['scalar_mask'],'Unexpected scalar register write')
            for s in f['sources']:
                need(not any(s[k] for k in ('absolute_temporary','relative_temporary','negated')),'Unqualified source modifier')
        rows.append(dict(slot=i,words=[f'{v:08X}' for v in raw],serialized=serial,fields=f))
    by_slot={r['slot']:r['fields'] for r in rows}
    need((by_slot[17]['vector_opcode'],by_slot[17]['vector_mask'])==(5,8) and
         [(s['bank'],s['register'],s['components']) for s in by_slot[17]['sources'][:2]]==
         [('constant',37,[3,3,3,3]),('constant',255,[2,2,2,2])],'Morph threshold is not c37.w>=0.5')
    need(by_slot[18]['scalar_opcode']==28 and by_slot[18]['sources'][2]['components']==[3,3,3,3],
         'Morph predicate is not PRED_SETNE of the threshold result')
    need([i for i in by_slot if by_slot[i].get('scalar_opcode')==23]==[27,30,34,38],
         'Bone address update order changed')
    need([i for i in by_slot if by_slot[i].get('export')]==[46,47,48,49],
         'Unexpected output/interpolator export')
    for i in range(46,50):
        f=by_slot[i]
        need((f['vector_opcode'],f['vector_destination'],f['vector_mask'])==(15,62,1<<(i-46)),
             'Position DOT4 export changed')
        need([(s['bank'],s['register'],s['components']) for s in f['sources'][:2]]==
             [('constant',i-46,[2,0,1,3]),('temporary',0,[0,1,2,3])],'Position matrix/export operands changed')
    return dict(control_flow=[[f'{a:08X}',f'{b:04X}'] for a,b in CF],jumps=jumps,instructions=rows,
                static_paths={'b0_false':list(range(7,17))+list(range(45,50)),
                              'b0_true_morph_false':list(range(7,19))+list(range(25,50)),
                              'b0_true_morph_true':list(range(7,50))})


def shader_record(data,index):
    va,sha,trailer=RECORDS[index]
    need(len(data)==4364 and digest(data)==sha,'Original zprepass shader record changed')
    need(screen.words(data,4,2)==(3688,676),'Original shader header/literal extent changed')
    need(struct.unpack_from('>16I',data,3688)==LITERALS,'Original literal bank changed')
    need(struct.unpack_from('>9I',data,3652)==FETCH_METADATA,'Original semantic/FETCH metadata changed')
    return program(data[3752:],trailer)


def constant_maps(body):
    need(len(body)==FX_BYTES-12,'Original effect body extent changed')
    need([word(body,a) for a in (0x120,0x124,0x130,0x134,0x138,0x13C)]==[2,1,77,4,4400,112],
         'Original dirty/parameter storage counts changed')
    descriptors=word(body,0x108)
    need((descriptors,word(body,0x128))==(0x390,0x620),'Private descriptor/default offsets changed')
    defaults=body[0x620:0x620+4400]
    need(digest(defaults)=='1a924d04999f10ef5527958b03a3ad0385c4367481828b54986144fa7d5c9134',
         'Original private defaults changed (they are not all zero)')
    for i,a,b in ((12,8,0x10010),(13,0xA,0x80003),(14,0x610,0x40011),(15,0x610,0x40012),
                  (16,0x102,0x3000041)):
        need((word(body,descriptors+8*i),word(body,descriptors+8*i+4))==(a,b),'Zprepass descriptor changed')
    for bone in range(64):
        need((word(body,descriptors+8*(17+bone)),word(body,descriptors+8*(17+bone)+4))==
             (0x5B0,0xC0013+4*bone),'Zprepass bone storage/transpose descriptor changed')
    shared_descriptors=word(body,word(body,0x10C))
    need((word(body,shared_descriptors+8),word(body,shared_descriptors+12))==(0x4007B0,0x100000),
         'Shared g_ViewProjection storage descriptor changed')
    for at,vs_entry,end in ((0x45C0,0x17B0,0x4BF0),(0x4BF0,0x28C4,0x5220)):
        need((word(body,at+0x48),word(body,at+0x4C),word(body,at+0x50))==(vs_entry,0x39D8,end),
             'Technique shader association or context extent changed')
        need(body[0x39D8:0x39E0]==bytes(8),'Original null PS sentinel changed')
        for space,leaves,mask_bytes in ((0,77,16),(1,4,8)):
            for cat in range(8):
                active=([0] if space else list(range(11,77))) if cat==0 else [10] if not space and cat==4 else []
                expected=bytearray(mask_bytes)
                for leaf in active:expected[leaf//8]|=0x80>>(leaf&7)
                off=word(body,at+space*32+4*cat)
                need(body[off:off+mask_bytes]==expected,'Original constant category mask changed')
            rows=word(body,at+0x40+4*space)
            for leaf in range(leaves):
                expected=(0,0,0,0)
                if space and leaf==0:expected=(0x40001,0xC00,0,0)
                elif not space and leaf==10:expected=(0x300014,0,0,0)
                elif not space and leaf in (11,12):expected=(((leaf+3)<<18)|(leaf<<1),36+leaf-11,0,0)
                elif not space and leaf>=13:expected=(((leaf+4)<<18)|(leaf<<1),0x800+52+3*(leaf-13),0,0)
                need(tuple(word(body,rows+16*leaf+4*i) for i in range(4))==expected,'Original constant register mapping changed')
    return {'shared':{'g_ViewProjection':'handle00040001, pool slot0..3 -> c0..3'},
            'private':{'kIsSkinned':'handle00300014, slot16.x stores float0/1, leaf10 -> Boolean b0 (category4)',
                       'kBlendWeights':'container00340016; leaves11/12, slots17/18 -> c36/c37',
                       'kBoneMatrices':'container0040001A; leaves13..76, slot19+4*i -> c52+3*i, three full vectors'},
            'unmapped':'g_World has no upload in this pass; c0..3 already hold the combined per-object matrix',
            'counts':{'private_leaves':77,'shared_leaves':4,'private_bytes':4400,'source_shared_bytes':112},
            'defaults':{'body_offset':'620','bytes':4400,'sha256':digest(defaults),
                        'note':'Copy original bytes; Boolean occupies slot16.x, not its default-one W lane.'}}


def effect_record(data):
    need(len(data)==FX_BYTES and digest(data)==FX_SHA,'Original zprepass effect changed')
    body=data[12:]
    need(tuple(word(body,0x1780+4*i) for i in range(5))==(0x3B63,0,0x45C0,0x3A00,0x3A30) and
         tuple(word(body,0x1794+4*i) for i in range(5))==(0x3B7D,0,0x4BF0,0x3A00,0x3A30),
         'Original pass association changed')
    need(body[0x3A00:0x3A30]==bytes.fromhex('02000000'+'00000000'*5+'000000010000003800000002'+'00000000'*3),
         'Original pass literal cull=2 changed')
    need(body[0x3A30:0x3A50]==bytes(32),'Original sampler block is not empty')
    return constant_maps(body)


def source_pin(source):
    need(digest(source.encode('utf-8'))==SOURCE_SHA,'Qualified zprepass HLSL source changed')


def source_equivalence(decoded,source):
    """Compare the VS body to a static per-slot transcription, without executing it.

    RAW/CF qualification precedes this check. Only three folds are permitted:
    c255 supplies zero/one/half/three; CND_EQ supplies homogeneous position;
    SETGTE followed by PRED_SETNE becomes the structured morph condition.
    Vector results precede co-issued MOVA, retaining slot30's old address.
    """
    rows={r['slot']:r['fields'] for r in decoded['instructions']}
    def operand(f,i,n=4):
        s=f['sources'][i]
        name=f"r{s['register']}" if s['bank']=='temporary' else f"c[{s['register']}{'+a0' if f['constant_address_register_relative'] else ''}]"
        swizzle=''.join('xyzw'[v] for v in s['components'][:n])
        return name+('' if swizzle=='xyzw' else '.'+swizzle)
    def vector(slot):
        f=rows[slot];mask=f['vector_mask'];n=mask.bit_count()
        destination=f"r{f['vector_destination']}"
        if mask!=15:destination+='.'+''.join('xyzw'[i] for i in range(4) if mask&(1<<i))
        if f['export']:destination='result.position.'+'xyzw'[slot-46]
        op=f['vector_opcode']
        if op==1:expr=operand(f,0,n)+'*'+operand(f,1,n)
        elif op==11:expr='mad('+','.join(operand(f,i,n) for i in range(3))+')'
        elif op==15:expr='dot('+operand(f,0)+','+operand(f,1)+')'
        else:raise ValueError('Unqualified static transcription opcode')
        return destination+'='+expr+';'
    # The native input rows follow the nine original semantic-matched FETCHes.
    initial=[]
    for i in range(9):
        f=rows[7+i];name='r'+str(f['destination_register'])
        value=f'input.source{i}'
        if i==2:value+='.wzyx'
        elif i!=1:value='float4('+value+',0)'
        initial.append(name+'='+value)
    expected=['precise float4 '+','.join(initial[a:b])+';' for a,b in ((0,3),(3,5),(5,7),(7,9))]
    expected+=['[branch]if(zprepassBooleans.x!=0){','[branch]if(c[37].w>=0.5){']
    expected.extend(vector(i) for i in range(19,25))
    expected+=['}','r0=float4(r5.z,r5.x,r5.y,1);','r4=r4*3;']
    for i in range(27,45):
        f=rows[i]
        if f['vector_mask']:expected.append(vector(i))
        if f['scalar_opcode']==23:
            component=operand(f,2,1)
            expected.append(('int ' if i==27 else '')+'a0=int(clamp(floor('+component+'+0.5),-256.0,255.0));')
    expected+=['}','r0=float4(r5.z,r5.x,r5.y,1);','ZPrepassOutput result;']
    expected.extend(vector(i) for i in range(46,50))
    expected.append('return result;')
    clean=re.sub(r'//[^\n]*','',source)
    body=re.search(r'ZPrepassOutput VSZPrepass\(ZPrepassInput input\)\s*\{(.*?)^\}',clean,re.S|re.M)
    need(body is not None,'Missing authored zprepass VS body')
    compact=lambda value:re.sub(r'\s+','',value)
    need(compact(body.group(1))==compact(''.join(expected)),
         'HLSL body differs from the original per-slot static transcription')
    return 'Full VS body equals static decoded FETCH/ALU/control-flow transcription; no execution.'


def inspect(image):
    need(len(image)==screen.IMAGE_SIZE and digest(image)==screen.IMAGE_SHA256,'Original image identity changed')
    for path,sha in ((four.UCODE,four.UCODE_SHA),(four.XENOS,four.XENOS_SHA)):
        need(digest(path.read_bytes())==sha,'Pinned static UCODE/enum reference changed')
    source=(ROOT/'renderer/zprepass_shader.hlsl').read_text(encoding='utf-8')
    source_pin(source)
    take=lambda va,n:image[va-screen.BASE:va-screen.BASE+n]
    constants=effect_record(take(FX_VA,FX_BYTES))
    shaders=[shader_record(take(va,4364),i) for i,(va,_,_) in enumerate(RECORDS)]
    source_proof=source_equivalence(shaders[0],source)
    a,b=(take(va,4364) for va,_,_ in RECORDS)
    need(a[:4352]==b[:4352] and a[4356:]==b[4356:],'VS pair differs outside first trailer word')
    for va,n,sha in CPU_SPANS:
        need(digest(take(va,n))==sha,f'Original CPU span {va:08X} changed')
    calls=[]
    for va,target,args in CALLS:
        raw=word(take(va,4),0);displacement=raw&0x03FFFFFC
        if displacement&0x02000000:displacement-=0x04000000
        need(raw>>26==18 and raw&3==1 and va+displacement==target,'Original narrow BL changed')
        calls.append(dict(site=f'{va:08X}',resume=f'{va+4:08X}',target=f'{target:08X}',arguments=args))
    return {'image_sha256':screen.IMAGE_SHA256,'effect':{'va':f'{FX_VA:08X}','sha256':FX_SHA,
            'selected_technique':'TechniqueOpaque, handle0003FFFC, Z+AC','pixel_shader':None,
            'literal_states':'SDK38(Cull)=2 only; no sampler rows; remaining effective state is inherited'},
            'shader_records':[{'va':f'{va:08X}','bytes':4364,'sha256':sha,'literal_offset':3688,
                               'code_offset':3752,'code_bytes':612,'executable_bytes':600} for va,sha,_ in RECORDS],
            'executable_sha256':EXEC_SHA,'literal_c252_to_c255':[f'{v:08X}' for v in LITERALS],
            'fetch_semantics':[{'slot':7+i,'usage':(v>>12)&15,'index':(v>>16)&15,
                                'native_semantic':f'TEXCOORD{i}'} for i,v in enumerate(FETCH_METADATA)],
            'program':shaders[0],'constants':constants,'calls':calls,
            'native':{'source':'renderer/zprepass_shader.hlsl','source_sha256_lf':SOURCE_SHA,
                      'source_proof':source_proof,
                      'entry':'VSZPrepass','profile':'vs_5_0','probe':'GSZPrepassProbe','probe_profile':'gs_5_0',
                      'float_buffer':'native b0: float4 c[244], 3904 bytes',
                      'boolean_buffer':'native b1: uint4, normalized original Boolean b0 in x, 16 bytes',
                      'outputs':'SV_Position only; no UV, PS, texture, alpha test or shadow-height clamp'},
            'scope':['Static original-byte and authored-source qualification, not native GPU execution.',
                     'Finite attributes/constants and integer bone indices0..63; no weight repair.',
                     'Absent skin/morph inputs are inert only when original Boolean b0=false.',
                     'Original floating-point/raster precision is not claimed identical to native D3D11.']}


def self_test(image):
    count=0
    def rejects(function,*args):
        nonlocal count
        try:function(*args)
        except (ValueError,struct.error):count+=1;return
        raise ValueError('Verifier accepted a mutated original contract')
    for i,(va,_,trailer) in enumerate(RECORDS):
        data=image[va-screen.BASE:va-screen.BASE+4364]
        for at in range(len(data)):
            bad=bytearray(data);bad[at]^=1;rejects(shader_record,bad,i)
        rejects(shader_record,data[:-1],i);rejects(shader_record,data+b'\0',i)
        code=data[3752:]
        for at in range(len(code)):
            for bit in range(8):
                bad=bytearray(code);bad[at]^=1<<bit;rejects(program,bad,trailer)
    code=image[RECORDS[0][0]-screen.BASE+3752:RECORDS[0][0]-screen.BASE+4364]
    for at in range(84):
        bad=bytearray(code);bad[at]^=1;rejects(control_flow,bad)
    body=image[FX_VA-screen.BASE+12:FX_VA-screen.BASE+FX_BYTES]
    for at in (0x45C0,0x4BF0):
        for space,n,mask_bytes in ((0,77,16),(1,4,8)):
            for cat in range(8):
                mask=word(body,at+32*space+4*cat)
                for i in range(mask_bytes):
                    bad=bytearray(body);bad[mask+i]^=1;rejects(constant_maps,bad)
            rows=word(body,at+0x40+4*space)
            for i in range(n*16):
                bad=bytearray(body);bad[rows+i]^=1;rejects(constant_maps,bad)
    source=(ROOT/'renderer/zprepass_shader.hlsl').read_text(encoding='utf-8')
    decoded=program(code,RECORDS[0][2])
    for old,new in (('register(b1)','register(b0)'),('zprepassBooleans.x!=0','c[40].x!=0'),
                    ('c[37].w>=0.5','c[37].w>0.5'),('c[37].w>=0.5','c[37].z>=0.5'),
                    ('input.source2.wzyx','input.source2'),('r6.wwww','r6.xxxx'),
                    ('c[54+a0].wxyz','c[54+a0].xyzw'),('r1.xwyz','r1.xyzw'),
                    ('r0=float4(r5.z,r5.x,r5.y,1)','r0=float4(r5.xyz,1)'),
                    ('c[0].zxyw','c[0].xyzw'),('c[36].www','c[37].zzz'),
                    ('float4 c[244]','float4 c[243]')):
        need(old in source,'Stale shader source mutation fixture');rejects(source_pin,source.replace(old,new))
        # Body changes must also fail independently of the whole-source digest.
        if old not in ('register(b1)','float4 c[244]'):
            rejects(source_equivalence,decoded,source.replace(old,new))
    print(f'PASS {count} original record/code/CF/mapping/source mutation checks')
    return count


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify',action='store_true',help='Suppress JSON; write no files')
    parser.add_argument('--self-test',action='store_true',help='Reject mutated original/source contracts')
    args=parser.parse_args()
    image=(ROOT/'analysis/simpsons.pe').read_bytes();report=inspect(image)
    if args.self_test:self_test(image)
    if not args.verify:print(json.dumps(report,indent=2))
    print('PASS exact zprepass VS pair/null PS, Boolean/morph branches, constants, CPU sites and HLSL source')


if __name__=='__main__':
    main()
