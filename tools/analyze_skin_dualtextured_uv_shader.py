"""Pinned four-record skin_dualtextured_uv transcription; distinct retail passes."""
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
import analyze_rigid_uv_shader as scalar_forms
from skin_shader_emit import indexed_bones
ROOT=Path(__file__).resolve().parents[1]
PROFILES=(
 ('VS',0x8204BA4C,5180,4100,1080,10,'41d5bf94782b09091ab58bf6a6706abaa101699555cbf4d53bdc26c11d2784be'),
 ('VSA',0x8204CE90,5180,4100,1080,10,'d669fa3f41c6e7d4c25d83dd26944f4702a9116b88cdb168ac060831ea0e2d44'),
 ('PS',0x8204E2DC,1144,700,444,4,'d7b6ef1f9e5e25513b60b2e49ce308baea61d5a768625a853df1423f9464a499'),
 ('PSA',0x8204E75C,1144,700,444,4,'176cc662ef94a43e03d6a7dd9f8c74b08559b05b712834462e7806d8a71084af'))
HEADERS={k:(0x102A1101,4036,1144,36,132,3840,3880,0,0)if k.startswith('VS')else(0x102A1100,636,508,36,132,540,580,0,0)for k,*_ in PROFILES}
VS_CF=((0xF555600A,0x1203),(0xF5556010,0x1203),(0x10093016,0x1000),(0x4006,0xB000),(0x6019,0x1200),(0x2007,0xB000),(0x101F,0x1200),(0,0xC200),(0x6020,0x1200),(0x6026,0x1200),(0x602C,0x1200),(0x6032,0x1200),(0x6038,0x1200),(0x603E,0x1200),(0,0xC400),(0x6044,0x1200),(0x604A,0x1200),(0x6050,0x1200),(0x3056,0x2200),(0,0))
PS_CF=((0x52004,0x1200),(0,0xC400),(0x6006,0x1200),(0x600C,0x1200),(0x6012,0x1200),(0x6018,0x1200),(0x601E,0x2200),(0,0))
CF={k:VS_CF if k.startswith('VS')else PS_CF for k,*_ in PROFILES}
VS_LITERALS=(0,)*8+(0xC0490FDB,0x40C90FDB,0x40400000,0,0,0x3F800000,0x3F000000,0x3E22F983)
PS_LITERALS=(0x3F666666,0x3F333333,0,0x3F800000,0x42000000,0x42800000,0x3DCCCCCD,0xBF800000,0x3E800000,0x42000000,0x3E4CCCCD,0x3F000000,0x3A802008,0x3A002008,0x3E800000,0x3F75C28F)
LITERALS={k:VS_LITERALS if k.startswith('VS')else PS_LITERALS for k,*_ in PROFILES}
SEMANTICS=(0x0010000A,0x0000300B,0x0000500C,0x0001500D,0x0000200E,0x0000100F,0x0000A010,0x00010011,0x00020012,0x00030013,0x00040014,0x00050015,0x00260016)
FETCHES=((0,(7,0,1,2)),(8,(0,1,2,7)),(10,(0,1,7,7)),(10,(7,7,0,1)),(5,(3,2,1,0)),(7,(0,1,2,3)),(1,(0,1,2,3)),(11,(0,1,2,7)),(4,(0,1,2,7)),(3,(0,1,2,7)),(2,(0,1,2,7)),(9,(0,1,2,7)),(6,(0,1,2,7)))

def decode_record(record,profile):
 stage,_,size,start,length,pairs,digest=profile
 screen.require(len(record)==size and hashlib.sha256(record).hexdigest()==digest,'Skin dual UV original record extent/hash changed')
 screen.require(struct.unpack_from('>9I',record)==HEADERS[stage],'Skin dual UV header changed')
 screen.require(struct.unpack_from('>2I',record,HEADERS[stage][6])==(64,length),'Skin dual UV code bounds changed')
 screen.require(struct.unpack_from('>16I',record,HEADERS[stage][1])==LITERALS[stage],'Skin dual UV literals changed')
 screen.require(record[-12:].hex()=={'VS':'4e4a0003','VSA':'4e4a0001','PS':'4e4a0002','PSA':'4e4a0000'}[stage]+'6386601b8d32a5ce','Skin dual UV pass trailer changed')
 if stage.startswith('VS'):screen.require(struct.unpack_from('>13I',record,3920)==SEMANTICS,'Skin dual UV thirteen associations changed')
 code=record[start:start+length];control=[]
 for i in range(pairs):
  a,b,c=screen.words(code,12*i);control.extend(((a,b&65535),(((b>>16)|(c<<16))&0xFFFFFFFF,c>>16)))
 screen.require(tuple(control)==CF[stage],'Skin dual UV complete control flow changed')
 rows={}
 for ci,(lo,hi)in enumerate(control):
  if hi>>12 not in(1,2):continue
  first,count,seq=lo&4095,(lo>>12)&7,(lo>>16)&4095
  screen.require(0<count<=6 and seq>>(2*count)==0,'Skin dual UV issue sequence changed')
  for n in range(count):
   slot=first+n;screen.require(slot not in rows,'Repeated skin dual UV slot')
   raw=screen.words(code,slot*12);fetch=bool(seq&(1<<(2*n)))
   rows[slot]=dict(raw=raw,fetch=fetch,control_index=ci,fields=screen.decode_fetch(raw)if fetch else edge.alu(*raw))
 screen.require(sorted(rows)==list(range(pairs,length//12-1)),'Skin dual UV issued-slot coverage changed')
 if stage.startswith('VS'):
  screen.require([n for n,r in rows.items()if r['fetch']]==list(range(10,23)),'Skin dual UV fetch count changed')
  for slot,(dest,swizzle)in zip(range(10,23),FETCHES):
   f=rows[slot]['fields'];screen.require(f['kind']=='vertex_fetch'and f['fetch_constant_index']==95 and f['destination_register']==dest and tuple(f['destination_swizzle'])==swizzle,'Skin dual UV fetch wiring changed')
 else:
  screen.require([n for n,r in rows.items()if r['fetch']]==[4,5],'Skin dual UV material fetch count changed')
  for slot,bank,reg,dest in((4,1,1,7),(5,0,0,6)):
   f=rows[slot]['fields'];screen.require(f['kind']=='texture_fetch'and f['fetch_constant_index']==bank and f['source_register']==reg and f['destination_register']==dest and f['source_components'][:2]==[0,1]and f['destination_swizzle']==[0,1,2,3]and f['normalized_coordinates']and f['computed_lod']and not f['register_lod']and not f['register_gradients']and f['dimension_field']==1 and f['fetch_valid_only']and not f['sample_location']and not f['lod_bias_field']and f['mag_filter']==f['min_filter']==f['mip_filter']==3 and f['anisotropy']==7 and f['offset_fields']==[0,0,0],'Skin dual UV sample contract changed')
 return rows

def inspect(image):
 screen.require(len(image)==screen.IMAGE_SIZE and hashlib.sha256(image).hexdigest()==screen.IMAGE_SHA256,'Original image changed')
 screen.require(hashlib.sha256(image[0x4A058:0x4A058+30640]).hexdigest()=='c8c955cbb051da8d849795fa3925b520d2173bc28668c21d3dbe3dbae68ef9ab','Original skin dual UV effect changed')
 result={p[0]:decode_record(image[p[1]-screen.BASE:p[1]-screen.BASE+p[2]],p)for p in PROFILES}
 for a,b in((PROFILES[0],PROFILES[1]),(PROFILES[2],PROFILES[3])):
  first=image[a[1]-screen.BASE:a[1]-screen.BASE+a[2]];second=image[b[1]-screen.BASE:b[1]-screen.BASE+b[2]]
  screen.require(first[:-12]==second[:-12],'Skin dual UV opaque/alpha header/literal/executable identity changed')
 return result

def issue(slot,row,stage):
 f=row['fields'];lines=[f'    {{ // slot{slot}']
 if row['fetch']:
  screen.require(stage=='PS','Vertex fetch belongs to independently pinned input interface')
  lines.append(f'        precise float4 v=skinDualUVTexture{f["fetch_constant_index"]}.Sample(skinDualUVSampler{f["fetch_constant_index"]},r{f["source_register"]}.xy);')
  lines.append(f'        r{f["destination_register"]}=v;');return '\n'.join(lines+['    }'])
 screen.require(not f['predicated']and not f['vector_destination_relative']and not f['scalar_destination_relative_or_export_zero']and not f['absolute_constants'],'Unqualified skin dual UV ALU modifiers')
 op,mask,sop,sm=f['vector_opcode'],f['vector_mask'],f['scalar_opcode'],f['scalar_mask']
 if mask:
  a,b,c=(rigid.operand(f,i,stage)for i in range(3))
  if op==0:expr=a+'+'+b
  elif op==1:expr='rigidLegacyMultiply('+a+','+b+')'
  elif op in(2,3):expr=('max'if op==2 else'min')+'('+a+','+b+')'
  elif op in(5,6):expr='float4('+a+('>'if op==5 else'>=')+b+')'
  elif op in(8,10):expr=('frac'if op==8 else'floor')+'('+a+')'
  elif op==11:expr='rigidLegacyMultiply('+a+','+b+')+'+c
  elif op==12:expr='float4('+','.join('('+a+').'+v+'==0?('+b+').'+v+':('+c+').'+v for v in'xyzw')+')'
  elif op in(15,16,17):
   lanes='xyzw'if op==15 else'xyz'if op==16 else'xy'
   lines.append('        precise float4 product=rigidLegacyMultiply('+a+','+b+');')
   expr='('+ '+'.join('product.'+v for v in lanes)+(' + ('+c+').x'if op==17 else'')+').xxxx'
  else:raise ValueError('Unqualified skin dual UV vector opcode '+str(op))
  lines.append('        precise float4 v='+('saturate('+expr+')'if f['vector_clamp']else expr)+';')
 if sop!=50:
  if sop==23:
   expr=rigid.operand(f,2,stage,1);screen.require(not sm and not f['scalar_clamp'],'Skin dual UV MaxAs form changed')
   lines.extend(['        precise float s='+expr+';','        int nextAddress=int(clamp(floor(s+0.5),-256.0,255.0));'])
  else:
   expr=scalar_forms.scalar(row,stage)
   if sop==2:
    operand=rigid.operand(f,2,stage)
    expr='rigidLegacyProduct(('+operand+').w,('+operand+').x)'
   if sop in(28,29):lines.extend(['        bool predicate='+expr+';','        precise float s=predicate?0.0:1.0;'])
   else:lines.append('        precise float s='+('saturate('+expr+')'if f['scalar_clamp']else expr)+';')
 if mask:
  sw=''.join('xyzw'[i]for i in range(4)if mask&(1<<i));target=('output'if f['export']else'r')+str(f['vector_destination']);lines.append(f'        {target}.{sw}=v.{sw};')
 if sm:
  screen.require(sop!=50 and(not f['export']or not(mask&sm)),'Skin dual UV overlapping/coissued scalar export changed')
  sw=''.join('xyzw'[i]for i in range(4)if sm&(1<<i));target=('output'+str(f['vector_destination']))if f['export']else'r'+str(f['scalar_destination']);lines.append(f'        {target}.{sw}=s.{"x"*len(sw)};')
 if sop!=50:lines.append('        ps=s;')
 if sop==23:lines.append('        a0=nextAddress;')
 if sop in(28,29):lines.append('        p0=predicate;')
 text='\n'.join(lines+['    }']);return indexed_bones(text,row)if stage=='VS'else text

def emit_control(rows,stage):
 lines=[]
 for ci,(lo,hi)in enumerate(CF[stage]):
  if stage.startswith('VS')and ci==3:lines.append('    [branch] if(p0) { // CF3: original conditional jump to CF6');continue
  if stage.startswith('VS')and ci==5:lines.append('    } else { // CF5: original unconditional jump to CF7');continue
  if stage.startswith('VS')and ci==7:lines.append('    }')
  op=hi>>12
  if op in(1,2):
   for slot in range(lo&4095,(lo&4095)+((lo>>12)&7)):
    if not(stage.startswith('VS')and rows[slot]['fetch']):lines.append(issue(slot,rows[slot],'VS'if stage.startswith('VS')else'PS'))
  else:screen.require(op in(0,12),'Unqualified skin dual UV control opcode')
 return lines

def shader_source(image):
 rows=inspect(image);common=rigid.shader_source(image).split('struct RigidInput {',1)[0]
 common=common[common.index('cbuffer RigidVertexConstants'):].replace('float4 vc[30]','float4 vc[256]').replace('float4 pc[50]','float4 pc[64]')
 for declaration in('Texture2D<float4> shadow0 : register(t0);','Texture2D<float4> shadow1 : register(t1);','SamplerState shadowSampler0 : register(s0);','SamplerState shadowSampler1 : register(s1);'):common=common.replace(declaration+'\n','')
 lines=['// Original skin_dualtextured_uv source8204A058; all four hash-pinned records.',common]
 for bank in range(2):lines.extend([f'Texture2D<float4> skinDualUVTexture{bank}:register(t{bank});',f'SamplerState skinDualUVSampler{bank}:register(s{bank});'])
 lines.append('struct SkinDualUVInput { float3 position:TEXCOORD0; float3 normal:TEXCOORD1; float4 weights:TEXCOORD2; float4 indices:TEXCOORD3; float4 color:TEXCOORD4; float2 uv:TEXCOORD5; float3 morph1:TEXCOORD6; float3 morph2:TEXCOORD7; float3 morph3:TEXCOORD8; float3 morph4:TEXCOORD9; float3 morph5:TEXCOORD10; float3 morph6:TEXCOORD11; float2 uv1:TEXCOORD12; };')
 for name in('SkinDualUV','SkinDualUVAlpha'):lines.append('struct '+name+'Output { float4 position:SV_Position; float2 t0:TEXCOORD0; float2 t1:TEXCOORD1; float2 t2:TEXCOORD2; float3 t3:TEXCOORD3; float3 t4:TEXCOORD4; float4 t5:TEXCOORD5; };')
 lines.append('cbuffer SkinDualUVProbeInputs:register(b2) { float4 skinDualUVProbe[6]; };')
 for stage,name in(('VS','SkinDualUV'),('VSA','SkinDualUVAlpha')):
  lines.extend([f'{name}Output VS{name}(SkinDualUVInput input) {{','    precise float4 r0=float4(0,input.position),r8=float4(input.normal,0),r10=float4(input.uv,input.uv1),r5=input.indices.wzyx,r7=input.weights,r1=input.color;',
   '    precise float4 r11=float4(input.morph1,0),r4=float4(input.morph2,0),r3=float4(input.morph3,0),r2=float4(input.morph4,0),r9=float4(input.morph5,0),r6=float4(input.morph6,0);',
   '    precise float4 output62=0,output0=0,output1=0,output2=0,output3=0,output4=0,output5=0; precise float ps=0; bool p0=false; int a0=0;'])
  for i in range(4):lines.append('    const float4 k'+str(252+i)+'=asfloat(uint4('+','.join('0x%08Xu'%x for x in VS_LITERALS[4*i:4*i+4])+'));')
  lines+=emit_control(rows[stage],stage);lines.append(f'    {name}Output o; o.position=output62; o.t0=output0.xy; o.t1=output1.xy; o.t2=output2.xy; o.t3=output3.xyz; o.t4=output4.xyz; o.t5=output5; return o; }}')
 for stage,name in(('PS','SkinDualUV'),('PSA','SkinDualUVAlpha')):
  lines.extend([f'float4 PS{name}({name}Output input):SV_Target0 {{','    precise float4 r0=float4(input.t0,0,0),r1=float4(input.t1,0,0),r2=float4(input.t2,0,0),r3=float4(input.t3,0),r4=float4(input.t4,0),r5=input.t5,r6=0,r7=0,output0=0; precise float ps=0; bool p0=false;'])
  for i in range(4):lines.append('    const float4 k'+str(252+i)+'=asfloat(uint4('+','.join('0x%08Xu'%x for x in PS_LITERALS[4*i:4*i+4])+'));')
  lines+=emit_control(rows[stage],stage);lines.append('    return output0; }')
 return '\n'.join(lines+[''])

def main():
 p=argparse.ArgumentParser();p.add_argument('--emit-hlsl',type=Path);p.add_argument('--verify',action='store_true');a=p.parse_args();source=shader_source((ROOT/'analysis/simpsons.pe').read_bytes())
 if a.emit_hlsl:a.emit_hlsl.write_text(source,encoding='utf-8')
 print('PASS skin dual UV: four records,79/79/32/32 original slots,13 inputs,2 material samplers,no shadow/cutoff')
if __name__=='__main__':main()
