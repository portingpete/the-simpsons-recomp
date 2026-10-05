"""Pinned application sampler evidence and bounded arithmetic fixtures.

Read-only original bytes; no guest execution, SDK object or GPU interpreter.
The only writable artifact is analysis/native-application-samplers.json.
Semantic annotations are reviewed facts, not decompiler-derived declarations.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / 'tools/analyze_poststart_integration.py'
SUPPORT_SHA = 'fdcb3c06ae15041bcbf98ce59d07272e51dce345c7332b44475844d20251b434'
if hashlib.sha256(SUPPORT.read_bytes()).hexdigest() != SUPPORT_SHA:
    raise RuntimeError('Frozen PE validation dependency changed')
from analyze_poststart_integration import (BASE, IMAGE_SHA, branch, checked_decode,
                                          hx, layout, sha, span, validate_identity, word)

REPORT = ROOT / 'analysis/native-application-samplers.json'
REFERENCE_SHA = '7857b51de405a2414d1b3f4c6b86854c4471077b0596acd35c78c26295093227'
FUNCTION_PINS = {
    0x82723C80: (0xFC, '932df63c08370f7ea903a642cf5a96bbd9b4234c0b244bc52850c57bc993635b'),
    0x82724840: (0x1008, '002851f49043ac5e0f73ceaaf2cba546491a1eb7536c632e89f09e7962778854'),
    0x82466800: (0x268, 'b11aac256fe4eb8d493f9f4249acec09cc7e132fb4271af4ff8d00ff21cef4e2'),
    0x824408E0: (0x178, '1b2bb7654b7db274a459cbcf93793bbe2cdacd5b76dde5a4911a9b14e47f3711'),
    0x82723858: (0x60, 'be54808f2f65f763b0bb5dc20f1fe3390d4f3010a558a32f41cebecee0a2fa6b'),
    0x827238B8: (0x50, '6895bbd4cf9bb91aac27e7bb81844c7c334345d60ba3568462c452f4c2a6e0b9'),
    0x82723AB0: (0x90, '918970755231a80758d365d9f47d8ceca34b75484076b23095d04fa031e73acd'),
    0x82723B40: (0x13C, 'a358b2ab3a6cff1d060b263d9edb67a6d9341ac1fad8d72ebd2282aba58ec1da'),
    0x82723978: (0x6C, '64e5f6862352b2692136f08cda046015d6b59ab474a2ad18b9f69c1bfb70883e'),
    0x827243E0: (0x1A4, '35970ed80f8f25dc763fecfb8281f27316404ddce239423411808698e336cf44'),
    0x827245F0: (0xD4, 'f67ceb098acb8d9839799fa8c9ce34010e0af7741249042603a8e8faddd9789d'),
    0x82724728: (0x7C, 'b1ae8f1f3daac7b852a14f87ac732a6759a93691ad68698c5195929150911cc0'),
    0x82724038: (0x1F4, 'd027ae990d6535d7b2c68e0844762a6fb46349dbb1fdef1fc05b16706cc9ed97'),
    0x82724230: (0x198, '3f75e6ee239f127f94ce2bd4c2d741898f84eee41efc5cdd25aa14db1bd6f518'),
    0x827237D0: (0x84, '93121142ca15de3c581da658b29f4386c75e063b99d5be5d8783a235e9270649'),
}
# Full original initializer is pinned but only its sampler-related windows are
# annotated/disassembled. Scalar dispatch is deliberately outside this task.
RANGE_PINS = {
    0x8243BA40: (0x9F0, 'aa28c814641d32b60113e0f0a3884caafda89cf0a58e58eacc993c448a973c6d'),
    0x826B7890: (0x30, 'db9a9d05d0182effac3c117aef2c69ddd22f6a566fe69c1786f250dff4a66b84'),
    0x826B79B0: (0xA4, '8e664f1758c647923f03e556020d494fa2a4b2f0017ea95ee572e1bfd311a934'),
    0x826B36A8: (0x38, '71eed6e3868b3c6dc8710dedfff4fa1c789fd5c8aa582f72215f14f77cad69ec'),
    0x826B3778: (0x1C, '4b207369184b54dc75ae7e761788fbf197d831bf39a80759f7e7ef9e32e21147'),
    0x826B3834: (0x20, '8e18dbb4abd36090b08718d9769cdb6e7613f469557e04dff90570d9abc9c59c'),
}
DATA_PINS = {
    0x821506E0: (0x54, 'bff8d0d12d1f2b9b687930d41394a048cff5202ad27b3a5363cb5fc37ca3fb7d'),
    0x82CD2D78: (0xF0, '083301a45aeec700a09d847d1db931901782a13101a33aadf128c0d4eb6236fe'),
}
SDK_ROWS = (
    (0x8243C1B8,0x8243C180,0), (0x8243C208,0x8243C1D0,0),
    (0x8243C258,0x8243C220,0), (0x8243C160,0x8243C110,0),
    (0x8243BC90,0x8243BBD0,0), (0x8243BB00,0x8243BA40,0),
    (0x8243BDA0,0x8243BD60,2), (0x8243BFD0,0x8243BF70,0),
    (0x8243C080,0x8243C010,0), (0x8243BEB8,0x8243BE50,1),
    (0x8243BD50,0x8243BCC8,0), (0x8243BBC0,0x8243BB38,0),
    (0x8243BE40,0x8243BDB8,0), (0x8243C100,0x8243C090,13),
    (0x8243C2B0,0x8243C270,0), (0x8243BF28,0x8243BEC8,0),
    (0x8243C308,0x8243C2C8,0), (0x8243C360,0x8243C320,0),
    (0x8243C3B8,0x8243C378,0), (0x8243C418,0x8243C3D0,1),
)
# Registration order: selector, original ASCII suffix/address, default raw word,
# category-map store PC, default-record stwx PC. Category is the row number only
# for the original first initialization, when 82D6D7EC == 0.
REGISTRATION = (
    (1,'ADDRESSU',0x8214EF38,2,0x82725514,0x8272550C),
    (2,'ADDRESSV',0x8214EF20,2,0x82725548,0x82725544),
    (3,'ADDRESSW',0x8214EF08,2,0x8272556C,0x82725568),
    (5,'MAGFILTER',0x8214EEF0,1,0x8272558C,0x82725588),
    (6,'MINFILTER',0x8214EED8,1,0x827255B8,0x827255B0),
    (7,'MIPFILTER',0x8214EEC0,2,0x827255F4,0x827255EC),
    (11,'MAGFILTERZ',0x8214EE90,1,0x8272562C,0x82725624),
    (12,'MINFILTERZ',0x8214EEA8,1,0x82725658,0x82725654),
    (4,'BORDERCOLOR',0x8214EE74,0,0x82725680,0x8272567C),
    (8,'MIPMAPLODBIAS',0x8214EE58,0,0x827256A8,0x827256A0),
    (9,'MAXMIPLEVEL',0x8214EDE4,0,0x827256D8,0x827256D0),
    (14,'MINMIPLEVEL',0x8214EDC8,13,0x827256F8,0x827256F4),
    (15,'TRILINEARTHRESHOLD',0x8214EE38,0,0x82725728,0x82725720),
    (10,'MAXANISOTROPY',0x8214EE1C,1,0x82725754,0x82725750),
    (16,'ANISOTROPYBIAS',0x8214EE00,0,0x8272577C,0x82725774),
    (13,'SEPARATEZFILTERENABLE',0x8214ED44,0,0x827257AC,0x827257A4),
    (17,'HGRADIENTEXPBIAS',0x8214ED24,0,0x827257D0,0x827257CC),
    (18,'VGRADIENTEXPBIAS',0x8214EDA8,0,0x827257F4,0x827257F0),
    (19,'WHITEBORDERCOLORW',0x8214ED88,0,0x82725818,0x82725810),
    (20,'POINTBORDERENABLE',0x8214ED68,1,0x8272583C,0x82725838),
)
CATEGORIES = tuple([0x7FFFFFFF] + [next(k for k,r in enumerate(REGISTRATION) if r[0]==t)
                                    for t in range(1,21)])
HOST_FIELDS = {0:'AddressU',4:'AddressV',8:'AddressW',12:'BorderSelector',
               16:'Magnification',20:'Minification',24:'MipFilter',28:'LodBiasBits',
               32:'MinimumMip',36:'MaximumAnisotropy',52:'MaximumMip'}
TRANSFORMS = (
    'word0[10:12] = raw low3', 'word0[13:15] = raw low3',
    'word0[16:18] = raw low3',
    'word5[0:1] = raw != 0; getter returns 0 or FFFFFFFF, not ARGB',
    'word3[19:20] = low2(raw | (raw>>2)); raw>>2 also feeds anisotropic flag/lookup and Z-filter recomputation; native subset raw 0/1 only',
    'word3[21:22] = low2(raw | (raw>>2)); raw>>2 also feeds anisotropic flag/lookup and Z-filter recomputation; native subset raw 0/1 only',
    'word3[23:24] = raw low2',
    'word4[12:21] = low10(trunc(float32(raw bits) * 32)); getter signed10 / 32 as f32 bits',
    'requested low byte at D+2EA6+stage; bound word4[2:5] = low4(max(texture minimum, raw))',
    'requested low byte at D+2E8C+stage; if anisotropic flags set, lookup 82069FD0[raw] feeds word3[25:27]',
    'flags byte at D+2EDA+stage = low8((old & ~1) | raw); recomputes combined Z filter; non-Boolean raw is not masked before OR',
    'flags byte at D+2EDA+stage = low8((old & ~2) | (raw<<1)); recomputes combined Z filter; non-Boolean raw is not masked before OR',
    'flags byte at D+2EDA+stage = low8((old & ~4) | (raw<<2)); selects separate Z or retained normal filter combination; non-Boolean raw is not masked before OR',
    'requested low byte at D+2EC0+stage; bound word4[6:9] = low4(min(texture maximum, raw))',
    'word5[3:4] = raw low2',
    'word5[5:8] = low4(trunc(float32(raw bits) * -8)); getter -signed4 / 8 as f32 bits',
    'word4[22:26] = raw low5; getter sign extends 5 bits',
    'word4[27:31] = raw low5; getter sign extends 5 bits',
    'word5[2] = raw low bit',
    'word1[11] = (raw == 0); getter is inverse of stored bit',
)
CALLS = {0x82723C84:(0x82A3C3C0,True), 0x826B7A04:(0x82723C80,False),
         0x82CB9588:(0x82725848,False),
         0x826B36D8:(0x826B79E8,True), 0x826B3790:(0x826B79E8,True),
         0x826B3850:(0x826B79E8,True), 0x82724550:(0x82723C80,True),
         0x82724690:(0x82723C80,True), 0x82724774:(0x82723C80,True),
         0x824668FC:(0x824408E0,True), 0x827242D4:(0x827237D0,True),
         0x827237F8:(0x8243BBD0,True), 0x82723808:(0x8243BA40,True)}
WORDS = {0x82723D04:0x4E800421, 0x82723D20:0x396B0698,
         0x826B79F0:0x3D4082D6, 0x826B79FC:0x386ADB78,
         0x82CB9580:0x3D6082D6, 0x82CB9584:0x386BDB78,
         0x827239A0:0x3BEA0D2C, 0x8272498C:0x3AE0000D,
         0x82724ACC:0x38800002, 0x82724938:0x38C00001,
         0x82466908:0x2B1C001A, 0x821DD3F4:0x42000000,
         0x8206A020:0x3D000000, 0x8206A01C:0xC1000000, 0x8206A014:0x3E000000}
ANISO = (0,0,2,2,3,3,3,4,4,4,4,4,4,5,5,5,5)


def uint(n, bits=32):
    if type(n) is not int or not 0 <= n < 1 << bits:
        raise ValueError('Expected bounded unsigned integer')
    return n


def parse_selectors(data):
    if len(data) != 84:
        raise ValueError('Selector table must contain exactly 21 BE words')
    rows = struct.unpack('>21I', data)
    if rows != (0,) + tuple(range(0,80,4)):
        raise ValueError('Unverified SDK offset mapping')
    return rows


def parse_methods(data):
    if len(data) != 240:
        raise ValueError('SDK method table must contain exactly 20 triples')
    rows = tuple(struct.iter_unpack('>III', data))
    if rows != SDK_ROWS:
        raise ValueError('Unverified SDK getter/setter/default')
    return rows


def parse_categories(data, count):
    uint(count)
    if count != 20 or len(data) != 84:
        raise ValueError('Uninitialized or appended application categories')
    rows = struct.unpack('>21I', data)
    if rows != CATEGORIES:
        raise ValueError('Unverified application category/sentinel')
    return rows


def checked_ascii(b, a):
    data = span(b,a,96)
    end = data.find(b'\0')
    if end < 1 or any(c < 32 or c > 126 for c in data[:end]):
        raise ValueError('Invalid bounded original ASCII name')
    return data[:end].decode('ascii')


def check_pins(b):
    _, pdata = layout(b)
    for table in (FUNCTION_PINS, RANGE_PINS, DATA_PINS):
        for a,(n,digest) in table.items():
            if sha(span(b,a,n)) != digest:
                raise ValueError('Changed original bytes at '+hx(a))
            if table is FUNCTION_PINS and (a not in pdata or pdata[a][0] != n):
                raise ValueError('Changed .pdata function extent at '+hx(a))
    for pc, dest in CALLS.items():
        if branch(pc,word(b,pc)) != dest:
            raise ValueError('Changed reviewed call at '+hx(pc))
    for pc,w in WORDS.items():
        if word(b,pc) != w:
            raise ValueError('Changed reviewed constant/instruction at '+hx(pc))
    parse_selectors(span(b,0x821506E0,84))
    parse_methods(span(b,0x82CD2D78,240))
    if span(b,0x82D6D648,84) != bytes(84) or word(b,0x82D6D7EC) != 0:
        raise ValueError('Changed image-time category BSS; not a live initialized map')
    if struct.unpack('>17I',span(b,0x82069FD0,68)) != ANISO:
        raise ValueError('Changed anisotropy lookup')
    for t,name,a,default,pc,default_pc in REGISTRATION:
        if checked_ascii(b,a) != 'SamplerState_'+name:
            raise ValueError('Changed sampler name')
        w = word(b,pc)
        if w >> 26 != 36 or (w >> 16) & 31 != 7 or w & 65535 != 4*t:
            raise ValueError('Changed category-map store')
        w = word(b,default_pc)
        if w >> 26 != 31 or (w >> 1) & 1023 != 151 or (w >> 11) & 31 != 3:
            raise ValueError('Changed default-record stwx')
    return pdata


def supported_host_update(stage, selector, value):
    """Snapshot of existing EngineState restrictions, NOT new renderer support."""
    uint(stage); uint(selector); uint(value)
    if stage >= 8 or not 1 <= selector <= 20:
        raise ValueError('Outside current native sampler stage/type subset')
    sdk_id = 4*(selector-1)
    if sdk_id not in HOST_FIELDS:
        raise ValueError('Unsupported auxiliary sampler state')
    allowed = range(8) if sdk_id in (0,4,8) else range(2) if sdk_id in (16,20) else \
              range(3) if sdk_id == 24 else (1,) if sdk_id == 36 else \
              (13,) if sdk_id == 52 else (0,)
    if value not in allowed:
        raise ValueError('Unsupported native sampler value')
    return sdk_id


def owner_word(owner, extent, offset):
    uint(owner); uint(extent)
    if owner == 0 or owner & 3 or extent == 0 or owner+extent > 1 << 32:
        raise ValueError('Invalid owner allocation bounds')
    if type(offset) is not int or offset < 0 or offset & 3 or offset+4 > extent:
        raise ValueError('Owner/frame word outside proven allocation')
    return owner+offset


def application_plan(owner, extent, stage, selector, value, force, current,
                     depth=0, baseline=0, dirty=0):
    """Pure original address/write equations for fixtures; performs no writes.

    Caller supplies checked memory observations. Native acceptance is separately
    validated by supported_host_update; the original has sixteen cache stages.
    This is not a CPU implementation or an allocation/liveness proof.
    """
    for n in (stage,selector,value,force,current,depth,baseline,dirty):
        uint(n)
    if stage >= 16 or not 1 <= selector <= 20:
        raise ValueError('Outside proven application cache shape')
    k = CATEGORIES[selector]
    cache = owner_word(owner,extent,0x7DC+0x50*stage+4*k)
    result = {'invoke':False, 'sdk_id':4*(selector-1), 'category':k,
              'cache_address':cache, 'writes':[]}
    if value == current and force & 255 == 0:
        return result
    result['invoke'] = True
    owner_word(owner,extent,0xD28)
    # Original cmpwi is signed; zero or a negative signed count skips the frame.
    if 0 < depth < 0x80000000:
        frame = 0x698+0x694*depth
        ba = owner_word(owner,extent,frame+0x148+0x50*stage+4*k)
        da = owner_word(owner,extent,frame+0x654+4*(stage+(k>>5)))
        mask = 1 << (k & 31)
        new_dirty = dirty & ~mask if value == baseline else dirty | mask
        result.update(frame_address=owner+frame,baseline_address=ba,dirty_address=da)
        result['writes'].append((da,new_dirty))
    result['writes'].append((cache,value))
    return result


def binding_mips(texture_min, texture_max, requested_min, requested_max):
    """Binding 824408E0 reads retained request BYTES, unlike the direct setters."""
    for n in (texture_min,texture_max):
        uint(n,4)
    for n in (requested_min,requested_max):
        uint(n,8)
    # Original bit insertion truncates; it does not repair an inverted interval.
    return (max(texture_min,requested_min)&15, min(texture_max,requested_max)&15)


def facts():
    return [
        {'status':'verified','id':'abi','text':'82723C80: r3 owner, r4 stage, r5 selector, r6 raw DWORD, low8(r7) force. No normalized success result. Public 826B79E8 materializes literal static owner address 82D5DB78 with lis/addi at 826B79F0/826B79FC; it does not load a pointer from that address. Static initializer 82CB9580/82CB9584 forms the same r3 address before tail 82CB9588 -> 82725848.'},
        {'status':'verified','id':'identity','text':'Category [82D6D648+4*T] indexes CPU cache; [821506E0+4*T] indexes SDK method offsets. Selector 0 is a sentinel, not ADDRESSU.'},
        {'status':'verified','id':'sdk_table','text':'82466800 installs triples from 82CD2D78 into D+1D4 setters and D+3B8 getters; initializes all 20 IDs on 26 SDK slots, null-binds each. Application loops cover 16 stages; current host covers 8.'},
        {'status':'verified','id':'cache','text':'Current=O+7DC+50*S+4*K. Equal raw word and zero low8(force) returns without call or writes. Changed/forced calls SDK with unchanged raw value, then publishes CPU cache.'},
        {'status':'verified','id':'frame','text':'Signed N=[O+D28]. If N>0, F=O+698+694*N = O+D2C+694*(N-1). Baseline=F+148+50*S+4*K; dirty=F+654+4*(S+(K>>5)), mask=1<<(K&31). Equal baseline clears mask, differing baseline sets it. Dirty store precedes current-cache store.'},
        {'status':'verified','id':'push_pop','text':'82723978 copies 694 bytes from O+694 to O+D2C+694*oldN, increments N, clears saved dirty masks. Pop 827243E0 restores selected sampler entries through 82723C80 across sixteen stages. No allocation or retain occurs in sampler dispatcher.'},
        {'status':'verified','id':'caller_records','text':'826B36D8/3790/3850 use four-word records {stage, selector, desired, saved}. First reads old application cache into +C; apply uses +8; restore uses +C; all pass force=0 and raw value unchanged.'},
        {'status':'verified','id':'bind','text':'824408E0 imports resource descriptor words while preserving sampler address/filter/bias fields. Recomputes mip interval using resource limits and request bytes D+2EA6/D+2EC0. Null binding retains inactive descriptor words. It is not a sampler-cache reset.'},
        {'status':'verified','id':'filter_arithmetic','text':'Let q=raw>>2, a=lookup[retained anisotropy byte], o=other normal-filter flag. MAG g=raw|q|((a & ~((o|q)-1))<<6); MIN uses <<4. New normal field=low2(g), anisotropy field=low3(g>>6 or >>4), own flag=low1(q). Arithmetic is uint32; invalid lookup indices are not validated by SDK.'},
        {'status':'verified','id':'z_arithmetic','text':'B=descriptor word3. P=((B>>19)&1)|(((B>>21)&0x7FF)<<1). In normal setters F is retained Z-filter byte; in Z setters F is the new untruncated flag word before stb. m=(F>>2)-1 as uint32. Word4 low2 = low2((P&m)+(F&~m)). Boolean separate-enable chooses P or F low2; arbitrary raw inputs are not newly authorized.'},
        {'status':'proposal','id':'hook','text':'Preflight at 82723C80, replace SDK-only block 82723CD8..82723D04, resume 82723D08. Block-entry r31=stage,r29=value,r28=category,r30=owner,r26=cache offset,r27=20*stage+category,r11=4*selector. Retain original CPU suffix, not a reimplemented frame cache.'},
        {'status':'proposal','id':'host_observer','text':'Use validated effective EngineState::setSampler without engine guest-cache equality skipping. Existing EngineRenderState::setSampler writes 82D0D170+320*S+4*ID, which this original application path bypasses. Do not silently make this new CPU side effect.'},
        {'status':'unresolved','id':'coverage','text':'Runtime owner allocation capacity/generation, live selector/stage demand, generalized native auxiliary states and semantic SDK getters are not proven here. Combined setters/getters need guards or their own native boundaries. This evidence does not grant a draw permit.'},
    ]


def build_report(b, image, disasm, reference):
    validate_identity(b)
    pdata = check_pins(b)
    if sha(reference.read_bytes()) != REFERENCE_SHA:
        raise ValueError('Changed read-only enum reference')
    ranges = {a:n for a,(n,_) in FUNCTION_PINS.items()
              if a not in (0x82724840,0x82466800,0x827243E0,0x827245F0)}
    ranges.update({a:n for a,(n,_) in RANGE_PINS.items()})
    ranges.update({0x827248AC:0x18, 0x827254C0:0x388, 0x82466874:0x9C,
                   0x827244CC:0xB0, 0x82724654:0x68})
    decoded = []
    for a,n in sorted(ranges.items()):
        out = subprocess.run([str(disasm),str(image),hx(BASE),hx(a),str(n//4)],
                             capture_output=True,text=True,check=True, timeout=60).stdout
        decoded.append({'start':hx(a),'bytes':n,'words':checked_decode(b,a,n,out)})
    records = []
    for k,(t,name,a,default,pc,dp) in enumerate(REGISTRATION):
        getter,setter,sdk_default = SDK_ROWS[t-1]
        records.append({'selector':t,'category':k,'sdk_id':4*(t-1),
                        'name':checked_ascii(b,a),'name_address':hx(a),
                        'name_sha256':sha(('SamplerState_'+name+'\0').encode('ascii')),
                        'first_initialization_default':default,'sdk_default':sdk_default,
                        'getter':hx(getter),'setter':hx(setter),'category_store_pc':hx(pc),
                        'default_store_pc':hx(dp),'host_field':HOST_FIELDS.get(4*(t-1)),
                        'transform':TRANSFORMS[t-1]})
    pins = []
    for kind,table in [('pdata_function',FUNCTION_PINS),('reviewed_code_range',RANGE_PINS),('data',DATA_PINS)]:
        for a,(n,digest) in sorted(table.items()):
            record = {'kind':kind,'start':hx(a),'bytes':n,'sha256':digest}
            if kind == 'pdata_function':
                record.update(pdata_address=hx(pdata[a][1]),pdata_word=hx(pdata[a][2]))
            pins.append(record)
    return {'schema':'simpsons.application-samplers.evidence.v1',
            'image':{'base':hx(BASE),'bytes':len(b),'sha256':IMAGE_SHA},
            'scope':'Offline bounded evidence; no scalar dispatcher, GPU model or native implementation',
            'reference':{'path':'K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h',
                         'sha256':REFERENCE_SHA,'role':'Enum labels corroboration only; original PPC words are authority'},
            'category_map_after_first_initialization':list(CATEGORIES),
            'category_map_image_bytes':'21 zero BE words; not initialized runtime state',
            'stage_limits':{'sdk_initialization':26,'application_arrays_and_loops':16,'existing_host':8},
            'records':sorted(records,key=lambda r:r['selector']), 'facts':facts(), 'byte_pins':pins,
            'critical_words':[{'address':hx(a),'word':hx(w)} for a,w in sorted(WORDS.items())],
            'calls':[{'pc':hx(a),'target':hx(t),'linked':linked,'lr':hx(a+4) if linked else None}
                     for a,(t,linked) in sorted(CALLS.items())],
            'disassembly':decoded,
            'validation':{'original_sha256':True,'pdata_function_count':len(FUNCTION_PINS),
                          'byte_extent_count':len(pins),'decoded_words':sum(len(r['words']) for r in decoded),
                          'range_padding':'INVALID rows between leaf functions are original alignment words, not interpreted operations',
                          'semantic_limit':'Reviewed equations, not execution equivalence or complete indirect closure'}}


class ContractTests(unittest.TestCase):
    def plan(self, **kwargs):
        values = dict(owner=0x10000,extent=0x5000,stage=3,selector=5,value=1,
                      force=0,current=0,depth=1,baseline=0,dirty=0xA5000000)
        values.update(kwargs)
        return application_plan(**values)

    def test_selector_zero_is_not_address_u(self):
        self.assertEqual(parse_selectors(struct.pack('>21I',0,*range(0,80,4)))[1],0)
        with self.assertRaises(ValueError): self.plan(selector=0)

    def test_public_wrapper_and_initializer_materialize_static_owner(self):
        for hi_pc,lo_pc,base_reg in ((0x826B79F0,0x826B79FC,10),
                                    (0x82CB9580,0x82CB9584,11)):
            hi,lo=word(self.image_bytes,hi_pc),word(self.image_bytes,lo_pc)
            self.assertEqual((hi>>26,(hi>>21)&31,(hi>>16)&31),(15,base_reg,0))
            self.assertEqual((lo>>26,(lo>>21)&31,(lo>>16)&31),(14,3,base_reg))
            displacement=(lo&65535)-65536 if lo&32768 else lo&65535
            self.assertEqual((((hi&65535)<<16)+displacement)&0xFFFFFFFF,0x82D5DB78)

    def test_method_truncation_endian_pointer_and_default_reject(self):
        data = b''.join(struct.pack('>III',*r) for r in SDK_ROWS)
        self.assertEqual(parse_methods(data),SDK_ROWS)
        for changed in (data[:-1],data+b'\0',b''.join(struct.pack('<III',*r) for r in SDK_ROWS),
                        bytes([data[0]^1])+data[1:],data[:-1]+bytes([data[-1]^1])):
            with self.assertRaises(ValueError): parse_methods(changed)

    def test_selector_table_framing_and_identity(self):
        data=struct.pack('>21I',0,*range(0,80,4))
        for changed in (data[:-4],data+b'\0'*4, data[:8]+struct.pack('>I',8)+data[12:]):
            with self.assertRaises(ValueError): parse_selectors(changed)

    def test_category_permutation_and_sentinel(self):
        data=struct.pack('>21I',*CATEGORIES)
        self.assertEqual(parse_categories(data,20)[4],8)
        for changed,count in ((bytes(84),20),(data,0),(data,40),(data[4:],20),
                              (data[:16]+data[20:24]+data[20:],20)):
            with self.assertRaises(ValueError): parse_categories(changed,count)

    def test_equal_early_exit_and_force_low_byte(self):
        for force in (0,0x100):
            p=self.plan(current=1,force=force,depth=0x7FFFFFFF)
            self.assertFalse(p['invoke']); self.assertEqual(p['writes'],[])
        for force in (1,0x101,255): self.assertTrue(self.plan(current=1,force=force)['invoke'])

    def test_category_not_selector_and_asymmetric_stage(self):
        p=self.plan(stage=3,selector=4)
        self.assertEqual(p['category'],8)
        self.assertEqual(p['cache_address'],0x10000+0x7DC+3*0x50+8*4)
        self.assertEqual(p['sdk_id'],12)
        self.assertNotEqual(p['cache_address'],self.plan(stage=4,selector=3)['cache_address'])

    def test_saved_frame_matches_original_push_exactly(self):
        for n in (1,2,3):
            p=self.plan(depth=n)
            f=0x10000+0xD2C+0x694*(n-1)
            self.assertEqual(p['frame_address'],f)
            self.assertEqual(p['baseline_address'],f+0x148+3*0x50+3*4)
            self.assertEqual(p['dirty_address'],f+0x654+3*4)

    def test_dirty_set_clear_preserves_unrelated_bits_and_write_order(self):
        p=self.plan(); self.assertEqual(p['writes'][0],(p['dirty_address'],0xA5000008))
        p=self.plan(baseline=1,dirty=0xA5000009)
        self.assertEqual(p['writes'][0][1],0xA5000001)
        self.assertEqual(p['writes'][1],(p['cache_address'],1))

    def test_nonpositive_signed_depth_has_only_current_write(self):
        for depth in (0,0x80000000,0xFFFFFFFF):
            p=self.plan(depth=depth); self.assertEqual(p['writes'],[(p['cache_address'],1)])

    def test_bounds_alignment_overflow_and_capacity(self):
        for change in ({'owner':0},{'owner':0x10001},{'owner':0xFFFFF000},
                       {'extent':0xD28},{'depth':0x7FFFFFFF},{'extent':0x1000,'depth':2},
                       {'stage':16},{'selector':21},{'value':1<<32},{'force':-1}):
            with self.assertRaises(ValueError): self.plan(**change)

    def test_all_twenty_app_categories_and_sixteen_stage_addresses(self):
        for s in range(16):
            addresses={self.plan(stage=s,selector=t,depth=0)['cache_address'] for t in range(1,21)}
            self.assertEqual(addresses,set(range(0x107DC+80*s,0x107DC+80*(s+1),4)))

    def test_current_host_subset_and_name_inversion(self):
        for stage in range(8):
            self.assertEqual(supported_host_update(stage,9,0),32)
            self.assertEqual(supported_host_update(stage,14,13),52)
        self.assertEqual(HOST_FIELDS[32],'MinimumMip')
        self.assertEqual(HOST_FIELDS[52],'MaximumMip')
        for t in (11,12,13,15,16,17,18,19,20):
            with self.assertRaises(ValueError): supported_host_update(0,t,0)

    def test_current_host_rejects_unsupported_even_equal_default(self):
        for s,t,v in ((8,1,0),(15,1,0),(0,20,1),(0,5,4),(0,7,3),
                      (0,8,0x80000000),(0,10,2),(0,14,12)):
            with self.assertRaises(ValueError): supported_host_update(s,t,v)

    def test_binding_asymmetric_mips_retains_constraints(self):
        self.assertEqual(binding_mips(2,7,0,13),(2,7))
        self.assertEqual(binding_mips(1,12,4,9),(4,9))
        self.assertEqual(binding_mips(2,7,9,1),(9,1)) # no invented interval clamp
        with self.assertRaises(ValueError): binding_mips(16,7,0,13)
        with self.assertRaises(ValueError): binding_mips(2,7,256,13)

    def test_original_pins(self):
        check_pins(self.image_bytes)

    def test_changed_instruction_and_table_rejected(self):
        for a in (0x82723D20,0x82CD2D78,0x821506E0):
            changed=bytearray(self.image_bytes); changed[a-BASE]^=1
            with self.assertRaises(ValueError): check_pins(changed)

    def test_disassembler_mismatched_word_rejected(self):
        with self.assertRaises(ValueError):
            checked_decode(self.image_bytes,0x82723C80,4,'82723C80 00000000 nop')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image',type=Path,default=ROOT/'analysis/simpsons.pe')
    parser.add_argument('--disasm',type=Path,default=ROOT/'build/generator-ninja/SimpsonsDisasm.exe')
    parser.add_argument('--reference',type=Path,default=Path('K:/Simpsons/RexGlueCurrent/include/rex/graphics/xenos.h'))
    parser.add_argument('--output',type=Path,help='Only analysis/native-application-samplers.json; otherwise stdout')
    parser.add_argument('--self-test',action='store_true')
    args=parser.parse_args()
    try:
        if args.output is not None and args.output.resolve()!=REPORT.resolve():
            raise ValueError('Output outside this task ownership')
        b=args.image.read_bytes(); validate_identity(b); check_pins(b)
        if args.self_test:
            ContractTests.image_bytes=b
            result=unittest.TextTestRunner(stream=sys.stderr,verbosity=2).run(
                unittest.defaultTestLoader.loadTestsFromTestCase(ContractTests))
            if not result.wasSuccessful(): return 1
        report=build_report(b,args.image.resolve(),args.disasm.resolve(),args.reference)
        output=json.dumps(report,indent=2,sort_keys=True)+'\n'
        if args.output:
            # Reject source aliases, including hardlinks, before the only write.
            inputs=(args.image,args.disasm,args.reference,SUPPORT,Path(__file__))
            if args.output.exists() and any(args.output.samefile(p) for p in inputs):
                raise ValueError('Report aliases a read-only input')
            args.output.write_text(output,encoding='utf-8',newline='\n')
            print(f'Wrote {args.output}; {report["validation"]["decoded_words"]} byte-checked words',file=sys.stderr)
        else:
            sys.stdout.buffer.write(output.encode('utf-8'))
        return 0
    except (OSError,ValueError,subprocess.CalledProcessError) as exc:
        print('error: '+str(exc),file=sys.stderr)
        return 1


if __name__=='__main__':
    raise SystemExit(main())
