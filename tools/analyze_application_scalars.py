"""Pinned first-initialization scalar registration and remaining setter evidence.

Only finite static constant propagation within the reviewed initializer slice;
no guest execution, SDK object, native renderer or command-stream interpreter.
Only analysis/native-application-scalars.json may be written.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT/'tools/analyze_poststart_integration.py'
if hashlib.sha256(SUPPORT.read_bytes()).hexdigest() != 'fdcb3c06ae15041bcbf98ce59d07272e51dce345c7332b44475844d20251b434':
    raise RuntimeError('Frozen original-byte validation dependency changed')
from analyze_poststart_integration import (BASE, IMAGE_SHA, branch, checked_decode,
                                          hx, layout, sha, span, validate_identity, word)

REPORT = ROOT/'analysis/native-application-scalars.json'
FUNCTIONS = {
    0x82724840:(0x1008,'002851f49043ac5e0f73ceaaf2cba546491a1eb7536c632e89f09e7962778854'),
    0x827246C8:(0x60,'5177a92624f9537f1046b5b99d248850c872e00c5e6b038ce25755a024efde8a'),
    0x82723D80:(0xD8,'1dde9f32b3935e160466fcc6564e6eadf1f8002e881c9ee5190c04d4b792d788'),
    0x82466800:(0x268,'b11aac256fe4eb8d493f9f4249acec09cc7e132fb4271af4ff8d00ff21cef4e2'),
    0x8243C430:(0xFC,'f80def68036d41538781f8c1475743fb39bd0fb61b272348bbe7d8fcd3af3a51'),
    0x82439C20:(0x2E0,'a06b2d898f3f60715af1950f71e6c6595a96a0cc26a5256802e646f08c581991'),
}
# These are reviewed multi-leaf ranges, not invented .pdata function extents.
CODE_RANGES = {
    0x8243AA48:(0x858,'e6f212a702c9c21d2d153d105926ac5fc4481b649fd69b15280fd379f894d3b6'),
    0x8243B260:(0x50,'f277e944cb9165ec0b563435a0925ab0c507c02a58b9999442934bc5669e9084'),
    0x8243B670:(0x110,'d1f6d9b7c517ff489019b9f6d8794b275676fb25cd3bf1566290dd4a3cbbff82'),
    0x8243B780:(0x28,'3e33cfd7f268c4a6535ee9cdbd8e3e73b907c8ee9ad29f4ef5835be2f1a22b6c'),
    0x8243B810:(0xC0,'61ba1fbf01e90db27589b29869b23f75159f9103051f8b3137ab75ad416a748f'),
    0x8243B960:(0xDC,'b22bbe9d8894e3f1aa81e6326f8dad2eff76a9a82baf1bd3cb1a973e5ba317f9'),
    0x8243D0E8:(0x10,'9b663bbd49669378f853d3a27d8407af12e6f3542c026f636f9b263291afe5bc'),
}
DATA = {
    0x82150580:(0x15C,'ecfab459f5d48b57db9a9ccc69c0ab2559ba81ae128a0e3590124b0bc0bc3e62'),
    0x82CD28B8:(0x4BC,'23df15b8f16d188785cd769d1f4de7ef30aced420ba77cfdd2f6434fc79742c3'),
    0x8214EF50:(0x8C0,'95c0b598e5cadf7914a5d28b3f24838264b06edb75c38928d1e334251fc84082'),
}
REFERENCES = {
    'registers.h':'2ccf7732db706133acfec34f7d70659e3d12b0533bc51e8b95ca8137cc4d1c40',
    'xenos.h':'7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227',
}
WORDS = {
    0x82724848:0x3E4082D7,0x8272484C:0x39000000,0x82724868:0x7D184378,
    0x82724884:0x392BD498,0x827248B0:0x38EBD648,
    0x82724B60:0x2B1B0000,0x82724B7C:0x409A0018,
    0x82724B90:0x48000008,0x82724B94:0x80BFD7F0,
    0x827246F8:0x38C00001,0x82725524:0x913AFFFC,
    0x8243B964:0x508B1F38,0x8243B994:0x508B177A,0x8243B9C4:0x508B2EB4,
    0x8243B9F0:0x98832942,0x8243BA10:0x90833504,0x8243BA24:0x508BB890,
    0x8243AA98:0x80632E48,0x8243AA9C:0x4E800020,
    0x8243B984:0x5563EFFE,0x8243B9B4:0x5563F7FE,0x8243B9E4:0x5563DFFE,
    0x8243BA08:0x88632942,0x8243BA18:0x80633504,0x8243BA34:0x55634E7E,
    0x821DD220:0x41800000,0x821DD23C:0x41000000,
}
CALLS = {0x82724708:(0x82723D80,True),0x8243D0F4:(0x8243C430,False),
         0x8243C514:(0x82439C20,True),0x82439C50:(0x82457CC8,True)}
SEQUENCE = (
    6,11,14,68,9,10,12,13,15,16,17,44,1,2,3,42,43,5,7,8,75,76,69,70,71,
    45,46,47,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,
    81,82,83,67,41,72,77,78,4,34,39,40,35,36,37,38,48,49,50,85,
    51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,73,74)
TAIL_DEFAULTS = (0,0,0,1,0,1,0x3F800000,0x3F800000,0,0,1,0xFFFF,
                 0x3F800000,0x3F800000,0,0x3F800000,0,0x3F800000,0x3F800000,1,
                 *([0]*16),1,0xFFFF)
UNREGISTERED = (0x4F,0x50,0x54,0x56)
BUILD073_IDS = set(range(0x28,0xAC,4)) | {0xCC,0xD0,0xD4,0xD8,0xDC,0xE0,
                                          0x134,0x138,0x13C,0x140,0x150,0x154}

# Raw setter observations, not native implementation or permission to render.
BEHAVIOR = {
    0x168:('HISTENCILENABLE','D+2940 bit3 = raw low1; dirty64 D+10 |= 100','exact 0 initially; canonical 0/1 does not prove enabled hi-stencil'),
    0x16C:('HISTENCILWRITEENABLE','D+2940 bit2 = raw low1; dirty64 D+10 |= 100','exact 0 initially; no enabled native claim'),
    0x170:('HISTENCILFUNC','D+2940 bit5 = raw low1; dirty64 D+10 |= 100','exact 0 initially; one bit, not ordinary comparison enum'),
    0x174:(None,'byte D+2942 = raw low8; dirty64 D+10 |= 100','unregistered selector: reject; semantic name unresolved'),
    0x178:('PRESENTINTERVAL','D+3504 = full raw word; no dirty-mask store or call','retain exact startup 1; native presentation consumer still needs policy'),
    0x17C:(None,'D+2E44 bits23..29 = raw low7; no dirty-mask store or call','unregistered selector: reject; semantic name unresolved'),
    0x160:(None,'float transfer to D+29D0; dirty64 D+20 bit31','unregistered; no application name inferred'),
    0x164:(None,'float transfer to D+29C8; dirty64 D+20 bit33','unregistered; no application name inferred'),
    0x130:('VIEWPORTENABLE','D+294C=raw?43F:400; D+2944 bit16=(raw==0); dirty64 D+10 |= A0','canonical 0/1; startup 1; active viewport/clip policy needs draw implementation'),
    0xC8:('SCISSORTESTENABLE','D+2E48=raw; tail 8243C430(D,D+317C) recomputes bounds and calls console emission 82439C20','startup 0 still recomputes viewport bounds; native replacement must not enter SDK helper'),
    0x144:('HALFPIXELOFFSET','D+29C0 bit0=raw low1; dirty64 D+20 bit35','canonical 0/1; startup 1; no arbitrary extra vertex shift'),
    0x158:('GUARDBAND_X','float transfer to D+29CC; dirty64 D+20 bit32','startup exact 3F800000; generalized clipping not proved'),
    0x15C:('GUARDBAND_Y','float transfer to D+29C4; dirty64 D+20 bit34','startup exact 3F800000; generalized clipping not proved'),
    0xAC:('CLIPPLANEENABLE','D+28B4=raw?1000:0; D+2944=(old&~3F)|raw unmasked; dirty64 D+10 |= 80|(1<<44)','canonical 0..3F; startup-only 0; user clip planes unsupported'),
    0xC0:('MULTISAMPLEANTIALIAS','D+2948 bit15=raw low1; dirty64 D+10 |= 40','canonical 0/1; startup 1 is not proof of MSAA target or coverage support'),
    0xC4:('MULTISAMPLEMASK','D+2A00=raw low16; dirty64 D+20 |= 80000','retain raw request separately; FFFF and SDK FFFFFFFF normalize identically'),
    0xB0:('POINTSIZE','float retained D+2E6C; trunc(f32(value*8)) low16 to D+2964 and D+2966; dirty64 D+18 bit54','startup exact 3F800000; arbitrary conversion/point draws unproved'),
    0xB4:('POINTSIZE_MIN','float retained D+2E70; trunc(f32(value*16)) low16 to D+296A; dirty64 D+18 bit53','startup exact 3F800000; preserve BE halfword position'),
    0xB8:('POINTSPRITEENABLE','D+2E64=full raw; no dirty-mask store or call','startup-only 0; later consumer may use it'),
    0xBC:('POINTSIZE_MAX','float retained D+2E74; trunc(f32(value*16)) low16 to D+2968; dirty64 D+18 bit53','startup exact 3F800000; SDK default is 42800000'),
    0xE4:('TESSELLATIONMODE','D+2978 low2=raw low2; dirty64 D+18 bit49','startup 0 is discrete mode, NOT tessellation disabled; gate tessellated draws'),
    0xE8:('MINTESSELLATIONLEVEL','float transfer to D+2980; dirty64 D+18 bit47','startup exact 3F800000; no tessellated draw support'),
    0xEC:('MAXTESSELLATIONLEVEL','float transfer to D+297C; dirty64 D+18 bit48','startup exact 3F800000; no tessellated draw support'),
    0x148:('PRIMITIVERESETENABLE','D+2948 bit21=raw low1; dirty64 D+10 |= 40','canonical 0/1; startup 1; indexed restart must be implemented or guarded'),
    0x14C:('PRIMITIVERESETINDEX','D+28D8=full raw word; dirty64 D+10 bit38','startup FFFF; hardware comparison width/timing not proved'),
}
for i in range(16):
    BEHAVIOR[0xF0+4*i]=(f'WRAP{i}',
        f'Word D+{0x292C+4*(i//8):X}, nibble {i%8}; slots0..6 OR unmasked shifted raw, slot7 inserts low4; dirty64 D+10 |= {0x2000 if i<8 else 0x1000:X}',
        'canonical 0..F; startup-only 0; not sampler address mode; reject unsupported coordinate-wrap draws')

GETTERS = {
    0x168:'([D+2940] >> 3) & 1',0x16C:'([D+2940] >> 2) & 1',
    0x170:'([D+2940] >> 5) & 1',0x174:'unsigned byte [D+2942]',
    0x178:'full word [D+3504]',0x17C:'([D+2E44] >> 23) & 7F',
    0x160:'word [D+29D0]',0x164:'word [D+29C8]',
    0x130:'[D+294C] & 1',0xC8:'full retained raw word [D+2E48]; no rectangle access or command call',
    0x144:'[D+29C0] & 1',0x158:'word [D+29CC]',0x15C:'word [D+29C4]',
    0xAC:'[D+2944] & 3F',0xC0:'([D+2948] >> 15) & 1',0xC4:'word [D+2A00] containing normalized low16',
    0xB0:'retained f32 at D+2E6C, returned as word via stfs/lwz',
    0xB4:'retained f32 at D+2E70, returned as word via stfs/lwz',
    0xB8:'full retained raw word [D+2E64]',
    0xBC:'retained f32 at D+2E74, returned as word via stfs/lwz',
    0xE4:'[D+2978] & 3',0xE8:'retained f32 at D+2980, returned as word via stfs/lwz',
    0xEC:'retained f32 at D+297C, returned as word via stfs/lwz',
    0x148:'([D+2948] >> 21) & 1',0x14C:'full word [D+28D8]',
}
for i in range(16):GETTERS[0xF0+4*i]=f'([D+{0x292C+4*(i//8):X}] >> {4*(i%8)}) & F'


@dataclass(frozen=True)
class Relative:
    """Symbolic object/stack label; never an actual guest address."""
    space: str
    offset: int


def plus(a,b):
    if isinstance(a,Relative) and type(b) is int:
        return Relative(a.space,a.offset+b)
    if isinstance(b,Relative) and type(a) is int:
        return Relative(b.space,b.offset+a)
    if type(a) is not int or type(b) is not int:
        raise ValueError('Invalid static address expression')
    return (a+b)&0xFFFFFFFF


def uint(x):
    if type(x) is not int or not 0<=x<=0xFFFFFFFF:
        raise ValueError('Expected uint32')
    return x


def parse_selectors(data):
    if len(data)!=348: raise ValueError('Expected 87 BE scalar selector words')
    result=struct.unpack('>87I',data)
    if result!=(0,)+tuple(4*(t+9) for t in range(1,87)):
        raise ValueError('Changed scalar selector-to-offset mapping')
    return result


def parse_methods(data):
    if len(data)!=1212 or sha(data)!=DATA[0x82CD28B8][1]:
        raise ValueError('Changed/framing-invalid 101-row SDK scalar method table')
    return tuple(struct.iter_unpack('>III',data))


def ascii_name(b,a):
    data=span(b,a,96); end=data.find(b'\0')
    if end<1 or any(x<32 or x>126 for x in data[:end]):
        raise ValueError('Invalid bounded original name')
    name=data[:end].decode('ascii')
    if not name.startswith('RenderState_'): raise ValueError('Not an original scalar registration name')
    return name


def check_pins(b):
    _,pdata=layout(b)
    for table in (FUNCTIONS,CODE_RANGES,DATA):
        for a,(n,digest) in table.items():
            if sha(span(b,a,n))!=digest: raise ValueError('Changed byte extent '+hx(a))
            if table is FUNCTIONS and (a not in pdata or pdata[a][0]!=n):
                raise ValueError('Changed original .pdata '+hx(a))
    for a,w in WORDS.items():
        if word(b,a)!=w: raise ValueError('Changed reviewed word '+hx(a))
    for a,dest in CALLS.items():
        if branch(a,word(b,a))!=dest: raise ValueError('Changed reviewed call '+hx(a))
    for a in (0x82D6D7E8,0x82D6D7EC,0x82D6D7F0,0x82D6D7F4,0x82D6D7F8):
        if word(b,a)!=0: raise ValueError('First-initialization BSS premise changed')
    parse_selectors(span(b,0x82150580,348)); parse_methods(span(b,0x82CD28B8,1212))
    return pdata


def extract_registrations(b):
    """Audited static assignment slice, only the first-init path of 82724840.

    No instruction fetch loop, arbitrary branch traversal, external memory,
    guest callbacks, floating execution or GPU semantics. Every input code byte
    is pinned before this fixed bounded slice. Unknown transfers reject.
    """
    check_pins(b)
    obj=Relative('object',0); stack=Relative('stack',0)
    # Reviewed definitions from the preamble. Original flag byte starts zero;
    # 82724864 sets r19 bit0 and 82724868 establishes raw positive float zero.
    regs={1:stack,3:obj,7:0x82D6D648,8:0,9:0x82D6D498,
          18:0x82D70000,19:1,24:0}
    memory={a:(word(b,a),None) for a in (0x82D6D7E8,0x82D6D7EC)}
    def store(address,value,pc):
        uint(value)
        if isinstance(address,Relative):
            okay=(address.space=='object' and 0<=address.offset<=0x148 and address.offset%4==0) or \
                 (address.space=='stack' and address.offset in (-160,-156))
        else:
            okay=(0x82D6D300<=address<0x82D6D7F0 and address%4==0) or address in (0x82D6D7F0,0x82D6D7F8)
        if not okay: raise ValueError('Unreviewed static destination '+str(address))
        memory[address]=(value,pc)
    for pc in range(0x827248C8,0x82725528,4):
        # One reviewed one-time 1.0f initializer: flags bit1 was clear on entry.
        # Compare before r27 reuse, not the later value of r27 at the branch.
        if pc==0x82724B60:
            if regs[27]!=0: raise ValueError('Unexpected one-time-default branch')
            continue
        if pc in (0x82724B7C,0x82724B90,0x82724B94): continue
        w=word(b,pc);op=w>>26;rs=(w>>21)&31;ra=(w>>16)&31;rb=(w>>11)&31
        d=w&65535;d=d-65536 if d&32768 else d
        try:
            if op in (14,15):
                regs[rs]=plus(regs[ra] if ra else 0,d if op==14 else d<<16)
            elif op==24:
                regs[ra]=uint(regs[rs])|(w&65535)
            elif op==21:
                shape=((w>>11)&31,(w>>6)&31,(w>>1)&31)
                if shape==(2,0,29): regs[ra]=(uint(regs[rs])<<2)&0xFFFFFFFF
                elif shape==(0,30,30): regs[ra]=uint(regs[rs])&2
                else: raise ValueError('Unreviewed static shift/mask')
            elif op==32:
                regs[rs]=memory[plus(regs[ra],d)][0]
            elif op==36:
                store(plus(regs[ra],d),regs[rs],pc)
            elif op==31 and (w>>1)&1023==151:
                store(plus(regs[ra],regs[rb]),regs[rs],pc)
            else:
                raise ValueError('Unreviewed static transfer')
        except (KeyError,TypeError,ValueError) as exc:
            raise ValueError('Unresolved static assignment at '+hx(pc)+': '+str(exc)) from exc
    if memory[0x82D6D7E8]!=(82,0x82725524): raise ValueError('Final registration count changed')
    methods=parse_methods(span(b,0x82CD28B8,1212))
    records=[]
    for k in range(82):
        a,np=memory[0x82D6D300+4*k]
        t,tp=memory[0x82D6D6A0+4*k]
        default,dp=memory[Relative('object',4*k)]
        category,mp=memory[0x82D6D498+4*t]
        if k!=category or t!=SEQUENCE[k]: raise ValueError('Registration order or category changed')
        sdk_id=4*(t+9);getter,setter,sdk_default=methods[sdk_id//4]
        name=ascii_name(b,a)
        if sdk_id in BEHAVIOR and BEHAVIOR[sdk_id][0] and name!='RenderState_'+BEHAVIOR[sdk_id][0]:
            raise ValueError('Semantic name disagrees with original registration')
        records.append({'rank':k,'selector':t,'sdk_id':sdk_id,'name':name,
                        'application_default':default,'sdk_default':sdk_default,
                        'getter':hx(getter),'setter':hx(setter),'name_address':hx(a),
                        'name_store_pc':hx(np),'reverse_selector_store_pc':hx(tp),
                        'default_store_pc':hx(dp),'category_store_pc':hx(mp),
                        'supported_in_stated_build073':sdk_id in BUILD073_IDS})
    if tuple(r['application_default'] for r in records[44:])!=TAIL_DEFAULTS:
        raise ValueError('Remaining startup default sequence changed')
    if set(range(1,87))-{r['selector'] for r in records}!=set(UNREGISTERED):
        raise ValueError('Unregistered selector set changed')
    return records


def replace_field(old,raw,shift,width):
    """Pure bit-insertion fixture, not an SDK-state implementation."""
    uint(old);uint(raw)
    if type(shift) is not int or type(width) is not int or width<1 or shift<0 or shift+width>32:
        raise ValueError('Invalid field bounds')
    mask=((1<<width)-1)<<shift
    return (old&~mask)|((raw<<shift)&mask)


def point_baseline(raw,multiplier):
    """Only the actual finite 1.0f startup case; no unproved conversion policy."""
    uint(raw)
    if raw!=0x3F800000 or multiplier not in (8,16):
        raise ValueError('Outside verified point baseline')
    value=struct.unpack('>f',struct.pack('>I',raw))[0]
    rounded=struct.unpack('>f',struct.pack('>f',value*multiplier))[0]
    return int(rounded)&65535


def make_report(b,image,disasm,reference):
    validate_identity(b);pdata=check_pins(b);records=extract_registrations(b)
    refs=[]
    for name,digest in REFERENCES.items():
        if sha((reference/name).read_bytes())!=digest: raise ValueError('Changed read-only reference '+name)
        refs.append({'name':name,'sha256':digest,'role':'Enum/field corroboration only; original words authoritative'})
    decode={a:n for a,(n,_) in CODE_RANGES.items()}
    decode.update({0x82724840:0xCE8,0x827246C8:0x60,0x82723D80:0xD8,
                   0x82466800:0x74,0x8243C430:0xFC,0x82439C20:0x38})
    decoded=[]
    for a,n in sorted(decode.items()):
        out=subprocess.run([str(disasm),str(image),hx(BASE),hx(a),str(n//4)],
                           check=True,capture_output=True,text=True, timeout=60).stdout
        decoded.append({'start':hx(a),'bytes':n,'words':checked_decode(b,a,n,out)})
    methods=parse_methods(span(b,0x82CD28B8,1212))
    descriptions=[]
    for sdk_id,(name,effects,limits) in sorted(BEHAVIOR.items()):
        g,s,d=methods[sdk_id//4]
        descriptions.append({'sdk_id':sdk_id,'verified_application_name':name,'getter':hx(g),
                             'setter':hx(s),'sdk_default':d,'original_effects':effects,
                             'getter_effects':GETTERS[sdk_id],
                             'native_constraint_proposal':limits})
    pins=[]
    for kind,table in (('pdata',FUNCTIONS),('code_range',CODE_RANGES),('data',DATA)):
        for a,(n,digest) in sorted(table.items()):
            p={'kind':kind,'address':hx(a),'bytes':n,'sha256':digest}
            if kind=='pdata':p.update(pdata_address=hx(pdata[a][1]),pdata_word=hx(pdata[a][2]))
            pins.append(p)
    return {'schema':'simpsons.application-scalars.evidence.v1',
            'image':{'base':hx(BASE),'bytes':len(b),'sha256':IMAGE_SHA},
            'scope':'First initializer registrations and remaining build073 startup setters; no implementation/draw permit',
            'registration_order':records,'remaining_from_boot044':records[44:],
            'unregistered_selectors':[{'selector':t,'sdk_id':4*(t+9)} for t in UNREGISTERED],
            'remaining_setter_evidence':descriptions,'byte_pins':pins,'references':refs,
            'critical_words':[{'pc':hx(a),'word':hx(w)} for a,w in sorted(WORDS.items())],
            'critical_calls':[{'pc':hx(a),'target':hx(t),'linked':link,'lr':hx(a+4) if link else None}
                              for a,(t,link) in sorted(CALLS.items())],
            'facts':[
                {'status':'verified','text':'82 first-init registrations. SDK scalar ID is 4*(selector+9), but category registration order controls startup.'},
                {'status':'verified','text':'827246C8 passes force=1 and each owner default at rank*4 to 82723D80; BL 82724708, LR 8272470C.'},
                {'status':'verified','text':'82466800 installs 101 scalar setter rows at context+40+SDK_ID and getters at context+224+SDK_ID; application registry is a separate subset.'},
                {'status':'verified','text':'SDK168/16C/170 are original HISTENCILENABLE/WRITEENABLE/FUNC names; all three write one bit, defaults zero.'},
                {'status':'verified','text':'SDK174 and17C are not application registrations. Their storage operations are known; semantic names are unresolved.'},
                {'status':'proposal','text':'Retain validated requests/effective state; reject unsupported consumer operations. Baseline scalar acceptance alone never enables drawing.'},
                {'status':'unresolved','text':'Enabled hi-stencil semantics, general guardband/point/tessellation/wrap behavior, primitive restart and presentation policy need native consumer proof. No end-to-end frame execution here.'}],
            'disassembly':decoded,
            'validation':{'registration_count':82,'new_ids_vs_stated_build073':sum(not r['supported_in_stated_build073'] for r in records),
                          'pinned_extents':len(pins),'decoded_words':sum(len(x['words']) for x in decoded),
                          'padding':'INVALID between leaf functions is original alignment data, not interpreted code',
                          'extraction':'Finite pinned first-init constant assignments, no guest execution or arbitrary branch traversal'}}


class EvidenceTests(unittest.TestCase):
    def test_original_bytes_pdata_and_names(self):
        check_pins(self.original)
        self.assertEqual(len(self.records),82)
        self.assertEqual(len({r['name'] for r in self.records}),82)

    def test_all_ordered_registrations_and_defaults(self):
        self.assertEqual(tuple(r['selector'] for r in self.records),SEQUENCE)
        self.assertEqual(tuple(r['application_default'] for r in self.records[44:]),TAIL_DEFAULTS)

    def test_remaining_thirty_seven_and_existing_fill(self):
        self.assertEqual(len(BUILD073_IDS),45)
        self.assertEqual(sum(not r['supported_in_stated_build073'] for r in self.records),37)
        self.assertEqual(self.records[52]['sdk_id'],0x34)

    def test_hi_stencil_names_and_one_bit_functions(self):
        self.assertEqual([r['name'] for r in self.records[44:47]],
                         ['RenderState_HISTENCILENABLE','RenderState_HISTENCILWRITEENABLE','RenderState_HISTENCILFUNC'])
        for pc,shift in ((0x8243B964,3),(0x8243B994,2),(0x8243B9C4,5)):
            w=word(self.original,pc)
            self.assertEqual((w>>26,(w>>11)&31,(w>>6)&31,(w>>1)&31),(20,shift,31-shift,31-shift))

    def test_field_insert_preserves_neighbors_and_truncates(self):
        for shift in (2,3,5):
            self.assertEqual(replace_field(0xA5A5A5A5,0,shift,1),0xA5A5A5A5&~(1<<shift))
            self.assertEqual(replace_field(0,3,shift,1),1<<shift)
            self.assertEqual(replace_field(0,2,shift,1),0)
        self.assertEqual(replace_field(0xA5A5002C,0xAB,8,8),0xA5A5AB2C)
        self.assertEqual(replace_field(0x80400001,0xFF,23,7),0xBFC00001)

    def test_exact_getter_widths_and_extraction_masks(self):
        for pc,op,offset in ((0x8243AA98,32,0x2E48),(0x8243BA08,34,0x2942),(0x8243BA18,32,0x3504)):
            w=word(self.original,pc)
            self.assertEqual((w>>26,(w>>21)&31,(w>>16)&31,w&65535),(op,3,3,offset))
            self.assertEqual(word(self.original,pc+4),0x4E800020)
        for pc,shape in ((0x8243B984,(29,31,31)),(0x8243B9B4,(30,31,31)),
                         (0x8243B9E4,(27,31,31)),(0x8243BA34,(9,25,31))):
            w=word(self.original,pc)
            self.assertEqual((w>>26,(w>>21)&31,(w>>16)&31),(21,11,3))
            self.assertEqual(((w>>11)&31,(w>>6)&31,(w>>1)&31),shape)
        self.assertEqual(set(GETTERS),set(BEHAVIOR))

    def test_unregistered_sdk_neighbors_not_named_or_authorized(self):
        selectors={r['selector'] for r in self.records}
        for t in UNREGISTERED:
            self.assertNotIn(t,selectors);self.assertIsNone(BEHAVIOR[4*(t+9)][0])
        self.assertEqual(next(r for r in self.records if r['sdk_id']==0x178)['name'],'RenderState_PRESENTINTERVAL')

    def test_startup_and_sdk_defaults_remain_distinct(self):
        by_id={r['sdk_id']:r for r in self.records}
        for sdk_id,app,sdk in ((0x144,1,0),(0x158,0x3F800000,0x40000000),(0xBC,0x3F800000,0x42800000),
                               (0xE4,0,1),(0x178,1,0),(0xC4,0xFFFF,0xFFFFFFFF),(0x148,1,0)):
            self.assertEqual((by_id[sdk_id]['application_default'],by_id[sdk_id]['sdk_default']),(app,sdk))

    def test_point_baseline_scaling_and_be_halfword_positions(self):
        self.assertEqual(point_baseline(0x3F800000,8),8)
        self.assertEqual(point_baseline(0x3F800000,16),16)
        self.assertEqual(word(self.original,0x8243AE00)&65535,0x296A)
        self.assertEqual(word(self.original,0x8243AE58)&65535,0x2968)
        for raw in (0x7FC00000,0x7F800000,0xBF800000,0x40000000):
            with self.assertRaises(ValueError):point_baseline(raw,8)

    def test_table_framing_endian_and_method_identity_rejections(self):
        selectors=span(self.original,0x82150580,348)
        methods=span(self.original,0x82CD28B8,1212)
        for b in (selectors[:-4],selectors+b'\0'*4,struct.pack('<87I',*parse_selectors(selectors))):
            with self.assertRaises(ValueError):parse_selectors(b)
        for b in (methods[:-1],methods+b'\0',bytes([methods[0]^1])+methods[1:]):
            with self.assertRaises(ValueError):parse_methods(b)

    def test_changed_initializer_setter_and_bss_rejected(self):
        for a in (0x82724B88,0x82724FA0,0x8243B964,0x82D6D7F8):
            b=bytearray(self.original);b[a-BASE]^=1
            with self.assertRaises(ValueError):extract_registrations(b)

    def test_invalid_field_and_static_address_bounds(self):
        for args in ((0,0,32,1),(0,0,2,0),(0,-1,2,1),(1<<32,0,2,1)):
            with self.assertRaises(ValueError):replace_field(*args)
        with self.assertRaises(ValueError):plus(Relative('object',0),Relative('stack',0))

    def test_mismatched_disassembler_word_rejected(self):
        with self.assertRaises(ValueError):checked_decode(self.original,0x8243B960,4,'8243B960 00000000 nop')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    p.add_argument('--disasm',type=Path,default=ROOT/'build/generator-ninja/SimpsonsDisasm.exe')
    p.add_argument('--reference',type=Path,default=Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics'))
    p.add_argument('--output',type=Path,help='Only analysis/native-application-scalars.json; otherwise stdout')
    p.add_argument('--self-test',action='store_true')
    args=p.parse_args()
    try:
        if args.output and args.output.resolve()!=REPORT.resolve():raise ValueError('Output outside owned report path')
        b=args.image.read_bytes();validate_identity(b)
        records=extract_registrations(b)
        if args.self_test:
            EvidenceTests.original=b;EvidenceTests.records=records
            result=unittest.TextTestRunner(stream=sys.stderr,verbosity=2).run(
                unittest.defaultTestLoader.loadTestsFromTestCase(EvidenceTests))
            if not result.wasSuccessful():return 1
        report=make_report(b,args.image.resolve(),args.disasm.resolve(),args.reference)
        data=(json.dumps(report,indent=2,sort_keys=True)+'\n').encode('utf-8')
        if args.output:
            inputs=[args.image,args.disasm,SUPPORT,Path(__file__),*(args.reference/n for n in REFERENCES)]
            if args.output.exists() and any(args.output.samefile(x) for x in inputs):
                raise ValueError('Report aliases read-only input')
            args.output.write_bytes(data)
            print('Wrote '+str(args.output)+'; '+str(report['validation']),file=sys.stderr)
        else:sys.stdout.buffer.write(data)
        return 0
    except (OSError,ValueError,subprocess.CalledProcessError) as exc:
        print('error: '+str(exc),file=sys.stderr);return 1


if __name__=='__main__':
    raise SystemExit(main())
