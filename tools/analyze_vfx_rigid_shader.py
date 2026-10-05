"""Verify and transcribe the retail rigid mesh-particle shader pair offline."""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
import analyze_168f8alpha_shader as alpha
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_screen_shaders as screen

ROOT = Path(__file__).resolve().parents[1]
PROFILES = (
    ('VS', 0x8205BE70, 640, 472, 168, 3, '1de5fdaab7ad07301797211b54f8d7e246ff0300690d3d6bf140d6ce89ad404c'),
    ('PS', 0x8205C100, 408, 336, 72, 2, '7e56a8c918c783d4e16150fec4e4e1b4b1dda4d35da16c539bec9fea4865194d'),
)
CONTROL = {
    'VS': [(0x70153003,0x1200),(0,0xC200),(0x4006,0x1200),(0,0xC400),(0x300A,0x2200),(0,0)],
    'PS': [(0x11002,0x1200),(0,0xC400),(0x2003,0x2200),(0,0)],
}

def inspect(image):
    screen.require(len(image)==screen.IMAGE_SIZE and hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,
                   'Original image changed')
    effect=image[0x5B848:0x5B848+0x1A90]
    screen.require(hashlib.sha256(effect).hexdigest()=='968b970886a3c0989b7fd0afd4b5afd1fe23a04c92423c79d04af58b9d078261',
                   'Original mesh-particle effect changed')
    result={}
    for stage,va,size,start,length,pairs,digest in PROFILES:
        record=image[va-screen.BASE:va-screen.BASE+size]
        screen.require(hashlib.sha256(record).hexdigest()==digest,stage+' record changed')
        code=record[start:start+length]
        control,schedule=alpha.schedule(code,pairs)
        screen.require(control==CONTROL[stage],stage+' control flow changed')
        rows={}
        for slot,fetch,select in schedule:
            screen.require(not select,'Unexpected issue selection')
            raw=screen.words(code,12*slot)
            fields=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
            if not fetch:
                for key in ('absolute_constants','vector_destination_relative','scalar_destination_relative_or_export_zero',
                            'predicated','predicate_condition','constant_address_register_relative','constant_0_relative','constant_1_relative'):
                    screen.require(not fields[key],'Unsupported ALU modifier '+key)
                screen.require(not any(s['relative_temporary'] for s in fields['sources']),'Relative temporary')
            rows[slot]=dict(fetch=fetch,raw=raw,fields=fields)
        result[stage]=dict(address=f'{va:08X}',sha256=digest,control=control,rows=rows)
    for slot,dest,swizzle in ((3,2,[0,1,2,5]),(4,1,[0,1,2,3]),(5,0,[0,1,5,7])):
        f=result['VS']['rows'][slot]['fields']
        screen.require(f['kind']=='vertex_fetch' and f['destination_register']==dest and f['destination_swizzle']==swizzle,
                       'Vertex fetch wiring changed')
    f=result['PS']['rows'][2]['fields']
    screen.require(f['source_register']==0 and f['destination_register']==0 and f['destination_swizzle']==[0,1,2,3],
                   'Particle texture wiring changed')
    alpha.a168_sample(f)
    return result

def shader_source(image):
    inv=inspect(image)
    lines=['// Retail VS8205BE70 / PS8205C100; generated from checked instruction records.',
        'cbuffer VfxVertexConstants : register(b0) { float4 vc[48]; };',
        'cbuffer VfxPixelConstants : register(b0) { float4 pc[51]; };',
        'Texture2D<float4> vfxBase : register(t0);', 'SamplerState vfxSampler : register(s0);',
        'float rigidLegacyProduct(float a,float b) {', '    precise float product=a*b;',
        '    return ((asuint(a)&0x7F800000u)==0u||(asuint(b)&0x7F800000u)==0u)?0.0:product;', '}',
        'struct VfxRigidInput { float3 position:TEXCOORD0; float4 color:TEXCOORD2; float2 uv:TEXCOORD3; };',
        'struct VfxRigidOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; float4 color:TEXCOORD1; };',
        'VfxRigidOutput VSVfxRigid(VfxRigidInput input) {',
        '    precise float4 r0=float4(input.uv,1,0),r1=input.color,r2=float4(input.position,1);',
        '    precise float4 output62=0,output0=0,output1=0;']
    lines += [rigid.issue(i,inv['VS']['rows'][i],'VS') for i in range(6,13)]
    lines += ['    VfxRigidOutput result;result.position=output62;result.uv=output0.xy;result.color=output1;return result;', '}',
        'float4 PSVfxRigid(VfxRigidOutput input):SV_Target0 {',
        '    precise float4 r0=vfxBase.Sample(vfxSampler,input.uv),r1=input.color,output0=0;',
        rigid.issue(3,inv['PS']['rows'][3],'PS')]
    row=copy.deepcopy(inv['PS']['rows'][4]);f=row['fields']
    screen.require(f['export'] and f['vector_destination']==0 and f['scalar_destination']==0 and
                   f['vector_mask']==7 and f['scalar_mask']==8 and f['scalar_opcode']==43,
                   'Particle co-issued color/alpha export changed')
    # Co-issued RGB and alpha both read the old registers, and neither writes
    # a source used by the other. Emit both exports explicitly.
    a,b=(rigid.operand(f,i,'PS') for i in (0,1))
    lines += ['    precise float4 rgb='+a+'*'+b+';',
              '    precise float alpha='+rigid.scalar(row,'PS')+';',
              '    return float4(rgb.xyz,alpha);','}']
    return '\n'.join(lines)+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emit-hlsl',type=Path);p.add_argument('--verify-hlsl',type=Path);p.add_argument('--output',type=Path)
    args=p.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes()
    source=shader_source(image)
    if args.emit_hlsl:args.emit_hlsl.write_text(source,encoding='utf-8')
    if args.verify_hlsl:screen.require(args.verify_hlsl.read_text(encoding='utf-8')==source,'Particle HLSL differs from original instruction transcription')
    if args.output:args.output.write_text(json.dumps(inspect(image),indent=2)+'\n',encoding='utf-8')
    print('PASS original mesh-particle shader: 3 vertex fetches, 7 vertex ALU slots, one t0 sample, exact RGB/alpha export')
if __name__=='__main__':main()
