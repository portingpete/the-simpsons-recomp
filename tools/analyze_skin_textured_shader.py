"""Hash-pinned original skin_textured opaque/alpha microcode transcription."""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
import analyze_screen_shaders as screen
import analyze_edge_shaders as edge
import analyze_rigid_shader as rigid
import analyze_skin_shader as skin
import analyze_168f8alpha_shader as a168
from skin_shader_emit import vs_issue
ROOT=Path(__file__).resolve().parents[1]
PROFILES=(
 ('VS',0x8201146C,4728,3864,864,8,'84e47d01c53b025a659e72ccf1186256396e4a9d7c198eff4b80131b9fcc99d4'),
 ('VSA',0x820126EC,4612,3808,804,7,'d7054b5b4139dba71f267a0664ca711c197ab13689b5560a86e12520ae2eb99b'),
 ('PS',0x82013900,1668,852,816,9,'6ffb653f2005d9fbc1aa003db0f6344297a517df309a5904130dd7cd80452287'),
 ('PSA',0x82013F8C,844,592,252,3,'eb52247fbcf636a924adb4a5c6305af361738a6cab555f9e9d3be2ba34b3c5b9'))
HEADERS={
 'VS':(0x102A1101,3800,928,36,124,3612,3652,0,0),
 'VSA':(0x102A1101,3744,868,36,124,3564,3604,0,0),
 'PS':(0x102A1100,788,880,36,124,696,736,0,0),
 'PSA':(0x102A1100,528,316,36,124,440,480,0,0)}
CF={
 'VS':((0xF5556008,0x1203),(0xF555600E,0x1203),(0x22014,0x1000),(0x4005,0xB000),(0x6016,0x1200),(0,0xC200),(0x601C,0x1200),(0x6022,0x1200),(0x6028,0x1200),(0x602E,0x1200),(0,0xC400),(0x6034,0x1200),(0x603A,0x1200),(0x6040,0x1200),(0x1046,0x2200),(0,0)),
 'VSA':skin.CF['VS'],
 'PS':((0x243009,0x1000),(0x4003,0xB000),(0x200C,0x1200),(0x600E,0x1200),(0x6014,0x1200),(0x601A,0x1200),(0x5020,0x1000),(0x400F,0xB000),(0x4025,0x1000),(0x400E,0xB000),(0x5556029,0x1200),(0x95602F,0x1200),(0x6035,0x1200),(0x203B,0x1200),(0x203D,0x1200),(0,0xC400),(0x403F,0x2200),(0,0)),
 'PSA':((0x243003,0x1000),(0x4003,0xB000),(0x2006,0x1200),(0,0xC400),(0x6008,0x1200),(0x600E,0x2200))}
LITERALS={'VS':skin.VS_LITERALS,'VSA':skin.VS_LITERALS,
 'PS':(0x3F333333,0x3E4CCCCD,0x3E99999A,0x3A802008,0,0x38D1B717,0x3F800000,0x44800000,0x3E800000,0x3F75C28F,0x3DCCCCCD,0x3F666666,0x3E800000,0x3F000000,0x42000000,0xBF000000),
 'PSA':(0,)*12+(0x3F800000,0,0x38D1B717,0)}
SEMANTIC_IDS=(0x00100000,0x00003000,0x00001000,0x00002000,0x0000A000,0x00005000,0x00010000,0x00020000,0x00030000,0x00040000,0x00050000,0x00260000)
FETCHES=((5,(0,1,2,7)),(8,(0,1,2,7)),(9,(0,1,2,3)),(4,(3,2,1,0)),(1,(0,1,2,3)),(11,(0,1,7,7)),(10,(0,1,2,7)),(3,(0,1,2,7)),(2,(0,1,2,7)),(0,(7,0,1,2)),(6,(0,1,2,7)),(7,(0,1,2,7)))

def decode_record(record,profile):
 stage,_,size,start,length,pairs,digest=profile
 screen.require(len(record)==size and hashlib.sha256(record).hexdigest()==digest,'Textured skin record extent/hash changed')
 screen.require(struct.unpack_from('>9I',record)==HEADERS[stage],'Textured skin header changed')
 screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6])==(64,length),'Textured skin code bounds changed')
 screen.require(struct.unpack_from('>16I',record,HEADERS[stage][1])==LITERALS[stage],'Textured skin literals changed')
 screen.require(record[-12:].hex()=={'VS':'4e4a0003','VSA':'4e4a0001','PS':'4e4a0002','PSA':'4e4a0000'}[stage]+'87ba0635f8a0dabb','Textured skin trailer changed')
 if stage.startswith('VS'):
  semantics=struct.unpack_from('>12I',record,HEADERS[stage][6]+40)
  screen.require(semantics==tuple(v|pairs+i for i,v in enumerate(SEMANTIC_IDS)),'Textured skin twelve input semantics changed')
 code=record[start:start+length];control=[]
 for i in range(pairs):
  a,b,c=screen.words(code,12*i);control.extend(((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)))
 screen.require(tuple(control)==CF[stage],'Textured skin control flow changed')
 rows={}
 for ci,(lo,hi)in enumerate(control):
  if hi>>12 not in(1,2):continue
  first,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
  screen.require(0<count<=6 and seq>>(2*count)==0,'Textured skin issue sequence changed')
  for n in range(count):
   slot=first+n;screen.require(slot not in rows,'Repeated textured skin slot')
   raw=screen.words(code,slot*12);fetch=bool(seq&(1<<(2*n)))
   rows[slot]=dict(raw=raw,fetch=fetch,control_index=ci,fields=screen.decode_fetch(raw)if fetch else edge.alu(*raw))
 screen.require(sorted(rows)==list(range(pairs,length//12-1)),'Incomplete textured skin issued slot coverage')
 if stage.startswith('VS'):
  screen.require([n for n,r in rows.items()if r['fetch']]==list(range(pairs,pairs+12)),'Textured skin fetch count changed')
  for slot,(dest,swizzle)in zip(range(pairs,pairs+12),FETCHES):
   f=rows[slot]['fields'];screen.require(f['kind']=='vertex_fetch'and f['fetch_constant_index']==95 and f['destination_register']==dest and tuple(f['destination_swizzle'])==swizzle,'Textured skin fetch wiring changed')
 return rows

def inspect(image):
 screen.require(hashlib.sha256(image[0xFB98:0xFB98+28000]).hexdigest()=='62f63d3eba363db31493a6898639e6de3465d2782582d186fb90d0d9b2e12a1f','Original textured skin effect changed')
 result={p[0]:decode_record(image[p[1]-screen.BASE:p[1]-screen.BASE+p[2]],p)for p in PROFILES}
 base=image[0x7C1C:0x7C1C+4604];alpha=image[0x126EC:0x126EC+4612]
 screen.require(alpha[3744:3808]==base[3736:3800]and alpha[3808:-12]==base[3800:-12],'Textured alpha VS literal/executable equivalence changed')
 screen.require(alpha[3644:3692]==base[3636:3684],'Textured alpha VS original semantic associations differ')
 return result

def ps_issue(slot,row,stage):
 f=row['fields']
 if row['fetch']:
  screen.require(f['kind']=='texture_fetch'and f['normalized_coordinates']and f['computed_lod']and not f['register_lod']and not f['register_gradients']and f['dimension_field']==1 and f['fetch_valid_only']and not f['sample_location']and not f['lod_bias_field']and f['mag_filter']==f['min_filter']==f['mip_filter']==3 and f['anisotropy']==7,'Textured skin sample contract changed')
  base=(stage=='PSA'or slot==10)
  screen.require(f['fetch_constant_index']==(0 if stage=='PSA'or not base else 1),'Textured skin resource stage changed')
  comps=f['source_components'][:2];offs=[((x+16)%32-16)//2 for x in f['offset_fields']]
  screen.require(all(x%2==0 for x in f['offset_fields']),'Fractional textured skin sample offset')
  if base:screen.require(comps==[0,1]and offs==[0,0,0],'Textured skin base coordinates changed')
  else:screen.require(41<=slot<=49 and comps==[1,2],'Textured skin shadow coordinates changed')
  coord='r'+str(f['source_register'])+'.'+''.join('xyzw'[i]for i in comps)
  sample=('skinTexturedAlphaBase.Sample(skinTexturedAlphaSampler,'+coord+')')if stage=='PSA'else ('skinTexturedBase.Sample(skinTexturedSampler,'+coord+')')if base else ('rigidShadowSample(shadow0.Sample(shadowSampler0,'+coord+',int2('+','.join(str(x)for x in offs[:2])+')))')
  lines=[f'    {{ // slot{slot}',f'        precise float4 v={sample};']
  for lane,sel in enumerate(f['destination_swizzle']):
   if sel!=7:screen.require(sel<4,'Unqualified textured skin fetch literal');lines.append(f'        r{f["destination_register"]}.{"xyzw"[lane]}=v.{"xyzw"[sel]};')
  return '\n'.join(lines+['    }'])
 screen.require(not any(f[k]for k in('constant_address_register_relative','constant_0_relative','constant_1_relative','vector_destination_relative','scalar_destination_relative_or_export_zero','absolute_constants')),'Unqualified textured skin PS modifiers')
 if f['vector_opcode']==25:
  screen.require(not f['vector_mask']and f['scalar_opcode']==50 and not f['scalar_mask'],'Textured skin kill shape changed')
  return f'    {{ // slot{slot}: KILLGT side effect\n        if(any({rigid.operand(f,0,"PS")}>{rigid.operand(f,1,"PS")})) discard;\n    }}'
 # Generic emitter uses one original rigid-specific slot15 check. This new
 # program has independently pinned issue numbers, so avoid that specialization.
 changed=row
 if f['scalar_opcode']==6:changed={**row,'fields':{**f,'scalar_opcode':5}}
 if f['sources'][2]['negated']and f['sources'][2]['bank']=='constant'and f['scalar_opcode']in(42,43,44,46,47):
  text=a168.a168_split_issue(slot,row,'PS')
 else:text=rigid.issue(slot+1000,changed,'PS').replace('// slot'+str(slot+1000),'// slot'+str(slot))
 if f['scalar_opcode']==6:
  expression=rigid.scalar(changed,'PS');text=text.replace(expression,expression.replace('max(','min(',1))
 if f['vector_mask']and f['vector_opcode']==1:
  a,b=[rigid.operand(f,i,'PS')for i in range(2)];text=text.replace(a+'*'+b,'rigidLegacyMultiply('+a+','+b+')')
 if f['scalar_opcode']==42:
  expression=rigid.scalar(row,'PS');a,b=expression.split('*');text=text.replace(expression,f'rigidLegacyProduct({a},{b})')
 if f['predicated']:
  screen.require(stage=='PS'and slot==38 and f['predicate_condition'],'Textured skin ALU predicate changed')
  text='    [branch] if(p0) {\n'+text+'\n    }'
 return text

def emit_control(rows,stage):
 lines=[];ends=[]
 for ci,(lo,hi)in enumerate(CF[stage]):
  while ends and ends[-1]==ci:lines.append('    }');ends.pop()
  op=hi>>12
  if op in(1,2):
   for slot in range(lo&4095,(lo&4095)+((lo>>12)&7)):
    if stage.startswith('VS')and rows[slot]['fetch']:continue
    lines.append(vs_issue(slot,rows[slot])if stage.startswith('VS')else ps_issue(slot,rows[slot],stage))
  elif op==11:
   target=lo&4095;screen.require(target>ci and lo&0x4000 and not lo&0x2000 and hi==0xB000,'Textured skin jump shape changed')
   lines.append(f'    [branch] if(p0) {{ // CF{ci} jumps to CF{target} if predicate false');ends.append(target)
  else:screen.require(op in(0,12),'Unqualified textured skin control opcode')
 screen.require(not ends,'Unclosed textured skin control block');return lines

def shader_source(image):
 rows=inspect(image);common=rigid.shader_source(image).split('struct RigidInput {',1)[0].replace('float4 vc[30]','float4 vc[256]').replace('float4 pc[50]','float4 pc[64]')
 lines=['// Retail skin_textured source8200FB98; complete opaque and alpha records.',common,
 'Texture2D<float4> skinTexturedBase : register(t1); SamplerState skinTexturedSampler : register(s1);',
 'Texture2D<float4> skinTexturedAlphaBase : register(t0); SamplerState skinTexturedAlphaSampler : register(s0);',
 'struct SkinTexturedInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1; float4 weights:TEXCOORD2; float4 indices:TEXCOORD3; float4 color:TEXCOORD4; float2 uv:TEXCOORD5; float3 morph1:TEXCOORD6; float3 morph2:TEXCOORD7; float3 morph3:TEXCOORD8; float3 morph4:TEXCOORD9; float3 morph5:TEXCOORD10; float3 morph6:TEXCOORD11; };',
 'struct SkinTexturedOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; float3 normal:TEXCOORD1; float4 world:TEXCOORD2; float4 shadow:TEXCOORD3; float4 color:TEXCOORD4; };',
 'struct SkinTexturedAlphaOutput { float4 position:SV_Position; float2 uv:TEXCOORD0; float3 normal:TEXCOORD1; float4 world:TEXCOORD2; float4 color:TEXCOORD3; };',
 'cbuffer SkinTexturedProbeInputs : register(b2) { float4 skinTexturedProbe[5]; };']
 for stage in('VS','VSA'):
  opaque=stage=='VS';name='SkinTextured'if opaque else'SkinTexturedAlpha'
  lines+=[f'{name}Output VS{name}(SkinTexturedInput input) {{',
   '    precise float4 r5=float4(input.position,0),r8=float4(input.normal,0),r9=input.weights,r4=input.indices.wzyx,r1=input.color,r11=float4(input.uv,0,0);',
   '    precise float4 r10=float4(input.morph1,0),r3=float4(input.morph2,0),r2=float4(input.morph3,0),r0=float4(0,input.morph4),r6=float4(input.morph5,0),r7=float4(input.morph6,0);',
   '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0; precise float ps=0; bool p0=false; int a0=0; const float4 k255=float4(0,1,0.5,3);']
  lines+=emit_control(rows[stage],stage)
  lines+=[f'    {name}Output o; o.position=output62; o.uv=output0.xy; o.normal=output1.xyz; o.world=output2;',
          '    o.shadow=output3; o.color=output4; return o; }'if opaque else'    o.color=output3; return o; }']
 for stage in('PS','PSA'):
  opaque=stage=='PS';name='SkinTextured'if opaque else'SkinTexturedAlpha';lines+=[f'float4 PS{name}({name}Output input):SV_Target0 {{']
  for i in range(4):lines.append('    const float4 k'+str(252+i)+'=asfloat(uint4('+','.join('0x%08Xu'%x for x in LITERALS[stage][4*i:4*i+4])+'));')
  lines+=['    precise float4 r0=float4(input.uv,0,0),r1=float4(input.normal,0),r2=input.world;',
          '    precise float4 r3=input.shadow,r4=input.color,r5=0,r6=0,output0=0;'if opaque else'    precise float4 r3=input.color,r4=0,r5=0,r6=0,output0=0;',
          '    precise float ps=0; bool p0=false;']
  lines+=emit_control(rows[stage],stage);lines+=['    return output0; }',f'[maxvertexcount(1)] void GS{name}Probe(point {name}Output input[1],inout PointStream<{name}Output> stream) {{ stream.Append(input[0]); }}',
    f'{name}Output VS{name}PixelProbe(uint id:SV_VertexID) {{',f'    {name}Output o; o.position=float4(id==1?3:-1,id==2?-3:1,0.5,1); o.uv=skinTexturedProbe[0].xy; o.normal=skinTexturedProbe[1].xyz; o.world=skinTexturedProbe[2];',
    '    o.shadow=skinTexturedProbe[3]; o.color=skinTexturedProbe[4]; return o; }'if opaque else'    o.color=skinTexturedProbe[3]; return o; }']
 return '\n'.join(lines+[''])

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--emit-hlsl',type=Path);p.add_argument('--verify',action='store_true');a=p.parse_args();source=shader_source((ROOT/'analysis/simpsons.pe').read_bytes())
 if a.emit_hlsl:a.emit_hlsl.write_text(source,encoding='utf-8')
 print('PASS skin_textured: VS63/VSA59/PS58/PSA17 original slots, both alpha cutoff side effects, twelve inputs')
