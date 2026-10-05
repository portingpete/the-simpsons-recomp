"""Pinned offline transcription of projected immediate VS821513B8/PS82150C98."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_fourtap_shaders as four
import analyze_immediate_shaders as immediate

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'renderer/projected_immediate_shader.hlsl'
need=screen.require
PROFILES={
 'VS':(0x821513B8,824,460,300,4,'54d2ce2a05bed7e87a68b951c67901715a5daaa12830a58e2af120a4ca0ad3f3'),
 'PS':(0x82150C98,636,344,228,2,'38073f6a85020ac7e1364df305042cf3484c857d45ebdde3db6bff7cc82ca428'),
 'PSDual':(0x82150F18,700,384,252,2,'90fe8241c19a06883059c8c9396d94e8e5d9bfa0ce0f4ba98960c01228f9c0d8')}
CF={
 'VS':((0x70956004,0x1000),(0x200A,0x1200),(0x600C,0x5600),(0x1012,0x5600),
       (0,0xC200),(0x1013,0x1200),(0,0xC400),(0x4014,0x2200)),
 'PS':((0x1555002,0x1200),(0,0xC400),(0x6007,0x1200),(0x500D,0x2200)),
 'PSDual':((0x5556002,0x1200),(0,0xC400),(0x6008,0x1200),(0x600E,0x2200))}
TAPS=((.5,-.5),(-.5,-.5),(.5,.5),(-.5,.5))

def record(image,stage):
 address,size,offset,code_size,pairs,digest=PROFILES[stage]
 b=image[address-screen.BASE:address-screen.BASE+size]
 need(hashlib.sha256(b).hexdigest()==digest,'Projected '+stage+' record changed')
 header=screen.words(b,0,9)
 need(header[1]==offset and header[2]==code_size+64 and offset+header[2]==size,
      'Projected literal/instruction framing changed')
 literals=(0,)*12+((0x3F000000,0xBF000000,0,0) if stage=='VS' else (0x44800000,0xBF000000,0x3F800000,0))
 need(screen.words(b,offset,16)==literals,'Projected literal bank changed')
 code=b[offset+64:];cf=[];slots=[]
 for i in range(pairs):
  a,x,y=screen.words(code,i*12);cf.extend(((a,x&65535),((x>>16|y<<16)&0xFFFFFFFF,y>>16)))
 need(tuple(cf)==CF[stage],'Projected control flow changed')
 for lo,hi in cf:
  if hi>>12 in (1,2,5):
   start,count,sequence=lo&4095,(lo>>12)&7,(lo>>16)&4095
   need(0<count<=6 and sequence>>(2*count)==0,'Projected issue sequence changed')
   slots.extend((start+i,bool(sequence&(1<<(2*i)))) for i in range(count))
 need([i for i,_ in slots]==list(range(pairs,code_size//12-1)),
      'Projected schedule has uncovered or repeated instructions')
 rows={}
 for i,fetch in slots:
  raw=screen.words(code,12*i);f=screen.decode_fetch(raw) if fetch else edge.alu(*raw)
  if not fetch:
   need(all(not f[k] for k in ('absolute_constants','vector_destination_relative',
        'scalar_destination_relative_or_export_zero','constant_address_register_relative',
        'constant_0_relative','constant_1_relative')),'Projected ALU addressing changed')
   need(not any(s['relative_temporary'] for s in f['sources']),'Projected relative operand changed')
   expected_predicate=stage=='VS' and 12<=i<=18
   need(f['predicated']==expected_predicate and f['predicate_condition']==expected_predicate,
        'Projected instruction predicate changed')
  rows[i]={'raw':raw,'fetch':fetch,'fields':f}
 if stage=='VS':
  for i,destination,swizzle in ((4,1,[0,1,2,7]),(5,0,[7,0,7,7]),(6,2,[0,1,2,3])):
   need(rows[i]['fields']==dict(kind='vertex_fetch',source_register=0,destination_register=destination,
        destination_swizzle=swizzle,fetch_constant_index=95,source_component=0,format_field=0,
        stride_dwords=0,offset_field=0,runtime_declaration_patch_verified=False),'Projected input fetch changed')
  need(rows[7]['fields']['scalar_opcode']==30 and rigid.operand(rows[7]['fields'],2,'VS')=='vc[25].xxxx',
       'Projected c25 >= zero predicate changed')
 else:
  for i,(x,y) in enumerate(TAPS,3 if stage=='PSDual' else 2):
   f=rows[i]['fields']
   need(f['fetch_constant_index']==2 and f['offset_fields']==[int(x*2)&31,int(y*2)&31,0]
        and f['source_register']==1 and f['source_components']==[0,1,1]
        and f['normalized_coordinates'] and f['dimension_field']==1 and f['computed_lod']
        and not f['register_lod'] and not f['register_gradients']
        and [f[k] for k in ('mag_filter','min_filter','mip_filter','anisotropy','arbitrary_filter',
                           'volume_mag_filter','volume_min_filter')]==[3,3,3,7,0,3,3]
        and f['fetch_valid_only'] and f['sample_location']==0 and f['lod_bias_field']==0,
        'Projected shadow half-texel sample changed')
 return rows

def operand(f,index,stage):
 value=rigid.operand(f,index,stage)
 def constant(match):
  n=int(match[1]);need(n<5 or 21<=n<=25,'Unqualified projected constant')
  return ('immediateConstants['+str(n)+']') if n<5 else ('projectionConstants['+str(n-21)+']')
 return re.sub(r'vc\[(\d+)\]',constant,value)

def scalar(row,stage):
 if row['fields']['scalar_opcode']==30:return '(projectionConstants[4].x>=0.0)'
 value=rigid.scalar(row,stage)
 for n in range(26):
  value=value.replace('vc['+str(n)+']',('immediateConstants['+str(n)+']') if n<5 else ('projectionConstants['+str(n-21)+']'))
 return value

def issue(i,row,stage):
 f=row['fields'];lines=['    { // original '+stage+' slot '+str(i)]
 if f.get('predicated'):lines+=['        if(p0) {']
 if row['fetch']:
  need(stage!='VS','VS fetches are the reviewed input interface')
  if f['fetch_constant_index']==2:
   x,y=TAPS[i-(3 if stage=='PSDual' else 2)]
   lines+=['        precise float sampleDepth=projectionDepth.Sample(projectionSampler,r1.xy+float2('+str(x)+','+str(y)+')/1024.0);',
           '        precise float4 v=sampleDepth.xxxx;']
  else:
   bank=f['fetch_constant_index'];components=[0,1] if bank==0 else [2,3]
   need(bank in (0,1) and f['source_components'][:2]==components
        and f['offset_fields']==[0,0,0] and f['destination_swizzle']==[0,1,2,3],
        'Projected base texture fetch changed')
   lines+=['        precise float4 v='+('immediateTexture.Sample(immediateSampler,r0.xy)' if bank==0 else
        'immediateTexture1.Sample(immediateSampler1,r0.zw)')+';']
  for lane,selector in enumerate(f['destination_swizzle']):
   if selector!=7:
    need(selector<4,'Projected fetch literal is unqualified')
    lines+=['        r'+str(f['destination_register'])+'.'+'xyzw'[lane]+'=v.'+'xyzw'[selector]+';']
 else:
  op,mask,sop,sm=f['vector_opcode'],f['vector_mask'],f['scalar_opcode'],f['scalar_mask']
  if mask:
   a,b,c=[operand(f,j,stage) for j in range(3)]
   expressions={0:lambda:a+'+'+b,1:lambda:a+'*'+b,2:lambda:'max('+a+','+b+')',
        5:lambda:'float4('+a+'>'+b+')',6:lambda:'float4('+a+'>='+b+')',11:lambda:a+'*'+b+'+'+c}
   need(op in expressions,'Projected vector opcode unqualified')
   expr=expressions[op]()
   lines+=['        precise float4 v='+('saturate('+expr+')' if f['vector_clamp'] else expr)+';']
  if sop!=50:
   expr=scalar(row,stage)
   if sop==30:lines+=['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;']
   else:lines+=['        precise float s='+('saturate('+expr+')' if f['scalar_clamp'] else expr)+';']
  if mask:
   target=('output'+str(f['vector_destination']) if f['export'] else 'r'+str(f['vector_destination']))
   sw=''.join('xyzw'[lane] for lane in range(4) if mask&(1<<lane))
   lines+=['        '+target+'.'+sw+'=v.'+sw+';']
  if sm:
   need(sop!=50,'Projected scalar result absent')
   target=('output'+str(f['scalar_destination']) if f['export'] else 'r'+str(f['scalar_destination']))
   sw=''.join('xyzw'[lane] for lane in range(4) if sm&(1<<lane))
   lines+=['        '+target+'.'+sw+'=s.'+'x'*len(sw)+';']
  if sop!=50:lines+=['        ps=s;']
  if sop==30:lines+=['        p0=predicate;']
 if f.get('predicated'):lines+=['        }']
 return '\n'.join(lines+['    }'])

def shader_source(image):
 vr=record(image,'VS')
 lines=['// Exact projected immediate VS821513B8 / PS82150C98.',
 '// Offline fixed-slot transcription; co-issued instructions read old registers.',
 '// The shadow descriptor is D24FS8 with RRRR format expansion before swizzling.',
 '// Half-texel offsets are signed 5-bit fields in units of 0.5 texel.',
 'cbuffer ProjectedImmediateConstants:register(b3) {float4 projectionConstants[5];};',
 'Texture2D<float> projectionDepth:register(t2);',
 'SamplerState projectionSampler:register(s2);',
 'struct ProjectedImmediateOutput {float4 position:SV_Position;float4 uv:TEXCOORD0;float4 projection:TEXCOORD1;float4 color:TEXCOORD2;};',
 'ProjectedImmediateOutput VSImmediateProjected(ImmediateInput input) {',
 '    float4 r0=float4(0,input.alpha,0,0),r1=float4(input.position,0),r2=input.uv,r3=0,r4=0;',
 '    float4 output62=0,output0=0,output1=0,output2=0;',
 '    const float4 k255=float4(0.5,-0.5,0,0);',
 '    float ps=0;bool p0=false;']
 lines.extend(issue(i,row,'VS') for i,row in vr.items() if not row['fetch'])
 lines+=['    ProjectedImmediateOutput output;output.position=output62;output.uv=output0;',
         '    output.projection=output1;output.color=output2;return output;','}']
 for stage,name in (('PS','PSImmediateProjected'),('PSDual','PSImmediateProjectedDual')):
  lines+=['float4 '+name+'(ProjectedImmediateOutput input):SV_Target0 {',
          '    float4 r0=input.uv,r1=input.projection,r2=input.color,r3=0,r4=0,r5=0,r6=0;',
          '    float4 output0=0;float ps=0;',
          '    const float4 k255=float4(1024,-0.5,1,0);']
  lines.extend(issue(i,row,stage) for i,row in record(image,stage).items())
  lines+=['    return output0;','}']
 return '\n'.join(lines+[''])

def source_equivalence(image,source):
 compact=lambda s:re.sub(r'\s+','',re.sub(r'//[^\n]*','',s))
 need(compact(source)==compact(shader_source(image)),'Projected HLSL differs from pinned per-slot transcription')

def inspect(image):
 need(hashlib.sha256(four.UCODE.read_bytes()).hexdigest()==four.UCODE_SHA,'Pinned instruction reference changed')
 reference=four.UCODE.read_text(encoding='utf-8')
 need('kSetpGe = 30' in reference and 'return data_.offset_x * 0.5f;' in reference,
      'Projected predicate/half-texel reference changed')
 need(screen.words(image,0x821516F0-screen.BASE,12)==immediate.DECLARATION,'Projected immediate declaration changed')
 source_equivalence(image,SOURCE.read_text(encoding='utf-8'))
 return dict(records={stage:dict(address=hex(p[0]),bytes=p[1],sha256=p[5],literal_bytes=64,
    instruction_bytes=p[3],slots=list(record(image,stage))) for stage,p in PROFILES.items()},
    depth_contract=rigid.depth_contract(image),shadow_taps=TAPS,source='Full per-slot HLSL transcription verified',
    scope='Finite inputs and nonzero projected W; original 1024-square shadow owner; no runtime translation.')

def self_test(image):
 checks=0
 def rejects(function,*args):
  nonlocal checks
  try:function(*args)
  except (ValueError,struct.error,IndexError):checks+=1;return
  raise ValueError('Accepted a mutated projected contract')
 mutable=bytearray(image)
 for stage,(a,n,*_) in PROFILES.items():
  for offset in range(0,n,4):
   at=a-screen.BASE+offset;mutable[at]^=1;rejects(record,mutable,stage);mutable[at]^=1
 source=SOURCE.read_text(encoding='utf-8')
 for old,new in (('projectionConstants[4].x>=0.0','projectionConstants[4].x>0.0'),
                 ('float2(0.5,-0.5)','float2(-0.5,0.5)'),('sampleDepth.xxxx','float4(sampleDepth,0,0,1)'),
                 ('r0.xxxx>=r4.xyzw','r0.xxxx>r4.xyzw'),('ps=s;','ps=0;'),
                 ('projectionConstants[0].zwxy','projectionConstants[0].xyzw')):
  need(old in source,'Stale projected mutation fixture '+old);rejects(source_equivalence,image,source.replace(old,new))
 return checks

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--write',action='store_true',help='Write the reviewed static transcription')
 parser.add_argument('--self-test',action='store_true')
 args=parser.parse_args();image=(ROOT/'analysis/simpsons.pe').read_bytes()
 if args.write:SOURCE.write_text(shader_source(image),encoding='utf-8')
 report=inspect(image)
 if args.self_test:report['mutation_checks']=self_test(image)
 print(json.dumps(report))
