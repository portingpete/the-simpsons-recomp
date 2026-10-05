"""Offline qualification and static HLSL transcription of the original mono pair."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_fourtap_shaders as four

ROOT=Path(__file__).resolve().parents[1]
need=screen.require
sha=lambda b:hashlib.sha256(b).hexdigest()
FX=0x8211F480
FX_HASH='24c7dc906da91b4b89cf0f59dd034c9584005dda68205d13753455cb9c3c68ca'
RECORDS=((0x82120C04,4060,'7a354196f8a2313951c7b97c426a502944957738d91e9d7d466a1d306ecbc3be'),
         (0x82121BE8,4060,'25c01d904a39cf8e72c1533e26658df86a63eac68dc6fb26e2c3e5d5954315c0'),
         (0x82122BD4,348,'40fb46979a9b30162836af56e04753584af1b743cdc4196c269cd5133e5517fe'),
         (0x82122D38,348,'2ea29bd5b34f0ea4bfd0c1f34f77cb20f23f12c6eeaba42edd8f022725948646'))
CF=((0x70954005,0x1200),(6,0xB000),(0x6009,0x1200),(0x600F,0x1200),
    (0x6015,0x1200),(0x201B,0x1200),(0,0xC200),(0x501D,0x1200),(0,0xC400),(5,0x2200))
CPU=((0x8273A878,0xF0,'3849c12b0f0ae1ed74c10b9b03725e441257b8f33dba399ef4f52c5da33c0522'),
     (0x82740680,0x56C,'be51c268553e3962c9c7f32fc53a358053de4b9c77cd9d28aed5b27f4b7463b4'),
     (0x826B5770,0x138,'2746714a327fbb552f4ff1951d7669a8c6d2852a2d13e2399c6f9b40fb7f4a45'))

def words(b,at=0,n=3):return struct.unpack_from('>'+str(n)+'I',b,at)

def program(vs,ps):
    need(len(vs)==420 and sha(vs[:408])=='6017885270f844d8f93e0510f8ee24c55dc08b0eb0e82a7be6fd460efeff48af','Mono VS executable differs')
    cf=[]
    for at in range(0,60,12):
        a,b,c=words(vs,at);cf.extend(((a,b&65535),(((b>>16)|(c<<16))&0xffffffff,c>>16)))
    need(tuple(cf)==CF,'Mono control flow differs')
    # CF1 jumps to CF6 iff Boolean b0 is false. No predication, loop or morph.
    need((cf[1][0]&0x6000)==0 and ((cf[1][1]>>2)&255)==0 and not(cf[1][1]&1024),'Mono Boolean jump differs')
    schedule=[]
    for lo,hi in cf:
        if hi>>12 in (1,2):
            start,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
            need(count<=6 and not(seq>>(2*count)),'Mono issue extent differs')
            schedule.extend((start+i,bool(seq&(1<<(2*i)))) for i in range(count))
    need([i for i,_ in schedule]==list(range(5,34)) and [i for i,f in schedule if f]==[5,6,7],'Mono issue schedule differs')
    rows={}
    for i,fetch in schedule:
        w=words(vs,12*i);f=screen.decode_fetch(w) if fetch else edge.alu(*w);rows[i]=f
        if fetch:
            need(f['source_register']==0 and f['destination_register']==i-5 and f['fetch_constant_index']==95 and
                 f['destination_swizzle']==([7,0,1,2] if i==5 else [0,1,2,3] if i==6 else [3,2,1,0]) and
                 not any(f[k] for k in ('source_component','format_field','stride_dwords','offset_field')),'Mono FETCH differs')
        else:
            need(not any(f[k] for k in ('vector_clamp','scalar_clamp','absolute_constants','vector_destination_relative',
                 'scalar_destination_relative_or_export_zero','predicated','predicate_condition','constant_1_relative')),'Mono ALU modifier differs')
            dynamic=i in set(range(12,18))|set(range(19,22))|set(range(23,26))
            need(f['constant_address_register_relative']==dynamic and f['constant_0_relative']==dynamic and not f['scalar_mask'],'Mono address or scalar write differs')
            need(f['export']==(i>=30),'Mono export extent differs')
            for s in f['sources']:need(not any(s[k] for k in ('absolute_temporary','relative_temporary','negated')),'Mono operand modifier differs')
    need([i for i in rows if rows[i].get('scalar_opcode')==23]==[11,14,18,22],'Mono MOVA order differs')
    need(len(ps)==36 and ps[:24]==bytes.fromhex('000000001001C4002200000014FEC0000000006CC20000FF'),'Mono PS code differs')
    p=edge.alu(*words(ps,12))
    need(p['export'] and p['vector_destination']==0 and p['vector_mask']==14 and p['scalar_mask']==15 and
         p['scalar_opcode']==5 and p['sources'][2]['bank']=='constant' and p['sources'][2]['register']==255 and
         p['sources'][2]['components']==[0,0,0,0],'Mono PS export differs')
    # Export masks overlap on YZW => literal1. X selects MAXs(c255.x,c255.x)
    # => literal1 as well. No vector operation result or interpolator is read.
    return rows

def constant_maps(body):
    u=lambda at:words(body,at,1)[0]
    need([u(p) for p in (0x120,0x124,0x130,0x134,0x138,0x13C)]==[2,1,75,4,4368,112],'Mono storage shape differs')
    for ctx,vertex,pixel,end in ((0x45E0,0x1770,0x3740,0x4BF0),(0x4BF0,0x2754,0x38A4,0x5200)):
        need(words(body,ctx+0x48)==(vertex,pixel,end),'Mono pass association differs')
        for space,leaves,mask_bytes in ((0,75,16),(1,4,8)):
            for category in range(8):
                active=([0] if space else list(range(11,75))) if category==0 else [10] if not space and category==4 else []
                mask=bytearray(mask_bytes)
                for leaf in active:mask[leaf//8]|=0x80>>(leaf%8)
                at=u(ctx+space*32+category*4)
                need(body[at:at+mask_bytes]==mask,'Mono upload mask differs')
            rows=u(ctx+0x40+4*space)
            for leaf in range(leaves):
                want=(0,0,0,0)
                if space and leaf==0:want=(0x40001,0xC00,0,0)
                elif not space and leaf==10:want=(0x300014,0,0,0)
                elif not space and leaf>=11:want=(((leaf+3)<<18)|(leaf<<1),0x800+52+3*(leaf-11),0,0)
                need(words(body,rows+16*leaf,4)==want,'Mono constant mapping differs')
    return {'shared':'g_ViewProjection pool slots0..3 -> c0..3',
            'private':'kIsSkinned slot16.x -> b0; bone slots17+4*i -> c52+3*i (64 bones)',
            'unused':'World and morph storage do not map directly to this shader.'}

def hlsl(rows):
    def operand(f,i):
        s=f['sources'][i];name='r'+str(s['register']) if s['bank']=='temporary' else f"c[{s['register']}{'+a0' if f['constant_address_register_relative'] else ''}]"
        swizzle=''.join('xyzw'[v] for v in s['components']);return name+('' if swizzle=='xyzw' else '.'+swizzle)
    def vector(i):
        f=rows[i];mask=f['vector_mask'];dest='r'+str(f['vector_destination'])
        if f['export']:dest='result.position.'+'xyzw'[i-30]
        elif mask!=15:dest+='.'+''.join('xyzw'[j] for j in range(4) if mask&(1<<j))
        op=f['vector_opcode'];a,b=operand(f,0),operand(f,1)
        if op==1:expr=a+'*'+b
        elif op==11:expr='mad('+a+','+b+','+operand(f,2)+')'
        elif op==15:expr='dot('+a+','+b+')'
        else:raise ValueError('Unsupported mono transcription operation')
        return dest+'='+expr+';'
    lines=['// Offline transcription of original VS82120C04/82121BE8 and PS82122BD4/82122D38.',
        'cbuffer MonoConstants : register(b0) { float4 c[244]; };',
        'cbuffer MonoBooleans : register(b1) { uint4 monoBooleans; };',
        'struct MonoInput { float3 position:TEXCOORD0; float4 weights:TEXCOORD1; float4 indices:TEXCOORD2; };',
        'struct MonoOutput { float4 position:SV_Position; };',
        'MonoOutput VSMono(MonoInput input) {',
        'precise float4 r0=float4(0,input.position),r1=input.weights,r2=input.indices.wzyx,r3=0,r4=0,r5=0;',
        '[branch] if(monoBooleans.x!=0) {','r3=float4(r0.w,r0.y,r0.z,1);','r5=r2*3;']
    for i in range(11,29):
        f=rows[i]
        if f['vector_mask']:lines.append(vector(i))
        if f['scalar_opcode']==23:
            component='xyzw'[f['sources'][2]['components'][0]]
            lines.append(('int ' if i==11 else '')+f'a0=int(clamp(floor(r5.{component}+0.5),-256.0,255.0));')
    lines+=['}','r0=float4(r0.w,r0.y,r0.z,1);','MonoOutput result;']+[vector(i) for i in range(30,34)]+['return result;','}',
        'float4 PSMono(MonoOutput input):SV_Target { return float4(1,1,1,1); }',
        '[maxvertexcount(1)] void GSMonoProbe(point MonoOutput input[1],inout PointStream<MonoOutput> stream) { stream.Append(input[0]); }',
        'MonoOutput VSMonoPixelProbe(uint id:SV_VertexID) { MonoOutput o; o.position=float4(id==2?3:-1,id==1?3:-1,0.5,1);return o; }']
    return '\n'.join(lines)+'\n'

def inspect(image):
    need(len(image)==screen.IMAGE_SIZE and sha(image)==screen.IMAGE_SHA256,'Original image identity differs')
    take=lambda a,n:image[a-0x82000000:a-0x82000000+n]
    need(sha(four.UCODE.read_bytes())==four.UCODE_SHA and sha(four.XENOS.read_bytes())==four.XENOS_SHA,'Instruction reference changed')
    need(sha(take(FX,0x5310))==FX_HASH,'Mono effect differs')
    for a,n,h in CPU:need(sha(take(a,n))==h,'Mono CPU span differs')
    data=[]
    for i,(a,n,h) in enumerate(RECORDS):
        b=take(a,n);need(sha(b)==h,'Mono shader record differs');offset=3576 if i<2 else 248
        need(words(b,4,2)==(offset,n-offset),'Mono shader extent differs')
        literal=(0,)*13+(0x3F800000,0x40400000,0) if i<2 else (0,)*12+(0x3F800000,0,0,0)
        need(words(b,offset,16)==literal,'Mono literals differ')
        if i<2:need(words(b,3564)==(0x00100005,0x00001006,0x00202007),'Mono FETCH semantic map differs')
        data.append(b[offset+64:])
    need(data[0][:-12]==data[1][:-12] and data[2][:-12]==data[3][:-12],'Mono passes have different executable code')
    rows=program(data[0],data[2]);maps=constant_maps(take(FX+12,0x5310-12));source=hlsl(rows)
    return {'effect_sha256':FX_HASH,'records':RECORDS,'cpu_spans':CPU,'mapping':maps,
            'vs_issues':29,'pixel_output':[1,1,1,1],'hlsl_sha256':sha(source.encode()),'runtime_execution':False},source

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--hlsl',type=Path);p.add_argument('--verify',action='store_true');p.add_argument('--self-test',action='store_true');a=p.parse_args()
    image=(ROOT/'analysis/simpsons.pe').read_bytes();report,source=inspect(image)
    if a.self_test:
        for at in (0x120C04,0x121BE8,0x122BD4,0x122D38,0x11F48C+0x4700,0x73A938):
            changed=bytearray(image);changed[at]^=1
            try:inspect(changed)
            except ValueError:
                pass  # Expected rejection of mutated source.
            else:raise RuntimeError('Changed source admitted')
        # Independent program checks are exercised without the outer image hash.
        for offset in (0,12,60,168,360):
            vs=bytearray(image[0x120C04+3640:0x120C04+4060]);vs[offset]^=1
            try:program(vs,image[0x122BD4+312:0x122BD4+348])
            except ValueError:
                pass  # Expected rejection of mutated program.
            else:raise RuntimeError('Changed program admitted')
        print('PASS mono source, schedule, constant mapping and mutation checks')
    if a.hlsl:a.hlsl.parent.mkdir(parents=True,exist_ok=True);a.hlsl.write_text(source, encoding="utf-8")
    if not a.verify:print(json.dumps(report,indent=2))
if __name__=='__main__':main()
